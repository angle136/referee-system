#include "armor_app.h"
#include "armor_config.h"
#include "armor_debug.h"
#include "armor_detector.h"
#include "armor_led.h"
#include "armor_protocol.h"
#include "armor_sensor.h"
#include "main.h"

static ArmorDetectorState_t armor_detector;
static uint32_t armor_last_heartbeat_tick;
static uint32_t armor_last_repeat_tick;
static uint8_t armor_repeat_remaining;
static uint8_t armor_repeat_event;

void ArmorApp_Init(void)
{
  uint32_t now = HAL_GetTick();

  ArmorSensor_Init();
  ArmorDetector_Init(&armor_detector, now);
  ArmorDebug_Init();
  armor_last_heartbeat_tick = now;
  armor_last_repeat_tick = now;
  armor_repeat_remaining = 0U;
  armor_repeat_event = 0U;
}

void ArmorApp_RunOnce(void)
{
  uint32_t now = HAL_GetTick();
  uint8_t dx_level;
  uint8_t hit_event;

  armor_run_count++;
  armor_adc_raw = ArmorSensor_ReadAdcRaw();
  dx_level = ArmorSensor_ReadDxLevel();
  armor_dx_level = dx_level;
  hit_event = ArmorDetector_Update(&armor_detector, now, armor_adc_raw, dx_level);
  armor_baseline = armor_detector.baseline;

  if (armor_detector.baseline_ready)
  {
    if (armor_repeat_remaining != 0U &&
        (uint32_t)(now - armor_last_repeat_tick) >= ARMOR_HIT_REPEAT_GAP_MS)
    {
      ArmorProtocol_Send(armor_repeat_event, armor_adc_raw, dx_level);
      armor_last_repeat_tick = now;
      armor_repeat_remaining--;
    }

    if (hit_event != 0U)
    {
      armor_last_event = hit_event;
      armor_repeat_event = hit_event;
      ArmorProtocol_Send(hit_event, armor_adc_raw, dx_level);
      armor_repeat_remaining = ARMOR_HIT_REPEAT_COUNT - 1U;
      armor_last_repeat_tick = now;
      ArmorLed_SetHit(true);
    }
  }

  if ((uint32_t)(now - armor_last_heartbeat_tick) >= ARMOR_HEARTBEAT_PERIOD_MS)
  {
    ArmorProtocol_Send(0U, armor_adc_raw, dx_level);
    armor_last_heartbeat_tick = now;
    ArmorLed_SetHit(false);
  }

  ArmorDebug_Record(now, hit_event);
}
