#include "armor_app.h"
#include "armor_config.h"
#include "armor_detector.h"
#include "armor_led.h"
#include "armor_protocol.h"
#include "armor_sensor.h"
#include "main.h"

static ArmorDetectorState_t armor_detector;
static uint32_t armor_last_heartbeat_tick;
static uint32_t armor_last_repeat_tick;
static uint32_t armor_led_hit_until;
static uint8_t armor_repeat_remaining;
static uint8_t armor_repeat_event;
static bool armor_led_hit_active;

void ArmorApp_Init(void)
{
  uint32_t now = HAL_GetTick();

  ArmorSensor_Init();
  ArmorDetector_Init(&armor_detector, now);
  ArmorProtocol_Init();
  ArmorLed_SetHit(false);
  armor_last_heartbeat_tick = now;
  armor_last_repeat_tick = now;
  armor_led_hit_until = now;
  armor_repeat_remaining = 0U;
  armor_repeat_event = 0U;
  armor_led_hit_active = false;
}

void ArmorApp_RunOnce(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t adc_raw;
  uint8_t dx_level;
  uint8_t hit_event;

  adc_raw = ArmorSensor_ReadAdcRaw();
  dx_level = ArmorSensor_ReadDxLevel();
  hit_event = ArmorDetector_Update(&armor_detector, now, adc_raw, dx_level);

  if (armor_detector.baseline_ready)
  {
    if (armor_repeat_remaining != 0U &&
        (uint32_t)(now - armor_last_repeat_tick) >= ARMOR_HIT_REPEAT_GAP_MS)
    {
      ArmorProtocol_Send(armor_repeat_event, adc_raw, dx_level);
      armor_last_repeat_tick = now;
      armor_repeat_remaining--;
    }

    if (hit_event != 0U)
    {
      armor_repeat_event = hit_event;
      ArmorProtocol_Send(hit_event, adc_raw, dx_level);
      armor_repeat_remaining = ARMOR_HIT_REPEAT_COUNT - 1U;
      armor_last_repeat_tick = now;
      ArmorLed_SetHit(true);
      armor_led_hit_until = now + ARMOR_HIT_LED_HOLD_MS;
      armor_led_hit_active = true;
    }
  }

  if (armor_led_hit_active &&
      (int32_t)(now - armor_led_hit_until) >= 0)
  {
    ArmorLed_SetHit(false);
    armor_led_hit_active = false;
  }

  if ((uint32_t)(now - armor_last_heartbeat_tick) >= ARMOR_HEARTBEAT_PERIOD_MS)
  {
    ArmorProtocol_Send(0U, adc_raw, dx_level);
    armor_last_heartbeat_tick = now;
  }

}
