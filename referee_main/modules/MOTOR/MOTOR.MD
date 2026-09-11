# 电机模块框架文档

## 概述

电机模块是一个**面向对象风格**的电机驱动框架，基于继承和多态模式统一管理不同类型的电机（CAN 总线电机、UART 步进电机、PWM 舵机等）。所有电机通过全局链表注册，由一个独立的 ThreadX 任务周期性调度。

### 已支持的电机

| 模块 | 传输层 | 电机型号 | 控制方式 |
|------|--------|---------|---------|
| [DJI](DJI/DJI.MD) | CAN | M2006/M3508/GM6020 | PID / LQR + 电流环 |
| [DAMIAO](DAMIAO/DAMIAO.MD) | CAN | DM4310/DM6220 | PID / LQR + MIT/POS/SPD/PSI |
| [SERVO](SERVO/SERVO.MD) | PWM | 标准舵机 | 角度→脉宽 |
| [ZDT_STEPMOTOR](ZDT_STEPMOTOR/ZDT_STEPMOTOR.MD) | UART | ZDT 步进电机 | 速度/位置 UART 协议 |

## 架构设计

### 继承体系

```
Motor_Base (基类)                   ← motor_base.h
  ├── Motor_Base  *next             ← 指向下一个电机
  ├── Motor_Type_e type             ← 电机型号
  ├── Motor_Transport_e transport   ← CAN / UART / PWM
  ├── Motor_Info_s       info       ← 物理参数
  ├── Motor_Setting_s    setting    ← 闭环类型, 反转标志, 反馈源...
  ├── Motor_Controller_s controller ← PID / LQR 实例 + ref + output
  ├── Motor_Measure_s    measure    ← 公共测量
  ├── Offline_Device    *offline_dev
  ├── void              *transport_dev
  └── void (*ControlAndSend)(Motor_Base *motor)  ← 虚函数

子类（继承 Motor_Base，base 必须是首字段）:
  ├── DJI_Motor_t      (CAN)     ← DJI motor
  ├── DM_Motor_t       (CAN)     ← DAMIAO motor
  ├── Servo_Motor_t    (PWM)     ← Servo
  └── UART_Motor_t     (UART)    ← ZDT stepper
```

通过 `MOTOR_GET_DERIVED(base_ptr, derived_type)` 宏将基类指针还原为子类指针（利用首字段地址相同的特性）。

### 传输层抽象

```c
typedef enum {
    MOTOR_TRANSPORT_CAN  = 0,  // CAN 总线 (DJI, DAMIAO)
    MOTOR_TRANSPORT_UART = 1,  // UART 串口 (ZDT)
    MOTOR_TRANSPORT_PWM  = 2,  // PWM  (Servo)
} Motor_Transport_e;

typedef struct {
    // ... 公共字段 ...
    Motor_Transport_e transport;
    union {
        Can_Device_Init_Config_s can;
        UART_Device_init_config  uart;
        PWM_Init_Config          pwm;
    } transport_config;
} Motor_Init_Config_s;
```

通过 `transport` 枚举 + `transport_config` 联合体，一套 `Motor_Init_Config_s` 即可初始化任意类型电机。


## 核心数据结构

### Motor_Setting_s — 控制设置

```c
typedef struct {
    Closeloop_Type_e         loop_type;             // 闭环类型 (位掩码)
    Control_Algorithm_Type_e algorithm_type;        // PID / LQR
    uint8_t                  enableflag;            // 使能标志
    uint8_t                  motor_reverse_flag;    // 输出反转
    uint8_t                  feedback_reverse_flag; // 反馈反转
    uint8_t                  angle_feedback_source; // 0=电机, 1=外部
    uint8_t                  speed_feedback_source; // 0=电机, 1=外部
} Motor_Setting_s;
```

### Motor_Controller_s — 控制器

