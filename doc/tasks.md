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

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 9 | **测试** | `main/test_main.c` | 测试各模块初始化顺序正确性（模拟 init 调用不崩溃）、FreeRTOS 任务创建参数（优先级/栈/核心） |
| 10 | 实现 | `main/main.c` | 初始化流程：HAL 各模块 → 服务层 → mode_manager → LVGL；创建 6 个 FreeRTOS 任务（lvgl / sensor / encoder / audio / network / logic）；启动调度器 |

> **里程碑 M1**：屏幕亮起 + 旋钮可切换两个占位页面。

---

## 阶段 2：菜单模式核心（M2：菜单模式 5 项功能验收通过）

### 2.1 菜单导航引擎 + 菜单主页面

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 11 | **测试** | `main/mode/menu/test_menu_engine.c` | 测试 5 项菜单列表双向滚动（番茄钟/天气/时钟/音乐/动画看板）、倾斜角→选项映射、按键进入子项/返回父级、选项边界循环 |
| 12 | 实现 | `main/mode/menu/menu_engine.c` | 菜单导航逻辑：5 项菜单项枚举、倾斜角度映射到选中项、按钮 enter/back 状态转换 |
| 13 | **测试** | `main/mode/menu/test_menu_ui.c` | 测试主菜单 LVGL 列表渲染、选中项高亮、图标/文字对齐、页面切换动画 |
| 14 | 实现 | `main/mode/menu/menu_ui.c` | LVGL 菜单主页面：5 项列表（lv_list / lv_roller）、选中高亮、倾斜滚动列表、进入子页面/返回 |

### 2.2 番茄钟

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 15 | **测试** | `main/mode/menu/test_pomodoro.c` | 测试工作时长/休息时长设置 [1,99] 边界、倒计时递减正确性、阶段切换（工作→休息→工作）、running 标志、环形进度条角度计算 |
| 16 | 实现 | `main/mode/menu/pomodoro.c` | 番茄钟逻辑：设置 work/break 时长、1s 递减 `remaining_seconds`、阶段切换（工作 ↔ 休息 + 提示音）、LVGL 环形进度条渲染 |

### 2.3 天气模块

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 17 | **测试** | `main/mode/menu/test_weather.c` | 测试天气 JSON 解析（city/temperature/description/icon_code 字段提取）、stale 标记逻辑、HTTP 失败重试、温度边界值 |
| 18 | 实现 | `main/mode/menu/weather.c` | HTTP 天气 API 请求（esp_http_client 流式）、cJSON 解析天气数据填充 `weather_data_t`、LVGL 展示（城市/温度/描述/图标）、数据过期标记 |

### 2.4 天气依赖：Wi-Fi 服务

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 19 | **测试** | `main/service/test_svc_wifi.c` | 测试 Wi-Fi 连接状态机（断开→连接中→已连接→失败）、SSID/PASS 从 NVS 读取、回调触发、重连逻辑 |
| 20 | 实现 | `main/service/svc_wifi.c` | Wi-Fi STA 模式连接管理：`svc_wifi_connect/disconnect`、连接状态回调、自动重连（最大 3 次）、NVS 凭据读取 |

### 2.5 天气依赖：HTTP 客户端

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 21 | **测试** | `main/service/test_svc_http.c` | 测试 HTTP GET 请求构建、响应状态码处理（200/404/超时）、流式读取回调、内存边界（≤4KB 缓冲） |
| 22 | 实现 | `main/service/svc_http.c` | esp_http_client 封装：流式响应读取、cJSON 增量解析、超时设置（10s）、错误码映射 |

### 2.6 时钟 & 闹钟

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 23 | **测试** | `main/mode/menu/test_clock_alarm.c` | 测试闹钟增删改查、enabled 开关、7 天重复规则、闹钟触发时间匹配（hour/minute 比较）、同时触发多闹钟 |
| 24 | 实现 | `main/mode/menu/clock_alarm.c` | 闹钟管理：最大 10 个闹钟的增删改、7 天重复、1Hz 时间检查触发、LVGL 时钟展示（HH:MM:SS） |

### 2.7 时钟依赖：SNTP 同步

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 25 | **测试** | `main/service/test_svc_sntp.c` | 测试 SNTP 同步成功/超时状态、`svc_sntp_synced` 返回值、同步后 RTC 时间更新 |
| 26 | 实现 | `main/service/svc_sntp.c` | SNTP 时间同步（`esp_netif_sntp`）、同步状态标记、同步后写入 RTC |

### 2.8 时钟依赖：RTC 驱动

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 27 | **测试** | `main/hal/test_hal_rtc.c` | 测试 RTC 读写往返一致性（set→get 验证）、时间戳范围 [0, UINT32_MAX]、断电保持 |
| 28 | 实现 | `main/hal/hal_rtc.c` | 片上 RTC 读写封装：`hal_rtc_set_time` / `hal_rtc_get_time`（基于 Unix timestamp） |

### 2.9 音乐播放器

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 29 | **测试** | `main/mode/menu/test_music_player.c` | 测试歌曲列表加载、播放/暂停/停止/上曲/下曲状态转换、文件夹切换、SD 卡无 MP3 文件的边界 |
| 30 | 实现 | `main/mode/menu/music_player.c` | SD 卡扫描 MP3 列表、DFPlayer 控制（播放/暂停/停止/上一曲/下一曲）、LVGL 歌曲列表 UI、文件夹浏览 |

