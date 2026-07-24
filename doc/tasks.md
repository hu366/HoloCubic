# 全息棱镜宠物助手 — 原子任务列表

> **规则**：每个任务只改一个文件。奇数任务写测试，偶数任务写实现。测试先行。

---

## 阶段 0：工程骨架搭建（纯配置文件，无需测试）✅ 已完成

| 任务 | 类型 | 文件 | 说明 | 状态 |
|------|------|------|------|------|
| 0.1 | 搭建 | `CMakeLists.txt` | 顶层 CMake | ✅ |
| 0.2 | 搭建 | `main/CMakeLists.txt` | main 组件 CMake | ✅（简化为 LCD 最小集） |
| 0.3 | 搭建 | `sdkconfig` | 默认 Kconfig | ✅（修复字体、FatFS、优化级别） |
| 0.4 | 定义 | `main/app_config.h` | 全局引脚宏 | ✅（SD MISO 13→3、MIRROR_Y→1） |
| 0.5 | 定义 | `main/app_types.h` | 公共类型 | ✅ |

---

## 阶段 1：硬件抽象层 HAL（M1：屏幕亮起 + 旋钮可切换占位页面）

### 1.1 显示驱动 ✅ 已完成

| 任务 | 类型 | 文件 | 说明 | 状态 |
|------|------|------|------|------|
| 1 | **测试** | `main/hal/test_hal_display.c` | 测试 `hal_display_init` 初始化流程、`hal_display_flush` 回调参数有效性（area 裁剪、px_map 非空）、双帧缓冲 PSRAM 分配 | ✅ |
| 2 | 实现 | `main/hal/hal_display.c` + `hal_display.h` | SPI→ST7789 驱动、LVGL display driver 注册、双 Framebuffer（240×240×2×2 = 230KB 放 PSRAM）、`hal_display_flush` 实现 | ✅ 屏幕已点亮 |

### 1.2 旋转编码器 ✅ 已完成

| 任务 | 类型 | 文件 | 说明 | 状态 |
|------|------|------|------|------|
| 3 | **测试** | `main/hal/test_hal_touch_encoder.c` | 测试 PCNT 脉冲计数正反转方向、按键去抖逻辑（短按 <800ms / 长按 ≥800ms）、回调注册非空 | ✅ |
| 4 | 实现 | `main/hal/hal_touch_encoder.c` + `hal_touch_encoder.h` | PCNT 硬件脉冲计数、旋转步进 `encoder_callback_t` 回调、按键去抖 + 短按/长按区分 `encoder_btn_callback_t` | ✅ 已验收（CW/CCW方向正确，短按/长按区分正确） |

### 1.3 IMU 驱动

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 5 | **测试** | `main/hal/hal_imu.h` + `main/main.c` | ✅ 测试直接写在 main.c 里，hal 目录只放驱动封装。测试覆盖：init 幂等、角度范围、加速度值、摇晃回调注册 |
| 6 | 实现 | `main/hal/hal_imu.c` | ✅ I2C 初始化 MPU6050（±4g 加速度计 + ±250°/s 陀螺仪）、读取姿态角（pitch/roll）、**HoloCubic 迟滞回差方案**检测左右摇晃（ax_raw 阈值 4000）+ 300ms 冷却防反冲 |

### 1.4 模式管理器（框架桩）✅ 已完成

| 任务 | 类型 | 文件 | 说明 | 状态 |
|------|------|------|------|------|
| 7 | **测试** | `main/mode/test_mode_manager.c` | 测试两模式切换（PET ↔ MENU）、编码器旋转在当前模式路由、按键短按/长按事件分发、倾斜角度传递、摇晃事件传递、tick 传递 | ✅（已删除，测试直接上板验证） |
| 8 | 实现 | `main/mode/mode_manager.c` + `mode_manager.h` | 模式状态机（`MODE_PET` ↔ `MODE_MENU`）、输入路由：旋转/按键 → 当前模式处理器、`mode_manager_on_*` 系列函数实现 | ✅ 已验收（路由正确，模式切换正常） |

### 1.5 入口 main.c

| 任务 | 类型 | 文件 | 说明 | 状态 |
|------|------|------|------|------|
| 9 | **测试** | ~~`main/test_main.c`~~（已删除） | 测试各模块初始化顺序正确性（模拟 init 调用不崩溃）、FreeRTOS 任务创建参数（优先级/栈/核心） | ✅ |
| 10 | 实现 | `main/main.c` | 初始化流程：HAL 各模块 → 服务层 → mode_manager → LVGL；创建 6 个 FreeRTOS 任务（lvgl / sensor / encoder / audio / network / logic）；启动调度器 | ✅ |

> **里程碑 M1**：屏幕亮起 + 旋钮可切换两个占位页面。✅

---

## 阶段 2：菜单模式核心（M2：菜单模式 5 项功能验收通过）

### 2.1 菜单导航引擎 + 菜单主页面 ✅ 已完成（含栈式子页面框架）

| 任务 | 类型 | 文件 | 说明 | 状态 |
|------|------|------|------|------|
| 11 | **测试** | `main/mode/menu/test_menu_engine.c` | 测试 5 项菜单双向滚动、倾斜→选项映射、按键进入/确认退出、边界循环、300ms 冷却 | ✅ |
| 12 | 实现 | `main/mode/menu/menu_engine.c` + `.h` | **二级状态机（TOP/SUB）**、倾斜轮播+冷却、输入全部转发给回调、`menu_engine_go_back()` 返回轮播。同时改 `mode_manager.c`（MENU 事件路由、摇晃屏蔽、默认 MENU）、`CMakeLists.txt`、`app_config.h` | ✅ |
| 13 | **测试** | `main/mode/menu/test_menu_ui.c` | 测试 LVGL 屏幕创建/销毁、选中项缓存、页面切换动画方向 | ✅ |
| 14 | 实现 | `main/mode/menu/menu_ui.c` + `.h` | **轮播（滑动切换）** + **栈式子页面框架（push/pop 无限嵌套）** + **可滚动按钮列表（3 按钮视口+滚动层）** + **手动动画引擎（A_PAGE/A_CAROUSEL/A_SCROLL）** + **跨核安全架构（Core 1 设标志/ Core 0 操作 LVGL）**。同时改 `main.c`（默认 MENU、栈 12KB、颜色测试已删）、`hal_display.c`（RGB565 字节交换） | ✅ |

> **🔧 子模块开发手册（Task 15~38 必读）**
>
> ### 子页面框架 API
> ```c
> // 1. 创建页面
> const char *btns[] = {"Back", "Start", "Stop"};
> menu_page_t *pg = menu_ui_page_create("Pomodoro", btns, 3, my_callback);
>
> // 2. 注册到菜单项
> menu_ui_register_creator(MENU_ITEM_POMODORO, my_creator);
>
> // 3. 按钮回调（Core 0 上下文，可安全操作 LVGL！）
> void my_callback(int index) {
>     if (index == 0) return;  // Back 已自动处理
>     if (index == 1) { /* Start 逻辑 */ }
> }
>
> // 4. 嵌套子页面
> menu_page_t *sub = menu_ui_page_create("Settings", btns2, n, cb2);
> menu_ui_push_page(sub);  // 自动滑入动画
> ```
>
> ### 关键约束
> - **不用 `lv_anim`**，用手动 tick 驱动动画
> - **所有 UI 改动后调 `lv_refr_now(NULL)`**，LVGL 脏区标记在该硬件不生效
> - 菜单显示名当前用英文，**Task 73-74 中文字库就绪后**替换 `s_names[]` 为中文
> - `menu_page_t.container` 可直接往里加自定义 LVGL 控件
> - `menu_page_t.focus_cursor` 可替换样式（改 `lv_obj_set_style_*`）

### 2.2 番茄钟

> **⚠ 开发前必读**：番茄钟作为子页面运行。进入时调用 `menu_engine_set_sub_callback()` 注册输入回调接收倾斜/按键事件，点 [返回] 时调用 `menu_engine_request_exit()` 触发确认弹窗。**不要用 `lv_anim`**（会导致 task_wdt），UI 更新后调 `lv_refr_now(NULL)` 强制刷新。中文等 Task 73-74 字库就绪后再加。

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 15 | **测试** | ~~`main/mode/menu/test_pomodoro.c`~~（已删除） | 测试工作时长/休息时长设置 [1,99] 边界、倒计时递减正确性、阶段切换（工作→休息→工作）、running 标志、环形进度条角度计算 | ✅ |
| 16 | 实现 | `main/mode/menu/pomodoro.c/.h` + `menu_ui.c/.h` + `main.c` + `CMakeLists.txt` | 番茄钟逻辑 + LVGL 环形进度条 + 单按钮轮播 + 旋钮调节 + 聚合初始化框架 | ✅ |

### 2.3 天气模块

> **⚠ 开发前必读**：同上（子页面规范），此外天气需要异步 HTTP 请求，结果通过回调更新 UI。

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 17 | **测试** | ~~`main/mode/menu/test_weather.c`~~（已删除） | 测试天气 JSON 解析（city/temperature/description/icon_code 字段提取）、stale 标记逻辑、HTTP 失败重试、温度边界值 | ✅ |
| 18 | 实现 | `main/mode/menu/weather.c/.h` + `service/svc_wifi.h` + `service/svc_http.h` + `menu_ui.c` + `CMakeLists.txt` | **心知天气 API v3** + cJSON 解析 + LVGL 展示 + **预设 8 城市列表（guangzhou/shenzhen/beijing/shanghai/chengdu/hangzhou/wuhan/nanjing）** + **城市选择弹窗（竖向列表 + 旋钮选城市 + 半透明遮罩）** + WiFi 状态感知 + 30 分钟自动刷新 | ✅ |

### 2.4 天气依赖：Wi-Fi 服务

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 19 | **测试** | ~~`main/service/test_svc_wifi.c`~~（已删除） | 测试 Wi-Fi 连接状态机（断开→连接中→已连接→失败）、SSID/PASS 从 NVS 读取、回调触发、重连逻辑 | ✅ |
| 20 | 实现 | `main/service/svc_wifi.c/.h` + `main.c` + `CMakeLists.txt` | Wi-Fi STA 模式连接管理：`svc_wifi_connect/disconnect`、连接状态回调、自动重连（最大 3 次）、凭据三级优先级（参数>NVS>app_config.h 默认值） | ✅ |

### 2.5 天气依赖：HTTP 客户端

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 21 | **测试** | ~~`main/service/test_svc_http.c`~~（已删除） | 测试 HTTP GET 请求构建、响应状态码处理（200/404/超时）、流式读取回调、内存边界（≤4KB 缓冲） | ✅ |
| 22 | 实现 | `main/service/svc_http.c/.h` + `CMakeLists.txt` | esp_http_client 流式封装：`svc_http_get_stream`、chunk 回调、状态码检查（200 OK）、超时 10s | ✅ |

### 2.6 时钟 & 闹钟

> **⚠ 开发前必读**：同上（子页面规范）。

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 23 | **测试** | ~~`main/mode/menu/test_clock_alarm.c`~~（已删除） | ✅ 测试闹钟增删改查、enabled 开关、7 天重复规则、闹钟触发时间匹配（hour/minute 比较）、同时触发多闹钟 |
| 24 | 实现 | `main/mode/menu/clock_alarm.c` + `.h` | ✅ 闹钟管理：最大 10 个闹钟 CRUD + 弹窗式向导（Hour→Minute→Repeat 预设→Confirm）+ LVGL 时钟展示（HH:MM:SS + 日期/星期）+ 多闹钟触发队列 |

### 2.7 时钟依赖：SNTP 同步

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 25 | **测试** | ~~`main/service/test_svc_sntp.c`~~（已删除） | ✅ SNTP 同步 + RTC 时间更新测试 |
| 26 | 实现 | `main/service/svc_sntp.c` + `.h` + `main.c` | ✅ esp_sntp 经典 API、WiFi 连上后自动对时、CST-8 时区 |

### 2.8 时钟依赖：RTC 驱动

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 27 | **测试** | ~~`main/hal/test_hal_rtc.c`~~（已删除） | ✅ RTC 读写往返、断电保持测试 |
| 28 | 实现 | `main/hal/hal_rtc.c` + `.h` + `main.c` | ✅ RTC_DATA_ATTR 持久化、SNTP 同步后自动保存 |

### 2.9 音频驱动（DFPlayer Mini）

> **⚠ 开发前必读**：DFPlayer 通过 UART 串口控制，代码只管发指令，不碰 DFPlayer 自带的 TF 卡。DFPlayer 自己读卡播放。

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 29 | **测试** | `main/hal/test_hal_audio.c` | 测试 DFPlayer UART 指令帧格式、音量范围 [0,30]、播放/暂停/停止/下一曲/上一曲指令序列、ACK 应答检测 |
| 30 | 实现 | `main/hal/hal_audio.c` | DFPlayer Mini UART 指令封装：`hal_audio_set_volume` / `play` / `stop` / `pause` / `resume` / `next` / `prev`、指令队列 + ACK 检测 + 重发 |

### 2.10 SD 卡驱动（SPI）

> **⚠ 开发前必读**：独立 SPI SD 卡模块，存动画帧、背景图、字库等（不是 DFPlayer 那张 TF 卡）。挂载 FatFS，提供文件/目录操作接口。

| 任务 | 类型 | 文件 | 说明 | 状态 |
|------|------|------|------|------|
| 31 | **测试** | `main/hal/test_hal_sd.c` | 测试 SD 卡挂载/卸载、目录列表读取、文件存在性检查、无卡/损坏的边界返回 | ✅（已删除） |
| 32 | 实现 | `main/hal/hal_sd.c` | SPI → FatFS SD 卡挂载（`esp_vfs_fat`）、`hal_sd_mount/unmount`、`hal_sd_list_dir` 目录遍历、内存释放 `hal_sd_free_dir_list` | ✅ 26 PASS / 0 FAIL |

### 2.11 音乐播放器

> **⚠ 开发前必读**：依赖 hal_audio（Task 30）+ hal_sd（Task 32）。SPI SD 卡存 MP3 列表，hal_audio 发 UART 指令控制 DFPlayer 播放。

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 33 | **测试** | `main/mode/menu/test_music_player.c` | 测试歌曲列表加载、播放/暂停/停止/上曲/下曲状态转换、文件夹切换、SD 卡无 MP3 文件的边界 |
| 34 | 实现 | `main/mode/menu/music_player.c` | SPI SD 卡扫描 MP3 列表、DFPlayer 控制（播放/暂停/停止/上一曲/下一曲）、LVGL 歌曲列表 UI、文件夹浏览 |

### 2.12 动画看板

