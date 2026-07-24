# 全息棱镜宠物助手

基于 ESP32-S3 + LVGL v9.5 的全息棱镜宠物桌面摆件。ST7789 240×240 屏幕透过分光棱镜显示虚拟宠物和实用工具菜单。

## 硬件

| 器件 | 型号 |
|------|------|
| 主控 | Waveshare ESP32-S3-Zero (8MB Flash / 8MB PSRAM) |
| 屏幕 | ST7789 1.54" 240×240 (N154-2424KBWPG05-H12) |
| IMU | MPU6050 (I2C, ±4g) |
| 编码器 | EC11 旋转编码器 + 按键 |
| 音频 | MP3-TF-16P (YX5200, UART1, GPIO43/44) |
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
| 音乐播放器 | 文件浏览器选歌，MP3-TF-16P 播放，旋钮调音量 |
| 动画看板 | SD 卡浏览动画序列循环播放 |

### 导航
- 短按旋钮进入/确认，长按切换 宠物↔菜单 模式
- 主菜单切换方式由 `MENU_INPUT_MODE` 配置（0=倾斜 1=旋钮 2=两者）
- 子页面支持无限嵌套（栈式 push/pop 动画），统一输入过滤
- 未实现功能显示 "Coming Soon" 占位页

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
│   │   ├── hal_imu.c/.h           # MPU6050 IMU
│   │   ├── hal_audio.c/.h         # MP3-TF-16P UART 驱动
│   │   └── hal_sd.c/.h            # SPI SD 卡 (FatFS)
│   ├── mode/
│   │   ├── mode_manager.c/.h      # 模式切换状态机
│   │   └── menu/
│   │       ├── menu_engine.c/.h   # 菜单导航 + 统一输入过滤
│   │       ├── menu_ui.c/.h       # 菜单 UI + 栈式子页面 + 聚合初始化
│   │       ├── pomodoro.c/.h      # 番茄钟
│   │       ├── weather.c/.h       # 天气
│   │       ├── clock_alarm.c/.h   # 时钟闹钟
│   │       ├── animation_board.c/.h # 动画看板
│   │       └── music_player.c/.h  # 音乐播放器
│   ├── assets/
│   │   └── font_noto_16.c/.h      # 中文字库 (Noto Sans SC 16px)
│   └── service/                   # 服务层（WiFi/HTTP/SNTP）
├── doc/                           # 设计文档
└── sdcard_layout/                 # SD 卡布局说明
```

## 开发进度

| 阶段 | 状态 |
|------|------|
| 阶段 0：工程骨架 | ✅ 完成 |
| 阶段 1：HAL + 框架（M1） | ✅ 屏幕亮起，旋钮可切换模式 |
| 阶段 2：菜单模式核心 | ✅ 完成（5 项功能全部验收） |
| 阶段 3：宠物模式 | ✅ 完成（IMU 视角旋转 + 倾斜飘动 + 摇晃受惊） |
| 阶段 4：边界处理 & 全息矫正 | ⏳ 待开始 |
| 阶段 5：打磨验收 | ⏳ 待开始 |

### 最新完成 (Task 29~34)
- **MP3-TF-16P 驱动**：YX5200 UART 协议，9600bps，校验和+查询响应
- **音乐播放器**：文件树浏览器（从 music_map.txt）、文件夹进入/返回、播放暂停/上下曲/旋钮调音量弹窗、番茄钟风格 3 可见按钮滑动切换、长按退出不切模式
- **中文字库**：Noto Sans SC 16px，292 中文 + ASCII，114KB
- **音乐部署脚本**：`scripts/setup_music.py` 一键生成 MP3-TF-16P 卡和 ESP32 SD 卡映射文件

## 构建

```bash
# 设置 ESP-IDF 环境
. /path/to/esp-idf/export.sh

# 编译
idf.py build

# 烧录
idf.py -p COMx flash monitor
```

## SD 卡资源准备

### 音乐播放器

音乐需要同时部署到两张卡——MP3-TF-16P 的 TF 卡（存音频文件）和 ESP32 的 SPI SD 卡（存映射索引）：

```bash
# 1. 准备音乐目录（正常文件名）
music/
├── 周杰伦/
│   ├── 晴天.mp3
│   └── 稻香.mp3
└── 孤勇者.mp3

# 2. 运行脚本
python scripts/setup_music.py music/

# 3. 部署
#    music/_mp3_output/*  → 复制到 MP3-TF-16P 的 TF 卡根目录
#    music/music_map.txt  → 复制到 ESP32 SPI SD 卡根目录
```

脚本自动处理：文件夹→编号、文件名→编号前缀、生成映射表。

**支持格式**：MP3、WAV（WMA 需定制模块固件）。

### 宠物精灵帧

用 Blender 将 3D 模型预渲染为精灵帧序列：

```powershell
blender --background --python scripts/render_frames.py
```

配置编辑 `scripts/render_frames.py` 顶部的 `CONFIG`：
```python
MODEL_PATH  = r"模型路径.glb"
OUTPUT_DIR  = r"pet_frames/"
SPRITE_SIZE = 128
ANGLE_STEPS = 10   # 10×10=100 角度帧
```

PC 预览：
```powershell
python scripts/preview.py
# 鼠标拖拽模拟 IMU 倾斜，空格播放/暂停动画
```

部署：把 `pet_frames/` 整个目录拷贝到 SD 卡根目录。

帧格式：ARGB8565（3字节/像素），meta.txt 描述宽高帧数。

### 动画看板视频

将视频/GIF 转换为 240×240 RGB565 帧序列（需 FFmpeg + Pillow）：

```bash
# 单文件转换
python scripts/video_to_frames.py video.mp4 -o frames/

# GIF 转换（建议 8fps）
python scripts/video_to_frames.py anim.gif -o frames/ -f 8

# 批量转换文件夹
python scripts/video_to_frames.py ./videos/ -o frames/

# 超出70帧自动跳过（ESP32 PSRAM 限制），降帧率可容纳更长视频：
python scripts/video_to_frames.py long.gif -o frames/ -f 6
```

部署：把 `frames/` 下各子目录放入 SD 卡 `/sdcard/animations/`：
```
/sdcard/animations/
  demo/            # 目录名即为视频名
    000.bin ...     # RGB565 帧
    meta.txt        # "240 240 帧数 fps"
  my_video/
    000.bin ...
    meta.txt
```

## 开发规范

- **不用 `lv_anim`**：手动 tick 驱动所有动画（避免 LVGL v9 task_wdt 问题）
- **UI 修改后调 `lv_refr_now(NULL)`**：LVGL 脏区标记在本硬件不生效
- **子页面 API**：`menu_ui_page_create()` 创建，`menu_ui_push_page()` 推入，`menu_ui_register_creator()` 注册到菜单项
- **按钮回调 `on_btn` 在 Core 0 执行**：可以安全操作 LVGL，直接创建嵌套页面
- 详情见 `doc/tasks.md` 末尾踩坑记录和子模块开发手册

## License

MIT
