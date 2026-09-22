#include "armor_app.h"
#include "armor_config.h"
#include "armor_detector.h"
#include "armor_link.h"
#include "armor_led.h"
#include "armor_protocol.h"
#include "armor_sensor.h"
#include "main.h"

static ArmorDetectorState_t armor_detector;
static uint32_t armor_last_heartbeat_tick;
static uint32_t armor_led_hit_started_tick;
static uint16_t armor_small_hit_count;
static uint16_t armor_big_hit_count;
static uint8_t armor_reset_epoch;
static uint8_t armor_reset_sequence;
static bool armor_led_hit_active;

void ArmorApp_Init(void)
{
  uint32_t now = HAL_GetTick();

  ArmorSensor_Init();
  ArmorDetector_Init(&armor_detector, now);
  ArmorProtocol_Init();
  ArmorLink_Init();
  ArmorLed_SetState(ArmorLink_GetTeam(), true);
  armor_last_heartbeat_tick = now;
  armor_led_hit_started_tick = now;
  armor_small_hit_count = 0U;
  armor_big_hit_count = 0U;
  armor_reset_epoch = 0U;
  armor_reset_sequence = 0U;
  armor_led_hit_active = false;
}

void ArmorApp_RunOnce(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t adc_raw;
  uint8_t dx_level;
  uint8_t reset_epoch;
  uint8_t reset_sequence;
  ArmorHitType_t hit_type;

  ArmorLink_Process(now);
  if (ArmorLink_TakeCounterReset(&reset_epoch, &reset_sequence))
  {
    armor_small_hit_count = 0U;
    armor_big_hit_count = 0U;
    armor_reset_epoch = reset_epoch;
    armor_reset_sequence = reset_sequence;
    ArmorProtocol_SendStatus(armor_small_hit_count,
                             armor_big_hit_count,
                             armor_reset_epoch,
                             ArmorLink_IsCounterResetAckActive(),
                             armor_reset_sequence);
    armor_last_heartbeat_tick = now;
  }
  adc_raw = ArmorSensor_ReadAdcRaw();
  dx_level = ArmorSensor_ReadDxLevel();
  hit_type = ArmorDetector_Update(&armor_detector, now, adc_raw, dx_level);

  if (armor_detector.baseline_ready && hit_type != ARMOR_HIT_NONE)
  {
    if (hit_type == ARMOR_HIT_BIG)
    {
      armor_big_hit_count++;
    }
    else
    {
      armor_small_hit_count++;
    }
    ArmorProtocol_SendStatus(armor_small_hit_count,
                             armor_big_hit_count,
                             armor_reset_epoch,
                             ArmorLink_IsCounterResetAckActive(),
                             armor_reset_sequence);
    armor_last_heartbeat_tick = now;
    armor_led_hit_started_tick = now;
    armor_led_hit_active = true;
  }

  if (armor_led_hit_active &&
      (uint32_t)(now - armor_led_hit_started_tick) >=
          (ARMOR_HIT_LED_PHASE_MS * 3U))
  {
    armor_led_hit_active = false;
  }

  if ((uint32_t)(now - armor_last_heartbeat_tick) >= ARMOR_HEARTBEAT_PERIOD_MS)
  {
    ArmorProtocol_SendStatus(armor_small_hit_count,
                             armor_big_hit_count,
                             armor_reset_epoch,
                             ArmorLink_IsCounterResetAckActive(),
                             armor_reset_sequence);
    armor_last_heartbeat_tick = now;
  }

  if (ArmorLink_IsTransitioning())
  {
    ArmorLed_SetTransition(ArmorLink_TransitionRedOn());
  }
  else
  {
    bool team_led_on = true;

    if (armor_led_hit_active)
    {
      uint32_t phase =
          (uint32_t)(now - armor_led_hit_started_tick) /
          ARMOR_HIT_LED_PHASE_MS;
      /* Hit indication: off -> on -> off, then restore the team color. */
      team_led_on = (phase == 1U);
    }
    ArmorLed_SetState(ArmorLink_GetTeam(), team_led_on);
  }

}