> **⚠ 开发前必读**：同上（子页面规范），依赖 hal_sd（Task 32）。

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 35 | **测试** | `main/mode/menu/test_animation_board.c` | 测试 SD 文件夹浏览→选中→循环播放流程、文件夹空/无动画文件的边界、播放/停止控制 |
| 36 | 实现 | `main/mode/menu/animation_board.c` | SD 卡动画文件夹浏览、选中后循环播放动画帧序列（复用 `pet_anim` 帧管理）、LVGL 播放界面 |

### 2.13 菜单模式音量调节

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 37 | **测试** | `main/mode/menu/test_menu_volume.c` | 测试菜单模式下旋钮调节全局音量的路由、音量值在 pet/menu 模式间共享、NVS 保存 |
| 38 | 实现 | `main/mode/menu/menu_volume.c` | 菜单模式旋钮→`svc_volume_set` 映射、音量 OSD 显示（LVGL 音量条短暂弹出） |

> **里程碑 M2**：菜单模式 5 项功能全部验收通过。

---

## 阶段 3：宠物模式核心（M3：宠物模式完整验收）

### 3.1 宠物引擎状态机

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 39 | **测试** | `main/mode/pet/test_pet_engine.c` | 测试状态迁移表：IDLE→HUNGRY（饥饿 <30）、IDLE→SAD（开心 <30）、任意→SHOCKED（摇晃）、IDLE→EATING/WALKING/PLAYING；需求值衰减 1Hz 计算；需求值边界 [0,100]；喂食/玩耍后需求回升 |✅
| 40 | 实现 | `main/mode/pet/pet_engine.c` | 7 状态宠物状态机（IDLE/HUNGRY/SAD/SHOCKED/EATING/PLAYING/WALKING）、需求衰减（饥饿 +0.1/s，开心 -0.05/s）、喂食/玩耍施加需求增量、离线衰减计算（基于 last_tick 时间差） |✅

### 3.2 宠物动画帧管理

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 41 | **测试** | `main/mode/pet/test_pet_anim.c` | 测试帧序列加载、双缓冲轮播切换、动画状态→帧索引映射、不可打断动画标志位、动画完成回调 | ✅ |
| 42 | 实现 | `main/mode/pet/pet_anim.c` | 从 SD 卡按状态读取图片帧序列、双缓冲轮播（前一帧显示 + 下一帧预读）、≥10fps 播放、`anim_busy` 标志管理 | ✅ |

### 3.3 倾斜→位置映射 & 平滑移动

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 43 | **测试** | `main/mode/pet/test_pet_motion.c` | 测试 tilt→速度映射函数（死区/线性/饱和三段）、位置边界裁剪、归位缓动 | ✅ |
| 44 | 实现 | `main/mode/pet/pet_motion.c` | 倾斜角→移动速度映射（死区 ±5° / 线性 / 饱和 ±45°）、平滑位置更新、3s 空闲归位 | ✅ |
| 45 | **测试** | `main/mode/pet/test_pet_shake.c` | 测试摇晃触发/冷却阻断/冷却倒计时 | ✅ |
| 46 | 实现 | `main/mode/pet/pet_shake.c` | 摇晃→SHOCKED 状态 + 2s 冷却窗口 | ✅ |
| ~~47~~ | ~~48~~ | ~~3.5 持久化~~ | **已砍**：用户决定去掉需求系统 |
| ~~49~~ | ~~50~~ | ~~3.6 音量管理~~ | **已砍** |
| 51 | **测试** | — | 跳过，直接写实现 | — |
| 52 | 实现 | `main/mode/pet/pet_ui.c` | 简化版：IMU→视角旋转 + 位置飘动 + 摇晃响应。用 lv_refr_now 强制刷屏（LVGL v9 脏区在此硬件不生效） | ✅ |

> **里程碑 M3**：宠物模式核心交互完成（IMU 旋转视角 + 倾斜飘动 + 摇晃受惊）。

---

## 阶段 4：边界处理 & 全息矫正（M4：全部边界覆盖，系统稳定）

### 4.1 全息镜像翻转

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 53 | **测试** | `main/hal/test_hal_mirror.c` | 测试 LVGL 旋转角度（0°/90°/180°/270°）、MIRROR_X / MIRROR_Y 宏开关组合效果、坐标映射正确性 |
| 54 | 实现 | `main/hal/hal_mirror.c` | LVGL 旋转/镜像配置：根据 `MIRROR_X` / `MIRROR_Y` 宏设置 `lv_display_set_rotation` 和镜像标志，适配分光棱镜光路 |

### 4.2 电池检测 & 电源管理

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 55 | **测试** | `main/hal/test_hal_battery.c` | 测试 ADC 读取→百分比映射（分压比计算）、-1 返回值（未检测）、电量边界 [0,100] |
| 56 | 实现 | `main/hal/hal_battery.c` | 电池 ADC 读取（`adc_oneshot`）、分压比→电量百分比换算、-1 表示无电池/无法读取 |
| 57 | **测试** | `main/service/test_svc_power.c` | 测试低电量阈值触发（≤10%）、休眠策略、长时间无操作降亮度逻辑 |
| 58 | 实现 | `main/service/svc_power.c` | 低电量检测（每 10s 在 logic_task 中调用 `svc_power_tick`）、≤10% 弹低电警告、≥30s 无操作降亮度 |

### 4.3 SD 卡边界处理

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 59 | **测试** | `main/service/test_sd_boundary.c` | 测试未插卡/卡损坏/缺文件夹/缺字库/缺配置 5 种边界的错误码和回退策略 |
| 60 | 实现 | `main/service/svc_sd_boundary.c` | SD 卡异常状态统一处理：`sd_ok` 标志、缺卡提示 UI、缺文件夹回退默认行为、缺字库回退内置字库 |

### 4.4 宠物边界 case

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 61 | **测试** | `main/mode/pet/test_pet_edge.c` | 测试需求归零状态、喂食已满禁止、连续摇晃冷却、平放回正、倾斜 180° 倒置处理、遮挡时宠物前端切换 |
| 62 | 实现 | `main/mode/pet/pet_edge.c` | 宠物边界处理：需求归零→悲伤动画、满需求时禁止喂食、连续摇晃冷却叠加、平放→IDLE、倒置→归位并 SHOCKED |

### 4.5 输入冲突 & 误触防抖

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 63 | **测试** | `main/mode/test_input_conflict.c` | 测试旋转+按键同时按下、快速连续旋转去抖、模式切换时输入队列清空、长按误触过滤 |
| 64 | 实现 | `main/mode/input_conflict.c` | 输入冲突处理：编码器事件队列、快速旋转去抖（≥30ms 间隔）、模式切换时清空输入缓冲、长按 ≥800ms 与短按互斥 |

### 4.6 网络/天气异常覆盖

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 65 | **测试** | `main/service/test_network_error.c` | 测试 Wi-Fi 断连重试 3 次后降级、HTTP 超时 10s、天气 API 返回非 200、JSON 解析失败、无网络时天气 UI 展示"无网络" |
| 66 | 实现 | `main/service/svc_network_error.c` | 网络异常统一处理：Wi-Fi 重连策略（间隔 5s×3 次）→ 告知用户、HTTP 错误码→UI 错误状态映射、JSON 解析异常→保持上次数据 + 标记 stale |

### 4.7 音频混合策略

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 67 | **测试** | `main/service/test_audio_mix.c` | 测试番茄钟提示音优先打断音乐、模式切换停止音乐、音量渐变、摇晃音效与音乐叠加策略 |
| 68 | 实现 | `main/service/svc_audio_mix.c` | 音频优先级策略：番茄钟提示音（最高）> 宠物音效 > 音乐播放器；模式切换→停止音乐播放器；音量渐变（100ms 淡入/淡出） |

### 4.8 多闹钟同时触发 + 极端场景

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 69 | **测试** | `main/mode/menu/test_alarm_extreme.c` | 测试 2+ 闹钟同时触发（依次响铃）、翻倒 180° 时闹钟仍触发、持续摇晃时 UI 不崩溃 |
| 70 | 实现 | `main/mode/menu/alarm_extreme.c` | 多闹钟排队触发（逐一响铃 + 用户确认）、极端姿态下闹钟/定时器保障执行、WatchDog 喂狗策略 |

### 4.9 持久化掉电保护

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 71 | **测试** | `main/service/test_power_loss.c` | 测试 NVS 写入中途掉电的恢复策略、写入前旧数据保留、CRC 校验检测损坏 |
| 72 | 实现 | `main/service/svc_power_loss.c` | NVS 双区写入（A/B 区交替）+ CRC16 校验、上电后选择有效区、损坏回退到旧区 |

> **里程碑 M4**：全部边界情况覆盖，系统稳定可靠。

---

## 阶段 5：打磨 & 验收（M5：全功能可交付）

### 5.1 内置字库

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 73 | **测试** | `main/assets/test_font_noto_16.c` | 测试字库包含常用中文字符（≥2000 字）、LVGL 字体渲染、缺字回退到默认字形 |
| 74 | 实现 | `main/assets/font_noto_16.c` | Noto Sans SC 子集 C 字库（lv_font_conv 离线生成）、16px 大小、覆盖 GB2312 一级字库 |

### 5.2 性能优化

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 75 | **测试** | `main/test_performance.c` | 测试帧率 ≥10fps、模式切换延迟 <500ms、音量响应延迟 <100ms、内存池水位监控 |
| 76 | 实现 | `main/performance.c` | 性能监控：lvgl 帧率统计、模式切换计时代码、FreeRTOS 任务栈水位检测（`uxTaskGetStackHighWaterMark`）、PSRAM 使用率 |

### 5.3 长时间稳定性

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 77 | **测试** | `main/test_stability.c` | 24h 压力测试：循环切换模式、持续动画播放、Wi-Fi 反复连断、闹钟批量触发，检查内存泄漏和任务栈溢出 |
| 78 | 实现 | `main/stability.c` | 内存泄漏检测（heap_caps_get_free_size 定期记录）、WatchDog 定时器配置、异常重启后宠物数据恢复 |

### 5.4 格式化工具 & CI 检查

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 79 | **测试** | `test_code_quality.c` | clang-format 检查脚本、编译零警告、Unity 测试全通过 |
| 80 | 实现 | `code_quality.c` | .clang-format 配置、编译警告全开（-Wall -Wextra）、Unity test runner 集成 |

### 5.5 SD 卡布局说明书

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 81 | **测试** | `sdcard_layout/test_readme.c` | 检查 README.md 包含所有必需目录结构说明、文件命名规则、格式要求 |
| 82 | 实现 | `sdcard_layout/README.md` | 存储卡文件布局说明书：目录结构（/pet_frames/、/music/、/animations/、/fonts/）、文件命名规则、格式要求，用户按文档准备文件即用 |

### 5.6 自定义背景图与动画背景

> **依赖**：SD 卡就绪（Task 33-34）、LVGL PNG 解码器启用（sdkconfig 中 `CONFIG_LV_USE_PNG=y`）。
>
> 每个页面（主页轮播 + 所有子页面）可独立设置静态 PNG 背景图或动画帧序列背景。
> 无 SD 卡/缺文件时回退纯色背景。

**5.6.1 静态背景图**

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 83 | **测试** | `main/service/test_bg_image.c` | SD 图片→LVGL 图像对象、缺文件回退纯色、各页面独立背景验证 |
| 84 | 实现 | `main/service/svc_bg_image.c` | 页面背景管理器：从 `/backgrounds/` 读取 `<page_name>.png` → `lv_obj_set_style_bg_image_src`；支持 carousel / pomodoro / weather / clock / music / animation 各页面独立背景 |

**5.6.2 动画帧序列背景（GIF 替代方案）**

> 原理：GIF 拆成 PNG 帧序列存 SD 卡，双缓冲轮播（复用 pet_anim 帧管理架构）。不自带 GIF 解码器，省 flash；帧从 SD 流式预读，不常驻内存。

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 85 | **测试** | `main/service/test_anim_bg.c` | 帧序列目录扫描、双缓冲轮播切换、缺帧/空目录回退到静态 PNG 或纯色、帧率控制（1-10fps 可调） |
| 86 | 实现 | `main/service/svc_anim_bg.c` | 动画背景引擎：SD 卡 `/backgrounds/anim/<page_name>/` 目录扫描 PNG 帧序列 → 排序 → 双缓冲轮播（当前帧显示 + 下一帧预读）→ 循环播放；每页面独立配置帧率；无动画帧回退到静态 PNG → 纯色 |

**SD 卡目录结构：**
```
/backgrounds/
├── carousel.png          # 主页轮播背景
├── pomodoro.png           # 番茄钟背景
├── weather.png            # 天气背景
├── clock.png              # 时钟背景
├── music.png              # 音乐背景
├── animation.png          # 动画看板背景
└── anim/                  # 动画帧序列（可选）
    ├── carousel/          # 主页动画帧
    │   ├── 0001.png
    │   ├── 0002.png
    │   └── ...
    ├── pomodoro/          # 番茄钟动画帧
    │   └── ...
    └── ...
``` |

> **里程碑 M5**：全功能验收通过，可交付。

---

## 任务统计

| 阶段 | 任务数 | 测试文件 | 实现文件 |
|------|--------|----------|----------|
| 阶段 0（工程骨架） | 5 | 0 | 5 |
| 阶段 1（HAL + 框架） | 10 | 5 | 5 |
| 阶段 2（菜单模式） | 28 | 14 | 14 |
| 阶段 3（宠物模式） | 14 | 7 | 7 |
| 阶段 4（边界处理） | 20 | 10 | 10 |
| 阶段 5（打磨验收） | 14 | 7 | 7 |
| **合计** | **91** | **43** | **48** |

---

## 执行约束

1. **严格顺序执行**：任务必须按编号递增顺序执行，不可跳跃。
2. **测试先于实现**：奇数任务（测试文件）必须编译通过并全部 FAIL（红条），偶数任务（实现文件）完成后测试必须全部 PASS（绿条）。
3. **单文件原则**：每个任务只允许创建或修改一个文件，禁止跨文件改动。
4. **编译门禁**：每个偶数任务完成后，项目必须能编译通过（0 error）。
5. **依赖确认**：阶段 0 执行前需向用户确认器件型号与引脚映射；后续阶段如发现硬件不匹配，回退修改 `app_config.h`。

---

## 踩坑记录（Task 3~4 实战经验）

> 以下问题在开发 EC11 旋转编码器驱动时遇到，记录下来避免后续 AI 开发重复踩坑。

### 坑 1：`driver/pulse_cnt.h` 需要单独的 CMake 依赖

- **现象**：`fatal error: driver/pulse_cnt.h: No such file or directory`
- **原因**：该头文件位于 `esp_driver_pcnt` 组件，不在默认的 `driver` 组件中
- **解决**：在 `CMakeLists.txt` 的 `REQUIRES` 中添加 `esp_driver_pcnt`

### 坑 2：ESP-IDF `TEST_CASE` 宏不生成描述性函数名

