#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_sntp.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_task_wdt.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#define PROGMEM
#include "immjson.h"
#include "sdkconfig.h"
#include "gfxfont.h"
#include "Blocktopia11pt7b.h"
#define FONT Blocktopia11pt7b
#include "esp_log.h"
#include "ssd1680.h"
#include "bitui.h"

static const char *TAG = "main";

static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define SCREEN_COLS 128
#define SCREEN_STRIDE ((SCREEN_COLS - 1) / 8 + 1)
#define SCREEN_ROWS 296
static uint8_t framebuffer[SCREEN_STRIDE * SCREEN_ROWS];

static void render_text(bitui_t ctx, const GFXfont *font, const char *str, const uint16_t bottom_left_x, const uint16_t bottom_left_y) {
    uint16_t x = bottom_left_x;
    uint16_t y = bottom_left_y;
    for (; *str; ++str) {
        const char c = *str;
        if (c == '\n') {
            x = bottom_left_x;
            y += font->yAdvance;
            continue;
        }

        assert(c >= font->first || c <= font->last);
        const GFXglyph glyph = font->glyph[c - font->first];

        bitui_paste_bitstream(ctx, font->bitmap + glyph.bitmapOffset, glyph.width, glyph.height, glyph.xOffset + x, y + glyph.yOffset);
        x += glyph.xAdvance;
    }
}

#define FORECAST_DURATION_DAYS 2
#define FORECAST_HOURLY_POINT_COUNT FORECAST_DURATION_DAYS * 24
struct Forecast {
    float latitude;
    float longitude;
    uint32_t utc_offset_seconds;
    const char *timezone_abbreviation;
    struct Hourly {
        uint64_t time[FORECAST_HOURLY_POINT_COUNT];
        float temperature_2m[FORECAST_HOURLY_POINT_COUNT];
        uint8_t weather_code_2m[FORECAST_HOURLY_POINT_COUNT];
    } hourly;
    time_t updated_at;
};

static void *static_reserve_uint64(void *cursor, size_t i) {
    return i < FORECAST_HOURLY_POINT_COUNT ? ((uint64_t*)cursor) + i : NULL;
}

static JsonArrayDescription array_describe_time(void *cursor) {
    return (JsonArrayDescription) {
        .exitpoint = (uint8_t*)cursor + sizeof(((struct Hourly*)NULL)->time),
        .array_reserve_fn = static_reserve_uint64,
        .item_tag = KIND_INTEGER,
        .item_val = { .integer = { .bitwidth = 64 } }
    };
}

static void *static_reserve_float(void *cursor, size_t i) {
    return i < FORECAST_HOURLY_POINT_COUNT ? ((float*)cursor) + i : NULL;
}

static JsonArrayDescription array_describe_temperature(void *cursor) {
    return (JsonArrayDescription) {
        .exitpoint = (uint8_t*)cursor + sizeof(((struct Hourly*)NULL)->temperature_2m),
        .array_reserve_fn = static_reserve_float,
        .item_tag = KIND_DOUBLE,
    };
}

static void *static_reserve_uint8(void *cursor, size_t i) {
    return i < FORECAST_HOURLY_POINT_COUNT ? ((uint8_t*)cursor) + i : NULL;
}

static JsonArrayDescription array_describe_weather_code(void *cursor) {
    return (JsonArrayDescription) {
        .exitpoint = (uint8_t*)cursor + sizeof(((struct Hourly*)NULL)->weather_code_2m),
        .array_reserve_fn = static_reserve_uint8,
        .item_tag = KIND_INTEGER,
        .item_val = { .integer = { .bitwidth = 8 } }
    };
}

const JsonObjectProperty forecast_schema[] = {
    { .key = JSON_SCHEMA_DOUBLE "latitude" },
    { .key = JSON_SCHEMA_DOUBLE "longitude" },
    { .key = JSON_SCHEMA_INTEGER "utc_offset_seconds", { .integer = { .bitwidth = 32 } } },
    { .key = JSON_SCHEMA_STRING "timezone_abbreviation" },
    { .key = JSON_SCHEMA_OBJECT "hourly", { .obj_desc = JSON_INLINE_OBJ_BEGIN } },
        { .key = JSON_SCHEMA_ARRAY "time", { .array_describe = array_describe_time } },
        { .key = JSON_SCHEMA_ARRAY "temperature_2m", { .array_describe = array_describe_temperature } },
        { .key = JSON_SCHEMA_ARRAY "weather_code", { .array_describe = array_describe_weather_code  }  },
    { .key = JSON_SCHEMA_OBJECT, { .obj_desc = JSON_INLINE_OBJ_END } },
    OBJECT_PROPERTIES_END()
};

