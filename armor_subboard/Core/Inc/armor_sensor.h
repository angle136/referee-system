#ifndef ARMOR_SENSOR_H
#define ARMOR_SENSOR_H

#include "main.h"

void ArmorSensor_Init(void);
uint32_t ArmorSensor_ReadAdcRaw(void);
uint8_t ArmorSensor_ReadDxLevel(void);

#endif
