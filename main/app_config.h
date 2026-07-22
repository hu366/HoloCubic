/**
 * @file    app_config.h
 * @brief   全局宏定义 —— 引脚映射、队列/定时器参数、阈值等
 *
 * @note    【待用户确认】本文件中所有引脚定义均为暂定值，
 *          请在拿到原理图 / 数据手册后逐一核对并修正。
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  1. 显示 (LCD, SPI2)
 *    屏幕：240×240，驱动 IC ST7789
 * ================================================================ */
#define LCD_SPI_HOST        SPI2_HOST
#define LCD_PIN_SCLK        12      // SPI 时钟
#define LCD_PIN_MOSI        11      // SPI 主出从入
#define LCD_PIN_DC          5       // 数据/命令选择
#define LCD_PIN_RST         GPIO_NUM_NC  // 复位（不接）
#define LCD_PIN_CS          6       // 片选
#define LCD_PIN_BACKLIGHT   GPIO_NUM_NC  // 背光（不接）
#define LCD_HOR_RES         240
#define LCD_VER_RES         240
#define LCD_PIXEL_CLOCK_MHZ 40      // SPI 像素时钟
#define LCD_GAP_X           0       // 列偏移（ST7789 240 列全部映射到物理像素）
#define LCD_GAP_Y           80      // 行偏移（物理像素对应 GRAM 第 80~319 行，即底部 240 行）

/* ------------------------------------------------------------------
 * 全息棱镜镜像标志（现场可调）
 * ------------------------------------------------------------------ */
#define MIRROR_X            1       // 1 = 水平镜像（左右翻转）
#define MIRROR_Y            1       // 1 = 垂直镜像（上下翻转）

/* ================================================================
 *  2. 旋转编码器 (GPIO + PCNT)
 * ================================================================ */
#define ENCODER_PIN_A       9       // 按钮 A
#define ENCODER_PIN_B       10      // 按钮 B
#define ENCODER_PIN_BTN     8       // 按钮 端
#define ENCODER_BTN_DEBOUNCE_MS    10    /* 10ms: 快速短按更灵敏 */
#define ENCODER_BTN_LONG_PRESS_MS  800     // 长按判定阈值

/* ================================================================
 *  3. IMU (I2C, MPU6050 / QMI8658)
 * ================================================================ */
#define IMU_I2C_PORT        0       // I2C_NUM_0
#define IMU_PIN_SCL         1
#define IMU_PIN_SDA         2
#define IMU_I2C_FREQ_HZ     100000  // 100kHz 标准模式
#define IMU_I2C_ADDR        0x68    // MPU6050 默认地址（AD0=0）
#define SHAKE_ACCEL_THRESHOLD   4000    // 加速度计摇晃阈值（原始值，±4g下≈0.49g）
#define IMU_TILT_THRESHOLD_DEG  45.0f   // 倾斜判定阈值（度）

/* ================================================================
 *  4. 音频 (DFPlayer Mini, UART)
 * ================================================================ */
#define DFP_UART_PORT       UART_NUM_1
#define DFP_PIN_TX          17
#define DFP_PIN_RX          18
#define DFP_UART_BAUDRATE   9600
#define DFP_VOLUME_MAX      30
#define DFP_VOLUME_DEFAULT  15
#define DFP_CMD_RETRY_MAX   3

/* ================================================================
 *  5. SD 卡 (SPI3)
 * ================================================================ */
#define SD_SPI_HOST         SPI3_HOST
#define SD_PIN_SCLK         21
#define SD_PIN_MOSI         14
#define SD_PIN_MISO         3       // 避开 LCD SCLK (GPIO13)
#define SD_PIN_CS           7
#define SD_MOUNT_POINT      "/sdcard"
#define SD_MAX_FILES_OPEN   4

/* ================================================================
 *  6. 电池 ADC
 * ================================================================ */
#define BATTERY_ADC_UNIT    ADC_UNIT_1
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_0     // GPIO0 (ADC1_CH0)
#define BATTERY_ADC_ATTEN   ADC_ATTEN_DB_11   // 0~3100mV
// 分压比：R1 (上) / R2 (下)，例如 100k / 100k → 2.0
#define BATTERY_DIVIDER_RATIO   2.0f
#define BATTERY_VOLTAGE_MAX     4.2f    // 锂电池满电电压
#define BATTERY_VOLTAGE_MIN     3.3f    // 锂电池低电电压
#define BATTERY_SAMPLE_COUNT    8       // 多次采样取平均
#define BATTERY_CHECK_INTERVAL_MS  10000   // 每 10s 检测一次

/* ================================================================
 *  7. 队列 & 定时器参数
 * ================================================================ */
#define EVENT_QUEUE_SIZE        16      // 编码器 / 传感器事件队列
#define AUDIO_CMD_QUEUE_SIZE    8       // DFPlayer 指令队列
#define LVGL_TASK_PERIOD_MS     30      // LVGL 刷新周期
#define SENSOR_TASK_PERIOD_MS   20      // IMU 读取周期 (50Hz)
#define LOGIC_TICK_PERIOD_MS    1000    // 宠物衰减/闹钟检查周期 (1Hz)

