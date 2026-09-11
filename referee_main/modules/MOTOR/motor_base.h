/*
 * @Author: laladuduqq 2807523947@qq.com
 * @Date: 2026-05-11 17:00:00
 * @FilePath: /mas_embedded_threadx/modules/MOTOR/motor_base.h
 * @Description: 
 */
#ifndef _MOTOR_BASE_H_
#define _MOTOR_BASE_H_

#include "module_offline.h"
#include "motor_def.h"
#include <stdint.h>

typedef struct Motor_Base Motor_Base;

struct Motor_Base
{
    Motor_Base       *next;
    Motor_Type_e      type;
    Motor_Transport_e transport;

    Motor_Info_s       info;
    Motor_Setting_s    setting;
    Motor_Controller_s controller;
    Motor_Measure_s    measure;
    Offline_Device    *offline_dev;

    void *transport_dev;

    void (*ControlAndSend)(Motor_Base *motor);
};

#define MOTOR_GET_DERIVED(base_ptr, derived_type) ((derived_type *)(base_ptr))

/**
 * @brief 注册电机
 * @param motor 电机实例指针
 */
void Motor_Register(Motor_Base *motor);

/**
 * @brief 更新所有电机(motor task里调用)
 */
void Motor_UpdateAll(void);

#endif /* _MOTOR_BASE_H_ */
