#ifndef ARMOR_DEBUG_H
#define ARMOR_DEBUG_H

#include <stdint.h>

typedef struct
{
  uint16_t adc_raw;
  uint16_t baseline;
  uint16_t tick_ms;
  uint8_t dx_level;
  uint8_t hit_event;
} ArmorDebugSample_t;

extern volatile uint32_t armor_run_count;
extern volatile uint32_t armor_adc_raw;
extern volatile uint32_t armor_dx_level;
extern volatile uint32_t armor_baseline;
extern volatile uint32_t armor_last_event;
extern volatile uint32_t armor_adc_error_count;
extern volatile uint32_t armor_tx_error_count;
extern volatile ArmorDebugSample_t armor_debug_samples[];
extern volatile uint32_t armor_debug_write_index;
extern volatile uint32_t armor_debug_sample_count;
void ArmorDebug_Init(void);

void ArmorDebug_Record(uint32_t now, uint8_t hit_event);

#endif
