#include <stdio.h>
#include <string.h>
#include "ssd1680.h"
#include "esp_log.h"

#define DC_COMMAND(DCPin) (DCPin)
#define DC_DATA(DCPin) (-(DCPin))
#define DC_LEVEL(SPI_TransactionUserData) (SPI_TransactionUserData < 0)
#define DC_PIN(SPI_TransactionUserData) ((SPI_TransactionUserData) < 0 ? (-SPI_TransactionUserData) : (SPI_TransactionUserData))
typedef gpio_num_t SPI_TransactionUserData;
_Static_assert(sizeof(SPI_TransactionUserData) <= sizeof(void*), "No memory allocation required");

// MAX CLK freq in WRITE mode:  20 Mhz
// MAX CLK freq in  READ mode: 2.5 Mhz
#define SSD1680_CLK_FREQ 2 * 1000 * 1000 /* 2Mhz*/

enum Command : uint8_t {
    CMD_DriverOutputControl = 0x01,
    CMD_GateDrivingVoltageControl = 0x03,
    CMD_SourceDrivingVoltageControl = 0x04,
    CMD_InitialCodeSettingOTPProgram = 0x08,
    CMD_WriteRegisterForInitialCodeSetting = 0x09,
    CMD_ReadRegisterForInitialCodeSetting = 0x0A,
    CMD_BoosterSoftStartControl = 0x0C,
    CMD_GateScanStartPosition = 0x0F, // not available on SSD1685
    CMD_DeepSleepMode = 0x10,
    CMD_DataEntryModeSetting = 0x11,
    CMD_SWReset = 0x12,
    CMD_HVReadyDetection = 0x14,
    CMD_VCIDetection = 0x15,
    CMD_TemperatureSensorSelection = 0x18,
    CMD_WriteTemperatureRegister = 0x1A,
    CMD_ReadTemperatureRegister = 0x1B,
    CMD_WriteCommandToExternalTemperatureSensor = 0x1C,
    CMD_MasterActivationUpdateSeq = 0x20,
    CMD_DisplayUpdateControl1 = 0x21,
    CMD_DisplayUpdateControl2 = 0x22,
    CMD_WriteRAM_BW = 0x24,
    CMD_WriteRAM_RED = 0x26,
    CMD_ReadRAM = 0x27,
    CMD_VCOMSense = 0x28,
    CMD_VCOMSenseDuration = 0x29,
    CMD_OTP_ProgramVcom = 0x2A,
    CMD_WriteVCOMControl = 0x2B,
    CMD_WriteVCOM = 0x2C,
    CMD_OTP_ReadDisplayOption = 0x2D,
    CMD_OTP_ReadUserID = 0x2E,
    CMD_ReadStatusBit = 0x2F,
    CMD_OTP_ProgramWaveform = 0x30,
    CMD_OTP_LoadWaveform = 0x31,
    CMD_WriteLUT = 0x32,
    CMD_CRC_Calculation = 0x34,
    CMD_CRC_StatusRead = 0x35,
    CMD_OTP_ProgramSelection = 0x36,
    CMD_WriteDisplayOption = 0x37,
    CMD_WriteUserID = 0x38,
    CMD_OTP_ProgramMode = 0x39,
    CMD_BorderWaveformControl = 0x3C,
    CMD_LUT_End = 0x3F,
    CMD_ReadRAMOption = 0x41,
    CMD_SetRAM_StartEnd_X = 0x44,
    CMD_SetRAM_StartEnd_Y = 0x45,
    CMD_AutoWriteRegularPattern_REDRAM = 0x46,
    CMD_AutoWriteRegularPattern_BWRAM = 0x47,
    CMD_SetRAM_Counter_X = 0x4E,
    CMD_SetRAM_Counter_Y = 0x4F,
    CMD_NOP = 0x7F,
};

