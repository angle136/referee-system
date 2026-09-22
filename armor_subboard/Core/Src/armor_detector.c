#include "armor_detector.h"
#include "armor_config.h"

void ArmorDetector_Init(ArmorDetectorState_t *state, uint32_t now)
{
  state->baseline = 0U;
  state->baseline_ready = false;
  state->baseline_sum = 0U;
  state->baseline_count = 0U;
  state->baseline_index = 0U;
  state->baseline_start = now;
  state->last_hit_tick = 0U;
  state->dx_was_active = false;
}

ArmorHitType_t ArmorDetector_Update(ArmorDetectorState_t *state,
                                   uint32_t now,
                                   uint32_t adc_raw,
                                   uint8_t dx_level)
{
  bool dx_active = (dx_level == ARMOR_DX_ACTIVE_HIGH);
  ArmorHitType_t hit_type = ARMOR_HIT_NONE;

  if (!state->baseline_ready)
  {
    if (state->baseline_count < ARMOR_BASELINE_SAMPLES)
    {
      state->baseline_samples[state->baseline_index] = (uint16_t)adc_raw;
      state->baseline_sum += adc_raw;
      state->baseline_count++;
    }
    else
    {
      state->baseline_sum -= state->baseline_samples[state->baseline_index];
      state->baseline_samples[state->baseline_index] = (uint16_t)adc_raw;
      state->baseline_sum += adc_raw;
    }
    state->baseline_index = (state->baseline_index + 1U) % ARMOR_BASELINE_SAMPLES;
    state->baseline = state->baseline_sum / state->baseline_count;
    state->dx_was_active = dx_active;
    if ((uint32_t)(now - state->baseline_start) < ARMOR_BASELINE_TIME_MS)
    {
      return ARMOR_HIT_NONE;
    }
    state->baseline_ready = true;
  }

  if ((uint32_t)(now - state->last_hit_tick) >= ARMOR_HIT_COOLDOWN_MS &&
      dx_active && !state->dx_was_active)
  {
    hit_type = adc_raw > (state->baseline + ARMOR_BIG_HIT_ADC_THRESHOLD)
                   ? ARMOR_HIT_BIG
                   : ARMOR_HIT_SMALL;
    state->last_hit_tick = now;
  }
  state->dx_was_active = dx_active;
  return hit_type;
}
