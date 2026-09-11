#ifndef ARMOR_DETECTOR_H
#define ARMOR_DETECTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "armor_config.h"

typedef struct
{
  uint32_t baseline;
  uint8_t event;
  bool baseline_ready;
  uint16_t baseline_samples[ARMOR_BASELINE_SAMPLES];
  uint32_t baseline_sum;
  uint32_t baseline_count;
  uint32_t baseline_index;
  uint32_t baseline_start;
  uint32_t last_hit_tick;
  bool dx_was_active;
  bool ax_was_high;
} ArmorDetectorState_t;

void ArmorDetector_Init(ArmorDetectorState_t *state, uint32_t now);
uint8_t ArmorDetector_Update(ArmorDetectorState_t *state,
                             uint32_t now,
                             uint32_t adc_raw,
                             uint8_t dx_level);

#endif
