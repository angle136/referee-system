#ifndef ARMOR_PROTOCOL_H
#define ARMOR_PROTOCOL_H

#include <stdint.h>

void ArmorProtocol_Send(uint8_t event, uint32_t adc_raw, uint8_t dx_level);
void ArmorProtocol_Init(void);

#endif