- **现象**：`RUN_TEST(hal_touch_encoder_init_with_valid_callbacks)` 报 `undeclared`
- **原因**：ESP-IDF 的 `TEST_CASE("name", "[tag]")` 生成的函数名是 `test_func_<行号>`，**不是**从描述字符串派生的。`RUN_TEST` 传描述性名字找不到函数。
- **解决**：如果想用 `RUN_TEST` 手动运行测试，别用 `TEST_CASE` 宏，直接写 `static void test_xxx(void)` 普通函数，然后 `RUN_TEST(test_xxx)`。

### 坑 3：EC11 编码器 A/B 脚必须开内部上拉

- **现象**：手一碰杜邦线或编码器，计数值疯狂跳动
- **原因**：EC11 是机械触点式编码器——转到齿上时导通到 GND，齿之间悬空。无上拉时悬空脚电平不确定，人体感应电压直接触发 PCNT 计数。
- **解决**：PCNT 通道创建后，对 A/B 脚额外调用 `gpio_config()` 开启 `GPIO_PULLUP_ENABLE`。这样悬空时稳定在高电平，只有转到齿上才拉到低电平。

### 坑 4：LVGL 字体不全都编译进了固件

- **现象**：`lv_font_montserrat_20/28 undeclared`，只有 `lv_font_montserrat_24` 能用
- **原因**：LVGL 的 Kconfig 默认只编译 14/24/32/48 号字体，20/28 未启用
- **解决**：用已有的字体号，如 `lv_font_montserrat_14`、`lv_font_montserrat_24`。

### 坑 5：ESP32-S3 部分 GPIO 有坑

- **现象**：GPIO6 一直读 LOW，按键检测失效
- **原因**：部分开发板 GPIO 被板载外设占用或默认电平受 strapping / 内部电路影响
- **解决**：选 GPIO 前确认板子引脚是否空闲。本项目 GPIO6 给按键不正常，可换 GPIO1 或其他空闲脚。

### 坑 6：用 GPIO 软件模拟正交波形可无编码器验证 PCNT

- **技巧**：上板测试时如果没有编码器，可以用 `gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT)` 把 A/B 脚切为推挽输出，然后 `gpio_set_level` 模拟 CW/CCW 正交序列，PCNT 会从 GPIO 矩阵读到这些跳变，从而验证计数方向和链路。测试结束后切回 `GPIO_MODE_INPUT`。
- **注意**：此方法仅限编码器**未连接**时使用，否则 GPIO 输出会与编码器触点打架。

---

## 踩坑记录（Task 5~6：MPU6050 IMU 驱动 + 摇晃检测）

> 以下问题在开发 MPU6050 驱动和左右摇晃检测时遇到。

### 坑 1：MPU6050 默认加速度计量程 ±2g 不够

- **现象**：`SHAKE_THRESHOLD_G = 2.5g`，但数据手册默认量程只有 ±2g，超出量程无法检测。
- **解决**：初始化时写 `ACCEL_CONFIG = 0x08`，设为 ±4g 量程（AFS_SEL=1，灵敏度 8192 LSB/g）。

### 坑 2：`app_config.h` 里用 `I2C_NUM_0` 不包含头文件会编译失败

- **现象**：`'I2C_NUM_0' undeclared`
- **原因**：`I2C_NUM_0` 定义在 `driver/i2c_types.h`，但 `app_config.h` 是公共配置头，不应依赖驱动头文件。
- **解决**：用 `0` 代替 `I2C_NUM_0`，标注注释 `// I2C_NUM_0`。配置头保持纯数值。

### 坑 3：GCC 15.2.0 在 `-Og` 下编译 `esp_lcd_panel_rgb.c` 内部崩溃

- **现象**：`internal compiler error: Segmentation fault during RTL pass: ira`
- **原因**：GCC 15.2.0 的 bug，`-Og` 优化 `esp_lcd` RGB 面板驱动时 ira pass 崩溃。不影响我们的代码（我们用 SPI 屏）。
- **解决**：`sdkconfig` 中把 `CONFIG_COMPILER_OPTIMIZATION_DEBUG` 改为 `CONFIG_COMPILER_OPTIMIZATION_NONE`（即 `-Og` → `-O0`）。顶层 CMakeLists.txt 的 `add_compile_options(-O0)` 无效。

### 坑 4：picolibc 的 `fgets` / `getchar` 在 USB 串口上不可靠

- **现象**：`fgets(stdin)` → 卡死在 `__bufio_get`；`getchar()` 阻塞导致串口超时。
- **原因**：picolibc 的缓冲 I/O 与 ESP32-S3 USB Serial/JTAG 控制台不兼容，`getchar()` 会阻塞整个串口输出。
- **解决**：**不要在 ESP32-S3 上用 stdio 做 CLI**。参数直接放 `app_config.h` 中，编译期配置。

### 坑 5：摇晃检测算法演进 —— 加速度绝对值/EMA/delta 都不可靠

- **正确方案**：借鉴 HoloCubic 开源固件的方案（`imu.cpp`）：
  - 用**加速度计原始值**（不是 g 值、不是 delta）
  - **迟滞回差（flag）**防抖：触发后 flag=0，值回到死区才重新 arm
  - 关键代码：
    ```c
    if (ax_raw > THRESHOLD && flag) { flag=0; trigger(); }
    else if (ax_raw < -THRESHOLD && flag) { flag=0; trigger(); }
    else { flag=1; }  // 值在死区内，重新 arm
    ```
- **为什么其他方案不行**：
  - 加速度绝对值 → 慢倾斜和快甩的 g 值差不多，无法区分
  - EMA 偏差 → 大幅慢倾斜时 EMA 跟不上一樣触发
  - 采样间 delta → 50Hz 采样下快甩每步 delta 只有 0.2~0.3g，阈值难调
  - 陀螺仪 → 方向映射复杂，且一直显示单向（需要先采集数据标定轴对应关系）
- **HoloCubic 方案本质**：不试图区分"倾斜"和"摇晃"的物理差异，而是用 flag 机制保证**每次越过阈值只触发一次**。慢倾斜只触发一次（越过阈值后不再 arm），用户不会感到困扰。持续摇晃因为值频繁回到死区，自然能重复触发。

### 坑 6：摇晃后的回正反冲会触发反向检测

- **现象**：往左甩显示"左摇晃"，手回正时显示"右摇晃"。
- **原因**：甩完后回正时加速度反冲尖峰越过反向阈值。
- **解决**：在 flag 迟滞回差基础上叠加 **300ms 最小冷却时间**。触发后 set cooldown，冷却期内即使值回到死区也不重新 arm。
  ```c
  s_shake_cooldown_us = esp_timer_get_time() + 300000;
  // re-arm 条件：冷却期满 AND 值在死区内
  ```

### 坑 7：方向映射必须用实际数据标定，不能靠猜

- **方法**：先写一个数据采集 main.c，每 100ms 打印原始 ax/ay/az。让用户按提示做动作（平放/慢倾/左甩/右甩/摔落），根据打印的原始值确定：
  - 左甩时 ax_raw 是正还是负
  - 右甩时 ax_raw 是正还是负
  - 阈值设为多少合适
- **本例结论**（±4g 量程）：左甩 ax_raw ≈ +5000~+7500，右甩 ax_raw ≈ -5000~-7500，阈值 4000 合适。

### 坑 8：所有可调参数放一个文件

- **原则**：`SHAKE_ACCEL_THRESHOLD`、`SHAKE_REARM_THRESHOLD`、`SHAKE_COOLDOWN_US` 等用户可能调整的参数全放 `app_config.h`，不要散落在 `hal_imu.c` 里。上一个 AI 修方向的时候到处找参数很痛苦。

### 坑 9：一次性读 14 字节优于分两次读

- MPU6050 从 0x3B 起可连续读 14 字节：6 字节加速度计 + 2 字节温度 + 6 字节陀螺仪。
- 一次 `i2c_master_transmit_receive` 全部拿到，比分别读 accel 和 gyro 快一倍。

### 最终参数表（app_config.h 中 IMU 区块）

```c
#define IMU_I2C_PORT        0       // I2C_NUM_0
#define IMU_PIN_SCL         1
#define IMU_PIN_SDA         2
#define IMU_I2C_FREQ_HZ     100000  // 100kHz 标准模式
#define IMU_I2C_ADDR        0x68    // MPU6050 默认地址（AD0=0）
#define SHAKE_ACCEL_THRESHOLD 4000  // 摇晃阈值（原始值，±4g下≈0.49g）
#define IMU_TILT_THRESHOLD_DEG 45.0f // 倾斜判定阈值（度）
```

### Task 5~6 最终文件清单

| 文件 | 说明 |
|------|------|
| `main/app_config.h` | IMU 引脚、I2C 参数、`SHAKE_ACCEL_THRESHOLD`、`IMU_TILT_THRESHOLD_DEG` |
| `main/hal/hal_imu.h` | 4 个接口：`init` / `get_angles` / `get_tilt_dir` / `get_shake_dir` / `set_shake_callback` |
| `main/hal/hal_imu.c` | I2C 初始化、14 字节批量读取、姿态角计算、HoloCubic 迟滞回差摇晃检测 + 300ms 冷却 |
| `main/main.c` | 极简测试：init → 注册回调 → 50Hz 循环。方向判断全在 HAL 内，调用方只读 `imu_shake_dir_t` 枚举 |
| `main/app_types.h` | `imu_angles_t`, `imu_accel_t`, `imu_tilt_dir_t`, `imu_shake_dir_t`, `imu_shake_callback_t` |

### HAL 对外接口总览（供后续任务调用）

```c
void    hal_imu_init(void);                        // 初始化（幂等）
imu_angles_t hal_imu_get_angles(void);             // 读姿态角（内部同时检测摇晃）
imu_accel_t  hal_imu_get_accel(void);              // 读加速度 g 值
imu_tilt_dir_t   hal_imu_get_tilt_dir(void);       // 倾斜方向：LEVEL/FRONT/BACK/LEFT/RIGHT
imu_shake_dir_t  hal_imu_get_shake_dir(void);      // 摇晃方向：NONE/LEFT/RIGHT
void    hal_imu_set_shake_callback(imu_shake_callback_t cb); // 注册摇晃回调
```

**注意**：方向映射已经内建在 HAL 中（`ax_raw > 0` = 左，`< 0` = 右），调用方不需要自己判断方向，直接读枚举值即可。

---

## 踩坑记录（Task 7~8：模式管理器 ModeManager）

> 以下问题在开发模式管理器和 main.c 串联时遇到。

### 坑 1：串口打印频率过高无法测试

- **现象**：主循环 50Hz 调用 `mode_manager_on_tilt`，每 20ms 打印一行，瞬间刷屏，完全看不到其他事件。
- **原因**：IMU 倾斜方向大部分时间不变，但每帧都 printf。
- **解决**：在 `mode_manager_on_tilt` 内部加去抖——记录 `s_last_tilt_dir`，方向没变就直接 return。模式切换时用 `(imu_tilt_dir_t)-1` 哨兵值重置，确保新模式下首次倾斜必触发。tick 也同理，每 5 秒才打印一次。

### 坑 2：测试文件的 printf 描述会误导后续 AI

- **现象**：最初写了一版"小猫向左走""小猫晕了"等行为模拟输出。用户指出这些描述会暗示后续 AI 这就是最终行为，但实际实现可能完全不同。
- **解决**：统一改为纯事件确认格式：`[MM] 收到: <模式> / <事件类型> <数据>`。只验证路由正确性，不模拟任何行为。后续 AI 只需看路由分支就知道该把实现写在哪里。

### 坑 3：摇晃回调签名需要传递方向

- **现象**：`hal_imu_set_shake_callback` 的回调类型是 `void (*)(void)`，无参数。但 `mode_manager_on_shake` 需要知道左甩还是右甩来路由。
- **解决**：在 main.c 的回调 `on_imu_shake()` 里先调 `hal_imu_get_shake_dir()` 拿到方向，再传给 `mode_manager_on_shake(dir)`。HAL 接口不动，main.c 做适配。

### Task 7~8 最终文件清单

| 文件 | 说明 |
|------|------|
| `main/mode/mode_manager.h` | 7 个公开接口：`init` / `on_rotary` / `on_btn` / `on_tilt` / `on_shake` / `tick` / `get_mode` |
| `main/mode/mode_manager.c` | 状态机（长按切模式）+ 事件路由（按当前模式分发）+ 串口确认（去抖/降频） |
| `main/main.c` | 全量重写：HAL 回调 → mode_manager，主循环 50Hz 倾斜 + 1Hz tick |
| `main/CMakeLists.txt` | 新增 `mode/mode_manager.c` 和 `mode` include 路径 |

### mode_manager 对外接口总览（供后续任务调用）

```c
void mode_manager_init(void);                                        // 初始化，默认 PET 模式
void mode_manager_on_rotary(int8_t step);                            // 旋钮旋转（+顺时针/-逆时针）
void mode_manager_on_btn(bool short_press);                          // 按键（短按=true，长按=false）
void mode_manager_on_tilt(imu_angles_t angles, imu_tilt_dir_t dir);  // 倾斜（角度+方向）
void mode_manager_on_shake(imu_shake_dir_t dir);                     // 摇晃（左/右）
void mode_manager_tick(uint32_t dt_ms);                              // 心跳（1Hz 调用即可）
app_mode_t mode_manager_get_mode(void);                              // 查询当前模式
```

**后续开发指南**：
1. `mode_manager.c` 中的路由分支已经分好，每个 `printf` 位置注释了该调用哪个子模块函数。
2. 写子模块时不要改 `mode_manager.h` 的接口签名，只需替换 `mode_manager.c` 中对应的 `printf` 为子模块函数调用。
3. 子模块函数调用时用 `s_current_mode` 判断当前模式，确保事件只发给激活的模块。

---

## 踩坑记录（Task 9~10：main.c + ST7789 显示偏移）

### 坑 1：ST7789 240×240 模组有 80 行垂直偏移

- **现象**：屏幕约 1/3 区域为静态白色噪点，剩下 2/3 纯黑，噪点在一侧边缘。
- **原因**：ST7789 原生 GRAM 为 240 列 × 320 行，但 240×240 模组只焊接了其中 240 行物理像素。本模组（N154-2424KBWPG05-H12）的物理像素对应 GRAM 第 **80~319 行**（底部 240 行），而非第 0~239 行。`esp_lcd_panel_init()` 默认从 GRAM 行 0 开始写，前 80 行落到无物理像素区域，显示为随机白噪点。
- **解决**：调用 `esp_lcd_panel_set_gap(s_panel, 0, 80)` 将 RASET 偏移 80 行，数据精确写入 GRAM 行 80~319。
  - 试过 gap_y=40（居中假设）→ 噪点变小但仍在，说明方向对了值不对
  - 最终 gap_y=80 → 完美显示
- **关键认知**：这个偏移值不在芯片手册里，是模组厂绑定时决定的。必须实验测试。

