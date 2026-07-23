# 全息棱镜宠物助手 — 技术方案

---

## 一、平台与约束

| 项目 | 内容 |
|------|------|
| 主控 | Waveshare ESP32-S3-Zero（8MB Flash / 8MB PSRAM） |
| SDK | ESP-IDF v6.0.2 |
| GUI框架 | LVGL v9.5 |
| 开发原则 | 优先 ESP 官方库/API；其次社区成熟库；不自行编写底层驱动 |
| 未确认项 | 进入实施阶段前需向用户确认器件型号与数据手册 |

---

## 二、目录结构

```
project/
├── CMakeLists.txt                 # 顶层 CMake
├── sdkconfig                      # 默认 Kconfig（ESP-IDF menuconfig 产物）
├── main/
│   ├── CMakeLists.txt
│   ├── main.c                     # 入口：初始化各模块、启动 FreeRTOS 任务
│   ├── app_config.h               # 全局宏定义（引脚、队列大小、定时器参数等）
│   ├── app_types.h                # 公共枚举 / 结构体 / 回调类型
│   │
│   ├── mode/
│   │   ├── mode_manager.c/.h      # 模式切换状态机（宠物 ↔ 菜单），输入路由
│   │   ├── pet/
│   │   │   ├── pet_engine.c/.h    # 宠物逻辑：状态机、需求衰减、倾斜映射
│   │   │   ├── pet_anim.c/.h      # 动画帧管理与渲染
│   │   │   └── pet_ui.c/.h        # 宠物模式 LVGL 页面组装
│   │   └── menu/
│   │       ├── menu_engine.c/.h   # 菜单导航逻辑（倾斜选择、旋钮确认/返回）
│   │       ├── menu_ui.c/.h       # 菜单主页面 LVGL 布局
│   │       ├── pomodoro.c/.h      # 番茄钟（设置 + 倒计时 + 环形进度条）
│   │       ├── weather.c/.h       # 天气（HTTP → JSON → LVGL 展示）
│   │       ├── clock_alarm.c/.h   # 时钟 & 闹钟（SNTP + RTC + 闹钟管理）
│   │       ├── music_player.c/.h  # 音乐播放器（DFPlayer 控制 + 歌曲列表）
│   │       └── animation_board.c/.h # 动画看板（SD 文件浏览 + 循环播放）
│   │
│   ├── hal/
│   │   ├── hal_display.c/.h       # 显示驱动封装（SPI → LVGL display driver）
│   │   ├── hal_touch_encoder.c/.h # 旋转编码器读取（脉冲计数 + 按键去抖）
│   │   ├── hal_imu.c/.h           # IMU 驱动封装（I2C → 姿态角 / 摇晃检测）
│   │   ├── hal_audio.c/.h         # DFPlayer 驱动封装（UART 指令）
│   │   ├── hal_sd.c/.h            # SD 卡挂载与文件操作（SPI → FatFS）
│   │   ├── hal_battery.c/.h       # 电池 ADC 读取与电量估算
│   │   └── hal_rtc.c/.h           # 片上 RTC 读写封装
│   │
│   ├── service/
│   │   ├── svc_wifi.c/.h          # Wi-Fi 连接管理（STA 模式，连接手机热点）
│   │   ├── svc_http.c/.h          # HTTP 客户端（天气 API 请求，基于 esp_http_client）
│   │   ├── svc_sntp.c/.h          # SNTP 时间同步
│   │   ├── svc_persistence.c/.h   # NVS 读写 + 宠物需求值持续性存储
│   │   ├── svc_volume.c/.h        # 全局音量管理（旋钮映射 + NVS 保存）
│   │   └── svc_power.c/.h         # 低电量检测 & 休眠策略
│   │
│   └── assets/                    # 内置资源（字体、默认占位图等，嵌入固件）
│       └── font_noto_16.c         # 中文最小字库（Noto Sans SC 子集，降级用）
│
├── sdcard_layout/                 # 存储卡文件布局说明书（发给用户准备文件用）
│   └── README.md
├── doc/
│   ├── spec.md
│   ├── plan.md                   # 本文件
│   └── changelog.md
└── components/                    # 第三方组件（通过 idf_component.yml / git submodule 引入）
    └── ...
```

---

## 三、核心数据模型

### 3.1 全局状态

```c
typedef enum {
    MODE_PET = 0,   // 宠物模式
    MODE_MENU       // 菜单模式
} app_mode_t;

typedef struct {
    app_mode_t mode;
    uint8_t     volume;          // 0~30（映射到 DFPlayer 音量）
    int8_t      battery_pct;     // 电量百分比 -1 表示未检测
    bool        sd_ok;           // SD 卡是否成功挂载
    bool        wifi_connected;
} app_state_t;
```