// After this command initiated, the chip will enter Deep Sleep Mode, BUSY pad will keep output high.
// To Exit Deep Sleep mode, User required to send HWRESET to the driver
enum DeepSleepMode : uint8_t {
    DEEP_SLEEP_DISABLED = 0x0,
    // Retain RAM data but cannot access the RAM
    // Typical power consumption: 1uA (max 3uA)
    DEEP_SLEEP_MODE_1 = 0x1,
    // Cannot retain RAM data
    // Typical power consumption: 0.7uA (max 3uA)
    DEEP_SLEEP_MODE_2 = 0x3,
};

enum SSD1680_SourceOutputMode : uint8_t {
    SSD1680_SOURCE_S0_TO_S175 = 0x00,
    SSD1680_SOURCE_S8_TO_S167 = 0x80,
};

enum SSD1685_ResolutionSelection : uint8_t {
    SSD1685_RES_200x384 = 0x00,
    SSD1685_RES_184x384 = 0x40,
    SSD1685_RES_168x384 = 0x80,
    SSD1685_RES_216x384 = 0xC0,
};

enum RAMOption : uint8_t {
    RAM_Normal = 0x0,
    RAM_BypassAs0 = 0x4,
    RAM_Inverse = 0x8,
};

enum TemperatureSensorSelection : uint8_t {
    TEMP_SENSOR_INTERNAL = 0x80,
    TEMP_SENSOR_EXTERNAL = 0x48,
};

// Set the direction in which the address counter is updated automatically after
// data are written to the RAM.
enum AM : uint8_t {
    // the address counter is updated in the X direction
    AM_X = 0,
    // the address counter is updated in the Y direction
    AM_Y = 1,
};

// Address automatic increment / decrement
enum DataEntryMode : uint8_t {
    DATA_ENTRY_X_INC = 0x1,
    DATA_ENTRY_Y_INC = 0x2,

    DATA_ENTRY_Y_DEC_X_DEC = 0,
    DATA_ENTRY_Y_DEC_X_INC = DATA_ENTRY_X_INC,
    DATA_ENTRY_Y_INC_X_DEC = DATA_ENTRY_Y_INC,
    DATA_ENTRY_Y_INC_X_INC = DATA_ENTRY_Y_INC | DATA_ENTRY_X_INC,
};

// Context (config and data) of the spi_ssd1680
struct ssd1680_context_t {
    union { // < Configuration by the caller.
        const ssd1680_config_t cfg;
        ssd1680_config_t init_cfg;
    };
    spi_device_handle_t spi; // SPI device handle
};

typedef struct ssd1680_context_t ssd1680_context_t;

static const char TAG[] = "ssd1680";

static void ssd1680_spi_pre_transfer_callback(spi_transaction_t *t)
{
    const SPI_TransactionUserData user_data = (SPI_TransactionUserData)t->user;
    gpio_set_level(DC_PIN(user_data), DC_LEVEL(user_data));
}

struct Cmd {
    enum Command id;
    uint8_t data_len;
    uint8_t data_bytes[4];
};
_Static_assert(sizeof(((struct Cmd*)NULL)->data_bytes) == sizeof(((spi_transaction_t*)NULL)->tx_data));

#define ssd1680_cmd_write(Handle, CmdId, ...) __ssd1680_cmd_write((Handle), (struct Cmd){ \
        .id = (CmdId), \
        .data_len = sizeof((uint8_t[]){ __VA_ARGS__ }), \
        .data_bytes = { __VA_ARGS__ }, \
    })

static esp_err_t __ssd1680_cmd_write(ssd1680_handle_t h, const struct Cmd cmd) {
    esp_err_t ret;

    // The SSD168x controller expects the D/C pin to be low when in sending the
    // command, and high, when sending data. Therefore, we can't use the SPI
    // Master's command and data phases as we rely on
    // ssd1680_spi_pre_transfer_callback to set the D/C pin state, which is
    // called for every transactions. Thus, the command and data are split in
    // two SPI transactions.

    /* Send COMMAND */
    spi_transaction_t command = {
        .length = sizeof(uint8_t) * 8,
        .tx_data[0] = cmd.id,
        .user = (void*)DC_COMMAND(h->cfg.dc_pin),
        .flags = SPI_TRANS_USE_TXDATA | (cmd.data_len > 0 ? SPI_TRANS_CS_KEEP_ACTIVE : 0)
    };

    ret = spi_device_polling_transmit(h->spi, &command);

    /* Send DATA if there is any */
    if (cmd.data_len == 0 || ret != ESP_OK) return ret;

    spi_transaction_t payload = {
        .length = sizeof(uint8_t) * 8 * cmd.data_len,
        .tx_data = { cmd.data_bytes[0], cmd.data_bytes[1], cmd.data_bytes[2], cmd.data_bytes[3] },
        .user = (void*)DC_DATA(h->cfg.dc_pin),
        .flags = SPI_TRANS_USE_TXDATA
    };

    ret = spi_device_polling_transmit(h->spi, &payload);

    return ret;
}

