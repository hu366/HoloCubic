# 项目状态摘要 — Task 11~14 完成

## 1. 已完成功能

### Task 11（测试）: `main/mode/menu/test_menu_engine.c`
- 19 个测试用例：类型验证、状态机转换、边界循环、冷却
- 独立文件，不编译进主固件

### Task 12（实现）: `main/mode/menu/menu_engine.c` + `.h`
- **三级状态机**：TOP（主菜单轮播）→ SUB（子页面）→ CONFIRM_EXIT（确认弹窗）
- 左/右倾斜切换菜单项，300ms 冷却防连滚
- 短按进入子页面 / 确认弹窗是/否切换
- 子页面输入回调注册（`menu_engine_set_sub_callback`）
- **集成修改**：`mode_manager.c` 路由 MENU 事件到 engine、`CMakeLists.txt` 加源文件、`app_config.h` 加 `MENU_SCROLL_COOLDOWN_MS`

### Task 13（测试）: `main/mode/menu/test_menu_ui.c`
- 13 个测试用例：屏幕创建/销毁、轮播更新、确认弹窗显隐
- 独立文件

### Task 14（实现）: `main/mode/menu/menu_ui.c` + `.h`
- **主菜单轮播**：5 项横向滑动切换（同子页面款动画），每项含标题 + 页码 + 5 指示圆点
- **子页面**：3 个占位按钮（Back / Option A / Option B），倾斜导航焦点，短按触发
- **确认退出弹窗**：半透明遮罩 + "Quit? YES/NO"
- **手动动画系统**：200ms ease-out，支持 ANIM_PAGE（子页面滑入滑出）和 ANIM_CAROUSEL（轮播滑动）
- 显示名暂用英文（Pomodoro/Weather/Clock/Music/Animation）
- **集成修改**：`main.c` 默认进入 MENU、PET 延迟创建、动画进行中锁、`CMakeLists.txt` 加 menu_ui

### 额外修改
- `mode_manager.c`：MENU 模式屏蔽摇晃检测、默认启动 MODE_MENU
- `main.c`：`lvgl_task` 栈 8KB→12KB

---

## 2. 当前处理的问题

- **✅ 已修复** — Task Watchdog 崩溃：根因是 `lv_anim_start` 与 LVGL v9 动画系统冲突，改用手动 tick 驱动
- **✅ 已修复** — 屏幕不更新：根因是 LVGL 脏区标记不触发，所有 UI 修改后跟 `lv_refr_now(NULL)` 强制刷新
- **✅ 已修复** — 进入子页面后崩溃：根因是 `sensor_task`（Core 1）回调直接操作 LVGL 对象导致内存损坏，改为设脏标 + `lvgl_task`（Core 0）统一刷新
- **⚠ 已知未修** — 字体边缘白点：LVGL 字体抗锯齿在全息镜像（MIRROR_X=1, MIRROR_Y=1）下的正常现象，后续阶段 4 关抗锯齿解决
- **⚠ 已知未修** — 中文字符显示为方块：Montserrat 不含 CJK 字符，当前英文占位，Task 73-74 中文字库就绪后替换

---

## 3. 下一步计划

### 立即可做
| 任务 | 文件 | 说明 |
|------|------|------|
| 15 (测试) | `test_pomodoro.c` | 番茄钟测试 |
| 16 (实现) | `pomodoro.c` | 番茄钟逻辑 + 环形进度条 |

### 子模块开发规范（所有 Task 15~38 遵守）
1. 进入子页面时调 `menu_engine_set_sub_callback()` 注册输入回调
2. 点 [返回] 时调 `menu_engine_request_exit()` 触发确认弹窗
3. **不用 `lv_anim`**（会 task_wdt），动画用手动 tick
4. UI 更新后调 `lv_refr_now(NULL)` 强制刷新
5. `sub_input_cb` 回调中只改状态变量，不操作 LVGL（Core 1 安全）
6. 中文等 Task 73-74 字库就绪后再加

### 后续任务序列
```
Task 15-16 → 番茄钟
Task 17-18 → 天气（依赖: Task 19-20 svc_wifi, Task 21-22 svc_http）
Task 23-24 → 时钟&闹钟（依赖: Task 25-26 svc_sntp, Task 27-28 hal_rtc）
Task 29-30 → 音乐播放器（依赖: Task 31-32 hal_audio, Task 33-34 hal_sd）
Task 35-36 → 动画看板
Task 37-38 → 菜单音量调节
→ 里程碑 M2：菜单模式完成
→ 阶段 3：宠物模式
→ 阶段 4：边界处理 & 全息矫正
→ 阶段 5：打磨验收（含 Task 73-74 中文字库）
```

---

## 4. 关键技术决策

| 决策 | 选择 | 原因 |
|------|------|------|
| 动画方案 | 手动 tick（不用 lv_anim） | `lv_anim_start` 导致 task_wdt 死锁 |
| 屏幕刷新 | `lv_refr_now(NULL)` 每帧 | LVGL 脏区标记在此硬件不触发 flush |
| UI 对象管理 | 轮播每次创建新容器（滑动切换）、子页面复用 | 滑动动画需要两个容器同时存在；子页面创建一次反复显隐 |
| 跨核安全 | Core 1 只设脏标，Core 0 刷新 UI | `sensor_task` 回调链路到 `sub_input_cb`，直接操作 LVGL 导致内存损坏 |
| 中文显示 | 英文占位，Task 73-74 字库就绪后替换 | Montserrat 无 CJK 字符 |
| MENU 模式摇晃 | 屏蔽（直接 return） | 摇晃加速度尖峰触发倾斜方向变化，干扰菜单导航 |
| 默认启动模式 | MODE_MENU | 开发阶段方便测试菜单功能 |
| 字体抗锯齿白点 | 暂不处理，阶段 4 解决 | 镜像翻转后的已知现象 |

---

## 5. 当前文件清单

### 新增文件
```
main/mode/menu/
├── menu_engine.h          # 菜单引擎头文件
├── menu_engine.c          # 三级状态机实现
├── menu_ui.h              # 菜单 UI 头文件
├── menu_ui.c              # 轮播+子页面+弹窗+手动动画
├── test_menu_engine.c     # Task 11 测试
└── test_menu_ui.c         # Task 13 测试
```

### 修改文件
```
main/main.c                # 默认 MENU、PET 延迟创建、动画锁、栈 12KB
main/CMakeLists.txt        # 加 menu_engine.c + menu_ui.c + mode/menu include
main/mode/mode_manager.c   # MENU 路由到 engine、屏蔽摇晃、默认 MENU
main/app_config.h          # MENU_SCROLL_COOLDOWN_MS
doc/tasks.md               # Task 11-14 状态 + 子模块开发规范 + 踩坑记录
```
