#include "armor_link.h"
#include "armor_config.h"
#include "armor_protocol.h"
#include "kfifo.h"
#include "main.h"
#include "usart.h"

typedef struct
{
  uint8_t frame[ARMOR_LINK_FRAME_SIZE];
  uint8_t length;
} ArmorLinkParser_t;

static Kfifo_t armor_rx_fifo;
static uint8_t armor_rx_storage[ARMOR_RX_QUEUE_CAPACITY];
static uint8_t armor_rx_byte;
static ArmorLinkParser_t armor_parser;
static uint32_t armor_last_master_tick;
static uint32_t armor_key_tick;
static uint8_t armor_team;
static bool armor_online;
static bool armor_transitioning;
static uint8_t armor_transition_phase;
static uint32_t armor_transition_next_tick;
static bool armor_key_red_latched;
static bool armor_key_blue_latched;

static bool ArmorLink_FrameValid(const uint8_t *frame)
{
  uint8_t sum = 0U;

  if (frame[0] != 0xA5U ||
      frame[1] != ARMOR_LINK_CMD_CONFIG ||
      frame[2] > ARMOR_TEAM_BLUE ||
      frame[3] > ARMOR_MAX_ID ||
      frame[4] != 1U)
  {
    return false;
  }

  for (uint8_t i = 0U; i < ARMOR_LINK_FRAME_SIZE - 1U; i++)
  {
    sum = (uint8_t)(sum + frame[i]);
  }
  return sum == frame[ARMOR_LINK_FRAME_SIZE - 1U];
}

static void ArmorLink_HandleFrame(const uint8_t *frame, uint32_t now)
{
  armor_team = frame[2];
  ArmorProtocol_SetArmorId(frame[3]);
  armor_last_master_tick = now;
  armor_online = true;
  armor_transitioning = false;
  armor_transition_phase = 0U;
}

static void ArmorLink_ParseByte(uint8_t byte, uint32_t now)
{
  if (armor_parser.length == 0U)
  {
    if (byte == 0xA5U)
    {
      armor_parser.frame[0] = byte;
      armor_parser.length = 1U;
    }
    return;
  }

  armor_parser.frame[armor_parser.length++] = byte;
  if (armor_parser.length < ARMOR_LINK_FRAME_SIZE)
  {
    return;
  }

  if (ArmorLink_FrameValid(armor_parser.frame))
  {
    ArmorLink_HandleFrame(armor_parser.frame, now);
  }
  armor_parser.length = 0U;
  if (byte == 0xA5U)
  {
    armor_parser.frame[0] = byte;
    armor_parser.length = 1U;
  }
}

static void ArmorLink_ProcessKeys(uint32_t now)
{
  bool red_pressed;
  bool blue_pressed;

  if (armor_online || armor_transitioning ||
      (uint32_t)(now - armor_key_tick) < ARMOR_KEY_DEBOUNCE_MS)
  {
    return;
  }

  armor_key_tick = now;
  red_pressed = (HAL_GPIO_ReadPin(KEY_1_GPIO_Port, KEY_1_Pin) == GPIO_PIN_RESET);
  blue_pressed = (HAL_GPIO_ReadPin(KEY_2_GPIO_Port, KEY_2_Pin) == GPIO_PIN_RESET);

  if (red_pressed && !blue_pressed && !armor_key_red_latched)
  {
    armor_team = ARMOR_TEAM_RED;
    armor_key_red_latched = true;
    armor_key_blue_latched = false;
  }
  else if (blue_pressed && !red_pressed && !armor_key_blue_latched)
  {
    armor_team = ARMOR_TEAM_BLUE;
    armor_key_blue_latched = true;
    armor_key_red_latched = false;
  }

  if (!red_pressed)
  {
    armor_key_red_latched = false;
  }
  if (!blue_pressed)
  {
    armor_key_blue_latched = false;
  }
}

static void ArmorLink_StartTransition(uint32_t now)
{
  armor_transitioning = true;
  armor_transition_phase = 0U;
  armor_transition_next_tick = now + ARMOR_LINK_FLASH_HALF_PERIOD_MS;
}

void ArmorLink_Init(void)
{
  uint32_t now = HAL_GetTick();

  Kfifo_Init(&armor_rx_fifo,
             armor_rx_storage,
             sizeof(armor_rx_storage[0]),
             ARMOR_RX_QUEUE_CAPACITY);
  armor_parser.length = 0U;
  armor_last_master_tick = now;
  armor_key_tick = now;
  armor_team = ARMOR_TEAM_BLUE;
  armor_online = false;
  armor_transitioning = false;
  armor_transition_phase = 0U;
  armor_transition_next_tick = now;
  armor_key_red_latched = false;
  armor_key_blue_latched = false;
  (void)HAL_UART_Receive_IT(&huart2, &armor_rx_byte, 1U);
}

void ArmorLink_Process(uint32_t now)
{
  uint8_t byte;

  while (1)
  {
    __disable_irq();
    if (!Kfifo_Pop(&armor_rx_fifo, &byte))
    {
      __enable_irq();
      break;
    }
    __enable_irq();
    ArmorLink_ParseByte(byte, now);
  }

  if (armor_online && !armor_transitioning &&
      (uint32_t)(now - armor_last_master_tick) >= ARMOR_LINK_ONLINE_TIMEOUT_MS)
  {
    ArmorLink_StartTransition(now);
  }

  if (armor_transitioning &&
      (int32_t)(now - armor_transition_next_tick) >= 0)
  {
    armor_transition_phase++;
    armor_transition_next_tick = now + ARMOR_LINK_FLASH_HALF_PERIOD_MS;
    if (armor_transition_phase >= 6U)
    {
      armor_transitioning = false;
      armor_online = false;
      armor_transition_phase = 0U;
    }
  }

  ArmorLink_ProcessKeys(now);
}

bool ArmorLink_IsOnline(void)
{
  return armor_online;
}

bool ArmorLink_IsTransitioning(void)
{
  return armor_transitioning;
}

bool ArmorLink_TransitionRedOn(void)
{
  return (armor_transition_phase % 2U) == 0U;
}

uint8_t ArmorLink_GetTeam(void)
{
  return armor_team;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart == &huart2)
  {
    (void)Kfifo_Push(&armor_rx_fifo, &armor_rx_byte);
    (void)HAL_UART_Receive_IT(&huart2, &armor_rx_byte, 1U);
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart == &huart2)
  {
    (void)HAL_UART_Receive_IT(&huart2, &armor_rx_byte, 1U);
  }
}
