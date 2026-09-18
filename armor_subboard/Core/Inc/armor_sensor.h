#ifndef ARMOR_SENSOR_H
#define ARMOR_SENSOR_H

#include "main.h"

extern volatile uint32_t armor_adc_raw_debug;
extern volatile uint32_t armor_adc_voltage_mv_debug;

void ArmorSensor_Init(void);
uint32_t ArmorSensor_ReadAdcRaw(void);
uint8_t ArmorSensor_ReadDxLevel(void);

#endif