### 坑 2：偏移值应作为配置宏

- **原则**：`LCD_GAP_X` 和 `LCD_GAP_Y` 放在 `app_config.h`，不在 `hal_display.c` 里写死。后续换屏只需改配置宏。

```c
// app_config.h
#define LCD_GAP_X  0   // 列偏移
#define LCD_GAP_Y  80  // 行偏移（物理像素在 GRAM 第 80~319 行）
```

### 坑 3：ST7789 初始化顺序

- **问题**：`invert_color` / `mirror` 应在 `disp_on_off(true)` 之前调用。先开显示再改 MADCTL 会导致短暂闪烁。
- **正确顺序**：`reset → init → set_gap → invert_color → mirror → swap_xy → disp_on_off(true)`

### 坑 4：hal_display 需要额外 CMake 依赖

- **现象**：启用 `hal_display.c` 后链接报 `undefined reference`
- **解决**：在 `main/CMakeLists.txt` 的 `REQUIRES` 中添加 `esp_driver_spi`、`esp_driver_ledc`、`esp_lcd`、`lvgl`

### 坑 5：6 个 FreeRTOS 任务的桩设计

- **原则**：阶段 1 只需创建任务壳，`audio_task`、`network_task`、`logic_task` 内部只有 `while(1) { vTaskDelay(1000); }`。后续阶段实现对应模块时，替换函数体即可，不需要改 main.c。
- **注意**：`encoder_task` 实际不需要——编码器由 `hal_touch_encoder` 的 `esp_timer` 直接回调 `mode_manager`，`encoder_task` 保留为桩，以防后续改用任务队列。

### 坑 6：LVGL v9 `lv_screen_load()` 不会立即刷新

- **现象**：调用 `lv_screen_load()` 切换页面后，屏幕显示不变化，直到下一帧（可能延迟 30ms+，甚至会丢帧不显示）。
- **原因**：`lv_screen_load()` 只是标记新屏幕为活跃，实际渲染发生在 `lv_timer_handler()` 中。如果后续逻辑依赖新屏幕已渲染，需要强制刷新。
- **解决**：`lv_screen_load()` 之后立即调用 `lv_refr_now(NULL)` 强制渲染。M1 页面切换用此方案，后续复杂的 UI 切换可改用动画过渡（`lv_screen_load_anim`）。

---

## 踩坑记录（Task 11~14：菜单导航引擎 + LVGL UI + 手动动画）

> 以下问题在开发 menu_engine / menu_ui 时遇到，与前几阶段踩坑不同——这次主要是 LVGL v9 渲染机制的问题。

### 坑 1：`lv_anim_start` 导致 task_wdt 超时崩溃

- **现象**：进入 MENU 模式后倾斜切换菜单项，串口打印正常（`[MENU] → Weather`），但几次切换后 task_wdt 触发，`lvgl_task` 卡死在 `lv_anim_start → anim_x_cb → lv_obj_set_x`。
- **原因**：`lv_anim_start` 在 LVGL v9 内部会立即调用 exec_cb 设置初始值，并与 LVGL 动画管理器交互。在 ESP32-S3 + FreeRTOS 环境下，栈上分配的 `lv_anim_t` + LVGL 内部定时器机制存在某种死锁/超时（具体原因未完全定位，现象稳定复现）。
- **解决**：**完全不用 `lv_anim`**。自己写手动动画系统：
  ```c
  // 用 esp_timer_get_time() 计时，在 lvgl_task 循环中每帧更新位置
  void menu_ui_tick_anim(void) {
      int32_t elapsed = (esp_timer_get_time() - s_anim.start_us) / 1000;
      int32_t eased = ease_out(elapsed, ANIM_MS);
      lv_obj_set_x(obj, start + (end - start) * eased / 256);
      lv_refr_now(NULL);
  }
  ```
- **原则**：后续所有动画（番茄钟环形进度条、页面切换等）都用手动 tick 方式，不要碰 `lv_anim`。

### 坑 2：`lv_label_set_text` / `lv_obj_set_x` 不触发屏幕重绘

- **现象**：轮播文字和页码通过 `lv_label_set_text` 修改后，屏幕无变化。加 `lv_obj_invalidate` 也不行。但 `lv_screen_load` + `lv_refr_now` 的屏幕切换能正常显示。
- **原因**：LVGL v9 的脏区标记机制在本硬件环境（ESP32-S3 + ST7789 SPI + 双帧缓冲 PSRAM）下不完全生效。修改子对象属性后，LVGL 内部标记了脏区，但 `lv_timer_handler` 不触发 flush。
- **解决**：**所有 UI 内容修改后都调 `lv_refr_now(NULL)` 强制全屏刷新**。虽然比脏区刷新慢，但 240×240 屏幕全刷一帧只需几毫秒，完全够用。
  ```c
  lv_label_set_text(label, "new text");
  lv_refr_now(NULL);  // 必须！否则不更新
  ```
- **影响**：后续所有子页面（番茄钟、天气等）在更新 UI 后都要加 `lv_refr_now(NULL)`。

### 坑 3：创建/销毁 LVGL 对象比修改现有对象更不可靠

- **现象**：最初轮播切换时创建新容器 + 动画滑入、删除旧容器。屏幕不动且 task_wdt 触发。
- **原因**：频繁 `lv_obj_create` + `lv_obj_delete` 在动画进行中容易导致 LVGL 内部状态不一致。
- **解决**：**轮播容器只创建一次**，切换时仅修改文字和样式（`lv_label_set_text` + `lv_obj_set_style_bg_color` + `lv_refr_now`）。子页面容器独立创建，用显隐（`LV_OBJ_FLAG_HIDDEN`）+ 位移动画切换。
- **原则**：优先复用现有对象，少创建新对象。页面切换用显隐 + 位移，不要频繁 create/delete。

### 坑 4：摇晃与倾斜在 MENU 模式下会相互干扰

- **现象**：左右甩设备时串口大量打印 `[MM] 收到: MENU / 摇晃-右甩`，同时菜单项不断跳动。
- **原因**：摇晃的加速度尖峰也会触发短暂的倾斜方向变化（RIGHT → LEVEL → RIGHT），每个方向变化都触发菜单滚动。
- **解决**：MENU 模式下 `mode_manager_on_shake` 直接 return（菜单不需要摇晃）。300ms 冷却可以过滤掉大部分倾斜抖动，但不能完全阻止连续快速摇晃时的滚动。

### 坑 5：`lv_font_montserrat_24` 不含中文字符

- **现象**：中文菜单名显示为方块（□）。
- **原因**：Montserrat 是纯拉丁字体，LVGL Kconfig 默认只编译 14/24/32/48 号蒙纳字体。
- **解决**：当前用英文短名（Pomodoro/Weather/Clock/Music/Animation）。中文名称保留在 `menu_engine_get_item_name()` 和 `s_display_names[]` 注释中，**Task 73-74 中文字库就绪后改回**。

### 动画系统设计

```
menu_ui_tick_anim()   ← lvgl_task 每 30ms 调用
  │
  ├─ A_SCROLL:   按钮列表滚动 + 游标同步滑动（150ms ease-out）
  ├─ A_CAROUSEL: 轮播水平滑动（200ms ease-out）
  └─ A_PAGE:     页面 push/pop 水平滑动（200ms ease-out）

跨核架构：
  sensor_task (Core 1)  →  stack_input_cb
    └─ 只改 focus_index / s_btn_pending / s_focus_dirty（纯数据，无 LVGL 调用）
  lvgl_task (Core 0)   →  menu_ui_apply_sub_updates / tick_anim
    └─ 所有 LVGL 操作 + 动画 + 按钮回调执行

按钮回调执行链：
  Core 1: btn_short → s_btn_pending = focus_index
  Core 0: apply_sub_updates → if idx==0: go_back()  else: pg->on_btn(idx)
```

---

## 踩坑记录（Task 11~14 补充：字节序、跨核安全、滚动列表）

> 以下是在开发过程中新发现的问题，补充到已有踩坑记录之后。

### 坑 6：ST7789 RGB565 字节序反了导致颜色错乱

- **现象**：`lv_color_hex(0xFF0000)`（红）显示为蓝色，`0x00FF00`（绿）显示为红色，`0x0000FF`（蓝）显示为绿色。
- **原因**：ESP32-S3 是小端序，RGB565 值 `0xF800`（红）在内存中存为 `[0x00, 0xF8]`，SPI 先发低字节 `0x00`，ST7789 按 MSB 优先解释为 `0x00F8`（蓝绿色）。每个像素的高低字节都反了。
- **解决**：在 `hal_display_flush` 中手动交换每对字节：
  ```c
  uint8_t *px = px_map;
  for (int i = 0; i < w * h; i++) {
      uint8_t tmp = px[0]; px[0] = px[1]; px[1] = tmp;
      px += 2;
  }
  ```
- **验证方法**：查面板数据手册，N154-2424KBWPG05-H12 规格书写 "Color arrangement: RGB Vertical stripe"——确认是 RGB 色序，排除色序问题后定位到字节序。

### 坑 7：`sensor_task`（Core 1）回调中操作 LVGL 导致内存损坏

- **现象**：进入子页面后 `LoadProhibited` 崩溃在 `xTaskIncrementTick`，FreeRTOS 内核被踩坏。
- **原因**：`stack_input_cb` 在 `sensor_task`（Core 1）上下文被调用，最初直接在回调里调了 `lv_label_set_text`、`lv_obj_set_style_*`、`lv_refr_now` 等 LVGL 函数。LVGL **不是线程安全的**，跨核操作导致内部状态损坏。
- **解决**：**Core 1 回调只设数据标志**（`s_btn_pending`、`s_focus_dirty`、`focus_index`），**Core 0 的 `lvgl_task` 统一执行 LVGL 操作**。按钮回调 `on_btn` 也延迟到 Core 0 执行，用户代码可以安全操作 LVGL。
- **原则**：凡是 `stack_input_cb` → `page_top()` → 操作 LVGL 的路径都不行。标志位 + 轮询是唯一安全模式。

### 坑 8：`lvgl_task` 栈 8KB 不够导致崩溃

- **现象**：进入子页面后 `Guru Meditation LoadProhibited`，`EXCVADDR: 0x00000204`。
- **原因**：`lv_refr_now(NULL)` 在 `-O0` 编译下栈消耗大（~3-4KB），加上动画 tick 嵌套调用，8KB 栈不够。
- **解决**：`lvgl_task` 栈增大到 **12288 字节**。
- **教训**：后续如果加更复杂的 LVGL 页面（环状进度条、多层嵌套），可能还需要继续加栈。

### 坑 9：子页面按钮列表滚动的边界计算

- **现象**：按钮列表初始只显示 2 个按钮，倾斜后一直往上跑直到看不见。
- **原因 1**：`scroll_target` 最初把焦点按钮居中到视口中间，但首个按钮（Back）居中时上方留空，只有下方一个按钮可见。
- **原因 2**：游标最初通过 `lv_obj_get_parent(focus_cursor)` 找滚动层，但游标在视口里、滚动层也在视口里——它们是兄弟，不是父子。滚动操作被应用到了视口上而非滚动层。
- **解决**：
  1. 在 `menu_page_t` 中加 `scroller` 字段直接存滚动层指针
  2. 游标改为非固定位置——随焦点移动（`cursor_vp_y` 计算），不在中间死锁
  3. 滚动边界改为 `max_y=0`（不空出顶部）、`min_y=-(count-VISIBLE)*BTN_STEP`（不空出底部）
  4. 游标和滚动层 **同步动画**（同一条 ease-out 曲线驱动两个对象的 Y 坐标）

### 坑 10：`hal_display.c` 改动注意事项

- `rgb_ele_order`：该面板（N154-2424KBWPG05-H12）手册明确写 "RGB Vertical stripe"，用 `LCD_RGB_ELEMENT_ORDER_BGR` 会导致 R↔B 互换。最终使用 `RGB`。
- `swap_color_bytes`：ESP-IDF v6.0.2 的 `esp_lcd_spi_flags_t` 没有此字段，**改为在 flush 回调中手动交换字节**。
- `invert_color`：该面板是 Normally black 模式（手册确认），`invert_color=true` 正常。

---

## 踩坑记录（Task 15~16：番茄钟 + 输入系统优化）

> 以下问题在开发番茄钟和优化输入系统时遇到。

### 坑 1：两个 FreeRTOS 任务同时操作 LVGL 会损坏状态

- **现象**：`pomodoro_tick()` 在 `logic_task`（Core 0, prio 2）中调用，调用 `lv_arc_set_value` / `lv_label_set_text` 等 LVGL 函数。虽然同核，但 `lvgl_task`（prio 3）可抢占 `logic_task`，导致 LVGL 内部状态不一致。
- **解决**：**全部 LVGL 操作收归 `lvgl_task` 统一执行**。`logic_task` 只设脏标（`s_pending_ticks++`），`lvgl_task` 的 `pomodoro_process_updates()` 处理所有 LVGL 操作。所有跨核/跨任务通信都用 `volatile` 脏标模式。

### 坑 2：`lv_obj_create` 创建的容器默认带滚动条

- **现象**：按钮右边出现小白条。
- **解决**：`lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE)` 禁用滚动条。

### 坑 3：`lv_obj_align` 定位后 `lv_obj_set_x` 动画冲突

- **现象**：按钮用 `lv_obj_align` 定位，动画期间用 `lv_obj_set_x` 滑出，动画结束后重新 `lv_obj_align` 归位。`lv_obj_set_x` 会覆盖 align 的效果。
- **解决**：按钮改用绝对定位 `lv_obj_set_pos`，不用 `lv_obj_align`，动画直接用 `lv_obj_set_x` 控制，无需复位。

### 坑 4：`snprintf` 编译警告 `-Werror=format-truncation`

- **现象**：`snprintf(buf, 8, "%02lu:%02lu", ...)` 编译器警告可能截断。虽然 MM:SS 最多 6 字符，但编译器按类型上限估算 `unsigned long` 可达 10+ 字符。
- **解决**：`buf[16]` 足够大即可消除警告。

### 坑 5：`uint32_t` 在 ESP32 上是 `unsigned int`，不是 `unsigned long`

- **现象**：`ASSERT_EQ(remaining, 600, "%lu")` 报 `-Werror=format` 错误。
- **解决**：`uint32_t` 用 `%u` 格式。或用 `(unsigned long)` 强制转换后配 `%lu`。

### 坑 6：主页面倾斜切换时回正反冲触发连续切换

- **现象**：倾斜切换菜单项后手回正，加速度反冲尖峰触发反向切换，导致一次物理动作连跳两页。
- **解决**：HoloCubic 迟滞回差方案——触发切换后 **disarm**，必须 IMU 先回到 LEVEL 才能 **re-arm** 接受下一次倾斜。此为 `s_tilt_armed` 标志，主菜单和子页面都适用。

