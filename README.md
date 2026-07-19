# 全息棱镜宠物助手

基于 ESP32-S3 + LVGL v9.5 的全息棱镜宠物桌面摆件。ST7789 240×240 屏幕透过分光棱镜显示虚拟宠物和实用工具菜单。

## 硬件

| 器件 | 型号 |
|------|------|
| 主控 | Waveshare ESP32-S3-Zero (4MB Flash / 4MB PSRAM) |
| 屏幕 | ST7789 1.54" 240×240 (N154-2424KBWPG05-H12) |
| IMU | MPU6050 (I2C, ±4g) |
| 编码器 | EC11 旋转编码器 + 按键 |
| 音频 | DFPlayer Mini |
| SDK | ESP-IDF v6.0.2 |

## 功能

### 宠物模式
- 虚拟宠物（猫）在棱镜中活动：发呆、走动、受惊反应
- 倾斜设备让宠物跟手滑动，摇晃触发特殊动画
- 饥饿度和开心度系统，关机后根据时间离线衰减

### 菜单模式（5 项）
| 功能 | 说明 |
|------|------|
| 番茄钟 | 自定义工作/休息时长，环形进度条，阶段切换提示 |
| 天气 | WiFi 连接后获取天气，展示城市/温度/状况 |
| 时钟闹钟 | SNTP 同步时间，多闹钟管理 |
| 音乐播放器 | SD 卡 MP3 列表，DFPlayer 控制 |
| 动画看板 | SD 卡浏览动画序列循环播放 |

### 导航
- 倾斜设备切换菜单项，短按旋钮确认，长按返回
- 子页面支持无限嵌套（栈式 push/pop 动画）
- 前后倾斜切换子页面按钮，游标滑动动画

## 目录结构

```
project/
├── main/
│   ├── main.c                     # 入口，初始化 + FreeRTOS 任务
│   ├── app_config.h               # 全局宏（引脚、参数）
│   ├── app_types.h                # 公共类型
│   ├── hal/                       # 硬件抽象层
│   │   ├── hal_display.c/.h       # ST7789 SPI 显示
│   │   ├── hal_touch_encoder.c/.h # EC11 编码器
│   │   └── hal_imu.c/.h           # MPU6050 IMU
│   ├── mode/
│   │   ├── mode_manager.c/.h      # 模式切换状态机
│   │   └── menu/
│   │       ├── menu_engine.c/.h   # 菜单导航逻辑
│   │       └── menu_ui.c/.h       # 菜单 UI + 栈式子页面框架
│   └── service/                   # 服务层（待实现）
├── doc/                           # 设计文档
└── sdcard_layout/                 # SD 卡布局说明
```

## 开发进度

| 阶段 | 状态 |
|------|------|
| 阶段 0：工程骨架 | ✅ 完成 |
| 阶段 1：HAL + 框架（M1） | ✅ 屏幕亮起，旋钮可切换模式 |
| 阶段 2：菜单模式核心 | 🔄 进行中（Task 11-14 完成） |
| 阶段 3：宠物模式 | ⏳ 待开始 |
| 阶段 4：边界处理 & 全息矫正 | ⏳ 待开始 |
| 阶段 5：打磨验收 | ⏳ 待开始 |

### 最新完成 (Task 11-14)
- **menu_engine**：TOP/SUB 二级状态机，倾斜轮播 + 300ms 冷却
- **menu_ui**：栈式子页面框架（push/pop 无限嵌套）、可滚动按钮列表（3 按钮视口）、手动动画引擎（A_PAGE / A_CAROUSEL / A_SCROLL）
- **跨核安全**：Core 1 只设数据标志，Core 0 统一操作 LVGL
- **显示修复**：RGB565 字节序交换（flush 回调手动 swap），面板 RGB 色序确认

## 构建

```bash
# 设置 ESP-IDF 环境
. /path/to/esp-idf/export.sh

# 编译
idf.py build

# 烧录
idf.py -p COMx flash monitor
```

## 开发规范

- **不用 `lv_anim`**：手动 tick 驱动所有动画（避免 LVGL v9 task_wdt 问题）
- **UI 修改后调 `lv_refr_now(NULL)`**：LVGL 脏区标记在本硬件不生效
- **子页面 API**：`menu_ui_page_create()` 创建，`menu_ui_push_page()` 推入，`menu_ui_register_creator()` 注册到菜单项
- **按钮回调 `on_btn` 在 Core 0 执行**：可以安全操作 LVGL，直接创建嵌套页面
- 详情见 `doc/tasks.md` 末尾踩坑记录和子模块开发手册

## License

MIT
