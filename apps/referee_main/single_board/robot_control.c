#include "robot_control.h"

#include "bsp_def.h"
#include "iwdg.h"
#include "main.h"
#include "tx_api.h"
#include "usart.h"

#include <stdio.h>

#define LOG_TAG "referee_controller"
#define LOG_LVL LOG_LVL_INFO
#include "ulog_def.h"

#define REFEREE_CONTROLLER_STACK_SIZE 1024U
#define REFEREE_CONTROLLER_PRIORITY   20U

static TX_THREAD referee_controller_thread;
APPS_STACK_SECTION static uint8_t referee_controller_stack[REFEREE_CONTROLLER_STACK_SIZE];

static void led_test_init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2, GPIO_PIN_RESET);

    gpio_init.Pin   = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2;
    gpio_init.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull  = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio_init);
}

static void led_test_set(uint32_t active_pin)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, active_pin, GPIO_PIN_SET);
}

static void uart_test_send(UART_HandleTypeDef *uart, const char *name, uint32_t counter)
{
    char message[64];
    int  length = snprintf(message, sizeof(message), "[%s] referee uart led test %lu\r\n", name, counter);

    if (length > 0)
    {
        HAL_UART_Transmit(uart, (uint8_t *)message, (uint16_t)length, 10);
    }
}

static void referee_controller_entry(ULONG thread_input)
{
    (void)thread_input;

    uint32_t led_index = 0;
    uint32_t counter   = 0;
    const uint32_t led_pins[] = {GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2};

    led_test_init();

    while (1)
    {
        HAL_IWDG_Refresh(&hiwdg);

        led_test_set(led_pins[led_index]);

        uart_test_send(&huart1, "USART1", counter);
        uart_test_send(&huart2, "USART2", counter);
        uart_test_send(&huart3, "USART3", counter);
        uart_test_send(&huart4, "UART4", counter);

        led_index = (led_index + 1U) % (sizeof(led_pins) / sizeof(led_pins[0]));
        counter++;

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