### 坑 7：拍打/摔落设备误触发倾斜切换

- **现象**：面包板拍在桌上或摔落，加速度尖峰被 IMU 误判为倾斜方向，触发选项切换。
- **解决**：在 `hal_imu_get_tilt_dir()` 加**去抖**——倾斜方向必须连续 3 帧（60ms @ 50Hz）不变才对外报告。短促冲击（1-2 帧）被过滤为 LEVEL。

### 坑 8：`ease_out` 函数在两处重复定义

- **现象**：`menu_ui.c` 和 `pomodoro.c` 各自实现了相同的缓动函数。
- **解决**：提取为 `menu_ui_ease_out()` 公开函数，`menu_ui.h` 声明，两处共用。

### 坑 9：`menu_ui_apply_sub_updates` 硬编码 index 0 = Back

- **现象**：番茄钟按钮想要 Back 在末尾（index 3），但框架强制 index 0 触发 `menu_ui_go_back()`。
- **解决**：移除框架硬编码，改为各页面的 `on_btn` 回调自行处理 Back。`test_nested_cb` 同步修复。

### 坑 10：未注册的菜单项 fallback 到 Pomodoro 造成困惑

- **现象**：点击 Weather / Clock 等未实现的菜单项全进番茄钟，后续 AI 开发会误解。
- **解决**：`menu_ui_enter_sub()` 改为显示 "Coming Soon" 占位页 + 返回按钮，清楚标明未实现。

### 坑 11：`MENU_INPUT_MODE` 用 `#if` 预处理不够灵活

- **现象**：番茄钟调节模式需要旋钮调值，但 `MENU_INPUT_MODE=0`（仅倾斜）时旋钮被 `#if` 裁掉，需要额外旁路机制。
- **解决**：改为运行时 `input_allowed()` 函数，`s_rotary_force` 标志统一处理旁路，对后续模块透明。

---

---
<br>

## 踩坑记录（硬件迁移：4MB→8MB Flash + 2MB→8MB PSRAM）

> 同一型号 ESP32-S3 板子换成 8MB Flash + 8MB PSRAM 版本时遇到的所有问题及解决方案。

### 前置：换了什么

| 参数 | 旧板 | 新板 |
|------|------|------|
| Flash | 4MB | **8MB** |
| PSRAM | 2MB (Quad SPI) | **8MB (Octal SPI)** |
| MCU | ESP32-S3 | ESP32-S3（不变） |
| 其他外设 | ST7789 / EC11 / MPU6050 | 不变 |

### 坑 1：sdkconfig 中 Flash Mode 不一致

- **现象**：`CONFIG_ESPTOOLPY_FLASHMODE_QIO=y` 选中了 QIO，但 `CONFIG_ESPTOOLPY_FLASHMODE="dio"`，互相矛盾。
- **原因**：未知（可能是 menuconfig 的 bug，也可能是之前手动改过）。
- **解决**：手动改 `CONFIG_ESPTOOLPY_FLASHMODE="dio"` → `"qio"`。不改的话 esptool 上传时可能选错模式，导致读写错误或性能下降。
- **教训**：换板重配时，不要只看 `.y` 选择项，也要检查对应的字符串值。最好直接跑一次 `idf.py menuconfig` 重新选择确认。

### 坑 2：8MB PSRAM 是 Octal SPI，不是 Quad SPI

- **现象**：烧录后板子反复重启，串口打印：
  ```
  E (260) quad_psram: PSRAM chip is not connected, or wrong PSRAM line mode
  E cpu_start: Failed to init external RAM!
  abort() was called at PC 0x42002d7c
  ```
- **原因**：ESP32-S3 的 8MB PSRAM（如 APS6408L 系列）使用 **Octal SPI**（8 线）接口，而 2MB PSRAM 用 Quad SPI（4 线）。原 sdkconfig 配置为 `CONFIG_SPIRAM_MODE_QUAD=y`，芯片根本不响应 Quad 指令。
- **解决**：sdkconfig 改为：
  ```
  # CONFIG_SPIRAM_MODE_QUAD is not set
  CONFIG_SPIRAM_MODE_OCT=y
  ```
- **教训**：PSRAM 容量和接口类型强相关——2MB 一般是 Quad，8MB 一般是 Octal。换大容量 PSRAM 时**必须**检查并修改 `CONFIG_SPIRAM_MODE_*`。

### 坑 3：`CONFIG_SPIRAM_TYPE_AUTO` 可以自动检测，但前提是 MODE 正确

- `CONFIG_SPIRAM_TYPE_AUTO=y` 能自动识别 PSRAM 芯片型号和容量，不需要手动设 `CONFIG_SPIRAM_TYPE_ESPPSRAM64`。
- **前提**：`CONFIG_SPIRAM_MODE` 必须与芯片物理接口匹配（Quad 或 Octal）。Mode 错了，Auto 根本没机会工作。
- 其他 PSRAM 配置（CLK=30, CS=26, Speed=80MHz, USE_MALLOC）跨板通用，不需要改。

### 改了什么文件

| 文件 | 改动 |
|------|------|
| `sdkconfig` | `CONFIG_ESPTOOLPY_FLASHMODE` `"dio"` → `"qio"` |
| `sdkconfig` | `CONFIG_SPIRAM_MODE_QUAD=y` → `# not set`，`CONFIG_SPIRAM_MODE_OCT` 启用 |

**代码无改动**。`hal_display.c` 的 `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` 与 PSRAM 大小无关，LVGL 双帧缓冲 230KB 远在 8MB 之内。

### 操作检查清单（后续再换同类型板子）

1. `idf.py fullclean`（必须，换 Flash/PSRAM 后旧的 build 缓存可能不兼容）
2. 检查 `sdkconfig` → `Serial flasher config` → Flash size = 8MB, Flash mode = QIO
3. 检查 `sdkconfig` → `ESP PSRAM` → Mode = Octal, Type = Auto
4. 烧录后看串口 boot 日志：`SPI Flash Size : 8MB`、无 PSRAM 报错
5. 确认 `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` 返回值 > 7MB

---
<br>

## 开发指南（后续 AI 开发必读）

### 子页面开发模式

1. **创建 `xxx.c/.h`**，参考 `pomodoro.c` 的模式：
   - `xxx_init()` — 注册 creator 到 `menu_ui_register_creator()`
   - `xxx_tick(dt_ms)` — logic_task 调用，**只设脏标，不碰 LVGL**
   - `xxx_process_updates()` — lvgl_task 调用，**统一处理所有 LVGL 操作**
2. **聚合注册** — 在 `menu_ui_init_modules()` / `menu_ui_tick_modules()` / `menu_ui_process_module_updates()` 各加一行
3. **占位页自动替换** — 一旦注册 creator，对应菜单项的 "Coming Soon" 自动消失
4. **旋钮调节需求** — 调 `menu_engine_rotary_override(true/false)` 即可，不用关心 `MENU_INPUT_MODE`

### 跨核安全铁律

- **Core 1 回调**（sensor_task 上下文）：只改数据 + 设 `volatile` 脏标，**严禁操作 LVGL**
- **Core 0 lvgl_task**：检查脏标 → 统一执行 LVGL 操作
- 所有 UI 更新后调 `lv_refr_now(NULL)` 强制刷新（脏区标记在本硬件不生效）

### 输入系统架构

```
MENU_INPUT_MODE → input_allowed() [menu_engine.c]
                         │
            ┌────────────┼────────────┐
            ▼            ▼            ▼
         TOP倾斜     TOP旋钮      SUB转发→子页面回调
        (切换菜单) (切换菜单)       (倾斜/旋钮各mode过滤)
```
改 `app_config.h` 的 `MENU_INPUT_MODE` 全局生效，无需改任何模块代码。

- `main/mode/menu/test_weather.c` — Task 17 测试，验收通过后删除
- `main/service/test_svc_wifi.c` — Task 19 测试，验收通过后删除
- `main/service/test_svc_http.c` — Task 21 测试，验收通过后删除

---
<br>

## 踩坑记录（Task 17~22：天气 + Wi-Fi + HTTP 客户端）

> 以下问题在心知天气 API 对接、WiFi 连接、HTTPS/TLS 过程中遇到。

### 坑 1：OpenWeatherMap API 格式不能直接用 —— 换了心知天气 v3

- **现象**：最初按 OpenWeatherMap 的 JSON 格式写解析（`root.name`、`root.main.temp(float)`、`root.weather[0].description`），但国内免费 OWM API 不稳定。
- **解决**：换成**心知天气 API v3**（`api.seniverse.com`），免费用户返回 `results[0].location.name` / `now.text` / `now.code` / `now.temperature(string)`。
- **关键差异**：temperature 是**字符串**不是数字，需要 `atoi()` 转换。

### 坑 2：心知天气 SDK 有公钥和私钥之分

- **现象**：用户提供了公钥（`PbSqzmmIe8x-hrOog`），但 API 调用不成功。
- **原因**：公钥是给前端 JavaScript SDK 用的（有域名白名单限制），服务端 HTTP API 调用必须用**私钥**。私钥在控制台同一页面，通常更长。
- **解决**：`app_config.h` 中配置 `WEATHER_API_KEY` 填**私钥**。

### 坑 3：ESP-IDF v6.0.2 中 `evt->event` 改名 `evt->event_id`

- **现象**：`esp_http_client_event_t` has no member named 'event'; did you mean 'event_id'?
- **原因**：ESP-IDF v6.0.2 改了这个结构体成员名。
- **解决**：全量替换 `evt->event` → `evt->event_id`。**所有用 esp_http_client 事件回调的地方都要改**，包括 weather.c（2 处）和 svc_http.c（1 处）。

### 坑 4：HTTPS 证书验证在 ESP-IDF v6 下不能只用 `skip_cert_common_name_check`

- **现象**：设置 `skip_cert_common_name_check=true` 后报 `No server verification option set` → `ESP_ERR_MBEDTLS_SSL_SETUP_FAILED`。
- **原因**：ESP-IDF v6 的 mbedTLS 要求要么用证书包（`crt_bundle_attach`），要么显式跳过所有验证。单纯 `skip_cert_common_name_check` 不够。
- **尝试 1**：用 `esp_crt_bundle_attach` → 编译报 `esp_crt_bundle.h: No such file` → 加 `esp_crt_bundle` 到 REQUIRES → CMake 报 `unknown component 'esp_crt_bundle'`（ESP-IDF v6 中它不是独立组件）→ 换 `mbedtls` → 还是找不到头文件。
- **最终方案**：**切到 HTTP 协议**。心知天气和 IP 查询服务都支持 HTTP，数据不敏感，不需要 TLS。`https://` → `http://`，去掉所有 `crt_bundle_attach` 和 `mbedtls` 依赖。

### 坑 5：公网 IP 查询服务在手机热点下不可用

- **现象**：`http://api.ipify.org` → `Connection reset by peer`；`http://icanhazip.com` → `Connection timed out`。
- **原因**：手机热点（特别是国内运营商）可能封锁境外 IP 查询服务。
- **解决**：**多源回退**机制，依次尝试 3 个 IP 查询源：
  1. `http://ip.3322.net`（国内 DDNS 服务商，最可靠）✅
  2. `http://icanhazip.com`（国际）
  3. `http://ifconfig.me`（国际）
  - 任意一个成功即停止；
  - 全失败回退到 `app_config.h` 里配置的 `WEATHER_API_LOCATION`。

### 坑 6：IP 查询返回的字符串末尾有换行符导致 URL 解析失败

- **现象**：`public IP: 39.144.251.62` 拿到了，但 Seniverse API 报 `Error parse url`。
- **原因**：`ip.3322.net` 返回 `"39.144.251.62\n"`，末尾 `\n` 被拼进 URL：`location=39.144.251.62\n&language=en`，`esp_http_client` 无法解析。
- **解决**：IP 接收后**修剪末尾 `\r` `\n` 空格**：
  ```c
  while (s_ip_buf_len > 0) {
      char c = s_ip_buf[s_ip_buf_len - 1];
      if (c == '\r' || c == '\n' || c == ' ') s_ip_buf[--s_ip_buf_len] = '\0';
      else break;
  }
  ```

### 坑 7：`logic_task` 栈 4KB 不够跑 HTTPS → 已切 HTTP 但谨慎起见仍扩大

- **现象**：`A stack overflow in task logic has been detected`。
- **原因**：`weather_do_fetch()` 在 logic_task 中同步执行 HTTPS 请求，mbedTLS 握手需要大栈。4KB 不够。
- **解决**：logic_task 栈从 **4096 → 12288**（和 lvgl_task 同级别）。即使切到 HTTP 后不需要 TLS，但 HTTP 请求本身也可能消耗较多栈空间，12288 更安全。

### 坑 8：cJSON 组件放置位置

- **现象**：用户下好了 `cJSON-master` 放在 `main/` 目录下。
- **解决**：**必须放到 `components/cjson/`**（ESP-IDF 组件标准位置），并创建 `components/cjson/CMakeLists.txt`（`idf_component_register`），然后在 `main/CMakeLists.txt` 的 `REQUIRES` 中加 `cjson`。不能放 `main/` 下面直接编译。

### 坑 9：PSRAM 和 Flash 配置被 `menuconfig` 重置

- **现象**：sdkconfig 中 `CONFIG_SPIRAM is not set`、Flash Size 2MB、Flash Mode DIO——之前的 8MB+QIO+Octal PSRAM 配置全部丢失。
- **解决**：**直接改 sdkconfig 文件**恢复：
  - `CONFIG_SPIRAM=y` + `CONFIG_SPIRAM_MODE_OCT=y` + `CONFIG_SPIRAM_TYPE_AUTO=y`
  - `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y` + `FLASHSIZE="8MB"`
  - `CONFIG_ESPTOOLPY_FLASHMODE_QIO=y` + `FLASHMODE="qio"`
  - `CONFIG_ESP32S3_SPIRAM_SUPPORT=y`
  - 改完后**必须 `idf.py fullclean`** 再编译，否则旧缓存不生效。

### 坑 10：分区表 1MB 不够装 WiFi+HTTP 固件

- **现象**：`Error: app partition is too small for binary (overflow 0x369e0)`。
- **原因**：WiFi + HTTP + cJSON + mbedTLS 把固件从 ~900KB 撑到了 ~1.27MB，超出 1MB factory 分区。
- **解决**：`CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y`，factory 分区扩到 ~3MB（8MB Flash 下充裕）。

### 坑 11：IP 定位 API 全部不可用 / 不准 → 换成预设城市列表 + 弹窗选择

- **过程**：
  1. IP2Location.io → 查询成功但定位不准（返回错误城市）
  2. `whois.pconline.com.cn` → **403 Forbidden**（加了 User-Agent 也不行）
  3. 最终**放弃所有 IP 定位方案**，改用**预设常用城市列表**