### 3.2 宠物数据

```c
typedef enum {
    PET_IDLE,         // 空闲 (≈50%~100% 需求)
    PET_HUNGRY,       // 饥饿 (<30%)
    PET_SAD,          // 不开心 (<30%)
    PET_SHOCKED,      // 摇晃反应中
    PET_EATING,       // 喂食动画中
    PET_PLAYING,      // 玩耍动画中
    PET_WALKING       // 随机走动
} pet_state_t;

typedef struct {
    // 需求值 0~100
    uint8_t hunger;       // 饥饿度
    uint8_t happiness;    // 开心度

    // 时间戳（Unix 秒），用于计算离线衰减
    uint64_t last_tick;

    // 位置
    struct {
        int16_t x;           // 当前 X 坐标（屏幕坐标）
        int16_t y;           // 当前 Y 坐标
        int16_t home_x;      // 归位目标 X
        int16_t home_y;      // 归位目标 Y
    } position;

    // 动画
    uint32_t idle_timer;       // 距离上次小动作的 ms 数
    uint32_t walk_timer;       // 距离上次走动的 ms 数
    uint32_t shake_cooldown;   // 摇晃冷却剩余 ms
    bool     anim_busy;        // 当前是否播放不可打断动画

    pet_state_t state;
} pet_data_t;
```

### 3.3 番茄钟数据

```c
typedef struct {
    uint8_t work_minutes;       // 工作时长 1~99
    uint8_t break_minutes;      // 休息时长 1~99
    bool    is_work_phase;      // 当前阶段 true=工作 false=休息
    uint32_t remaining_seconds; // 剩余秒数
    bool    running;            // 是否正在计时
} pomodoro_data_t;
```

### 3.4 闹钟数据

```c
#define MAX_ALARMS 10

typedef struct {
    uint8_t  id;
    uint8_t  hour;          // 0~23
    uint8_t  minute;        // 0~59
    bool     enabled;
    bool     repeat[7];     // 周一~周日
} alarm_entry_t;

typedef struct {
    alarm_entry_t entries[MAX_ALARMS];
    uint8_t       count;        // 当前闹钟数量
} alarm_data_t;
```

### 3.5 天气数据

```c
typedef struct {
    char     city[32];
    int8_t   temperature;       // 摄氏度
    char     description[32];   // e.g. "多云"
    char     icon_code[8];      // 天气图标编号
    uint64_t last_update;       // 上次更新时间戳
    bool     stale;             // 数据是否过期
} weather_data_t;
```

### 3.6 NVS 持久化键表

| 键名 | 类型 | 说明 |
|------|------|------|
| `pet_hunger` | u8 | 饥饿度快照 |
| `pet_happy` | u8 | 开心度快照 |
| `pet_tick` | u64 | 上次保存时间戳 |
| `sys_volume` | u8 | 全局音量值 |
| `pomo_work` | u8 | 番茄钟工作时长 |
| `pomo_break` | u8 | 番茄钟休息时长 |
| `alarms` | blob | 闹钟列表序列化 |
| `wifi_ssid` | string | WiFi SSID |
| `wifi_pass` | string | WiFi 密码（简单加密） |

---

## 四、接口定义

### 4.1 HAL 接口

```c
// ---------------------------------- 显示 ----------------------------------
void hal_display_init(void);
void hal_display_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
// 注意：需要传入镜像标志 mirror_x / mirror_y 以适配分光棱镜

// ---------------------------------- 编码器 ----------------------------------
typedef void (*encoder_callback_t)(int8_t step);         // 旋转步进
typedef void (*encoder_btn_callback_t)(bool short_press); // true=短按 false=长按(>800ms)
void hal_encoder_init(encoder_callback_t rot_cb, encoder_btn_callback_t btn_cb);

// ---------------------------------- IMU -----------------------------------
typedef struct {
    float pitch;        // 俯仰角 °
    float roll;         // 横滚角 °
} imu_angles_t;

typedef void (*imu_shake_callback_t)(void);
void hal_imu_init(void);
imu_angles_t hal_imu_get_angles(void);
void hal_imu_set_shake_callback(imu_shake_callback_t cb);
// 摇晃阈值在 app_config.h 中配置 SHAKE_THRESHOLD_G（默认 2.5g）

// ---------------------------------- 音频 -----------------------------------
void hal_audio_init(void);
void hal_audio_set_volume(uint8_t vol);       // 0~30
void hal_audio_play(uint16_t track_id);       // DFPlayer 按编号播放
void hal_audio_play_folder(uint8_t folder, uint8_t file);
void hal_audio_stop(void);
void hal_audio_pause(void);
void hal_audio_resume(void);
void hal_audio_next(void);
void hal_audio_prev(void);

// ---------------------------------- SD 卡 ----------------------------------
bool hal_sd_mount(void);
void hal_sd_unmount(void);
bool hal_sd_list_dir(const char *path, char ***names, size_t *count);
void hal_sd_free_dir_list(char **names, size_t count);

// ---------------------------------- 电池 -----------------------------------
int8_t hal_battery_read_pct(void);            // 返回 0~100，-1 表示无法读取

// ---------------------------------- RTC ------------------------------------
void hal_rtc_set_time(time_t t);
time_t hal_rtc_get_time(void);
```

