#include "robot_control.h"

#include "bsp_def.h"
#include "tx_api.h"

#define LOG_TAG "referee_controller"
#define LOG_LVL LOG_LVL_INFO
#include "ulog_def.h"

#define REFEREE_CONTROLLER_STACK_SIZE 1024U
#define REFEREE_CONTROLLER_PRIORITY   20U

static TX_THREAD referee_controller_thread;
APPS_STACK_SECTION static uint8_t referee_controller_stack[REFEREE_CONTROLLER_STACK_SIZE];

static void referee_controller_entry(ULONG thread_input)
{
    (void)thread_input;

    LOG_I("referee_controller thread running");

    while (1)
    {
        tx_thread_sleep(100);
    }
}

void robot_control_init(void)
{
    UINT status = tx_thread_create(&referee_controller_thread, "referee_controller", referee_controller_entry, 0,
                                   referee_controller_stack, REFEREE_CONTROLLER_STACK_SIZE, REFEREE_CONTROLLER_PRIORITY,
                                   REFEREE_CONTROLLER_PRIORITY, TX_NO_TIME_SLICE, TX_AUTO_START);
    if (status != TX_SUCCESS)
    {
        LOG_E("referee_controller create failed: %u", status);
        return;
    }

    LOG_I("referee_controller started");
}