```c
typedef struct {
    const float *other_angle_feedback_ptr;  // 外部角度反馈指针
    const float *other_speed_feedback_ptr;  // 外部速度反馈指针
    PIDInstance  speed_PID;                 // 速度环 PID
    PIDInstance  angle_PID;                 // 位置环 PID
    LQRInstance  lqr;                       // LQR 控制器
    float        ref;                       // 当前参考值
    float        output;                    // 当前输出值
    float        output_torque;             // 当前力矩输出 (Nm)
    float feedforward_torque;               // 前馈力矩 (Nm)，用于补偿
} Motor_Controller_s;
```

### Motor_Measure_s — 公共测量数据

```c
typedef struct
{
    float speed_rad;          /* 角速度 (rad/s) */
    float single_round_angle; /* 单圈角度 (rad) */
    float total_angle;        /* 总角度 (rad)   */
    float torque_nm;          /* 当前力矩 (Nm)  */
} Motor_Measure_s;
```

> 各子类有自己的扩展测量结构（如 `DJI_Measure_s` 含温度、编码器等），公共字段用于功率控制和上层统一读取。

## 初始化流程

```
Module_Motor_Init()
  ├── Motor_BaseInit()               ── 初始化全局链表
  ├── tx_thread_create("motor")      ── 创建调度线程 (PRIO=12, STACK=1024)
  └── 线程自动启动

用户代码:
  Motor_DJI_Init(&config)            ── 初始化具体电机
  ├── BSP_MEM_ALLOC (子类)
  ├── 填充 base 字段 + 子类专属字段
  ├── BSP_xxx_Device_Init()          ── 注册传输层设备
  ├── PIDInit() / LQRInit()          ── 初始化控制器
  ├── Module_Offline_register()       ── 离线检测
  ├── base.ControlAndSend = xxx_ControlAndSend  ← 绑定虚函数
  └── Motor_Register(&base)          ── 加入全局链表
```

## 如何添加一个新电机

以添加一个**"XX 品牌 CAN 电机"**为例，按以下步骤操作：

### 目录结构

```
modules/MOTOR/XX/
  ├── motor_xx.h
  └── motor_xx.c
```

### 定义子类结构 (`motor_xx.h`)

```c
#ifndef _MOTOR_XX_H_
#define _MOTOR_XX_H_

#include "motor_base.h"
#include "bsp_can.h"       // 按需: CAN / UART / PWM

// 1. 定义测量数据结构 (可选)
typedef struct {
    float position;
    float velocity;
    float current;
    uint8_t temperature;
    // ... 电机特有数据
} XX_Measure_s;

// 2. 定义电机子类 — base 必须是首字段
typedef struct {
    Motor_Base   base;         // [必须首字段]
    XX_Measure_s measure;      // 扩展测量数据
    // ... 子类专属字段
} XX_Motor_t;

// 3. 声明 API
XX_Motor_t *Motor_XX_Init(Motor_Init_Config_s *config);
void        Motor_XX_Start(XX_Motor_t *motor);
void        Motor_XX_Stop(XX_Motor_t *motor);
void        Motor_XX_SetRef(XX_Motor_t *motor, float ref);

#endif
```

### 实现 ControlAndSend (`motor_xx.c`)

```c
static void xx_ControlAndSend(Motor_Base *base)
{
    XX_Motor_t *m = MOTOR_GET_DERIVED(base, XX_Motor_t);

    if (!base->setting.enableflag) return;

    // 1. PID/LQR 计算 (可选)
    float output;
    if (base->setting.algorithm_type == CONTROL_PID) {
        output = PIDCalculate(&base->controller.speed_PID, measure, base->controller.ref);
    } else {
        output = LQRCalculate(&base->controller.lqr, ...);
    }
    base->controller.output = output;

    // 2. 填充 base->measure (供功率控制使用)
    base->measure.speed_rad   = m->measure.velocity;
    base->measure.total_angle = m->measure.position;
    base->measure.torque_nm   = /* 计算或读取 */;

    // 3. 通过传输层发送
    BSP_CAN_SendMessage(&msg);   // CAN
    // 或 BSP_UART_Send(...)    // UART
    // 或 BSP_PWM_SetPulse(...) // PWM
}
```

