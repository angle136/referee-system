#include "armor_protocol.h"
#include "armor_config.h"
#include "kfifo.h"
#include "main.h"
#include "usart.h"

static uint8_t armor_protocol_armor_id;
static Kfifo_t armor_tx_fifo;
static uint8_t armor_tx_storage[ARMOR_TX_QUEUE_CAPACITY][ARMOR_PACKET_SIZE];
static uint8_t armor_tx_active_packet[ARMOR_PACKET_SIZE];
static volatile bool armor_tx_active;

static void ArmorProtocol_StartNext(void)
{
  if (armor_tx_active || !Kfifo_Pop(&armor_tx_fifo, armor_tx_active_packet))
  {
    return;
  }

  armor_tx_active = true;
  if (HAL_UART_Transmit_IT(&huart2,
                           armor_tx_active_packet,
                           ARMOR_PACKET_SIZE) != HAL_OK)
  {
    armor_tx_active = false;
  }
}

void ArmorProtocol_Init(void)
{
  Kfifo_Init(&armor_tx_fifo,
             armor_tx_storage,
             ARMOR_PACKET_SIZE,
             ARMOR_TX_QUEUE_CAPACITY);
  armor_protocol_armor_id = ARMOR_ID;
  armor_tx_active = false;
}

void ArmorProtocol_SetArmorId(uint8_t armor_id)
{
  armor_protocol_armor_id = armor_id;
}

void ArmorProtocol_SendStatus(uint16_t small_hit_count,
                              uint16_t big_hit_count,
                              uint8_t reset_epoch,
                              bool reset_ack,
                              uint8_t reset_sequence)
{
  uint8_t packet[ARMOR_PACKET_SIZE];
  uint8_t sum = 0U;
  uint32_t primask;

  packet[0] = 0xA5U;
  packet[1] = (uint8_t)(armor_protocol_armor_id & ARMOR_STATUS_ARMOR_ID_MASK);
  if (reset_ack)
  {
    packet[1] = (uint8_t)(reset_sequence & ARMOR_STATUS_ARMOR_ID_MASK);
    packet[1] |= ARMOR_STATUS_RESET_ACK;
  }
  packet[2] = (uint8_t)(small_hit_count & 0xFFU);
  packet[3] = (uint8_t)(small_hit_count >> 8U);
  packet[4] = (uint8_t)(big_hit_count & 0xFFU);
  packet[5] = (uint8_t)(big_hit_count >> 8U);
  packet[6] = reset_epoch;
  for (uint32_t i = 0U; i < ARMOR_PACKET_SIZE - 1U; i++)
  {
    sum = (uint8_t)(sum + packet[i]);
  }
  packet[7] = sum;
  primask = __get_PRIMASK();
  __disable_irq();
  (void)Kfifo_Push(&armor_tx_fifo, packet);
  ArmorProtocol_StartNext();
  if (primask == 0U)
  {
    __enable_irq();
  }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart == &huart2)
  {
    armor_tx_active = false;
    ArmorProtocol_StartNext();
  }
}