- **方案**：8 个城市拼音（guangzhou/shenzhen/beijing/shanghai/chengdu/hangzhou/wuhan/nanjing）存在 `CITY_LIST[]` 中，心知天气 API 直接用拼音查。底部按钮从 2 个扩展到 3 个：`City` → `Refresh` → `Back`。
- **城市选择弹窗**：按 City 按钮 → 弹出半透明遮罩 + 圆角弹窗 + 竖向列表（8 城市 + Cancel）→ 旋钮/倾斜上下选择 → 按钮确认。仿照 pomodoro.c 的 `s_adjusting` 双模输入，用 `s_city_select` 标志位路由旋钮事件。
- **复杂度**：弹窗全部手写 LVGL 对象（overlay/box/viewport/scroller/cursor），不用框架的 `menu_ui_page_create`。约 200 行新代码，只改 `weather.c` 一个文件。

### 坑 12：动画中 `lv_refr_now()` 导致 task_wdt 超时

- **现象**：天气页按钮动画中调用 `lv_refr_now(NULL)`，223 秒后 task_wdt 触发，lvgl_task 卡在 `lv_obj_set_x`。
- **原因**：动画每帧调 `lv_refr_now()` 同步全屏渲染（240×240×40MHz SPI），长时间累计导致 IDLE0 得不到执行。
- **解决**：动画中间帧去掉 `lv_refr_now()`，靠 lvgl_task 每 30ms 的 `lv_timer_handler()` 渲染。动画结束和 UI 数据更新时保留 `lv_refr_now()` 确保最终状态立刻可见。

### 关键架构决策

1. **所有 HTTP 请求走 HTTP 不用 HTTPS**：手机热点网络环境下 TLS 配置不稳定，数据不敏感，HTTP 可用且简单。
2. **预设城市列表代替 IP 自动定位**：IP 查询 API 在 ESP32 环境下可靠性太差（被封/不准/超时），预设城市列表 + 弹窗选择更可靠，且减少一次 HTTP 请求。
3. **天气请求在 logic_task 中同步执行**：WiFi 连接后才触发，30 分钟一次，频率低，同步阻塞可接受。logic_task 栈扩大到 12KB 以容纳 HTTP 栈消耗。

---
<br>

## ⚠️ Task 17~22 反思总结（后续 AI 开发必读）

> 这次 6 个任务开发过程暴露出几个方法论问题，记录在此警示后续 AI。

### 教训 1：先问用户，别自己猜

**踩坑**：上来就按 OpenWeatherMap API 格式写 JSON 解析（`root.main.temp`、`root.weather[0].description`）。代码写完后用户给了心知天气文档，发现字段完全不同（`results[0].now.temperature`、`now.text`），temperature 是字符串不是数字。全部推翻重写。

**原则**：**任何外部 API / 第三方服务，开发前先向用户确认到底是哪一个。** 不要自己选一个就开写。

### 教训 2：能用 HTTP 就别碰 HTTPS

**踩坑**：默认用 HTTPS，花了好几轮折腾证书验证：`skip_cert_common_name_check` 不生效 → `esp_crt_bundle_attach` 找不到组件 → 换 `mbedtls` → PSA 签名间歇性失败 → 证书验证通过但握手仍失败。最后用户改成 `http://`，一切太平。

**原则**：天气数据、IP 查询不是敏感数据，选 HTTP 就够了。**ESP32 嵌入式环境下 TLS 是复杂度放大器**，能不碰就不碰。

### 教训 3：从最简开始，跑通再加

**踩坑**：在还没验证"固定城市名 + 天气 API"能跑通的情况下，同时叠了 IP 自动定位功能。两个新功能的 bug 混在一起（IP 末尾换行符污染 URL + HTTP 连接问题），定位问题多花了不少时间。

**原则**：**先做最小可行版本（固定 location 查天气），确认串口能打印 "fetch OK: Shenzhen 27C" 之后，再加 IP 自动定位。** 一次只验证一件事。

### 教训 4：sdkconfig 改动必须 fullclean

**踩坑**：改 PSRAM/Flash 大小/分区表这些底层配置后直接编译，旧缓存（bootloader 等）不重建，导致 PSRAM 不初始化、Flash 仍识别为 2MB。

**原则**：**只要改 sdkconfig，就必须 `idf.py fullclean`。** 没有例外。否则旧二进制和旧配置会让你在一个虚假的"已配置"状态下浪费大量时间。

---
<br>

### 已删除文件

---

<br>

## 踩坑记录（Task 23~28：时钟 + 闹钟 + SNTP + RTC）

> 以下问题在开发时钟模块和 SNTP/RTC 时遇到。

### 坑 1：SNTP API 不兼容 —— ESP-IDF v6.0.2 只能用经典 API

- **现象**：`esp_netif_sntp.h` 中 `sync_interval` 字段不存在、`sntp_get_sync_status` 隐式声明、`SNTP_SYNC_STATUS_COMPLETED` 未定义。
- **原因**：ESP-IDF v6.0.2 的 `esp_sntp_config_t` 字段名和 v5.x/v6.x 最新版不同，`esp_netif_sntp_init()` 新 API 不稳定。
- **解决**：换回经典 API：`#include "esp_sntp.h"` → `esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL)` → `esp_sntp_setservername(0, "pool.ntp.org")` → `esp_sntp_init()`。`sntp_get_sync_status()` 和 `SNTP_SYNC_STATUS_COMPLETED` 在此头文件中可用。
- **原则**：ESP-IDF 版本碎片化严重，网络相关优先用经典 API，不要追新。

### 坑 2：前向声明缺失导致编译错误

- **现象**：`wizard_save()` 调用了后面才定义的 `hide_alarm_list()` 和 `update_time_display()`，编译报 implicit declaration + conflicting types。
- **原因**：函数重排时忘了加前向声明。
- **解决**：在文件顶部加 `static void update_time_display(void); static void hide_alarm_list(void);` 前向声明。

### 坑 3：LOCALTIME 默认 UTC 导致时差 8 小时

- **现象**：时钟显示 10:29 但实际是 18:29。
- **原因**：`time(NULL)` 返回 UTC，`localtime_r()` 无 TZ 环境变量时也按 UTC 解析。
- **解决**：SNTP 同步成功后调用 `setenv("TZ", "CST-8", 1); tzset();` 设北京时间。注意 `CST-8` 表示 UTC+8（POSIX 格式：时区名+偏移小时取负）。

### 坑 4：ESP32-S3 片上 RTC 断电就丢

- **认知**：`RTC_DATA_ATTR` 靠芯片内部电容维持，深度睡眠/软复位保留，但拔电就清零。ESP32-S3 没有 VBAT 引脚接纽扣电池，不是 STM32 那种真正的硬件 RTC。
- **实际方案**：RTC 只是"上次大概几点"的缓存。上电后用 WiFi + SNTP 对时，几秒内时间自动修正。用户无感。

### 坑 5：REPEAT 逐日 toggle 太难用

- **现象**：REPEAT 步骤用旋钮选日期 + 短按 toggle，用户不知道"怎么进入下一步"，Next 按钮和 toggle 冲突。
- **原因**：7 天逐日选择对旋钮+编码器交互不友好，没有"完成"的概念。
- **解决**：改为 **4 个预设模式**（Every day / Weekdays / Weekend / Once），旋钮一键切换，短按 Next 直接下一步。90% 场景一次旋钮+一次按键搞定。

### 坑 6：Edit/Del 列表模式退不出导致误操作

- **现象**：进入 Edit/Del 闹钟列表后，旋钮改焦点，短按直接删除/编辑——用户没有"取消"的路径。按 Back 直接退出整个时钟页面而非列表。
- **原因**：列表模式的短按在 `clock_input_cb`（Core 1）里直接处理，没有走 `activate_btn → Back` 的取消路径。
- **解决**：列表末尾加 `-- Cancel --` 条目（深红色背景），旋钮移到上面按一下即退出。同时重构 `activate_btn` 列表模式逻辑，短按统走 `s_btn_pending → activate_btn`。

### ⚠️ 核心反思：跨核 LVGL 违规反复出现

这次开发中反复犯同一个错误：**在 Core 1 回调中操作 LVGL**。从最初 `clock_input_cb` 里直接调 `hide_alarm_list()`、`popup_create()`，到 `wizard_start()` 中创建弹窗，都是跨核违规。

**根因**：虽然 tasks.md 里有"跨核安全铁律"的文档，但写代码时没有先查分发包（pomodoro.c / weather.c）的参考实现，看到回调里有 `btn_short` 就直接处理了。

**正确的开发流程**：
1. 写 Core 1 回调前，**先打开 pomodoro.c 看 `pomo_input_cb`** 是怎么写的
2. 它的模式只有一句话：所有短按 → `s_btn_pending = true`
3. **不创新、不优化、不"顺手处理"** — 哪怕看起来只是改个标志位后面跟着 `hide_alarm_list()` 也不行

**检查清单**（写任何 `*_input_cb` 前过一遍）：
- [ ] 函数内有没有调 LVGL 函数？（`lv_*`）
- [ ] 有没有调 `popup_create` / `hide_alarm_list` / `update_time_display` 等间接 LVGL 操作？
- [ ] 有没有调 `menu_engine_rotary_override`？（这个安全，但最好也延迟）
- [ ] 是否只做了：改数据 + 设 `volatile` 脏标？

### 坑 7：ESP32-S3 无片上 RTC 备份电源

- **现象**：断电后 `hal_rtc_get_time()` 返回 0，用户问能不能关机也走时。
- **认知**：ESP32-S3 RTC 域无 VBAT 引脚，拔电全丢。需要 DS3231 等外接 RTC 芯片才有断电走时能力。但 WiFi + SNTP 对时方案下这不是必须的。

---
<br>

## 踩坑记录（Task 31~32：SD 卡 HAL 驱动）

> 以下问题在开发 SD 卡 SPI 驱动（esp_vfs_fat + sdspi）时遇到。

### 坑 1：`esp_vfs_fat.h` 需要 `fatfs` 组件

- **现象**：`fatal error: esp_vfs_fat.h: No such file or directory`
- **原因**：该头文件在 `fatfs` 组件中（`components/fatfs/vfs/`），不是默认包含的。
- **解决**：在 `CMakeLists.txt` 的 `REQUIRES` 中添加 `esp_driver_sdspi`、`sdmmc`、`fatfs` 三个组件。

### 坑 2：ESP-IDF v6.0.2 自定义 SPI 引脚不能靠 `esp_vfs_fat_sdspi_mount` 解决

- **现象**：`SDSPI_HOST_DEFAULT()` 和 `SDSPI_DEVICE_CONFIG_DEFAULT()` 只暴露 CS 引脚配置，MOSI/MISO/SCLK 从 Kconfig/menuconfig 读取，无法通过代码传入。
- **解决路径**：预调用 `spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO)` 用自定义引脚初始化总线，让后续 `sdspi_host_init_device` 内的 `spicommon_bus_initialize_io()` 检测到总线已被占用后复用现有配置。
- **关键**：`spicommon_bus_initialize_io` 会先检查 `periph_claimed(host_id)`，如果总线已被占用则返回 OK 且不覆盖引脚配置。这正是我们需要的。

### 坑 3：设 `host.init = NULL` 导致 InstrFetchProhibited 崩溃

- **现象**：`Guru Meditation Error: Core 0 panic'ed (InstrFetchProhibited). PC: 0x00000000`，崩溃在 `esp_vfs_fat_sdspi_mount` 内。
- **原因**：`esp_vfs_fat_sdspi_mount` 内部先调 `sdmmc_host_init()` → `host->init()`。把 `host.init` 设为 NULL 不仅跳过了 SPI 总线初始化，也跳过了 `sdspi_host_init_device()`——该函数负责建立 **sdspi 设备内部状态**（CS 引脚中断、SPI 事务处理句柄等）。后续 `sdmmc_card_init` → `host->do_transaction` → `sdspi_host_do_transaction` 用了空的设备句柄导致调用地址 0x0 的函数指针。
- **解决**：**不要设 `host.init = NULL`**。保持默认的 `sdspi_host_init`，它内部会调 `sdspi_host_init_device`，检测到 SPI 总线已被我们预初始化后跳过总线初始化但完成设备状态建立。
- **教训**：高层封装函数（`esp_vfs_fat_sdspi_mount`）内部依赖复杂的多层初始化链，不要试图绕过其中某一环。改成"先占住总线资源，让它自己发现已有配置"的策略而非"跳过调用"。

### 坑 4：SDSPI 需要 GPIO ISR 服务

- **现象**：`gpio_isr_handler_add(564): GPIO isr service is not installed, call gpio_install_isr_service() first`
- **原因**：SDSPI 驱动需要 GPIO 中断来检测 SD 卡插入/移除（CS 脚），使用前必须安装全局 GPIO ISR 服务。
- **解决**：在 `spi_bus_initialize` 之后、`esp_vfs_fat_sdspi_mount` 之前调用 `gpio_install_isr_service(0)`。使用 `s_gpio_isr` 标志确保只安装一次（多次调用会报错）。
- **注意**：ISR 服务在应用生命周期中只安装一次，unmount 时不要卸载。

### 坑 5：`esp_vfs_fat_sdcard_unmount` 不释放预初始化的 SPI 总线

- **现象**：第一次 mount→unmount 正常，第二次 mount 报 `spi_bus_initialize(895): SPI bus already initialized`。
- **原因**：`esp_vfs_fat_sdcard_unmount` 内部调了 `sdspi_host_deinit`，它释放了 sdspi 设备，但**不会释放我们手动通过 `spi_bus_initialize` 初始化的总线**——因为总线是在 `esp_vfs_fat_sdspi_mount` 外部被占用的，unmount 不知道总线的"所有者"是我们。
- **解决**：`hal_sd_unmount` 中在 `esp_vfs_fat_sdcard_unmount` 之后，**手动调用 `spi_bus_free(SD_SPI_HOST)`** 补充释放。设置 `s_bus_init = false` 以允许下一次 mount 重新初始化。

### 坑 6：`sdspi_dev_handle_t` 是 `int` 不是指针

- **现象**：`initialization of 'sdspi_dev_handle_t' {aka 'int'} from 'void *' makes integer from pointer without a cast`
- **原因**：ESP-IDF v6.0.2 中该类型定义为 `typedef int sdspi_dev_handle_t`，不能用 NULL 比较。
- **解决**：用 `0` 初始化/比较，表示"未初始化"。

### 最终正确流程（hal_sd_mount 7 步）

```
1. spi_bus_initialize(SPI3_HOST, custom_pins)  ← 占住总线（自定义引脚）
2. gpio_install_isr_service(0)                  ← SDSPI 中断检测需要
3. SDSPI_HOST_DEFAULT() + host.slot = SPI3     ← 不设 init=NULL
4. esp_vfs_fat_sdspi_mount()
   ├─ sdmmc_host_init() → host->init() → sdspi_host_init()
   │   └─ sdspi_host_init_device()
   │       └─ spicommon_bus_initialize_io()
   │           └─ 检测总线已占用 → 返回 OK（复用步骤1的引脚配置）
   ├─ sdmmc_card_init()  ← 设备状态已建立，正常通信
   └─ 挂载 FatFS
```

