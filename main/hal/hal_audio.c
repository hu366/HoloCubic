/**
 * @file    hal_audio.c
 * @brief   MP3-TF-16P (YX5200) UART 驱动实现
 *
 * 命令帧格式：7E FF 06 CMD FB DH DL [CHK_H CHK_L] EF
 * 校验和 = 0 - (FF + 06 + CMD + FB + DH + DL)  取低16位
 */

#include <string.h>
#include <stdio.h>
#include "hal_audio.h"
#include "app_config.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

/* ---- 指令格式常量 ---- */
#define YX_START    0x7E
#define YX_VERSION  0xFF
#define YX_LEN      0x06
#define YX_END      0xEF
#define YX_FB_NO    0x00
#define YX_FB_YES   0x01

/* ---- 查询响应超时 ---- */
#define QUERY_TIMEOUT_MS  500
#define RX_BUF_SIZE       256   /* 必须 > UART FIFO(128) */

static const char *TAG = "hal_audio";

/* ---- 计算校验和 ---- */
static void calc_checksum(uint8_t *buf) {
    uint16_t sum = 0;
    for (int i = 0; i < YX_LEN; i++) {
        sum += buf[i];
    }
    sum = 0 - sum;
    buf[6] = (uint8_t)(sum >> 8);
    buf[7] = (uint8_t)(sum & 0xFF);
}

/* ---- 发送带校验和的指令 ---- */
static void send_cmd(uint8_t cmd, uint8_t fb, uint8_t dh, uint8_t dl) {
    uint8_t buf[10];
    buf[0] = YX_VERSION;  // 校验从这开始
    buf[1] = YX_LEN;
    buf[2] = cmd;
    buf[3] = fb;
    buf[4] = dh;
    buf[5] = dl;
    calc_checksum(buf);

    uint8_t frame[10];
    frame[0] = YX_START;
    memcpy(&frame[1], buf, 8);
    frame[9] = YX_END;

    uart_write_bytes(AUDIO_UART_PORT, (const char *)frame, 10);
    uart_wait_tx_done(AUDIO_UART_PORT, pdMS_TO_TICKS(50));
    printf("[AUDIO] TX: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
           frame[0], frame[1], frame[2], frame[3], frame[4],
           frame[5], frame[6], frame[7], frame[8], frame[9]);
}

/* ---- 备用：不带校验和的短帧（保留备用） ---- */
__attribute__((unused))
static void send_cmd_no_chk(uint8_t cmd, uint8_t fb, uint8_t dh, uint8_t dl) {
    uint8_t frame[8];
    frame[0] = YX_START;
    frame[1] = YX_VERSION;
    frame[2] = YX_LEN;
    frame[3] = cmd;
    frame[4] = fb;
    frame[5] = dh;
    frame[6] = dl;
    frame[7] = YX_END;
    uart_write_bytes(AUDIO_UART_PORT, (const char *)frame, 8);
    uart_wait_tx_done(AUDIO_UART_PORT, pdMS_TO_TICKS(50));
}

/* ---- 等待并读取响应（阻塞，用于查询指令） ---- */
static int wait_response(uint8_t *out, size_t out_len, uint32_t timeout_ms) {
    uint32_t elapsed = 0;
    int total = 0;
    while (elapsed < timeout_ms) {
        uint8_t c;
        int n = uart_read_bytes(AUDIO_UART_PORT, &c, 1, pdMS_TO_TICKS(10));
        if (n > 0) {
            if (total < (int)out_len) {
                out[total++] = c;
            }
            elapsed = 0;  // 有数据就重置超时
        } else {
            elapsed += 10;
        }
    }
    return total;
}

/* ---- 清空接收缓冲区 ---- */
static void flush_rx(void) {
    uint8_t dummy;
    while (uart_read_bytes(AUDIO_UART_PORT, &dummy, 1, pdMS_TO_TICKS(1)) > 0);
}

/* ================================================================
 *  公开 API
 * ================================================================ */

