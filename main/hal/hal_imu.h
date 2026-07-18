/**
 * @file    hal_imu.h
 * @brief   IMU 驱动抽象层 —— MPU6050 I2C 初始化、姿态角、倾斜/摇晃检测
 */
#ifndef HAL_IMU_H
#define HAL_IMU_H

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 I2C 总线和 MPU6050（±4g 加速度计 + ±250°/s 陀螺仪） */
void hal_imu_init(void);

/**
 * @brief 读取姿态角 + 内部执行摇晃检测
 *
 * 一次 I2C 读取 14 字节（accel+gyro），计算 pitch/roll，
 * 同时用 HoloCubic 迟滞回差方案检测左右摇晃。
 *
 * @return imu_angles_t  pitch 和 roll，单位：度
 */
imu_angles_t hal_imu_get_angles(void);

/** 获取最近一次加速度计原始值（g） */
imu_accel_t hal_imu_get_accel(void);

/** 获取当前倾斜方向（前/后/左/右/水平） */
imu_tilt_dir_t hal_imu_get_tilt_dir(void);

/** 获取最近一次摇晃方向（左/右/无） */
imu_shake_dir_t hal_imu_get_shake_dir(void);

/** 注册摇晃回调（摇晃发生时触发） */
void hal_imu_set_shake_callback(imu_shake_callback_t cb);

#ifdef __cplusplus
}
#endif

#endif /* HAL_IMU_H */
