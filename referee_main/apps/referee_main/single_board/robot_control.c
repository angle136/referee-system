#include "robot_control.h"

#include <stdint.h>

#include "armor_counter.h"
#include "armor_link.h"
#include "bsp_def.h"
#include "bsp_uart.h"
#include "gpio.h"
#include "iwdg.h"
#include "REFEREE/referee_protocol.h"
#include "referee_config.h"
#include "referee_packet.h"
#include "referee_state.h"
#include "referee_ui.h"
#include "tx_api.h"
#include "usart.h"

#define LOG_TAG "referee_controller"
#define LOG_LVL LOG_LVL_INFO
#include "ulog_def.h"

#define REFEREE_MAIN_THREAD_STACK_SIZE 1024U

static TX_THREAD referee_tx_thread;
static TX_THREAD armor_rx_thread;
static TX_THREAD referee_watchdog_thread;

APPS_STACK_SECTION static uint8_t referee_tx_thread_stack[REFEREE_MAIN_THREAD_STACK_SIZE];
APPS_STACK_SECTION static uint8_t armor_rx_thread_stack[REFEREE_MAIN_THREAD_STACK_SIZE];
APPS_STACK_SECTION static uint8_t referee_watchdog_thread_stack[
    REFEREE_MAIN_WATCHDOG_STACK_SIZE];

static TX_MUTEX referee_tx_mutex;
static TX_MUTEX armor_counter_mutex;

static UART_Device *referee_uart;
static UART_Device *armor_uart[REFEREE_MAIN_ARMOR_COUNT];
static uint8_t      armor_rx_buffer[REFEREE_MAIN_ARMOR_COUNT][REFEREE_MAIN_ARMOR_RX_BUFFER_SIZE]
    BUFFER_SECTION;
static uint8_t referee_sequence;
static uint8_t armor_config_sequence;
static armor_counter_manager_t armor_counters;
static volatile uint32_t referee_tx_count;
static volatile uint32_t referee_tx_error_count;
static volatile uint32_t armor_config_tx_count;
static volatile uint32_t armor_config_tx_error_count;
static volatile uint16_t referee_last_command_id;
static volatile uint8_t armor_adc_debug_active;