void hal_audio_init(void) {
    uart_config_t uart_config = {
        .baud_rate  = AUDIO_UART_BAUDRATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };

    ESP_ERROR_CHECK(uart_driver_install(AUDIO_UART_PORT, RX_BUF_SIZE * 2,
                                        0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(AUDIO_UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(AUDIO_UART_PORT,
                                 AUDIO_PIN_TX, AUDIO_PIN_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    /* 等待模块上电初始化完成（~1.5s） */
    vTaskDelay(pdMS_TO_TICKS(1600));

    /* 选 TF 卡作为播放设备 */
    hal_audio_select_tf();

    /* 设置默认音量 */
    hal_audio_set_volume(AUDIO_VOLUME_DEFAULT);

    ESP_LOGI(TAG, "init ok (TX=%d RX=%d baud=%d vol=%d)",
             AUDIO_PIN_TX, AUDIO_PIN_RX,
             AUDIO_UART_BAUDRATE, AUDIO_VOLUME_DEFAULT);
}

void hal_audio_set_volume(uint8_t vol) {
    if (vol > 30) vol = 30;
    send_cmd(0x06, YX_FB_NO, 0, vol);
}

uint8_t hal_audio_get_volume(void) {
    flush_rx();
    send_cmd(0x43, YX_FB_NO, 0, 0);

    uint8_t rx[20];
    int len = wait_response(rx, sizeof(rx), QUERY_TIMEOUT_MS);

    /* 响应格式: 7E FF 06 43 00 00 VOL CHK_H CHK_L EF → VOL 在 rx[6] */
    if (len >= 8 && rx[0] == YX_START && rx[3] == 0x43) {
        return rx[6] <= 30 ? rx[6] : 30;
    }
    return 0xFF;
}

void hal_audio_select_tf(void) {
    send_cmd(0x09, YX_FB_NO, 0, 2);   /* 0x09 + data=2 → TF 卡 */
    vTaskDelay(pdMS_TO_TICKS(250));    /* 手册要求等 200ms */
}

void hal_audio_play_track(uint16_t num) {
    if (num < 1) num = 1;
    if (num > 2999) num = 2999;
    send_cmd(0x03, YX_FB_NO, (uint8_t)(num >> 8), (uint8_t)(num & 0xFF));
}

void hal_audio_play_folder_track(uint8_t folder, uint8_t track) {
    if (folder < 1)  folder = 1;
    if (folder > 99) folder = 99;
    if (track < 1)   track = 1;
    // track 是 uint8_t，天然 ≤255，无需检查上限
    send_cmd(0x0F, YX_FB_NO, folder, track);
}

void hal_audio_play(void) {
    send_cmd(0x0D, YX_FB_NO, 0, 0);
}

void hal_audio_pause(void) {
    send_cmd(0x0E, YX_FB_NO, 0, 0);
}

void hal_audio_stop(void) {
    send_cmd(0x16, YX_FB_NO, 0, 0);
}

void hal_audio_next(void) {
    send_cmd(0x01, YX_FB_NO, 0, 0);
}

void hal_audio_prev(void) {
    send_cmd(0x02, YX_FB_NO, 0, 0);
}

void hal_audio_loop_all(bool on) {
    send_cmd(0x11, YX_FB_NO, 0, on ? 1 : 0);
}

uint16_t hal_audio_get_current_track(void) {
    flush_rx();
    send_cmd(0x4C, YX_FB_NO, 0, 0);

    uint8_t rx[20];
    int len = wait_response(rx, sizeof(rx), QUERY_TIMEOUT_MS);

    /* 响应: 7E FF 06 4C 00 DH DL CHK_H CHK_L EF */
    if (len >= 9 && rx[0] == YX_START && rx[3] == 0x4C) {
        return ((uint16_t)rx[5] << 8) | rx[6];
    }
    return 0;
}

uint16_t hal_audio_get_total_tracks(void) {
    flush_rx();
    send_cmd(0x48, YX_FB_NO, 0, 0);

    uint8_t rx[20];
    int len = wait_response(rx, sizeof(rx), QUERY_TIMEOUT_MS);

    /* 响应: 7E FF 06 48 00 DH DL CHK_H CHK_L EF */
    if (len >= 9 && rx[0] == YX_START && rx[3] == 0x48) {
        return ((uint16_t)rx[5] << 8) | rx[6];
    }
    return 0;
}
