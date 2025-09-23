#include "ld2410s.h"

#include "driver/uart.h"

#include <string.h>

enum : uint32_t {
    FRAME_BEGIN = 0xFAFBFCFD,
    FRAME_END = 0x01020304
};

enum : uint8_t {
    MINIMAL_REPORT_FRAME_BEGIN = 0x6E,
    MINIMAL_REPORT_FRAME_END = 0x62
};

struct ld2410s_handle {
    uart_port_t uart;
};

struct ld2410s_frame_header {
    uint16_t cmd_len; /* Total length of the following fields excluding itself */
    ld2410s_cmd_t cmd_word;
};
_Static_assert(sizeof(struct ld2410s_frame_header) == sizeof(int));

static inline uint16_t payload_length(struct ld2410s_frame_header header) {
    return header.cmd_len - sizeof(header.cmd_word);
}

#define ld2410s_send_frame(Handle, Cmd, TxPtr) ld2410s_send_frame_((Handle), \
        (struct ld2410s_frame_header){ .cmd_len = sizeof(*(TxPtr)) + sizeof(ld2410s_cmd_t), .cmd_word = (Cmd) }, \
        (const uint8_t *)(TxPtr))

static esp_err_t ld2410s_send_frame_(ld2410s_t handle, struct ld2410s_frame_header header, const uint8_t *tx)
{
    const struct {
        uint32_t frame_begin;
        struct ld2410s_frame_header header;
    } prelude = { .frame_begin = FRAME_BEGIN, .header = header };

    if (uart_write_bytes(handle->uart, &prelude, sizeof(prelude)) == -1)
        return ESP_ERR_INVALID_ARG;

    const uint16_t tx_len = payload_length(header);
    if (tx_len > 0) {
        if (uart_write_bytes(handle->uart, tx, tx_len) == -1)
            return ESP_ERR_INVALID_ARG;
    }

    const uint32_t frame_end = FRAME_END;
    if (uart_write_bytes(handle->uart, &frame_end, sizeof(frame_end)) == -1)
        return ESP_ERR_INVALID_ARG;

    return uart_wait_tx_done(handle->uart, 10 * portTICK_PERIOD_MS);
}

#define ld2410s_recv_frame(Handle, Cmd, RxPtr) ld2410s_recv_frame_((Handle), \
        (struct ld2410s_frame_header){ .cmd_len = sizeof(*(RxPtr)) + sizeof(ld2410s_cmd_t), .cmd_word = (Cmd) }, \
        (uint8_t *)(RxPtr))

static esp_err_t ld2410s_recv_frame_(ld2410s_t handle, struct ld2410s_frame_header header, uint8_t *rx)
{
    uart_flush(handle->uart);
    uint32_t received_frame_begin = 0;
    if (uart_read_bytes(handle->uart, &received_frame_begin, sizeof(received_frame_begin), 120 * portTICK_PERIOD_MS) == -1)
        return ESP_ERR_INVALID_RESPONSE;
    if (received_frame_begin != FRAME_BEGIN) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    struct ld2410s_frame_header received_header;
    if (uart_read_bytes(handle->uart, &received_header, sizeof(received_header), sizeof(received_header) * portTICK_PERIOD_MS) == -1)
        return ESP_ERR_INVALID_RESPONSE;
    if (received_header.cmd_len != header.cmd_len || received_header.cmd_word != header.cmd_word)
        return ESP_ERR_INVALID_RESPONSE;

    if (uart_read_bytes(handle->uart, rx, payload_length(header), payload_length(header) * portTICK_PERIOD_MS) == -1)
        return ESP_ERR_INVALID_RESPONSE;

    uint32_t received_frame_end;
    if (uart_read_bytes(handle->uart, &received_frame_end, sizeof(received_frame_end), 2 * portTICK_PERIOD_MS) == -1)
        return ESP_ERR_INVALID_RESPONSE;
    if (received_frame_end != FRAME_END)
        return ESP_ERR_INVALID_RESPONSE;

    return ESP_OK;
}

