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

#include "immjson.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "ssd1680.h"
#include "bitui.h"
#include "gui.h"

static const char *TAG = "main";

static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static uint8_t framebuffer[SCREEN_STRIDE * SCREEN_ROWS];

static void *static_reserve_hourly_uint64(void *cursor, size_t i) {
    return i < FORECAST_HOURLY_POINT_COUNT ? ((uint64_t*)cursor) + i : NULL;
}

static JsonArrayDescription array_describe_hourly_time(void *cursor) {
    return (JsonArrayDescription) {
        .exitpoint = (uint8_t*)cursor + sizeof(((struct ForecastHourly*)NULL)->time),
        .array_reserve_fn = static_reserve_hourly_uint64,
        .item_tag = KIND_INTEGER,
        .item_val = { .integer = { .bitwidth = 64 } }
    };
}

static void *static_reserve_float(void *cursor, size_t i) {
    return i < FORECAST_HOURLY_POINT_COUNT ? ((float*)cursor) + i : NULL;
}

static JsonArrayDescription array_describe_temperature(void *cursor) {
    return (JsonArrayDescription) {
        .exitpoint = (uint8_t*)cursor + sizeof(((struct ForecastHourly*)NULL)->temperature_2m),
        .array_reserve_fn = static_reserve_float,
        .item_tag = KIND_DOUBLE,
    };
}

static void *static_reserve_uint8(void *cursor, size_t i) {
    return i < FORECAST_HOURLY_POINT_COUNT ? ((uint8_t*)cursor) + i : NULL;
}

static JsonArrayDescription array_describe_weather_code(void *cursor) {
    return (JsonArrayDescription) {
        .exitpoint = (uint8_t*)cursor + sizeof(((struct ForecastHourly*)NULL)->weather_code),
        .array_reserve_fn = static_reserve_uint8,
        .item_tag = KIND_INTEGER,
        .item_val = { .integer = { .bitwidth = 8 } }
    };
}

static void *static_reserve_daily_int64(void *cursor, size_t i) {
    return i < FORECAST_DURATION_DAYS ? ((int64_t*)cursor) + i : NULL;
}

static JsonArrayDescription array_describe_daily_time(void *cursor) {
    return (JsonArrayDescription) {
        .exitpoint = (uint8_t*)cursor + sizeof(((struct ForecastDaily*)NULL)->time),
        .array_reserve_fn = static_reserve_daily_int64,
        .item_tag = KIND_INTEGER,
        .item_val = { .integer = { .bitwidth = 64, .is_signed = true } }
    };
}

const JsonObjectProperty forecast_schema[] = {
    { .key = JSON_SCHEMA_DOUBLE "latitude" },
    { .key = JSON_SCHEMA_DOUBLE "longitude" },
    { .key = JSON_SCHEMA_OBJECT "hourly", { .obj_desc = JSON_INLINE_OBJ_BEGIN } },
        { .key = JSON_SCHEMA_ARRAY "time", { .array_describe = array_describe_hourly_time } },
        { .key = JSON_SCHEMA_ARRAY "temperature_2m", { .array_describe = array_describe_temperature } },
        { .key = JSON_SCHEMA_ARRAY "weather_code", { .array_describe = array_describe_weather_code  }  },
    { .key = JSON_SCHEMA_OBJECT, { .obj_desc = JSON_INLINE_OBJ_END } },
    { .key = JSON_SCHEMA_OBJECT "daily", { .obj_desc = JSON_INLINE_OBJ_BEGIN } },
        { .key = JSON_SCHEMA_ARRAY "time", { .array_describe = array_describe_daily_time } },
        { .key = JSON_SCHEMA_ARRAY "sunrise", { .array_describe = array_describe_daily_time } },
        { .key = JSON_SCHEMA_ARRAY "sunset", { .array_describe = array_describe_daily_time  }  },
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
static esp_netif_t* netif;
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
        ESP_UNUSED(event);
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    netif = esp_netif_create_default_wifi_sta();

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
        .controller = SSD1685,
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

static gui_data_t gui_data;
static bitui_ctx_t bitui_handle;

#define __str(s) #s
#define str(s) __str(s)

#define WEATHER_WEB_SERVER "api.open-meteo.com"
#define WEATHER_WEB_PORT "443"
#define WEATHER_WEB_PATH "/v1/forecast?latitude=48.753899&longitude=2.297500&hourly=temperature_2m,weather_code&daily=sunrise,sunset&forecast_days=" str(FORECAST_DURATION_DAYS) "&timeformat=unixtime"
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
    typeof(src.remainder) remainder = src.remainder;
#define HTTP_HEAD_END "\r\n\r\n"
    src.remainder.head = memmem(src.remainder.head, src.remainder.tail - src.remainder.head, HTTP_HEAD_END, sizeof(HTTP_HEAD_END) - 1);
    if (src.remainder.head != NULL) {
        src.remainder.head += sizeof(HTTP_HEAD_END) - 1;
        struct Forecast *out = &gui_data.forecast;
        if (!json_deserialize_object(&src, (void**)&out, forecast_schema)) {
            ESP_LOGE(TAG, "Failed to deserialize forecast\n");
            ESP_LOGE(TAG, " %zu:%zu  | %.*s\n", src.line, json_source_column(&src), (int)(src.remainder.tail - src.remainder.head), src.remainder.head);
        } else {
            ESP_LOGI(TAG, "Got lat=%f, lon=%f", gui_data.forecast.latitude, gui_data.forecast.longitude);

            ESP_LOGD(TAG, "forecast.hourly=[");
            for (int i = 0; i < FORECAST_HOURLY_POINT_COUNT; i++) {
                ESP_LOGD(TAG, "\ttime[%d] = %lld\ttemperature_2m[%d] = %.1f\tweather_code[%d] = %hhu,", i, gui_data.forecast.hourly.time[i], i, gui_data.forecast.hourly.temperature_2m[i], i, gui_data.forecast.hourly.weather_code[i]);
            }
            ESP_LOGD(TAG, "]");

            ESP_LOGI(TAG, "forecast.daily=[");
            for (int i = 0; i < FORECAST_DURATION_DAYS; i++) {
                ESP_LOGD(TAG, "\ttime[%d] = %lld\tsunrise[%d] = %lld\tsunset[%d] = %lld,", i, gui_data.forecast.daily.time[i], i, gui_data.forecast.daily.sunrise[i], i, gui_data.forecast.daily.sunset[i]);
            }
            ESP_LOGD(TAG, "]");
            time(&gui_data.forecast.updated_at);
        }
    } else {
        ESP_LOGE(TAG, "Failed to deserialize forecast: `%.*s`\n", (int)(remainder.tail - remainder.head), remainder.head);
    }
}

static void render_gui(bitui_t ctx) {
    gui_render(ctx, &gui_data);

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

    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    bitui_handle = (bitui_ctx_t){
        .width = SCREEN_COLS,
        .height = SCREEN_ROWS,
        .stride = SCREEN_STRIDE,
        .framebuffer = framebuffer,
        .rot = BITUI_ROT_090,
        .color = true,
    };

    bitui_t ctx = &bitui_handle;

    render_gui(ctx);

    gui_data.current_screen = GUI_WIFI_INIT;
    render_gui(ctx);
    wifi_init_sta();
    esp_netif_sntp_start();
    ESP_ERROR_CHECK(esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000))); // 10s

    gui_data.forecast.updated_at = 0;
    gui_data.current_screen = GUI_HOME;

    render_gui(ctx);

    https_get_request(WEATHER_WEB_URL, WEATHER_REQUEST, decode_weather);

    render_gui(ctx);

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
