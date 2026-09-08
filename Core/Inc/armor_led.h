#ifndef ARMOR_LED_H
#define ARMOR_LED_H

#include <stdbool.h>
#include <stdint.h>

void ArmorLed_SetState(uint8_t team, bool hit);
void ArmorLed_SetTransition(bool red_on);

#endif