### 4.2 服务层接口

```c
// ---------------------------------- WiFi -----------------------------------
typedef void (*wifi_connect_cb_t)(bool connected);
void svc_wifi_init(void);
void svc_wifi_connect(const char *ssid, const char *pass);
void svc_wifi_disconnect(void);
bool svc_wifi_is_connected(void);
void svc_wifi_set_callback(wifi_connect_cb_t cb);

// ---------------------------------- HTTP -----------------------------------
// 鉴于 PSRAM 有 8MB，使用 esp_http_client 流式解析 JSON（cJSON）
// 不使用缓冲区累积完整响应
typedef void (*weather_callback_t)(const weather_data_t *data, bool ok);
void svc_weather_fetch(weather_callback_t cb);
// 天气 API URL 在 app_config.h 配置，SSID/PASS 从 NVS 读取

// ---------------------------------- SNTP ------------------------------------
void svc_sntp_sync(void);
bool svc_sntp_synced(void);

// ---------------------------------- 持久化 ----------------------------------
void svc_persistence_init(void);
void svc_persistence_save_pet(const pet_data_t *pet);
void svc_persistence_load_pet(pet_data_t *pet);
void svc_persistence_save_alarms(const alarm_data_t *alarms);
void svc_persistence_load_alarms(alarm_data_t *alarms);
void svc_persistence_save_wifi(const char *ssid, const char *pass);
bool svc_persistence_load_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len);

// ---------------------------------- 音量 ------------------------------------
void svc_volume_init(void);
void svc_volume_set(uint8_t vol);
uint8_t svc_volume_get(void);

// ---------------------------------- 电源 ------------------------------------
void svc_power_init(void);
void svc_power_tick(void);     // 在定时器中定期调用，检查电量并触发低电提示
```

### 4.3 模式管理器接口

```c
// 由 main.c 调用
void mode_manager_init(void);
void mode_manager_on_rotary(int8_t step);    // 来自编码器旋转
void mode_manager_on_btn(bool short_press);  // 来自编码器按键
void mode_manager_on_tilt(imu_angles_t angles); // 来自 IMU 数据
void mode_manager_on_shake(void);            // 来自 IMU 摇晃
void mode_manager_tick(uint32_t dt_ms);      // 每帧逻辑更新（由 LVGL timer 驱动）
```

### 4.4 LVGL 刷新架构

```
[硬件 SPI → DMA] → hal_display_flush → LVGL render → lv_timer_handler
                          ↑
    双 Framebuffer（240×240×2 = 115KB/个 × 2 = 230KB，放入 PSRAM）
    LVGL 使用 PSRAM 分配 draw buffer
```

刷新周期：`lv_timer_handler` 每 30ms 调用一次（≈33fps，满足 ≥10fps）。

---

## 五、FreeRTOS 任务规划

| 任务名 | 优先级 | 栈 | 核心 | 说明 |
|--------|--------|-----|------|------|
| `lvgl_task` | 3 | 8KB | 0 | LVGL tick + render，每 30ms |
| `sensor_task` | 4 | 4KB | 1 | 每 20ms 读取 IMU（50Hz），检测摇晃 |
| `encoder_task` | 5 | 3KB | 0 | PCNT 硬件脉冲计数，去抖，发送事件 |
| `audio_task` | 2 | 4KB | 1 | DFPlayer UART 指令队列 + 播放状态机 |
| `network_task` | 1 | 6KB | 1 | WiFi 连接、SNTP、HTTP 请求 |
| `logic_task` | 2 | 4KB | 0 | 宠物需求衰减（1Hz）、闹钟检查（1Hz）、电池检测（10s） |

**任务同步**：使用 `lv_lock()/lv_unlock()` 保护 LVGL 对象跨任务访问，或在 `lvgl_task` 上下文通过事件队列串行化写入。

