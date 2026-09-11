#include "motor_base.h"

static Motor_Base *g_motor_list = NULL;

void Motor_Register(Motor_Base *motor)
{
    if (motor == NULL) return;
    motor->next = g_motor_list;
    g_motor_list = motor;
}

void Motor_UpdateAll(void)
{
    for (Motor_Base *motor = g_motor_list; motor; motor = motor->next)
    {
        if (motor->ControlAndSend) motor->ControlAndSend(motor);
    }
}