static JsonSlice read_from_tls(void *user_data) {
    static char buf[256];
    esp_tls_t *tls = user_data;

    int ret;
    do {
        ret = esp_tls_conn_read(tls, buf, sizeof(buf));
    } while (ret == ESP_TLS_ERR_SSL_WANT_WRITE  || ret == ESP_TLS_ERR_SSL_WANT_READ);

    JsonSlice slice = { .head = buf, .tail = buf };
    if (ret < 0) {
        ESP_LOGE(TAG, "esp_tls_conn_read  returned [-0x%02X](%s)", -ret, esp_err_to_name(ret));
    } else if (ret == 0) {
        ESP_LOGI(TAG, "connection closed");
    } else {
        slice.tail = buf + ret;
    }
    return slice;
}

static char *alloc_str_fn(void *user_data, char *oldptr, size_t old_size, size_t new_size) {
    // TODO
    (void)user_data;
    (void)old_size;
    return realloc(oldptr, new_size);
}

static bool noRetry = false;
static esp_netif_ip_info_t ip_info;
static int s_retry_num = 0;
#define MAXIMUM_RETRY 3

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected");
        if (noRetry) return;
        if (s_retry_num < MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ip_info = event->ip_info;
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&config);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            .sae_h2e_identifier = "",
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s", wifi_config.sta.ssid);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", wifi_config.sta.ssid);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

ssd1680_handle_t ssd1680_handle;

void init_devices() {
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing SPI...");
    spi_bus_config_t buscfg = {
        .mosi_io_num = GPIO_NUM_7,
        .miso_io_num = -1,
        .sclk_io_num = GPIO_NUM_6,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };
    //Initialize the SPI bus
    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Initializing SSD1680...");
    ret = ssd1680_init(&(ssd1680_config_t) {
        .host = SPI2_HOST,
        .busy_pin = GPIO_NUM_0,
        .reset_pin = GPIO_NUM_1,
        .dc_pin = GPIO_NUM_5,
        .cs_pin = GPIO_NUM_10,

        .rows = SCREEN_ROWS,
        .cols = SCREEN_COLS,
        .framebuffer = framebuffer
    }, &ssd1680_handle);
    ESP_ERROR_CHECK(ret);
}

static struct Forecast forecast;
static bitui_ctx_t bitui_handle;

#define __str(s) #s
#define str(s) __str(s)

#define WEATHER_WEB_SERVER "api.open-meteo.com"
#define WEATHER_WEB_PORT "443"
#define WEATHER_WEB_PATH "/v1/forecast?latitude=48.753899&longitude=2.297500&hourly=temperature_2m,weather_code&timezone=Europe%2FLondon&forecast_days=" str(FORECAST_DURATION_DAYS) "&timeformat=unixtime"
#define WEATHER_WEB_URL "https://" WEATHER_WEB_SERVER WEATHER_WEB_PATH

static const char *WEATHER_REQUEST = "GET " WEATHER_WEB_PATH " HTTP/1.0\r\n"
    "Host: "WEATHER_WEB_SERVER":"WEATHER_WEB_PORT"\r\n"
    "User-Agent: esp-idf/1.0 esp32c3\r\n"
    "Accept: application/json\r\n"
    "\r\n";