---

## 六、数据流图

```
┌──────────┐   倾斜角    ┌──────────────┐
│  IMU     │──────────▶│ sensor_task  │
│  (I2C)   │           │  50Hz        │
└──────────┘           └──────┬───────┘
                              │ event / queue
                              ▼
┌──────────┐   旋转/按键 ┌──────────────┐   事件    ┌───────────────┐
│ Encoder  │──────────▶│ encoder_task │─────────▶│ mode_manager  │
│ (GPIO)   │           │  PCNT        │          │  (状态机)     │
└──────────┘           └──────────────┘          └───┬───────┬───┘
                                                     │       │
                                              UI 调用 │       │ 逻辑调用
                                                     ▼       ▼
                                            ┌──────────┐ ┌──────────┐
                                            │ LVGL UI  │ │ 各个引擎 │
                                            │ 页面渲染 │ │ 数据更新 │
                                            └────┬─────┘ └────┬─────┘
                                                 │            │
                                                 ▼            ▼
                                          ┌────────────────────────┐
                                          │ lvgl_task (30ms)       │
                                          │ 绘制 → display flush   │
                                          └────────────────────────┘
```

---

## 七、关键三方库速查表

| 需求 | 推荐库 | 来源 |
|------|--------|------|
| IMU 驱动 (MPU6050) | `nated0g3/mpu6050` | ESP Registry |
| 天气 JSON 解析 | `DaveGamble/cJSON` | ESP Registry / IDF 自带 |
| SD 卡 FatFS | ESP-IDF 内置 `esp_vfs_fat` | 官方组件 |
| DFPlayer Mini | `kerbars/esp32-dfplayer` | ESP Registry |
| LVGL 字体 | `lvgl/lv_font_conv`（离线工具生成 C 字库） | npm 工具 |

> **向用户确认** —— 实施前需要确认：
> 1. 屏幕型号与驱动 IC（是否 ST7789 / ILI9341 / GC9A01？SPI 引脚定义？）
> 2. IMU 型号（MPU6050 / QMI8658？）
> 3. 编码器规格（每步脉冲数？带按键？）
> 4. DFPlayer 通信方式（UART 还是直接 GPIO 控制？）
> 5. 电池检测的分压电阻比值

---

## 八、实施阶段

### 第一阶段：硬件就绪 & 基础框架（0→1）

| 任务 | 产出 | 前置 |
|------|------|------|
| 1.1 确认所有器件型号与引脚，阅读数据手册 | 引脚映射表 `app_config.h` | 用户提供资料 |
| 1.2 搭建 CMake 工程骨架，引入 ESP-IDF / LVGL 9.5 | 编译通过，LVGL 示例跑通 | 1.1 |
| 1.3 实现 `hal_display`，SPI 驱动屏幕，LVGL 贴一个测试按钮 | 屏幕点亮 | 1.2 |
| 1.4 实现 `hal_encoder`，读取旋钮旋转与按键 | 串口打印旋钮事件 | 1.1 |
| 1.5 实现 `hal_imu`，读取姿态角与检测摇晃 | 串口打印姿态 + 摇晃日志 | 1.1 |
| 1.6 实现 `mode_manager` 框架（两模式切换桩） | 按旋钮切换模式，屏幕显示对应占位文字 | 1.2、1.4 |

**里程碑 M1**：屏幕亮起 + 旋钮可切换两个占位页面。

---

### 第二阶段：宠物模式核心

| 任务 | 产出 | 前置 |
|------|------|------|
| 2.1 实现 `pet_engine` 状态机（空闲 / 饥饿 / 开心 / 受惊 / 吃食 / 玩耍 / 走动） | 逻辑可单元验证 | M1 |
| 2.2 实现 `pet_anim` 帧管理（从 SD 卡读取图片序列，双缓冲轮播） | 宠物动起来 10fps+ | 1.3、hal_sd |
| 2.3 实现倾斜→位置映射，平滑移动 + 边缘停止 + 归位 | 宠物跟手滑动 | 1.5、2.1 |
| 2.4 实现摇晃触发受惊动画 + 音效 + 冷却 | 用力晃，猫吓一跳 | 1.5、hal_audio、2.2 |
| 2.5 实现 `svc_persistence` 宠物需求存取 + 离线衰减计算 | 关机1h 开机掉6点 | M1 |
| 2.6 实现 `svc_volume` + 旋钮调宠物音量 | AC-10 | 1.4、hal_audio |
| 2.7 实现 `pet_ui` LVGL 画面：宠物层 + 状态图标（饥饿/电量）+ 需求图标 | 完整宠物模式验收 | 2.1~2.6 |