### 实现 Init 函数

```c
XX_Motor_t *Motor_XX_Init(Motor_Init_Config_s *config)
{
    XX_Motor_t *motor = NULL;
    BSP_MEM_ALLOC_WAIT(motor, sizeof(XX_Motor_t), TX_NO_WAIT);
    if (!motor) return NULL;
    memset(motor, 0, sizeof(XX_Motor_t));

    // 1. 填充基类
    motor->base.type      = config->motor_init_info.motor_type;
    motor->base.transport = MOTOR_TRANSPORT_CAN;  // 或 UART / PWM
    motor->base.info      = config->motor_init_info;
    motor->base.setting   = config->setting_init_config;

    // 2. 注册传输层设备
    void *dev = BSP_CAN_Device_Init(&config->transport_config.can);
    if (!dev) { BSP_MEM_FREE(motor); return NULL; }
    motor->base.transport_dev = dev;

    // 3. 设置接收回调 (CAN/UART 电机需要)
    // ((Can_Device *)dev)->rx_callback = xx_rx_callback;
    // ((Can_Device *)dev)->user_arg    = motor;

    // 4. 初始化控制器 (如果需要 PID/LQR)
    PIDInit(&motor->base.controller.speed_PID, &config->controller_init_config.speed_PID);
    PIDInit(&motor->base.controller.angle_PID, &config->controller_init_config.angle_PID);

    // 5. 离线检测
    motor->base.offline_dev = Module_Offline_register(&config->offline_init_config);

    // 6. 绑定虚函数 + 注册
    motor->base.ControlAndSend = xx_ControlAndSend;
    Motor_Register(&motor->base);

    return motor;
}
```

### 注册电机类型 (`motor_def.h`)

在 `Motor_Type_e` 枚举中添加新类型：

```c
typedef enum {
    // ... 已有类型 ...
    XX_MOTOR_TYPE,        // ← 新增
} Motor_Type_e;
```

### 注册头文件 (`module_motor.h`)

```c
#include "motor_xx.h"   // ← 新增
```

### 在CMakeLists.txt中增加编译文件

```c
if(MODULE_MOTOR)
    list(APPEND _module_sources
        MOTOR/module_motor.c
        MOTOR/motor_base.c
        MOTOR/DJI/motor_dji.c
        MOTOR/POWER_CONTROL/power_control.c
        MOTOR/DAMIAO/motor_damiao.c
        MOTOR/SERVO/motor_servo.c
        MOTOR/ZDT_STEPMOTOR/motor_zdt.c
        MOTOR/XX/motor_xx.c //--> 新增
    )
    list(APPEND _module_includes
        ${CMAKE_CURRENT_SOURCE_DIR}/MOTOR
        ${CMAKE_CURRENT_SOURCE_DIR}/MOTOR/DJI
        ${CMAKE_CURRENT_SOURCE_DIR}/MOTOR/DAMIAO
        ${CMAKE_CURRENT_SOURCE_DIR}/MOTOR/SERVO
        ${CMAKE_CURRENT_SOURCE_DIR}/MOTOR/ZDT_STEPMOTOR
        ${CMAKE_CURRENT_SOURCE_DIR}/MOTOR/POWER_CONTROL
        ${CMAKE_CURRENT_SOURCE_DIR}/MOTOR/XX  //--> 新增
    )
endif()
```

## API 接口说明

```c
void Motor_BaseInit(void); 
```

```c
void Motor_Register(Motor_Base *motor);              
```

```c
void Motor_UpdateAll(void);                          
```

```c
void Module_Motor_Init(void);                       
```

## 注意事项
- 这里的电机数据接收目前只有can的，`uart需要自行建立task去接收电机数据`

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0 | 2026-05-11 | 初始版本 |
| v1.1 | 2026-05-15 | 将内部指针改为const，增加前馈力矩补偿，fix：掉线失能问题 |
| v1.2 | 2026-05-21 | 使用单向链表，方便调试 |