static bool ssd1680_check_controller_resolution(ssd1680_controller_t controller, uint16_t cols, uint16_t rows) {
    switch (controller) {
    case SSD1680:
        return cols >= SSD1680_MIN_COLS && cols <= SSD1680_MAX_COLS
            && rows >= SSD1680_MIN_ROWS && rows <= SSD1680_MAX_ROWS;
    case SSD1685:
        return cols >= SSD1685_MIN_COLS && cols <= SSD1685_MAX_COLS
            && rows >= SSD1685_MIN_ROWS && rows <= SSD1685_MAX_ROWS;
    default:
        break;
    }
    return 0;
}

esp_err_t ssd1680_init(const ssd1680_config_t *cfg, ssd1680_handle_t* out_handle)
{
    esp_err_t err = ESP_OK;
    if (cfg->controller == SSD168x_UNKNOWN) {
        ESP_LOGE(TAG, "Missing SSD168x variant. Please select one of ssd1680_controller_t.");
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->host == SPI1_HOST) {
        ESP_LOGE(TAG, "interrupt cannot be used on SPI1 host.");
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->framebuffer == NULL) {
        ESP_LOGE(TAG, "Framebuffer is required.");
        return ESP_ERR_INVALID_ARG;
    }
    if (!ssd1680_check_controller_resolution(cfg->controller, cfg->cols, cfg->rows)) {
        ESP_LOGE(TAG, "Resolution not supported by the current controller.");
        return ESP_ERR_INVALID_ARG;
    }

    ssd1680_context_t *ctx = malloc(sizeof(ssd1680_context_t));
    if (ctx == NULL)
        return ESP_ERR_NO_MEM;

    ctx->init_cfg = *cfg;

    spi_device_interface_config_t devcfg = {
        .mode = 0,
        .clock_speed_hz = SSD1680_CLK_FREQ,
        .spics_io_num = cfg->cs_pin,
        .flags = SPI_DEVICE_HALFDUPLEX,
        .queue_size = 1,
        .pre_cb = cfg->dc_pin == -1 ? NULL : ssd1680_spi_pre_transfer_callback
    };

    err = spi_bus_add_device(ctx->cfg.host, &devcfg, &ctx->spi);
    if (err != ESP_OK)
        goto cleanup;

    const gpio_config_t cs_dc_reset_cfg = {
        .pin_bit_mask = BIT64(ctx->cfg.dc_pin) | BIT64(ctx->cfg.reset_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = true,
    };
    gpio_config(&cs_dc_reset_cfg);

    const gpio_config_t busy_cfg = {
        .pin_bit_mask = BIT64(ctx->cfg.busy_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = true,
    };
    gpio_config(&busy_cfg);

    /* 2. Set Initial Configuration
     *    • Define SPI interface to communicate with MCU
     *    • HW Reset
     *    • SW Reset by Command 0x12
     */
    gpio_set_level(ctx->cfg.reset_pin, 0);
    vTaskDelay(1 / portTICK_PERIOD_MS);
    gpio_set_level(ctx->cfg.reset_pin, 1);
    vTaskDelay(10 / portTICK_PERIOD_MS);

    // Acquire bus required
    err = spi_device_acquire_bus(ctx->spi, portMAX_DELAY);
    if (err != ESP_OK) goto cleanup;

    err = ssd1680_cmd_write(ctx, CMD_SWReset);
    if (err != ESP_OK) goto cleanup;
    err = ssd1680_wait_until_idle(ctx);
    if (err != ESP_OK) goto cleanup;

    /* 3. Send Initialization Code
     *    • Set gate driver output by Command 0x01
     *    • Set display RAM size by Command 0x11, 0x44, 0x45
     *    • Set panel border by Command 0x3C
     */
    {
        const uint16_t rows = cfg->rows - 1;

        err = ssd1680_cmd_write(ctx, CMD_DriverOutputControl,
            (uint8_t)(rows & 0xff),
            (uint8_t)(rows >> 8), // SAFETY: ssd1680_check_controller_resolution checks controller support
            (0 << 2) // Selects the 1st output Gate
                | (0 << 1) // Change scanning order of gate driver.
                | (0 << 0), // Change scanning direction of gate driver.
            );
        if (err != ESP_OK) goto cleanup;
    }

    {
        const uint8_t data_entry_setting = (AM_X << 2)
            | DATA_ENTRY_Y_INC_X_INC; // Increment X, at the end of the line, increment Y

        err = ssd1680_cmd_write(ctx, CMD_DataEntryModeSetting, data_entry_setting);
        if (err != ESP_OK) goto cleanup;
    }

    {
        const uint8_t border_waveform_ctrl = (0x00 << 6) // Select VBD [SET TO] GS Transition
            | (0x00 << 4) // Fix Level Setting for VBD [SET TO] VSS
            | (1 << 2) // GS Transition control [SET TO] Follow LUT (NOT RED)
            | (1); // GS Transition setting for VBD [SET TO] LUT1
        err = ssd1680_cmd_write(ctx, CMD_BorderWaveformControl, border_waveform_ctrl);
        if (err != ESP_OK) goto cleanup;
    }

    /* 4. Load Waveform LUT
     *    • Sense temperature by int/ext TS by Command 0x18
     *    • Load waveform LUT from OTP by Command 0x22, 0x20 or by MCU
     *    • Wait BUSY Low
     */

    {
        const uint8_t temp_sensor_selection = TEMP_SENSOR_INTERNAL;
        err = ssd1680_cmd_write(ctx, CMD_TemperatureSensorSelection, temp_sensor_selection);
        if (err != ESP_OK) goto cleanup;
    }

    {
        uint8_t val_byte2; // TODO: find a better name
        switch (ctx->cfg.controller) {
        case SSD1680: val_byte2 = SSD1680_SOURCE_S8_TO_S167; break;
        case SSD1685: val_byte2 = SSD1685_RES_168x384; break; // TODO: select proper resolution
        case SSD168x_UNKNOWN: return ESP_ERR_INVALID_STATE; // Should have been caught by ssd1680_check_controller_resolution
        }

        err = ssd1680_cmd_write(ctx, CMD_DisplayUpdateControl1,
            ((RAM_BypassAs0) << 4) /* Bypass Red RAM */ | RAM_Normal, // BW RAM
            val_byte2
        );
        if (err != ESP_OK) goto cleanup;
    }

    spi_device_release_bus(ctx->spi);
    *out_handle = ctx;
    return ESP_OK;

cleanup:
    if (ctx->spi) {
        spi_device_release_bus(ctx->spi);
        spi_bus_remove_device(ctx->spi);
        ctx->spi = NULL;
    }
    free(ctx);
    return err;
}

esp_err_t ssd1680_deinit(ssd1680_handle_t *ctx) {
    if ((*ctx)->spi == NULL)
        return ESP_ERR_INVALID_ARG;

    esp_err_t err;

    err = spi_device_acquire_bus((*ctx)->spi, portMAX_DELAY);
    if (err != ESP_OK) goto cleanup;

    /* 6. Power Off
     *    • Deep sleep by Command 0x10
     */
    {
        const uint8_t deep_sleep_mode = DEEP_SLEEP_MODE_1;
        err = ssd1680_cmd_write(*ctx, CMD_DeepSleepMode, deep_sleep_mode);
    }

cleanup:
    spi_device_release_bus((*ctx)->spi);
    spi_bus_remove_device((*ctx)->spi);
    (*ctx)->spi = NULL;

    free(*ctx);
    *ctx = NULL;
    return err;
}

esp_err_t ssd1680_flush(ssd1680_handle_t h, ssd1680_rect_t rect) {
    if (h->spi == NULL)
        return ESP_ERR_INVALID_ARG;
    if (rect.x > h->cfg.cols || rect.x + rect.w > h->cfg.cols)
        return ESP_ERR_INVALID_ARG;
    if (rect.y > h->cfg.rows || rect.y + rect.h > h->cfg.rows)
        return ESP_ERR_INVALID_ARG;

    esp_err_t err;

    // Acquire bus required to use SPI_TRANS_CS_KEEP_ACTIVE
    err = spi_device_acquire_bus(h->spi, portMAX_DELAY);
    if (err != ESP_OK) goto defer;

    /* Setup address auto-increment */

    err = ssd1680_cmd_write(h, CMD_SetRAM_StartEnd_X,
        (               rect.x / 8) & 0x1f, // X start on 5 bits
        ((rect.x + rect.w - 1) / 8) & 0x1f, // X end on 5 bits
    );
    if (err != ESP_OK)
        goto defer;

    err = ssd1680_cmd_write(h, CMD_SetRAM_StartEnd_Y,
        (             rect.y) & 0xff, // Y start low
        (             rect.y) >>   8, // Y start high
        (rect.y + rect.h - 1) & 0xff, // Y end low
        (rect.y + rect.h - 1) >>   8, // Y end high
    );
    if (err != ESP_OK)
        goto defer;

    err = ssd1680_cmd_write(h, CMD_SetRAM_Counter_X, rect.x / 8);
    if (err != ESP_OK)
        goto defer;

    err = ssd1680_cmd_write(h, CMD_SetRAM_Counter_Y, rect.y & 0xff, rect.y >> 8);

    /* Send framebuffer row by row */

    spi_transaction_t command = {
        .length = sizeof(uint8_t) * 8,
        .tx_data[0] = CMD_WriteRAM_BW,
        .user = (void*)DC_COMMAND(h->cfg.dc_pin),
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_CS_KEEP_ACTIVE
    };

    err = spi_device_polling_transmit(h->spi, &command);
    if (err != ESP_OK)
        goto defer;

    const size_t row_stride = (h->cfg.cols - 1) / 8 + 1;
    spi_transaction_t payload = {
        .length = h->cfg.cols,
        .tx_buffer = h->cfg.framebuffer,
        .user = (void*)DC_DATA(h->cfg.dc_pin),
        .flags = SPI_TRANS_CS_KEEP_ACTIVE
    };

    for (uint16_t row = 0; row < h->cfg.rows; ++row) {
        payload.tx_buffer = &h->cfg.framebuffer[row * row_stride];
        if (row + 1 == h->cfg.rows) payload.flags = 0;

        err = spi_device_polling_transmit(h->spi, &payload);
        if (err != ESP_OK)
            goto defer;
    }


defer:
    spi_device_release_bus(h->spi);
    return err;
}

esp_err_t ssd1680_full_refresh(ssd1680_handle_t ctx) {
    if (ctx->spi == NULL)
        return ESP_ERR_INVALID_ARG;

    esp_err_t err;

    err = spi_device_acquire_bus(ctx->spi, portMAX_DELAY);
    if (err != ESP_OK) goto defer;

    err = ssd1680_cmd_write(ctx, CMD_DisplayUpdateControl2, 0xF7);
    if (err != ESP_OK) goto defer;

    err = ssd1680_cmd_write(ctx, CMD_MasterActivationUpdateSeq);

defer:
    spi_device_release_bus(ctx->spi);
    return err;
}

esp_err_t ssd1680_wait_until_idle(ssd1680_handle_t ctx) {
    if (ctx->spi == NULL)
        return ESP_ERR_INVALID_ARG;

    while (gpio_get_level(ctx->cfg.busy_pin)) {
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
    return ESP_OK;
}
