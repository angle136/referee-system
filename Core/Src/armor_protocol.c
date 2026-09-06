#include "armor_protocol.h"
#include "armor_config.h"
#include "usart.h"

static uint8_t armor_protocol_seq;

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
  (void)HAL_UART_Transmit(&huart2, packet, sizeof(packet), 10U);
}
