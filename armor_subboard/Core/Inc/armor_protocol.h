#ifndef ARMOR_PROTOCOL_H
#define ARMOR_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

void ArmorProtocol_SendStatus(uint16_t small_hit_count,
                              uint16_t big_hit_count,
                              uint8_t reset_epoch,
                              bool reset_ack,
                              uint8_t reset_sequence);
void ArmorProtocol_Init(void);
void ArmorProtocol_SetArmorId(uint8_t armor_id);

#endif