**里程碑 M2**：宠物模式达到 AC-1 ~ AC-11 验收标准（除菜单入口外）。

---

### 第三阶段：菜单模式核心

| 任务 | 产出 | 前置 |
|------|------|------|
| 3.1 实现 `menu_engine` + `menu_ui`：5 项列表 + 倾斜选 + 按钮进/返 | AC-13~AC-15 | M1、1.5 |
| 3.2 实现番茄钟（环形进度条 + 设置 + 倒计时 + 阶段切换提示音） | AC-16~AC-18 | 3.1 |
| 3.3 实现天气模块（WiFi → HTTP 天气 API → cJSON 解析 → LVGL 展示） | AC-19~AC-20 | 3.1、svc_wifi、svc_http |
| 3.4 实现时钟模块（SNTP 同步 + RTC 保持 + 闹钟增删改 + 触发） | AC-21~AC-22 | 3.1、svc_sntp、hal_rtc |
| 3.5 实现音乐播放器（SD 扫描 MP3 列表 + DFPlayer 控制 + 播放/暂/上下曲） | AC-23~AC-25 | 3.1、hal_audio、hal_sd |
| 3.6 实现动画看板（SD 浏览文件夹 → 选中循环播放动画序列） | AC-26 | 3.1、hal_sd、pet_anim 复用 |
| 3.7 菜单模式旋钮调全局音量 | AC-27 | 2.6 |

**里程碑 M3**：菜单模式 5 项功能全部验收通过。

---

### 第四阶段：边界处理 & 全息矫正

| 任务 | 产出 | 前置 |
|------|------|------|
| 4.1 全息镜像翻转（LVGL 旋转/镜像配置） | AC-28~AC-29 从棱镜看正常 | 1.3 |
| 4.2 存储卡边界处理（未插卡 / 缺文件夹 / 缺字库 / 缺配置） | EC-1~EC-4 | 贯穿 |
| 4.3 电量检测 & 低电提示 & 长时间不操作降亮度 | EC-5~EC-6 | hal_battery |
| 4.4 宠物边界 case（需求归零、喂食已满、连续摇晃、平放） | EC-7~EC-11 | 2.1 |
| 4.5 输入冲突 & 误触防抖 | EC-12~EC-14 | 1.4 |
| 4.6 网络/天气异常全场景覆盖 | EC-15~EC-18 | 3.3 |
| 4.7 音频混合策略（番茄钟优先）& 模式切换音乐停止 | EC-19~EC-23、EC-26 | 3.2、3.5 |
| 4.8 极端场景（翻倒 180° / 持续剧烈摇晃 / 读写中掉电） | EC-27~EC-29 | 贯穿 |
| 4.9 多闹钟同时触发处理 | EC-25 | 3.4 |

**里程碑 M4**：全部边界情况覆盖，系统稳定可靠。

---

### 第五阶段：打磨 & 验收

| 任务 | 产出 | 前置 |
|------|------|------|
| 5.1 性能优化：确保 ≥10fps 动画、模式切换 <500ms、音量延迟 <100ms | 非功能需求达标 | M4 |
| 5.2 长时间运行稳定性测试（24h 无死机 / 无内存泄漏） | 内存/任务栈水位监控 | M4 |
| 5.3 用户 SD 卡文件布局说明文档 `sdcard_layout/README.md` | 用户按文档放文件即用 | M4 |
| 5.4 整机联调 & 根据实际棱镜效果微调镜像参数 | 最终验收 | 全阶段 |

**里程碑 M5**：全功能验收通过，可交付。

---

## 九、风险与缓解

| 风险 | 影响 | 缓解 |
|------|------|------|
| PSRAM 有 8MB，双帧缓冲 + 动画缓存充足 | 动画卡顿或 OOM | 动画帧即时从 SD 流式读取，不常驻内存；仅缓冲下一帧 |
| LVGL v9.5 API 与参考资料有差异 | 开发受阻 | 阶段性查 LVGL v9.5 官方文档确认 |
| DFPlayer UART 指令不可靠 | 音效丢失或错乱 | 加入应答检测 + 重发机制 |
| SD 卡 SPI 读与显示刷新争抢 SPI 总线 | 画面撕裂 | 分时使用 SPI，或动画帧读取放在 VBlank 间隙 |
| 棱镜光路导致镜像方向判断不准确 | 最终显示方向错误 | 预留 `MIRROR_X` / `MIRROR_Y` 宏开关，现场可调 |