static void https_get_request(const char *WEB_SERVER_URL, const char *REQUEST, void (*decode)(esp_tls_t *tls_session))
{
    int ret;

    esp_tls_t *tls = esp_tls_init();
    if (!tls) {
        ESP_LOGE(TAG, "Failed to allocate esp_tls handle!");
        goto exit;
    }

    esp_tls_cfg_t cfg = {
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    if (esp_tls_conn_http_new_sync(WEB_SERVER_URL, &cfg, tls) == 1) {
        ESP_LOGI(TAG, "Connection established...");
    } else {
        ESP_LOGE(TAG, "Connection failed...");
        int esp_tls_code = 0, esp_tls_flags = 0;
        esp_tls_error_handle_t tls_e = NULL;
        esp_tls_get_error_handle(tls, &tls_e);
        /* Try to get TLS stack level error and certificate failure flags, if any */
        ret = esp_tls_get_and_clear_last_error(tls_e, &esp_tls_code, &esp_tls_flags);
        if (ret == ESP_OK) {
            ESP_LOGE(TAG, "TLS error = -0x%x, TLS flags = -0x%x", esp_tls_code, esp_tls_flags);
        }
        goto cleanup;
    }

    const size_t request_len = strlen(REQUEST);
    size_t written_bytes = 0;
    do {
        ret = esp_tls_conn_write(tls,
                                 REQUEST + written_bytes,
                                 request_len - written_bytes);
        if (ret >= 0) {
            ESP_LOGI(TAG, "%d bytes written", ret);
            written_bytes += ret;
        } else if (ret != ESP_TLS_ERR_SSL_WANT_READ  && ret != ESP_TLS_ERR_SSL_WANT_WRITE) {
            ESP_LOGE(TAG, "esp_tls_conn_write  returned: [0x%02X](%s)", ret, esp_err_to_name(ret));
            goto cleanup;
        }
    } while (written_bytes < request_len);

    ESP_LOGI(TAG, "Reading HTTP response...");
    decode(tls);
cleanup:
    esp_tls_conn_destroy(tls);
exit:
    ESP_LOGD(TAG, "esp-tls finished");
}

static void decode_weather(esp_tls_t *tls_session) {
    JsonSource src = {
        .read_fn = { .closure = read_from_tls, .user_data = tls_session },
        .string_buffer.alloc_str_fn.closure = alloc_str_fn,
        .remainder = read_from_tls(tls_session)
    };
    src.remainder.head = memmem(src.remainder.head, src.remainder.tail - src.remainder.head, "\r\n\r\n", 4);
    if (src.remainder.head != NULL) {
        src.remainder.head += 4;
        struct Forecast *out = &forecast;
        if (!json_deserialize_object(&src, (void**)&out, forecast_schema)) {
            ESP_LOGI(TAG, "Failed to deserialize forecast\n");
            ESP_LOGI(TAG, " %zu:%zu  | %.*s\n", src.line, json_source_column(&src), (int)(src.remainder.tail - src.remainder.head), src.remainder.head);
        } else {
            ESP_LOGI(TAG, "Got lat=%f, lon=%f", forecast.latitude, forecast.longitude);
            time(&forecast.updated_at);
        }
    }
}

static char temp_str[80];

static void render_weather(void) {
    const uint64_t timestamp = forecast.updated_at;

    enum {
        GRAPH_START_X = SCREEN_ROWS - 2 * FORECAST_HOURLY_POINT_COUNT,
        BAR_START_Y = 6,
        BAR_MAX_HEIGHT = 48,
        BAR_MIN_HEIGHT = 2,
        CURSOR_HEIGHT = 4,
        CURSOR_PADDING = 2,
    };
    size_t cursor = 0;
    while (cursor < FORECAST_HOURLY_POINT_COUNT && forecast.hourly.time[cursor] < timestamp) ++cursor;
    if (cursor == FORECAST_HOURLY_POINT_COUNT) return;

    ESP_LOGI(TAG, "Current time : %lu, Closest data point : %lu", (long)timestamp, (long)forecast.hourly.time[cursor]);

    float temp_min = 256.0f;
    float temp_max = -256.0f;
    for (size_t i = 0; i < FORECAST_HOURLY_POINT_COUNT; ++i) {
        if (forecast.hourly.temperature_2m[i] < temp_min) temp_min = forecast.hourly.temperature_2m[i];
        if (forecast.hourly.temperature_2m[i] > temp_max) temp_max = forecast.hourly.temperature_2m[i];
    }

    bitui_t ctx = &bitui_handle;
    ctx->color = false;
    for (size_t i = 0; i < FORECAST_HOURLY_POINT_COUNT; ++i) {
        float fill = (forecast.hourly.temperature_2m[i] - temp_min) / (temp_max - temp_min);
        bitui_hline(ctx, GRAPH_START_X + i*2, BAR_START_Y, BAR_START_Y + BAR_MIN_HEIGHT + (uint16_t)(fill * BAR_MAX_HEIGHT));
    }

    bitui_point(ctx, GRAPH_START_X + cursor*2 - 1, 127);
    bitui_point(ctx, GRAPH_START_X + cursor*2 + 1, 127);
    bitui_hline(ctx, GRAPH_START_X + cursor*2 + 0, 0, CURSOR_HEIGHT);

    uint16_t h = (uint16_t)((forecast.hourly.temperature_2m[cursor] - temp_min) / (temp_max - temp_min) * BAR_MAX_HEIGHT);
    bitui_point(ctx, GRAPH_START_X + cursor*2 - 1, BAR_START_Y + BAR_MIN_HEIGHT + BAR_MAX_HEIGHT + CURSOR_PADDING + CURSOR_HEIGHT + 4);
    bitui_point(ctx, GRAPH_START_X + cursor*2 + 1, BAR_START_Y + BAR_MIN_HEIGHT + BAR_MAX_HEIGHT + CURSOR_PADDING + CURSOR_HEIGHT + 4);
    bitui_hline(ctx, GRAPH_START_X + cursor*2 + 0, BAR_START_Y + BAR_MIN_HEIGHT + h + CURSOR_PADDING, BAR_START_Y + BAR_MIN_HEIGHT + BAR_MAX_HEIGHT + 2 + CURSOR_HEIGHT);

    snprintf(temp_str, sizeof(temp_str), "Now %.1f C\nMin %.1f C\nMax %.1f C", forecast.hourly.temperature_2m[cursor], temp_min, temp_max);

    ctx->color = true;
    render_text(ctx, &FONT, temp_str, 16, 72);

    esp_err_t ret = ssd1680_wait_until_idle(ssd1680_handle);
    ESP_ERROR_CHECK(ret);
    ret = ssd1680_flush(ssd1680_handle, (ssd1680_rect_t){
        .x = 0, .y = 0,
        .w = SCREEN_COLS, .h = SCREEN_ROWS
    });
    ESP_ERROR_CHECK(ret);
    ret = ssd1680_full_refresh(ssd1680_handle);
    ESP_ERROR_CHECK(ret);
}

void app_main(void)
{
    esp_err_t ret;

    init_devices();

    bitui_handle = (bitui_ctx_t){
        .width = SCREEN_COLS,
        .height = SCREEN_ROWS,
        .stride = SCREEN_STRIDE,
        .framebuffer = framebuffer,
        .rot = BITUI_ROT_090,
        .color = true,
    };

    bitui_t ctx = &bitui_handle;

    memset(framebuffer, 0xff, sizeof(framebuffer));
    render_text(ctx, &FONT, "Connecting to Wi-Fi", 32, 48);

    // Clear other pixels, clear the dirty zone
    ctx->dirty = (bitui_rect_t){0};
    ret = ssd1680_flush(ssd1680_handle, (ssd1680_rect_t){
        .x = 0, .y = 0,
        .w = SCREEN_COLS, .h = SCREEN_ROWS
    });
    ESP_ERROR_CHECK(ret);
    ret = ssd1680_full_refresh(ssd1680_handle);
    ESP_ERROR_CHECK(ret);

    vTaskDelay(10 / portTICK_PERIOD_MS);

    wifi_init_sta();

    snprintf(temp_str, sizeof(temp_str), "Connected\nIP: " IPSTR "\nSyncing time...", IP2STR(&ip_info.ip));

    memset(framebuffer, 0xff, sizeof(framebuffer));
    render_text(ctx, &FONT, temp_str, 32, 48);

    ret = ssd1680_wait_until_idle(ssd1680_handle);
    ESP_ERROR_CHECK(ret);
    ret = ssd1680_flush(ssd1680_handle, (ssd1680_rect_t){
        .x = 0, .y = 0,
        .w = SCREEN_COLS, .h = SCREEN_ROWS
    });
    ESP_ERROR_CHECK(ret);
    ret = ssd1680_full_refresh(ssd1680_handle);
    ESP_ERROR_CHECK(ret);

    esp_netif_sntp_start();
    ESP_ERROR_CHECK(esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000))); // 10s

    time_t now = 0;
    struct tm timeinfo = { 0 };
    time(&now);

    setenv("TZ", "CEST", 1);
    tzset();
    localtime_r(&now, &timeinfo);
    strftime(temp_str, sizeof(temp_str), "%A %d %b, %R", &timeinfo);

    memset(framebuffer, 0xff, sizeof(framebuffer));
    render_text(ctx, &FONT, temp_str, 16, 20);

    ret = ssd1680_wait_until_idle(ssd1680_handle);
    ESP_ERROR_CHECK(ret);
    ret = ssd1680_flush(ssd1680_handle, (ssd1680_rect_t){
        .x = 0, .y = 0,
        .w = SCREEN_COLS, .h = SCREEN_ROWS
    });
    ESP_ERROR_CHECK(ret);
    ret = ssd1680_full_refresh(ssd1680_handle);
    ESP_ERROR_CHECK(ret);

    vTaskDelay(100 / portTICK_PERIOD_MS);

    forecast.updated_at = 0;
    https_get_request(WEATHER_WEB_URL, WEATHER_REQUEST, decode_weather);

    if (forecast.updated_at != 0) {
        render_weather();
    }

    vTaskDelay(100 / portTICK_PERIOD_MS);

    noRetry = true;
    ESP_ERROR_CHECK(esp_wifi_stop());

    esp_netif_sntp_deinit();

    ret = ssd1680_wait_until_idle(ssd1680_handle);
    ESP_ERROR_CHECK(ret);
    ret = ssd1680_deinit(&ssd1680_handle);
    ESP_ERROR_CHECK(ret);

    while (1) {
        //esp_task_wdt_reset();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}