esp_err_t ld2410s_init(uart_port_t uart, ld2410s_t *handle, int tx_pin, int rx_pin) {
    esp_err_t err;

    err = uart_driver_install(UART_NUM_1, SOC_UART_FIFO_LEN + 4 /* rx buf */, SOC_UART_FIFO_LEN + 4 /* tx buf */, 10, NULL, 0);
    if (err != ESP_OK)
        return err;

    err = uart_param_config(uart, &(uart_config_t){
        .baud_rate = LD2410S_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    });
    if (err != ESP_OK)
        return err;

    err = uart_set_pin(UART_NUM_1, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
        return err;

    struct ld2410s_handle *h = malloc(sizeof(struct ld2410s_handle));
    if (h == NULL)
        return ESP_ERR_NO_MEM;

    h->uart = uart;
    *handle = h;

    return err;
}

typedef struct {
    uint16_t enable_configuration_value;
} ld2410s_enable_config_cmd_t;

typedef struct {
    uint16_t enable;
    uint16_t protocol_version_number;
    uint16_t buffer_size;
} ld2410s_enable_config_res_t;

ld2410s_cfg_t ld2410s_cfg_begin(ld2410s_t handle) {
    esp_err_t err;

    const ld2410s_enable_config_cmd_t cmd = { .enable_configuration_value = 1 };
    err = ld2410s_send_frame(handle, LD2410S_BEGIN_CONFIGURATION, &cmd);
    if (err != ESP_OK)
        return NULL;

    ld2410s_enable_config_res_t res;
    err = ld2410s_recv_frame(handle, LD2410S_CMD_ACK(LD2410S_BEGIN_CONFIGURATION), &res);
    if (err != ESP_OK)
        return NULL;

    return (ld2410s_cfg_t) handle;
}

typedef struct {
} ld2410s_end_config_cmd_t;
_Static_assert(sizeof(ld2410s_end_config_cmd_t) == 0);

typedef enum : uint16_t {
   LD2410S_ACK_SUCCESS = 0,
   LD2410S_ACK_FAILURE = 1
} ld2410s_ack_t;

typedef struct {
    ld2410s_ack_t ack;
} ld2410s_end_config_res_t;

esp_err_t ld2410s_cfg_end(ld2410s_cfg_t cfg) {
    ld2410s_t handle = (ld2410s_t)cfg;
    esp_err_t err;

    err = ld2410s_send_frame(handle, LD2410S_END_CONFIGURATION, (ld2410s_end_config_cmd_t *)NULL);
    if (err != ESP_OK)
        return err;

    ld2410s_end_config_res_t res = { .ack =  LD2410S_ACK_FAILURE };
    err = ld2410s_recv_frame(handle, LD2410S_CMD_ACK(LD2410S_END_CONFIGURATION), &res);
    if (err != ESP_OK)
        return err;

    return res.ack == LD2410S_ACK_SUCCESS ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

typedef struct {
    uint16_t __reserved_must_be_zero1;
    ld2410s_report_mode_t mode;
    uint16_t __reserved_must_be_zero2;
} ld2410s_switch_output_mode_cmd_t;

typedef struct {
    ld2410s_ack_t ack;
} ld2410s_switch_output_mode_res_t;

esp_err_t ld2410s_cfg_set_reporting_mode(ld2410s_cfg_t cfg, ld2410s_report_mode_t mode) {
    ld2410s_t handle = (ld2410s_t)cfg;
    esp_err_t err;

    ld2410s_switch_output_mode_cmd_t cmd = { .mode = mode };
    err = ld2410s_send_frame(handle, LD2410S_SWITCH_OUTPUT_FORMAT, &cmd);
    if (err != ESP_OK)
        return err;

    ld2410s_switch_output_mode_res_t res = { .ack =  LD2410S_ACK_FAILURE };
    err = ld2410s_recv_frame(handle, LD2410S_CMD_ACK(LD2410S_SWITCH_OUTPUT_FORMAT), &res);
    if (err != ESP_OK)
        return err;

    return res.ack == LD2410S_ACK_SUCCESS ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

typedef struct {
    ld2410s_ack_t ack;
} ld2410s_write_params_res_t;

esp_err_t ld2410s_cfg_write_params_(ld2410s_cfg_t cfg, ld2410s_cmd_t cmd, const ld2410s_param_t *params, uint8_t params_size) {
    ld2410s_t handle = (ld2410s_t)cfg;
    esp_err_t err;

    struct ld2410s_frame_header cmd_header = {
        .cmd_len = sizeof(cmd) + params_size,
        .cmd_word = cmd
    };
    err = ld2410s_send_frame_(handle, cmd_header, (uint8_t *)params);
    if (err != ESP_OK)
        return err;

    ld2410s_write_params_res_t res = { .ack =  LD2410S_ACK_FAILURE };
    err = ld2410s_recv_frame(handle, LD2410S_CMD_ACK(cmd), &res);
    if (err != ESP_OK)
        return err;

    return res.ack == LD2410S_ACK_SUCCESS ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t ld2410s_poll_minimal_report(ld2410s_t handle, ld2410s_minimal_report_t *report) {
    size_t avail_len;
    esp_err_t err = uart_get_buffered_data_len(handle->uart, &avail_len);
    if (err != ESP_OK)
        return err;
    if (avail_len < sizeof(*report))
        return ESP_ERR_NOT_FOUND;

    uint8_t frame_head;
    if (uart_read_bytes(handle->uart, &frame_head, sizeof(frame_head), 1 * portTICK_PERIOD_MS) == -1)
        return ESP_ERR_INVALID_RESPONSE;
    if (frame_head != MINIMAL_REPORT_FRAME_BEGIN)
        return ESP_ERR_INVALID_RESPONSE;

    if (uart_read_bytes(handle->uart, report, sizeof(*report), 1 * portTICK_PERIOD_MS) == -1)
        return ESP_ERR_INVALID_RESPONSE;

    uint8_t frame_end;
    if (uart_read_bytes(handle->uart, &frame_end, sizeof(frame_end), 1 * portTICK_PERIOD_MS) == -1)
        return ESP_ERR_INVALID_RESPONSE;
    if (frame_end != MINIMAL_REPORT_FRAME_END)
        return ESP_ERR_INVALID_RESPONSE;


    return ESP_OK;
}