static int referee_send_frame(uint16_t command_id, const void *payload, uint16_t payload_length)
{
    uint8_t  frame[REFEREE_MAIN_FRAME_CAPACITY];
    uint16_t frame_length;
    int      result;

    if (referee_uart == 0)
    {
        referee_tx_error_count++;
        return -1;
    }
    if (tx_mutex_get(&referee_tx_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
    {
        referee_tx_error_count++;
        return -1;
    }

    frame_length = referee_packet_build(frame,
                                        sizeof(frame),
                                        command_id,
                                        payload,
                                        payload_length,
                                        &referee_sequence);
    if (frame_length == 0)
    {
        referee_tx_error_count++;
        tx_mutex_put(&referee_tx_mutex);
        return -1;
    }

    result = BSP_UART_Send(referee_uart, frame, frame_length, 50);
    tx_mutex_put(&referee_tx_mutex);
    if (result == frame_length)
    {
        referee_tx_count++;
        referee_last_command_id = command_id;
        return 0;
    }
    referee_tx_error_count++;
    return -1;
}

static void referee_send_robot_status(void)
{
    referee_state_snapshot_t snapshot;
    robot_status_t            payload = {0};

    referee_state_get_snapshot(&snapshot);
    payload.robot_id = snapshot.team == REFEREE_MAIN_TEAM_BLUE
                           ? REFEREE_MAIN_ROBOT_ID_BLUE
                           : REFEREE_MAIN_ROBOT_ID_RED;
    payload.robot_level = REFEREE_MAIN_ROBOT_LEVEL;
    payload.current_hp = snapshot.current_hp;
    payload.maximum_hp = snapshot.maximum_hp;
    payload.chassis_power_limit = REFEREE_MAIN_CHASSIS_POWER_LIMIT;
    payload.power_output.gimbal_output = 1;
    payload.power_output.chassis_output = 1;
    payload.power_output.shooter_output = 1;

    if (referee_send_frame(CMD_ID_ROBOT_STATUS, &payload, sizeof(payload)) != 0)
    {
        LOG_W("send robot status failed");
    }
}

static void referee_send_power_heat(void)
{
    power_heat_data_t payload = {0};

    if (referee_send_frame(CMD_ID_POWER_HEAT_DATA, &payload, sizeof(payload)) != 0)
    {
        LOG_W("send power heat failed");
    }
}

static void referee_send_allowed_bullet(void)
{
    allowed_bullet_t payload = {0};

    if (referee_send_frame(CMD_ID_ALLOWED_BULLET, &payload, sizeof(payload)) != 0)
    {
        LOG_W("send allowed bullet failed");
    }
}

static void referee_send_robot_hp(void)
{
    referee_state_snapshot_t snapshot;
    robot_hp_data_t          payload = {0};

    referee_state_get_snapshot(&snapshot);
    payload.ally_infantry_3_hp = snapshot.current_hp;

    if (referee_send_frame(CMD_ID_ROBOT_HP, &payload, sizeof(payload)) != 0)
    {
        LOG_W("send robot hp failed");
    }
}

static void referee_send_game_status(void)
{
    game_status_t payload = {0};

    payload.type_progress.game_type = 1;
    payload.type_progress.game_progress = 0;

    if (referee_send_frame(CMD_ID_GAME_STATUS, &payload, sizeof(payload)) != 0)
    {
        LOG_W("send game status failed");
    }
}

static void referee_send_hurt_status(uint8_t armor_id)
{
    hurt_status_t payload = {0};

    payload.armor_id = armor_id & 0x0FU;
    payload.hurt_type = 0;

    if (referee_send_frame(CMD_ID_HURT_STATUS, &payload.byte, sizeof(payload.byte)) != 0)
    {
        LOG_W("send hurt status failed");
    }
}

static void referee_send_armor_config(void)
{
    referee_state_snapshot_t snapshot;

    referee_state_get_snapshot(&snapshot);

    for (uint8_t port_id = 0; port_id < REFEREE_MAIN_ARMOR_COUNT; port_id++)
    {
        uint8_t frame[REFEREE_MAIN_ARMOR_CONFIG_SIZE] = {0};
        uint8_t mode = REFEREE_MAIN_ARMOR_MODE_ENABLE;
        uint8_t reset_epoch;
        uint8_t reset_pending;
        uint8_t sum = 0U;

        if (armor_adc_debug_active != 0U)
        {
            mode |= REFEREE_MAIN_ARMOR_MODE_ADC_DEBUG;
            reset_pending = 0U;
            reset_epoch = 0U;
        }
        else
        {
            tx_mutex_get(&armor_counter_mutex, TX_WAIT_FOREVER);
            reset_pending = armor_counter_get_reset_request(&armor_counters,
                                                            port_id,
                                                            &reset_epoch);
            if (reset_pending != 0U)
            {
                mode |= REFEREE_MAIN_ARMOR_MODE_RESET_COUNTERS;
            }
            tx_mutex_put(&armor_counter_mutex);
        }

        frame[0] = REFEREE_SOF;
        frame[1] = REFEREE_MAIN_ARMOR_CONFIG_CMD;
        frame[2] = snapshot.team;
        frame[3] = port_id;
        frame[4] = mode;
        frame[5] = reset_epoch;
        frame[6] = armor_config_sequence++;
        if (reset_pending != 0U)
        {
            tx_mutex_get(&armor_counter_mutex, TX_WAIT_FOREVER);
            armor_counter_mark_reset_sent(&armor_counters, port_id, frame[6]);
            tx_mutex_put(&armor_counter_mutex);
        }
        for (uint8_t index = 0; index < REFEREE_MAIN_ARMOR_CONFIG_SIZE - 1U; index++)
        {
            sum = (uint8_t)(sum + frame[index]);
        }
        frame[7] = sum;

        if (armor_uart[port_id] == 0 ||
            BSP_UART_Send(armor_uart[port_id], frame, sizeof(frame), 50U) !=
                (int)sizeof(frame))
        {
            armor_config_tx_error_count++;
            LOG_W("send armor config failed: port=%u", (unsigned int)port_id);
        }
        else
        {
            armor_config_tx_count++;
        }
    }
}

static void armor_packet_received(uint8_t port_id, const armor_link_packet_t *packet)
{
    armor_counter_delta_t delta;
    armor_counter_result_t result;

    if (packet == 0 || port_id >= REFEREE_MAIN_ARMOR_COUNT)
    {
        return;
    }

    if (packet->frame_type == REFEREE_MAIN_ARMOR_DEBUG_CMD)
    {
        /* ADC collection frames are diagnostics only. They must never enter
         * the cumulative counter or HP path. Port identity is authoritative. */
        referee_state_mark_armor_seen(port_id);
        return;
    }

    tx_mutex_get(&armor_counter_mutex, TX_WAIT_FOREVER);
    result = armor_counter_update(&armor_counters,
                                  port_id,
                                  packet->small_hit_count,
                                  packet->big_hit_count,
                                  packet->reset_epoch,
                                  packet->reset_ack != 0U,
                                  packet->reset_sequence,
                                  &delta);
    if (result == ARMOR_COUNTER_HIT)
    {
        referee_state_apply_hits(port_id,
                                 delta.small_hit_delta,
                                 delta.big_hit_delta);
    }
    else
    {
        referee_state_mark_armor_seen(port_id);
    }
    tx_mutex_put(&armor_counter_mutex);

    if (result == ARMOR_COUNTER_HIT)
    {
        referee_state_snapshot_t snapshot;
        referee_send_hurt_status(port_id);
        referee_state_get_snapshot(&snapshot);
        LOG_I("armor hit: port=%u small=%u big=%u hp=%u",
              (unsigned int)port_id,
              (unsigned int)delta.small_hit_delta,
              (unsigned int)delta.big_hit_delta,
              (unsigned int)snapshot.current_hp);
    }
}

static void armor_rx_thread_entry(ULONG thread_input)
{
    uint8_t  data[32];
    uint32_t received_length;

    (void)thread_input;
    while (1)
    {
        uint8_t received = 0;

        for (uint8_t port_id = 0; port_id < REFEREE_MAIN_ARMOR_COUNT; port_id++)
        {
            received_length = 0;
            if (BSP_UART_Read(armor_uart[port_id],
                              data,
                              sizeof(data),
                              &received_length,
                              TX_NO_WAIT) > 0)
            {
                received = 1;
                armor_link_process(port_id, data, received_length);
            }
        }

        if (!received)
        {
            tx_thread_sleep(1);
        }
    }
}

static uint8_t referee_time_reached(ULONG now, ULONG deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static void referee_led_write(GPIO_TypeDef *port, uint16_t pin, uint8_t on)
{
    HAL_GPIO_WritePin(port,
                      pin,
                      (GPIO_PinState)(on != 0U
                                          ? REFEREE_MAIN_LED_ACTIVE_LEVEL
                                          : REFEREE_MAIN_LED_INACTIVE_LEVEL));
}

static void referee_led_apply_team(uint8_t team)
{
#if REFEREE_MAIN_LED_ENABLE
    referee_led_write(LED_R_GPIO_Port, LED_R_Pin, team == REFEREE_MAIN_TEAM_RED);
    referee_led_write(LED_B_GPIO_Port, LED_B_Pin, team == REFEREE_MAIN_TEAM_BLUE);
    referee_led_write(LED_G_GPIO_Port, LED_G_Pin, 0U);
#else
    (void)team;
#endif
}

static void referee_control_refresh_led(void)
{
    referee_state_snapshot_t snapshot;

    referee_state_get_snapshot(&snapshot);
    referee_led_apply_team(snapshot.team);
}

void referee_control_toggle_team(void)
{
    referee_state_toggle_team();
    referee_control_refresh_led();
}

void referee_control_reset_system(void)
{
    armor_adc_debug_active = 0U;
    armor_link_set_debug_mode(0U);
    tx_mutex_get(&armor_counter_mutex, TX_WAIT_FOREVER);
    armor_counter_request_reset(&armor_counters);
    referee_state_reset();
    tx_mutex_put(&armor_counter_mutex);
    referee_control_refresh_led();
}

void referee_control_start_adc_debug(void)
{
    armor_adc_debug_active = 1U;
    armor_link_set_debug_mode(1U);
}

void referee_control_stop_adc_debug(void)
{
    armor_adc_debug_active = 0U;
    armor_link_set_debug_mode(0U);
}

uint8_t referee_control_is_adc_debug_active(void)
{
    return armor_adc_debug_active;
}

static void referee_watchdog_thread_entry(ULONG thread_input)
{
    (void)thread_input;

    while (1)
    {
#if REFEREE_MAIN_WATCHDOG_ENABLE
        HAL_IWDG_Refresh(&hiwdg);
#endif
        tx_thread_sleep(REFEREE_MAIN_WATCHDOG_PERIOD_MS);
    }
}

static void referee_tx_thread_entry(ULONG thread_input)
{
    ULONG next_status;
    ULONG next_hp;
    ULONG next_game;
    ULONG next_armor_config;

    (void)thread_input;
    next_status = tx_time_get();
    next_hp = next_status;
    next_game = next_status;
    next_armor_config = next_status;

    while (1)
    {
        ULONG now = tx_time_get();

        if (referee_time_reached(now, next_armor_config))
        {
            referee_send_armor_config();
            next_armor_config += REFEREE_MAIN_ARMOR_CONFIG_PERIOD_MS;
        }
        if (referee_time_reached(now, next_status))
        {
            referee_send_robot_status();
            referee_send_power_heat();
            referee_send_allowed_bullet();
            next_status += REFEREE_MAIN_TX_PERIOD_MS;
        }
        if (referee_time_reached(now, next_hp))
        {
            referee_send_robot_hp();
            next_hp += REFEREE_MAIN_HP_PERIOD_MS;
        }
        if (referee_time_reached(now, next_game))
        {
            referee_send_game_status();
            next_game += REFEREE_MAIN_GAME_PERIOD_MS;
        }

        tx_thread_sleep(REFEREE_MAIN_TX_THREAD_PERIOD_MS);
    }
}

static int referee_uart_init(void)
{
    UART_Device_init_config referee_config = {
        .huart = &huart1,
        .rx_buf = 0,
        .rx_buf_size = 0,
        .expected_rx_len = 0,
        .rx_mode = UART_MODE_BLOCKING,
        .tx_mode = UART_MODE_BLOCKING,
    };
    UART_HandleTypeDef *armor_handles[REFEREE_MAIN_ARMOR_COUNT] = {
        &huart2,
        &huart3,
        &huart4,
        &huart5,
    };

    referee_uart = BSP_UART_Device_Init(&referee_config);
    if (referee_uart == 0)
    {
        return -1;
    }

    for (uint8_t port_id = 0; port_id < REFEREE_MAIN_ARMOR_COUNT; port_id++)
    {
        UART_Device_init_config armor_config = {
            .huart = armor_handles[port_id],
            .rx_buf = armor_rx_buffer[port_id],
            .rx_buf_size = REFEREE_MAIN_ARMOR_RX_BUFFER_SIZE,
            .expected_rx_len = 0,
            .rx_mode = UART_MODE_IT,
            .tx_mode = UART_MODE_BLOCKING,
        };

        armor_uart[port_id] = BSP_UART_Device_Init(&armor_config);
        if (armor_uart[port_id] == 0)
        {
            return -1;
        }
    }

    return 0;
}

void robot_control_init(void)
{
    UINT status;

    __HAL_DBGMCU_FREEZE_IWDG();
    HAL_IWDG_Refresh(&hiwdg);

    if (referee_state_init() != 0)
    {
        LOG_E("referee state init failed");
        return;
    }

    referee_control_refresh_led();

    status = tx_mutex_create(&referee_tx_mutex, "referee_tx", TX_INHERIT);
    if (status != TX_SUCCESS)
    {
        LOG_E("referee tx mutex create failed: %u", status);
        return;
    }

    status = tx_mutex_create(&armor_counter_mutex, "armor_counter", TX_INHERIT);
    if (status != TX_SUCCESS)
    {
        LOG_E("armor counter mutex create failed: %u", status);
        return;
    }

    armor_counter_init(&armor_counters);
    armor_adc_debug_active = 0U;

    armor_link_init(armor_packet_received);
    if (referee_uart_init() != 0)
    {
        LOG_E("referee uart init failed");
        return;
    }
    HAL_IWDG_Refresh(&hiwdg);

    status = tx_thread_create(&armor_rx_thread,
                              "armor_rx_thread",
                              armor_rx_thread_entry,
                              0,
                              armor_rx_thread_stack,
                              sizeof(armor_rx_thread_stack),
                              12,
                              12,
                              TX_NO_TIME_SLICE,
                              TX_AUTO_START);
    if (status != TX_SUCCESS)
    {
        LOG_E("armor rx thread create failed: %u", status);
        return;
    }

    status = tx_thread_create(&referee_tx_thread,
                              "referee_tx_thread",
                              referee_tx_thread_entry,
                              0,
                              referee_tx_thread_stack,
                              sizeof(referee_tx_thread_stack),
                              15,
                              15,
                              TX_NO_TIME_SLICE,
                              TX_AUTO_START);
    if (status != TX_SUCCESS)
    {
        LOG_E("referee tx thread create failed: %u", status);
        return;
    }

    status = referee_ui_init();
    if (status != TX_SUCCESS)
    {
        LOG_E("referee ui init failed: %u", status);
        return;
    }

    status = tx_thread_create(&referee_watchdog_thread,
                              "referee_watchdog",
                              referee_watchdog_thread_entry,
                              0,
                              referee_watchdog_thread_stack,
                              sizeof(referee_watchdog_thread_stack),
                              5,
                              5,
                              TX_NO_TIME_SLICE,
                              TX_AUTO_START);
    if (status != TX_SUCCESS)
    {
        LOG_E("referee watchdog thread create failed: %u", status);
        return;
    }

    LOG_I("referee controller started");
}

void referee_control_get_diagnostics(referee_control_diagnostics_t *diagnostics)
{
    if (diagnostics == 0)
    {
        return;
    }

    diagnostics->referee_tx_count = referee_tx_count;
    diagnostics->referee_tx_error_count = referee_tx_error_count;
    diagnostics->armor_config_tx_count = armor_config_tx_count;
    diagnostics->armor_config_tx_error_count = armor_config_tx_error_count;
    tx_mutex_get(&armor_counter_mutex, TX_WAIT_FOREVER);
    diagnostics->armor_counter_resync_count =
        armor_counter_get_resync_count(&armor_counters);
    tx_mutex_put(&armor_counter_mutex);
    diagnostics->last_command_id = referee_last_command_id;
}