### 2.10 音乐依赖：音频驱动

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 31 | **测试** | `main/hal/test_hal_audio.c` | 测试 DFPlayer UART 指令帧格式、音量范围 [0,30]、播放/暂停/停止/下一曲/上一曲指令序列、ACK 应答检测 |
| 32 | 实现 | `main/hal/hal_audio.c` | DFPlayer Mini UART 指令封装：`hal_audio_set_volume` / `play` / `stop` / `pause` / `resume` / `next` / `prev`、指令队列 + ACK 检测 + 重发 |

### 2.11 音乐依赖：SD 卡驱动

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 33 | **测试** | `main/hal/test_hal_sd.c` | 测试 SD 卡挂载/卸载、目录列表读取、文件存在性检查、无卡/损坏的边界返回 |
| 34 | 实现 | `main/hal/hal_sd.c` | SPI → FatFS SD 卡挂载（`esp_vfs_fat`）、`hal_sd_mount/unmount`、`hal_sd_list_dir` 目录遍历、内存释放 `hal_sd_free_dir_list` |

### 2.12 动画看板

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
| 39 | **测试** | `main/mode/pet/test_pet_engine.c` | 测试状态迁移表：IDLE→HUNGRY（饥饿 <30）、IDLE→SAD（开心 <30）、任意→SHOCKED（摇晃）、IDLE→EATING/WALKING/PLAYING；需求值衰减 1Hz 计算；需求值边界 [0,100]；喂食/玩耍后需求回升 |
| 40 | 实现 | `main/mode/pet/pet_engine.c` | 7 状态宠物状态机（IDLE/HUNGRY/SAD/SHOCKED/EATING/PLAYING/WALKING）、需求衰减（饥饿 +0.1/s，开心 -0.05/s）、喂食/玩耍施加需求增量、离线衰减计算（基于 last_tick 时间差） |

### 3.2 宠物动画帧管理

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 41 | **测试** | `main/mode/pet/test_pet_anim.c` | 测试帧序列加载、双缓冲轮播切换、动画状态→帧索引映射、不可打断动画标志位、动画完成回调 |
| 42 | 实现 | `main/mode/pet/pet_anim.c` | 从 SD 卡按状态读取图片帧序列、双缓冲轮播（前一帧显示 + 下一帧预读）、≥10fps 播放、`anim_busy` 标志管理 |

### 3.3 倾斜→位置映射 & 平滑移动

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 43 | **测试** | `main/mode/pet/test_pet_motion.c` | 测试 tilt→速度映射函数（死区/线性/饱和三段）、位置边界裁剪 [0,240]×[0,240]、归位缓动曲线、遮挡时宠物移到前端 |
| 44 | 实现 | `main/mode/pet/pet_motion.c` | 倾斜角→移动速度映射（死区 ±5° / 线性 / 饱和 ±45°）、平滑位置更新、边界停止、空闲 3s 自动归位缓动 |

### 3.4 摇晃响应（受惊动画 + 音效 + 冷却）

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 45 | **测试** | `main/mode/pet/test_pet_shake.c` | 测试摇晃状态机（进入 SHOCKED → 播放受惊动画 → 恢复 IDLE）、冷却时间 2s 内忽略重复摇晃、音效触发、与 `anim_busy` 的交互 |
| 46 | 实现 | `main/mode/pet/pet_shake.c` | 摇晃触发受惊动画（表情变化 + 弹跳）、触发 DFPlayer 播放受惊音效、2s 冷却窗口（`shake_cooldown`）、冷却结束后恢复原状态 |

### 3.5 持久化存取 & 离线衰减

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 47 | **测试** | `main/service/test_svc_persistence.c` | 测试 NVS 读写 pet 需求值（hunger/happy/tick）、离线衰减计算（1h 离线 → 饥饿 +6 开心 -3）、alarm 列表序列化/反序列化、WiFi 凭据存取、错误处理（NVS 满/未初始化） |
| 48 | 实现 | `main/service/svc_persistence.c` | NVS 初始化、`svc_persistence_save/load_pet`（hunger/happy/last_tick 读写）、离线衰减计算（按 1%/10min 衰减）、alarm blob 序列化、WiFi 凭据保存 |

### 3.6 音量管理

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 49 | **测试** | `main/service/test_svc_volume.c` | 测试音量范围 [0,30] 边界、NVS 存取一致性、旋钮→音量映射、切换模式后的音量策略 |
| 50 | 实现 | `main/service/svc_volume.c` | 全局音量管理：`svc_volume_set/get`、NVS 持久化（键 `sys_volume`）、旋钮增量映射到音量值 |

### 3.7 宠物模式 LVGL 页面组装

| 任务 | 类型 | 文件 | 说明 |
|------|------|------|------|
| 51 | **测试** | `main/mode/pet/test_pet_ui.c` | 测试页面创建/销毁、宠物精灵层层级、状态图标（饥饿/电量）显隐逻辑、需求图标位置、UI 元素不超出屏幕范围 |
| 52 | 实现 | `main/mode/pet/pet_ui.c` | LVGL 宠物模式页面：宠物动画层（lv_image）、状态图标栏（饥饿图标 / 电量图标）、需求数值条、页面生命周期（进入/退出/刷新） |

> **里程碑 M3**：宠物模式功能完整（AC-1 ~ AC-11 验收标准，除菜单入口外）。

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
| 阶段 5（打磨验收） | 10 | 5 | 5 |
| **合计** | **87** | **41** | **46** |

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
