#include "armor_protocol.h"
#include "armor_config.h"
#include "kfifo.h"
#include "main.h"
#include "usart.h"

static uint8_t armor_protocol_armor_id;
typedef struct
{
  uint8_t data[ARMOR_ADC_DEBUG_FRAME_SIZE];
  uint8_t length;
} ArmorProtocolTxPacket_t;
static Kfifo_t armor_tx_fifo;
static ArmorProtocolTxPacket_t armor_tx_storage[ARMOR_TX_QUEUE_CAPACITY];
static ArmorProtocolTxPacket_t armor_tx_active_packet;
static volatile bool armor_tx_active;

static void ArmorProtocol_StartNext(void)
{
  if (armor_tx_active || !Kfifo_Pop(&armor_tx_fifo, &armor_tx_active_packet))
  {
    return;
  }

  armor_tx_active = true;
  if (HAL_UART_Transmit_IT(&huart2,
                           armor_tx_active_packet.data,
                           armor_tx_active_packet.length) != HAL_OK)
  {
    armor_tx_active = false;
  }
}

void ArmorProtocol_Init(void)
{
  Kfifo_Init(&armor_tx_fifo,
             armor_tx_storage,
             sizeof(armor_tx_storage[0]),
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
  ArmorProtocolTxPacket_t queued;
  uint8_t *packet = queued.data;
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
  queued.length = ARMOR_PACKET_SIZE;
  primask = __get_PRIMASK();
  __disable_irq();
  (void)Kfifo_Push(&armor_tx_fifo, &queued);
  ArmorProtocol_StartNext();
  if (primask == 0U)
  {
    __enable_irq();
  }
}

void ArmorProtocol_SendAdcDebug(const uint16_t *samples)
{
  ArmorProtocolTxPacket_t queued;
  uint8_t sum = 0U;

  if (samples == NULL)
  {
    return;
  }
  queued.data[0] = 0xA5U;
  queued.data[1] = ARMOR_ADC_DEBUG_CMD;
  for (uint8_t index = 0U; index < ARMOR_ADC_HISTORY_COUNT; index++)
  {
    queued.data[2U + index * 2U] = (uint8_t)(samples[index] & 0xFFU);
    queued.data[3U + index * 2U] = (uint8_t)(samples[index] >> 8U);
  }
  for (uint8_t index = 0U; index < ARMOR_ADC_DEBUG_FRAME_SIZE - 1U; index++)
  {
    sum = (uint8_t)(sum + queued.data[index]);
  }
  queued.data[ARMOR_ADC_DEBUG_FRAME_SIZE - 1U] = sum;
  queued.length = ARMOR_ADC_DEBUG_FRAME_SIZE;

  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  (void)Kfifo_Push(&armor_tx_fifo, &queued);
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
