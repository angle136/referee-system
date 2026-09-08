#include "armor_protocol.h"
#include "armor_config.h"
#include "kfifo.h"
#include "main.h"
#include "usart.h"

static uint8_t armor_protocol_seq;
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
  armor_protocol_seq = 0U;
  armor_tx_active = false;
}

void ArmorProtocol_Send(uint8_t event, uint32_t adc_raw, uint8_t dx_level)
{
  uint8_t packet[ARMOR_PACKET_SIZE];
  uint8_t sum = 0U;

  packet[0] = 0xA5U;
  packet[1] = ARMOR_ID;
  packet[2] = event;
  packet[3] = dx_level;
  packet[4] = (uint8_t)(adc_raw & 0xFFU);
  packet[5] = (uint8_t)((adc_raw >> 8) & 0xFFU);
  packet[6] = armor_protocol_seq++;
  for (uint32_t i = 0U; i < ARMOR_PACKET_SIZE - 1U; i++)
  {
    sum = (uint8_t)(sum + packet[i]);
  }
  packet[7] = sum;
  __disable_irq();
  (void)Kfifo_Push(&armor_tx_fifo, packet);
  ArmorProtocol_StartNext();
  __enable_irq();
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart == &huart2)
  {
    armor_tx_active = false;
    ArmorProtocol_StartNext();
  }
}
