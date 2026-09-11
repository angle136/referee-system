/*
 * @Author: laladuduqq 2807523947@qq.com
 * @Date: 2026-05-11 17:00:00
 * @FilePath: /mas_embedded_threadx/modules/MOTOR/DJI/motor_dji.h
 * @Description:
 */
#ifndef _MOTOR_DJI_H_
#define _MOTOR_DJI_H_

#include "motor_base.h"
#include "motor_def.h"
#include <stdint.h>

typedef struct
{
    uint16_t last_ecd;     /* 上一次编码器值 */
    uint16_t ecd;          /* 0-8191 */
    float    speed_rpm;    /* 角速度 (RPM) */
    int16_t  real_current; /* 实际电流 (A) */
    uint8_t  temperature;  /* 温度 (℃) */
    int32_t  total_round;  /* 总圈数 */
} DJI_Measure_s;

/* DJI_Motor_t — 继承 Motor_Base */
typedef struct
{
    Motor_Base    base;         /* [必须首字段] 公共基类 */
    DJI_Measure_s measure;      /* DJI电机数据 */
    uint8_t       sender_group; /* CAN 发送分组 */
    uint8_t       message_num;  /* 组内序号 0-3 */
} DJI_Motor_t;

/**
 * @brief DJI 电机初始化
 * @param config 电机初始化配置 (transport 必须为 MOTOR_TRANSPORT_CAN)
 * @return DJI_Motor_t 指针, 失败返回 NULL
 */
DJI_Motor_t *Motor_DJI_Init(Motor_Init_Config_s *config);

/**
 * @brief DJI 电机停止 (清零使能标志)
 */
void Motor_DJI_Stop(DJI_Motor_t *motor);

/**
 * @brief DJI 电机使能
 */
void Motor_DJI_Start(DJI_Motor_t *motor);

/**
 * @brief 修改闭环反馈数据源
 * @param loop            闭环类型 (ANGLE_LOOP / SPEED_LOOP)
 * @param feedback_source  0=电机反馈, 1=外部反馈
 */
void Motor_DJI_ChangeFeed(DJI_Motor_t *motor, Closeloop_Type_e loop, uint8_t feedback_source);

/**
 * @brief 设置外环类型
 */
void Motor_DJI_OuterLoop(DJI_Motor_t *motor, Closeloop_Type_e outer_loop);

/**
 * @brief 设置参考值
 * @param ref 位置 (rad) 或 速度 (rad/s) , 取决于闭环类型
 */
void Motor_DJI_SetRef(DJI_Motor_t *motor, float ref);

/**
 * @brief 发送 DJI CAN 帧
 * @note  在 Motor_UpdateAll() 之后调用, 批量发送所有启用的 DJI CAN 分组
 */
void Motor_DJI_Flush(void);

/**
 * @brief 设置前馈扭矩
 * @param torque 前馈扭矩 (Nm)
 */
void Motor_DJI_SetForwardTorque(DJI_Motor_t *motor, float torque);

#endif /* _MOTOR_DJI_H_ */