/* ================================================================
 *  8. 宠物参数
 * ================================================================ */
#define PET_HUNGER_DECAY_PER_SEC    0.1f    // 饥饿每秒衰减量
#define PET_HAPPINESS_DECAY_PER_SEC 0.05f   // 开心每秒衰减量
#define PET_HUNGER_THRESHOLD        30      // 低于此值触发 HUNGRY
#define PET_HAPPINESS_THRESHOLD     30      // 低于此值触发 SAD
#define PET_FEED_AMOUNT             25      // 喂食恢复量
#define PET_PLAY_AMOUNT             25      // 玩耍恢复量
#define PET_SHAKE_COOLDOWN_MS       2000    // 摇晃冷却
#define PET_IDLE_AUTO_RETURN_MS     3000    // 空闲自动归位时间
#define PET_TILT_DEADZONE_DEG       5.0f    // 倾斜死区
#define PET_TILT_SATURATION_DEG     45.0f   // 倾斜饱和角
#define PET_MOVE_SPEED_MAX          3       // 每帧最大移动像素

/* ================================================================
 *  9. 番茄钟参数
 * ================================================================ */
#define POMODORO_WORK_DEFAULT   25      // 默认工作时长 (分钟)
#define POMODORO_BREAK_DEFAULT  5       // 默认休息时长 (分钟)
#define POMODORO_WORK_MAX       99
#define POMODORO_BREAK_MAX      99

/* ================================================================
 *  10. 天气 API（心知天气 v3）
 *     免费用户返回：天气现象文字(text) + 天气代码(code) + 温度(temperature)
 *     https://api.seniverse.com/v3/weather/now.json
 * ================================================================ */
#define WEATHER_API_KEY     "SFCBDWLOzbVENJO-6"  // 心知天气 API 私钥
#define WEATHER_DEFAULT_CITY 0                    // 默认城市索引（城市列表见 weather.c）
#define WEATHER_API_URL_FMT \
    "http://api.seniverse.com/v3/weather/now.json?key=%s&location=%s&language=en&unit=c"
#define WEATHER_UPDATE_INTERVAL_MS  (30 * 60 * 1000)  // 30 分钟刷新一次（免费用户合理频率）
#define WEATHER_HTTP_TIMEOUT_MS     10000              // HTTP 超时
#define WEATHER_STALE_MS            (60 * 60 * 1000)   // 数据过期阈值 1h

/* ================================================================
 *  11. 闹钟
 * ================================================================ */
#define ALARM_CHECK_INTERVAL_MS     1000    // 每秒检查一次
#define ALARM_RING_DURATION_MS      30000   // 响铃持续 30s 无操作自动停

/* ================================================================
 *  12. 电源管理
 * ================================================================ */
#define POWER_LOW_BATTERY_PCT       10      // ≤10% 弹低电警告
#define POWER_IDLE_DIM_MS           30000   // 30s 无操作降低背光亮度
#define POWER_DIM_BRIGHTNESS        30      // 降亮度百分比 (0~100)

/* ================================================================
 *  13. NVS 命名空间
 * ================================================================ */
#define NVS_NAMESPACE           "hologram_pet"
#define NVS_KEY_PET_HUNGER      "pet_hunger"
#define NVS_KEY_PET_HAPPY       "pet_happy"
#define NVS_KEY_PET_TICK        "pet_tick"
#define NVS_KEY_SYS_VOLUME      "sys_volume"
#define NVS_KEY_WIFI_SSID       "wifi_ssid"
#define NVS_KEY_WIFI_PASS       "wifi_pass"

/* ================================================================
 *  14. Wi-Fi
 * ================================================================ */
#define WIFI_DEFAULT_SSID    "天国拯救2077"       // 替换为实际 SSID
#define WIFI_DEFAULT_PASS    "woyoumima"      // 替换为实际密码（为空则开放网络）
#define WIFI_CONNECT_RETRY_MAX      3
#define WIFI_CONNECT_RETRY_INTERVAL_MS  5000
#define WIFI_SCAN_METHOD     WIFI_ALL_CHANNEL_SCAN
#define WIFI_SORT_METHOD     WIFI_CONNECT_AP_BY_SIGNAL
#define WIFI_AUTH_MODE       WIFI_AUTH_WPA2_PSK

/* ================================================================
 *  15. 菜单参数
 * ================================================================ */
#define MENU_SCROLL_COOLDOWN_MS  300  /* 两次切换的最小间隔 */
/* 主菜单切换方式：0=仅倾斜  1=仅旋钮  2=倾斜+旋钮 */
#define MENU_INPUT_MODE  1

/* ================================================================
 *  16. LVGL draw buffer（PSRAM 双缓冲）
 *     240 × 240 × 2 字节(RGB565) = 115,200 字节/帧 × 2 = 230,400 字节
 * ================================================================ */
#define LVGL_DRAW_BUF_SIZE  (LCD_HOR_RES * LCD_VER_RES * 2)

#ifdef __cplusplus
}
#endif

#endif /* APP_CONFIG_H */