### 文件清单

| 文件 | 说明 |
|------|------|
| `main/hal/hal_sd.h` | 4 个公开接口：`mount` / `unmount` / `list_dir` / `free_dir_list` |
| `main/hal/hal_sd.c` | SPI → FatFS 完整实现，7 步挂载流程，POSIX 目录遍历 |
| `main/hal/test_hal_sd.c` | 26 个断言覆盖挂载/卸载/列表/边界，验收通过后从编译列表移除 |
| `main/CMakeLists.txt` | 新增 `hal_sd.c`，REQUIRES 加 `esp_driver_sdspi`、`sdmmc`、`fatfs` |
| `main/main.c` | 无持久改动（仅测试时临时加调用入口，已移除） |

### hal_sd 对外接口

```c
bool hal_sd_mount(void);                              // 挂载 SD 卡（含 SPI 总线初始化）
void hal_sd_unmount(void);                            // 卸载 SD 卡（含 SPI 总线释放）
bool hal_sd_list_dir(const char *path, char ***names, size_t *count);  // 列出目录内容
void hal_sd_free_dir_list(char **names, size_t count); // 释放目录列表内存
```

---

<br>

## 踩坑记录（Task 39~40：宠物引擎状态机 + 需求衰减）

> 以下问题在开发 pet_engine 7 状态状态机和衰减系统时遇到。

### 坑 1：浮点累加器 vs 手动改结构体的同步问题

- **现象**：测试里 `pet_engine_init` 后设 `pet->hunger = 31`，再 `tick(15000)`。衰减没生效，hunger 还是靠近 100。
- **原因**：内部用 `static float` 累加器跟踪衰减，init 设为 100。手动改 `pet->hunger` 不会同步累加器，`tick()` 从 100 开始减，不是从 31。
- **解决方案演进**：
  1. 在 `tick()` 开头从结构体同步累加器 → 每帧从 uint8_t 重建 float，**小数精度全丢**（10 秒后 90 不是 99）
  2. 定点数 `int32_t × 65536` → **0.1 和 0.05 无法精确表示为整数倍**，ceil/floor 常量都有累计误差
  3. **最终方案：步进计数**——he 每满 10000ms 减 1 hunger，每满 20000ms 减 1 happiness。零乘法、零除法、零精度损失。tick 直接用 uint8_t 值，无内部累加器。

### 坑 2：为什么不用浮点

- 0.1 和 0.05 在 IEEE 754 float32 中都有循环二进制表示。10 次 `-= 0.1f` 不等于 `1.0f`（≈0.99999），截断为 uint8_t 时 `99.9999→99`，但阈值测试中 `29.9999→29` 看似对，实则不可靠。
- 定点数方案（×65536 或 ×1048576）一劳永逸？0.1×65536=6553.6，无论取 6553 还是 6554，10s 累计差 ±4/65536，离线 10 分钟累计差 ±240/1048576→结果 39 而非 40。
- **结论**：步进计数是唯一零误差方案。不适合需要亚秒级精度的场景，但对宠物需求衰减（10s/20s 粒度和粒度）完美匹配。

### 坑 3：`dt_ms` 会累积且四舍五入

- `s_hunger_decay_ms += dt_ms`，while ≥10000 减 10000。毫秒累加器保证 10s 精确 1 步，不丢余数。
- 测试中 `5×2000ms` 和 `10×1000ms` 效果相同（都是 10000ms），而浮点方案下两者结果可能不同。

### 最终实现：pet_engine 核心 API

```c
void pet_engine_init(pet_data_t *pet);                     // 初始化（hunger=100, happiness=100, state=IDLE）
void pet_engine_sync(pet_data_t *pet);                     // 手动改结构体后同步累加器（仅重置 ms 累加器）
void pet_engine_tick(pet_data_t *pet, uint32_t dt_ms);     // 步进衰减 + 冷却 + 状态迁移
void pet_engine_feed(pet_data_t *pet);                     // hunger += 25, state→EATING
void pet_engine_play(pet_data_t *pet);                     // happiness += 25, state→PLAYING
void pet_engine_on_shake(pet_data_t *pet);                 // 任意状态→SHOCKED, 2s 冷却
void pet_engine_apply_offline_decay(pet_data_t *pet, uint64_t current_time); // 离线衰减
```

### 文件清单

| 文件 | 说明 |
|------|------|
| `main/mode/pet/pet_engine.h` | 7 个公开接口声明 |
| `main/mode/pet/pet_engine.c` | 步进计数衰减 + 7 状态状态机 + 冷却/离线衰减 |
| `main/mode/pet/test_pet_engine.c` | 19 测试函数 42 断言，验收通过后从编译列表移除 |
| `main/CMakeLists.txt` | 新增 `pet_engine.c` 和 `mode/pet` include 路径 |

---

## 踩坑记录（Task 41~42：宠物动画帧管理 + 3D 预渲染工具链）

> 以下问题在开发 pet_anim 双模式帧管理器 + Blender 预渲染脚本 + PC 预览工具时遇到。

### 前置：为什么需要预渲染

ESP32-S3 无 GPU，无法实时渲染 3D。方案是离线把 GLB 模型从多角度预渲染成 ARGB8565 .bin 帧，运行时按 IMU 角度查表显示。效果等价于实时 3D 旋转。

### 坑 1：Blender 5.2 渲染像素缓冲访问方式变了

- **现象**：`bpy.data.images['Render Result'].pixels` 在 Blender 5.2 中报 `IndexError: index 0 out of range`，数组为空。
- **原因**：Blender 5.2 中 `bpy.ops.render.render(write_still=False)` 后的 Render Result 不再自动填充像素缓冲。
- **解决**：改为 `write_still=True` 写临时 PNG → `bpy.data.images.load(tmp_png)` → 读像素 → 删除临时文件。兼容所有 Blender 版本。

### 坑 2：PIL 依赖问题

- **现象**：Blender 自带 Python 没有 PIL，`from PIL import Image` 失败。
- **解决**：用 `bpy.data.images.load()` 返回的 `img.pixels`（Blender 内置浮点数组）直接读像素，零外部依赖。

### 坑 3：渲染结果上下颠倒

- **现象**：猫渲染出来头朝下脚朝上。
- **原因**：相机 `to_track_quat` 的 up 轴设为 `'Y'`，但 Blender 世界 Z 是上方。相机从正前方（0,-5,0）看向原点时，Y 轴 as up 导致视野翻转。
- **尝试的修复**：改为 `to_track_quat('-Z', 'Z')` —— 无效，特定角度下仍颠倒。
- **最终方案**：渲染后逐帧做垂直翻转。在 `render_to_argb8565()` 中写像素时 row y 映射为 `H-1-y`，一劳永逸。

### 坑 4：PC 预览页反复折腾

- **现象**：先写了统一 HTTP 服务器预览（`preview.py`），页面空白无内容。
- **根因链**：
  1. 自动点击 `btn.click()` 时 `event` 变量未定义 → JS 崩溃 → 整个页面无渲染
  2. 图片路径服务器根目录与页面目录不一致：页面在 `/pet_frames/`，图片 `idle/000.png` 是对的（相对路径），加了 ROOT 前缀后变成 `pet_frames/idle/000.png` → 浏览器解析为 `/pet_frames/pet_frames/idle/000.png` → 404
- **最终方案**：回归最简单的做法——每个动画目录下放一个独立的 `png/preview.html`，PNG 和 HTML 在同一目录，纯本地文件打开，不依赖 HTTP 服务器。这个方法在 Task 41-42 早期验证小猫时就已验证可用。

### 坑 5：多 mesh 模型的动画同步

- **现象**：小鸡模型导入后有 7 个独立 mesh 对象（body、tail、legs×4），只移动 body 时腿不动。
- **解决**：`render_seq()` 保存所有 mesh 对象的状态（位置+缩放），动画时统一移动，结束后全部恢复。

### 最终文件清单

| 文件 | 说明 |
|------|------|
| `main/mode/pet/pet_anim.h` | 公共 API：双模式帧管理（角度映射 + 时序播放） |
| `main/mode/pet/pet_anim.c` | 实现：PSRAM 全量缓存（idle 100 帧 4.8MB）+ 双缓冲流式读取（时序动画） |
| `main/mode/pet/test_pet_anim.c` | 15 项逻辑测试（角度→索引、时序推进、meta 解析），验收后已删除 |
| `main/CMakeLists.txt` | 取消注释 `pet_anim.c` |
| `scripts/render_frames.py` | 通用 Blender 渲染脚本：GLB/OBJ→ARGB8565 .bin，可用任意模型 |
| `scripts/preview.py` | PC 预览工具：.bin→PNG + 生成独立 HTML 预览页 |
| `README_FRAMES.md` | 帧格式说明 + 使用文档（项目根目录） |

---

## ⚠️ Task 41~42 反思总结

> 这次开发 3D 预渲染工具链暴露的问题。

### 教训 1：先跑通最简单的版本

最早的预览方案就是每个目录一个独立 HTML，小猫时跑通了。后来想"优化"成统一 HTTP 服务器 + 侧边栏导航，结果 JS 事件、路径解析、PIL 依赖连环崩，折腾了好几轮。**结论：MVP 跑通了就别动架构，除非新需求逼你改。**

### 教训 2：Blender 不同版本 API 不兼容

Blender 5.2 的 Render Result 像素访问行为与旧版不同。写 Blender 脚本时应避免依赖隐式行为（如"渲染后像素缓冲自动填充"），用显式流程（渲染到文件→加载文件→读像素）。

### 教训 3：纯本地 HTML 比本地 HTTP 服务器更可靠

`file://` 协议的相对路径简单直观，浏览器直接打开。`http://` 服务器多了一层根目录映射，路径容易搞错。除非需要 AJAX/fetch 实时数据，否则能本地打开就别起服务器。

### 教训 4：多 mesh 模型要统一操作

GLB 导入后可能是多个独立 mesh（body、legs、tail），移动/缩放时只操作一个对象会导致模型散架。应收集所有 mesh，统一变换，统一恢复。

---

## 踩坑记录（Task 43~52：pet_motion + pet_shake + pet_ui 集成）

> 将 pet_anim / pet_motion / pet_shake 串上 LVGL 屏幕，经历大量渲染调试。

### 坑 1：ARGB8565 格式 LVGL v9 不渲染

- **现象**：用 `LV_COLOR_FORMAT_ARGB8565` 的 `lv_image_dsc_t` 作为 `lv_image_set_src` 源，屏幕什么也不显示。
- **解决**：在 `pet_anim` 加载帧时实时转换：ARGB8565 (3B/px) → RGB565 (2B/px)，剥掉 Alpha 字节。同时省 33% PSRAM（100帧 4.8→3.2MB）。RGB565 是 ST7789 原生格式，显示正常。

### 坑 2：`lv_image_set_src` 重复调用不刷新

- **现象**：动画帧数据变了，`lv_image_set_src` 也调了，但屏幕图像不变。
- **排查**：新建红方块每秒闪烁测试——`lv_obj_set_style_bg_opa` 改变后红方块不闪，说明不是 image 的问题，是整个 LVGL 渲染不刷新。
- **根因**（tasks.md 坑 8 已记录）：LVGL v9 脏区标记在此硬件（ESP32-S3 + ST7789 SPI + PSRAM 双帧缓冲）不完全生效，`lv_timer_handler` 不触发 flush。
- **解决**：任何视觉变化后调用 `lv_refr_now(NULL)` 强制全屏刷新。重量级但必须。

### 坑 3：IMU 轴映射反复调整

- **现象**：倾斜设备后小鸡旋转方向（前后↔左右）和移动方向都反了。
- **根因**：Blender 预渲染时相机坐标系与 MPU6050 物理安装方向不一致，且 `MIRROR_X=1 MIRROR_Y=1` 镜像翻转进一步扰乱映射。
- **解决**：`angle_to_index()` 内交换 pitch/roll 并取反；`pet_motion` 调用时也交换并取反。最终只左右取反。

### 坑 4：边界裁剪与镜像偏移

- **现象**：小鸡飘动无法到达屏幕物理左上角，但右下角正常。
- **根因**：`MIRROR_X=1 MIRROR_Y=1` 使 LVGL 右下角映射到物理左上角。边界裁剪 `[64, 176]` 对上/左太保守。
- **解决**：放宽边界到 `[-64, 304]`，允许精灵中心超出屏幕范围。

### 坑 5：LVGL 屏幕默认有滚动条

- **现象**：屏幕右侧出现白色滚动条。
- **解决**：`lv_obj_set_scrollbar_mode(s_screen, LV_SCROLLBAR_MODE_OFF)`。

### 最终 pet 模式文件清单

| 文件 | 职责 |
|------|------|
| `pet_anim.c/h` | 双模式帧管理：角度映射（IMU→视角帧）+ 时序播放（动画） |
| `pet_motion.c/h` | 倾斜→速度映射 + 位置飘动 + 3s 归位 |
| `pet_shake.c/h` | 摇晃→SHOCKED + 2s 冷却 |
| `pet_ui.c/h` | LVGL 页面组装：串联以上模块 + lv_refr_now 强制刷屏 |
| `pet_engine.c/h` | 状态机（保留，简化后仅 IDLE/SHOCKED） |
| `mode_manager.c` | 路由 IMU 倾斜/摇晃到 pet_ui |
| `main.c` | 启动→SD 挂载→默认 PET 模式 |

## ⚠️ Task 43~52 反思总结

### 教训 1：LVGL v9 在 ESP32-S3 + ST7789 + PSRAM 组合下脏区机制不可靠

这是重复踩坑（tasks.md 坑 8 已经记录过），但在图像渲染调试时忘了。每次怀疑 "是不是 lv_image 坏了 → 是不是 canvas 坏了 → 是不是数据错了"，绕了一大圈才发现是 `lv_timer_handler` 不 flush。
**教训：遇到 LVGL 不刷新，第一时间上 `lv_refr_now`，不要怀疑数据。**

### 教训 2：先验证屏幕能刷新，再调试业务逻辑

红方块闪烁测试只要 5 行代码，能快速区分"屏幕刷新坏了"和"我的逻辑错了"。以后任何 UI 问题第一时间用这个。

### 教训 3：镜像后坐标系要对着屏幕实测

预判 MIRROR_X/MIRROR_Y 的映射关系不如让用户实际试，反馈"左上不到、右下可以"直接定位到哪边边界保守。

---

## ✅ 动画看板 Task（animation_board）

### 新增文件

| 文件 | 职责 |
|------|------|
| `scripts/video_to_frames.py` | PC 脚本：视频/GIF → 240×240 RGB565 .bin 帧序列 + meta.txt |
| `main/mode/menu/animation_board.c/h` | PSRAM 全量预加载播放器 + SD 卡视频列表选择 |

