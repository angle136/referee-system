#ifndef ARMOR_LINK_H
#define ARMOR_LINK_H

#include <stdbool.h>
#include <stdint.h>

void ArmorLink_Init(void);
void ArmorLink_Process(uint32_t now);
bool ArmorLink_IsOnline(void);
bool ArmorLink_IsTransitioning(void);
bool ArmorLink_TransitionRedOn(void);
uint8_t ArmorLink_GetTeam(void);

#endif
