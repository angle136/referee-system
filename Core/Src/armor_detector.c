#include "armor_detector.h"
#include "armor_config.h"

void ArmorDetector_Init(ArmorDetectorState_t *state, uint32_t now)
{
  state->baseline = 0U;
  state->event = 0U;
  state->baseline_ready = false;
  state->baseline_sum = 0U;
  state->baseline_count = 0U;
  state->baseline_index = 0U;
  state->baseline_start = now;
  state->last_hit_tick = 0U;
  state->dx_was_active = false;
  state->ax_was_high = false;
}

uint8_t ArmorDetector_Update(ArmorDetectorState_t *state,
                             uint32_t now,
                             uint32_t adc_raw,
                             uint8_t dx_level)
{
  bool dx_active = (dx_level == ARMOR_DX_ACTIVE_HIGH);
  uint8_t hit_event = 0U;

  if (!state->baseline_ready)
  {
    if ((uint32_t)(now - state->baseline_start) >= ARMOR_BASELINE_TIME_MS)
    {
      state->baseline_ready = true;
    }
    else
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
      state->ax_was_high = false;
      return 0U;
    }
  }

  bool ax_high = (adc_raw > (state->baseline + ARMOR_AX_THRESHOLD));
  if ((uint32_t)(now - state->last_hit_tick) >= ARMOR_HIT_COOLDOWN_MS &&
      ((dx_active && !state->dx_was_active) || (ax_high && !state->ax_was_high)))
  {
    hit_event = (dx_active ? 1U : 0U) | (ax_high ? 2U : 0U);
    state->last_hit_tick = now;
    state->event = hit_event;
  }
  state->dx_was_active = dx_active;
  state->ax_was_high = ax_high;
  return hit_event;
}
