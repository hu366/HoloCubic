/**
 * @file    settings.h
 * @brief   设置页面 —— 屏幕镜像等系统选项
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void settings_init(void);
void settings_tick(uint32_t dt_ms);
void settings_process_updates(void);

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_H */
