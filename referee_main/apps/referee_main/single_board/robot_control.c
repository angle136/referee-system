#include "robot_control.h"

#include <stdint.h>

#include "armor_link.h"
#include "bsp_def.h"
#include "bsp_uart.h"
#include "REFEREE/referee_protocol.h"
#include "referee_config.h"
#include "referee_packet.h"
#include "referee_state.h"
#include "tx_api.h"
#include "usart.h"

#define LOG_TAG "referee_controller"
#define LOG_LVL LOG_LVL_INFO
#include "ulog_def.h"

#define REFEREE_MAIN_THREAD_STACK_SIZE 1024U

enum
{
    ARMOR_EVENT_WORD_ARMOR_ID = 0,
    ARMOR_EVENT_WORD_EVENT,
    ARMOR_EVENT_WORD_DX,
    ARMOR_EVENT_WORD_AX_RAW
};

static TX_THREAD referee_tx_thread;
static TX_THREAD armor_rx_thread;
static TX_THREAD hit_event_thread;

APPS_STACK_SECTION static uint8_t referee_tx_thread_stack[REFEREE_MAIN_THREAD_STACK_SIZE];
APPS_STACK_SECTION static uint8_t armor_rx_thread_stack[REFEREE_MAIN_THREAD_STACK_SIZE];
APPS_STACK_SECTION static uint8_t hit_event_thread_stack[REFEREE_MAIN_THREAD_STACK_SIZE];

static TX_QUEUE armor_event_queue;
static ULONG    armor_event_queue_storage[REFEREE_MAIN_ARMOR_EVENT_QUEUE_LENGTH *
                                          REFEREE_MAIN_ARMOR_EVENT_WORDS];
static TX_MUTEX referee_tx_mutex;

static UART_Device *referee_uart;
static UART_Device *armor_uart[REFEREE_MAIN_ARMOR_COUNT];
static uint8_t      armor_rx_buffer[REFEREE_MAIN_ARMOR_COUNT][REFEREE_MAIN_ARMOR_RX_BUFFER_SIZE]
    BUFFER_SECTION;
static uint8_t referee_sequence;

static int referee_send_frame(uint16_t command_id, const void *payload, uint16_t payload_length)
{
    uint8_t  frame[REFEREE_MAIN_FRAME_CAPACITY];
    uint16_t frame_length;
    int      result;

    if (referee_uart == 0)
    {
        return -1;
    }
    if (tx_mutex_get(&referee_tx_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
    {
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
        tx_mutex_put(&referee_tx_mutex);
        return -1;
    }

    result = BSP_UART_Send(referee_uart, frame, frame_length, 50);
    tx_mutex_put(&referee_tx_mutex);
    return result == frame_length ? 0 : -1;
}

static void referee_send_robot_status(void)
{
    referee_state_snapshot_t snapshot;
    robot_status_t            payload = {0};

    referee_state_get_snapshot(&snapshot);
    payload.robot_id = REFEREE_MAIN_ROBOT_ID;
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

static void armor_packet_received(uint8_t port_id, const armor_link_packet_t *packet)
{
    ULONG event[REFEREE_MAIN_ARMOR_EVENT_WORDS] = {0};

    if (packet == 0 || port_id >= REFEREE_MAIN_ARMOR_COUNT)
    {
        return;
    }

    if (packet->event != REFEREE_MAIN_ARMOR_EVENT_HEARTBEAT &&
        (packet->event < REFEREE_MAIN_ARMOR_EVENT_HIT_DX ||
        packet->event > REFEREE_MAIN_ARMOR_EVENT_HIT_BOTH)
    )
    {
        return;
    }

    referee_state_mark_armor_seen(port_id, packet->ax_raw, packet->dx);
    if (packet->event == REFEREE_MAIN_ARMOR_EVENT_HEARTBEAT)
    {
        return;
    }

    event[ARMOR_EVENT_WORD_ARMOR_ID] = port_id;
    event[ARMOR_EVENT_WORD_EVENT] = packet->event;
    event[ARMOR_EVENT_WORD_DX] = packet->dx;
    event[ARMOR_EVENT_WORD_AX_RAW] = packet->ax_raw;
    if (tx_queue_send(&armor_event_queue, event, TX_NO_WAIT) != TX_SUCCESS)
    {
        LOG_W("armor event queue full");
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

static void hit_event_thread_entry(ULONG thread_input)
{
    ULONG message[REFEREE_MAIN_ARMOR_EVENT_WORDS];

    (void)thread_input;
    while (1)
    {
        referee_state_snapshot_t snapshot;

        if (tx_queue_receive(&armor_event_queue, message, TX_WAIT_FOREVER) != TX_SUCCESS)
        {
            continue;
        }

        referee_state_apply_hit((uint8_t)message[ARMOR_EVENT_WORD_ARMOR_ID],
                                (uint16_t)message[ARMOR_EVENT_WORD_AX_RAW],
                                (uint8_t)message[ARMOR_EVENT_WORD_DX]);
        referee_send_hurt_status((uint8_t)message[ARMOR_EVENT_WORD_ARMOR_ID]);
        referee_state_get_snapshot(&snapshot);
        LOG_I("armor hit: id=%lu event=%lu ax=%lu dx=%lu hp=%u",
              (unsigned long)message[ARMOR_EVENT_WORD_ARMOR_ID],
              (unsigned long)message[ARMOR_EVENT_WORD_EVENT],
              (unsigned long)message[ARMOR_EVENT_WORD_AX_RAW],
              (unsigned long)message[ARMOR_EVENT_WORD_DX],
              (unsigned int)snapshot.current_hp);
    }
}

static uint8_t referee_time_reached(ULONG now, ULONG deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static void referee_tx_thread_entry(ULONG thread_input)
{
    ULONG next_status;
    ULONG next_hp;
    ULONG next_game;

    (void)thread_input;
    next_status = tx_time_get();
    next_hp = next_status;
    next_game = next_status;

    while (1)
    {
        ULONG now = tx_time_get();

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

        tx_thread_sleep(10);
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
            .expected_rx_len = REFEREE_MAIN_ARMOR_PACKET_SIZE,
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

    if (referee_state_init() != 0)
    {
        LOG_E("referee state init failed");
        return;
    }

    status = tx_mutex_create(&referee_tx_mutex, "referee_tx", TX_INHERIT);
    if (status != TX_SUCCESS)
    {
        LOG_E("referee tx mutex create failed: %u", status);
        return;
    }

    status = tx_queue_create(&armor_event_queue,
                             "armor_event_queue",
                             REFEREE_MAIN_ARMOR_EVENT_WORDS,
                             armor_event_queue_storage,
                             sizeof(armor_event_queue_storage));
    if (status != TX_SUCCESS)
    {
        LOG_E("armor event queue create failed: %u", status);
        return;
    }

    armor_link_init(armor_packet_received);
    if (referee_uart_init() != 0)
    {
        LOG_E("referee uart init failed");
        return;
    }

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

    status = tx_thread_create(&hit_event_thread,
                              "hit_event_thread",
                              hit_event_thread_entry,
                              0,
                              hit_event_thread_stack,
                              sizeof(hit_event_thread_stack),
                              10,
                              10,
                              TX_NO_TIME_SLICE,
                              TX_AUTO_START);
    if (status != TX_SUCCESS)
    {
        LOG_E("hit event thread create failed: %u", status);
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

    LOG_I("referee controller started");
}