### 功能

1. **PC 脚本**：FFmpeg+Pillow 将任意视频/GIF 转为 RGB565 帧，保持比例居中填充。支持单文件/批量/文件夹。超过 70 帧自动拒绝（PSRAM 限制），失败后删除输出目录不留空壳。
2. **ESP32 播放器**：从 `/sdcard/animations/` 扫描视频目录，显示旋钮可选的滚动列表（参照 weather 城市列表实现：视口裁剪+Y 轴移动滚动+选中项文字变色高亮）。选中的视频全量读入 PSRAM，播放时纯指针切换，零延迟。循环播放，短按返回列表，长按退出。
3. **加载进度条**：加载期间显示进度条 + "已加载/总数" 文字，实时刷新。

### 架构决策

- **全量预加载**而非流式读取：避免每帧 115ms SD 读阻塞渲染周期导致卡顿。8MB PSRAM 约可存 70 帧（~8 秒 @8fps）。
- **就地高亮**而非独立游标：独立游标和列表不同父容器会导致坐标偏移，改为直接修改选中项的背景和文字颜色。

### 菜单集成

在 `menu_ui.c` 的 `init_modules` / `process_module_updates` 中添加 `animation_board_init` / `animation_board_process_updates`，遵循现有模式。

---

## ✅ 屏幕旋转 Task

### 改动

| 文件 | 改动 |
|------|------|
| `app_config.h` | 新增 `LCD_ROTATION 270`（右转90°）、MIRROR_X→0 |
| `hal_display.c` | 旋转适配 swap_xy + mirror + gap 互换（90°/270°时 gap_x/gap_y 交换） |
| `pet_ui.c` | IMU 轴重映射：`p=roll, r=pitch` |

### 关键：gap 和旋转的顺序

`esp_lcd_panel_set_gap` 必须在 swap_xy/mirror 之后调用，否则 gap 对应的行列方向是旋转前的。90°/270° 时 gap_X 和 gap_Y 要互换（原来 gap_y=80 现在变成 gap_x=80），最终用户实测 gap 全设为 0 正常。

---

## ⚠️ Task 动画看板反思总结

### 教训 1：列表 UI 不要发明自己的，直接抄已有的成功实现

第一版自创的光标+滚动方案失败（光标跑到屏幕外、坐标错位），第二版参照 weather 的 popup 模式（视口裁剪 + Y 移动滚动 + 就地变色高亮）一次性通过。**项目里已有成熟的列表实现时，先看懂它，照搬它的模式，不要另起炉灶。**

### 教训 2：延迟初始化避免页面动画冲突

页面创建器（creator）中调用 `lv_refr_now` 时容器还在屏幕外（x=240 等待滑入动画），导致加载进度条不可见。改为 creator 只设标志位，等页面完全就绪后在 `process_updates` 中延迟初始化。

### 教训 3：屏幕旋转不只是 swap_xy

只改 swap_xy 不够，gap 偏移量也必须互换，且 gap 设置必须在旋转配置之后。这些连锁依赖要一次考虑全。

---

## ✅ Task 29~30：音频驱动（MP3-TF-16P / YX5200）

### 硬件变更

实际模块是 **MP3-TF-16P（YX5200-24SS）**，而非原本计划的 DFPlayer Mini。协议完全不同。

| 参数 | 值 |
|------|-----|
| 接口 | UART TTL 3.3V, 9600bps |
| 引脚 | ESP32 TX=GPIO43, RX=GPIO44 |
| 音频 | 自带 8002 功放（3W），支持 SPK1/SPK2 或 DAC_L/DAC_R |
| 格式 | MP3、WAV（WMA 需定制） |
| TF卡 | FAT16/FAT32，最大 32GB，自带卡槽 |

### UART 协议要点

- 帧格式：`7E FF 06 CMD FB DH DL [CHK_H CHK_L] EF`（校验和 = 中间 6 字节累加取反）
- 可选不带校验：`7E FF 06 CMD FB DH DL EF`
- 上电后等 1.5s 初始化；选设备后等 200ms 再发指令
- 核心指令：`0x03` 指定曲目、`0x0F` 指定文件夹曲目、`0x0D` 播放、`0x0E` 暂停、`0x01/0x02` 上下曲、`0x06` 音量、`0x09` 选设备（2=TF）

### 新增/修改文件

| 文件 | 职责 |
|------|------|
| `main/hal/hal_audio.c/h` | YX5200 UART 驱动：帧封装+校验和+阻塞查询响应 |
| `scripts/setup_music.py` | PC 脚本：音乐目录 → MP3-TF-16P 卡（编号命名）+ ESP32 SD 卡 music_map.txt |
| `scripts/regenerate_font.py` | 中文字库重生成脚本 |
| `main/app_config.h` | 音频引脚改 TX=43 RX=44，宏名 DFP→AUDIO |

---

## ✅ Task 33~34：音乐播放器 + Task 73~74：中文字库

### 新增文件

| 文件 | 职责 |
|------|------|
| `main/mode/menu/music_player.c/h` | 文件浏览器 + 播放器 UI |
| `main/assets/font_noto_16.c/h` | Noto Sans SC 16px 中文字库（292 中文 + ASCII，114KB） |

### 架构：两张卡 + 映射文件

```
ESP32 SPI SD 卡 (/sdcard/music_map.txt)     MP3-TF-16P TF 卡根目录
  F 周杰伦                                    01/001晴天.mp3
    M 1 1 晴天.mp3                             01/002稻香.mp3
    M 1 2 稻香.mp3                             001孤勇者.mp3
  M 0 1 孤勇者.mp3
```

- ESP32 卡：只需 `music_map.txt`（路径→编号映射，几 KB）
- MP3-TF-16P 卡：`scripts/setup_music.py` 自动重命名+编文件夹
- 用户操作：脚本一次生成两份，分别复制到两张卡

### UI 交互

| 模式 | 操作 | 行为 |
|------|------|------|
| 列表 | 旋钮 | 滚动文件/文件夹 |
| 列表 | 短按文件夹 | 进入 |
| 列表 | 短按 `..` | 返回上级 |
| 列表 | 短按文件 | 选中进入播放器（不播放） |
| 列表 | 短按 `< Back` | 退出音乐页面 |
| 播放器 | 按 Play | 开始播放，变 Paus |
| 播放器 | 按 Paus | 暂停，变 Play |
| 播放器 | Prev/Next | 切歌并播放 |
| 播放器 | Vol 短按 | 弹窗 + 旋钮调音量 + 短按确认 |
| 全局 | 长按 | 返回（子页面内不切换 PET 模式） |

### 按钮栏（番茄钟风格）

5 个按钮，3 个可见，2 页滑动切换：
- Page0: `[Prev] [Play] [Next]`
- Page1: `[Vol] [Back]`

跨页时整行 200ms ease-out 滑出/滑入动画（参照 pomodoro 单按钮动画模式）。

### 音量弹窗

点击 Vol → 屏幕中央弹窗显示 "Vol: XX" → 旋钮调节 → 短按关闭。使用 `menu_engine_rotary_override` 劫持旋钮。

### 中文字库（Task 73-74 提前）

使用 `lv_font_conv` 从 Noto Sans SC Regular TTF 生成 16px 4bpp LVGL 字库。覆盖项目中所有可见中文（天气城市名、菜单项、音乐文件名）及常用歌名字，共 292 个中文 + ASCII。文件 114KB。

regenerate_font.py 脚本支持 `--add "新字"` 追加字符。

---

## 踩坑记录（Task 29~34：音乐播放器 + MP3-TF-16P + 中文字库）

> 以下问题在开发音乐模块时遇到，涵盖硬件协议、GPIO 冲突、音量交互和中文字库。

### 坑 1：实际模块是 YX5200 不是 DFPlayer

- **现象**：plan.md 里写的是 DFPlayer Mini，但用户实际用的是 MP3-TF-16P（YX5200-24SS 芯片）。两个模块 UART 协议完全不同——DFPlayer 用 `7E FF 06 ...` 格式和不同的命令字。
- **解决**：完全按 MP3-TF-16P 手册重写 `hal_audio.c`。帧格式：`7E FF 06 CMD FB DH DL CHK_H CHK_L EF`，校验和 = 0 - 累加值。命令字也不同（如 0x03 指定曲目、0x0D 播放）。
- **原则**：**模块型号一定要在开发前确认**，不要根据 plan 里的假设写代码。

### 坑 2：GPIO43/44 与 USB Serial/JTAG 冲突

- **现象**：`uart_driver_install` 报 `rx buffer length error`，然后 ESP32 重启。后来 buffer 修好了能发送命令但没声音。
- **根因**：ESP32-S3 的 GPIO43(TX) 和 GPIO44(RX) 是 USB Serial/JTAG 的专用引脚。当 `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y` 时，这些引脚被 USB 控制器占用，UART1 无法使用。
- **排查过程**：
  1. 先怀疑 RX buffer 太小（32B < UART FIFO 128B）→ 改 256B，不再崩溃
  2. 关掉所有串口调试（`CONFIG_ESP_CONSOLE_NONE=y`）→ 命令发出去了但没声音
  3. 真正问题：模块的 TF 卡文件没拷对
- **教训**：ESP32-S3 的 GPIO43/44 是特殊引脚。如果要用作普通 UART，必须关掉 `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG`，否则硬件 pin 冲突。

### 坑 3：`uart_driver_install` RX buffer 必须 > 128

- **现象**：`ESP_ERROR_CHECK` 崩溃，`uart rx buffer length error`。
- **原因**：ESP32-S3 的 UART 硬件 FIFO 是 128 字节，`rx_buffer_size` 必须大于它。原来的 `RX_BUF_SIZE=32` → `*2=64` 不够。
- **解决**：改为 `RX_BUF_SIZE=256` → `*2=512`。

### 坑 4：0x03 指令自带播放

- **现象**：列表里点一首歌就直接开始播放了，不等用户按 Play。
- **原因**：MP3-TF-16P 的 `0x03` 指令（指定曲目）语义是"指定曲目**并播放**"，不是"选歌等待"。
- **解决**：选歌时只记录曲目信息（`do_select`），Play 按钮才真正发送 `0x03` 开始播放。这样用户可以浏览列表、选中歌曲、决定要不要播。

### 坑 5：音量调节方案反复折腾（方法论文本）

- **迭代过程**：
  1. 第一版：两个按钮 Vol-/Vol+，短按 ±1。用户觉得不够。
  2. 第二版：短按 ±1，连按加速（±3/±5）。用户不想要，要长按持续。
  3. 第三版：长按持续调音量（150ms 一级），超时 3s 自动停。但编码器没有"松手"事件，长按只触发一次，导致无法停止。
  4. 最终版：Vol 按钮打开弹窗，旋钮直接调音量，短按确认关闭。**这是最自然的交互**——和番茄钟调时间一模一样。
- **教训**：**音量调节用旋钮比用按键直观得多**。应该一开始就做弹窗+旋钮方案，少走三轮弯路。

### 坑 6：长按退出与模式切换冲突

- **现象**：音乐播放器里长按要退出页面，但 `mode_manager` 同时把长按解释为 PET↔MENU 切换，导致退出子页面后立刻跳到宠物模式。
- **根因**：`mode_manager_on_btn` 对所有长按一刀切切模式，不考虑当前菜单层级。
- **解决**：在 `mode_manager_on_btn` 中检查 `menu_engine_get_level() == MENU_LEVEL_SUB`，子页面内长按转发给 `menu_engine_go_back()`，不再切模式。主页面长按仍可切换。
- **原则**：输入路由要感知上下文（当前在哪个页面/层级），不能全局一刀切。

### 坑 7：Montserrat 字体不含汉字

- **现象**：中文文件名显示为方块（□）。
- **原因**：Montserrat 是纯拉丁字体，无 CJK 字形。
- **解决**：用 `lv_font_conv` + Noto Sans SC 生成 16px 中文字库 `font_noto_16`（292 中文 + ASCII, 114KB）。音乐播放器和天气城市名都改成用这个字体。

### 坑 8：`lv_font_conv` 生成的代码编译但不渲染

- **现象**：字库 .c 文件编译通过，但 screen 上文字全消失（不是方块，是彻底没有）。
- **排查**：`lv_font_conv 1.5.3` 生成代码里自带 `#if FONT_NOTO_16` 守卫（默认 `#define FONT_NOTO_16 1`），所以编译是正常的。但 bpp=4 的 4bit 抗锯齿位图格式与 LVGL v9 不完全兼容（v9 改了字体渲染管线）。
- **解决**：改用 `--bpp 1`（纯黑白）生成，94KB，正常渲染。

### 坑 9：Play/Pause 按钮状态不同步

- **现象**：进入播放器后按钮显示 "Play"，但 `s_audio_playing` 已经是 true。按一下变成 "Paus"（暂停），再按还是 "Paus"（文字没变）。
- **根因**：`do_select` 后 `s_audio_playing=false`，进入播放器时 `s_ui_dirty` handler 没有更新按钮文字。`activate_btn` 里更新了 Label 文字，但 `highlight_btns` 后又可能被覆盖。
- **解决**：在 `s_ui_dirty` handler 中显式更新 Play 按钮的 `lv_label_set_text`，确保进入播放器视图时按钮文字正确反映 `s_audio_playing` 状态。

---

## ⚠️ Task 33~34 反思总结

### 教训 1：模块型号要开发前确认，协议看手册不靠猜

plan 里写了 DFPlayer，实际是 YX5200。两个模块协议完全不一样，相当于白写了 DFPlayer 驱动。**任何硬件模块必须向用户确认型号+拿到数据手册再动代码。**

### 教训 2：编码器单按键下"长按持续"不可行

编码器只有短按、长按两个事件，没有"按住中"和"松手"事件。做"按住持续调音量"需要持续事件流，但编码器硬件给不了。**不要和硬件能力较劲**——旋钮调音量才是自然方案，短按确认关闭，简单可靠。

### 教训 3：先跑通最小可行版本，交互慢慢迭代

音量交互改了 4 版才稳定。如果一开始就问用户"旋钮调音量可以吗？"，或者直接参照番茄钟的 adjust 模式，一下午的折腾可以避免。

### 教训 4：特殊 GPIO 要查数据手册

ESP32-S3 的 GPIO43/44 是 USB Serial/JTAG 引脚，不是普通 GPIO。直接 assign 给 UART1 会导致 pin conflict。**任何引脚分配都要查芯片数据手册确认有没有复用冲突**，不能只看 PCB 走线。

### 教训 5：字体方案要测试后才知道兼容性

`lv_font_conv` 声称支持 LVGL v9，但 bpp=4 实际不渲染。用 bpp=1 最简单可靠，16px 下抗锯齿差异肉眼不可辨。
