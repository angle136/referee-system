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
static uint16_t armor_adc_history[ARMOR_ADC_HISTORY_COUNT];
static uint8_t armor_adc_history_count;
static uint8_t armor_adc_history_write_index;

static void ArmorApp_RecordAdc(uint16_t adc_raw)
{
  armor_adc_history[armor_adc_history_write_index] = adc_raw;
  armor_adc_history_write_index =
      (uint8_t)((armor_adc_history_write_index + 1U) % ARMOR_ADC_HISTORY_COUNT);
  if (armor_adc_history_count < ARMOR_ADC_HISTORY_COUNT)
  {
    armor_adc_history_count++;
  }
}

static void ArmorApp_GetAdcHistory(uint16_t samples[ARMOR_ADC_HISTORY_COUNT])
{
  uint8_t index;
  uint8_t oldest = armor_adc_history_count == ARMOR_ADC_HISTORY_COUNT
                       ? armor_adc_history_write_index
                       : 0U;

  for (index = 0U; index < ARMOR_ADC_HISTORY_COUNT; index++)
  {
    if (index < armor_adc_history_count)
    {
      uint8_t source = (uint8_t)((oldest + index) % ARMOR_ADC_HISTORY_COUNT);
      samples[index] = armor_adc_history[source];
    }
    else
    {
      samples[index] = 0U;
    }
  }
}

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
  for (uint8_t index = 0U; index < ARMOR_ADC_HISTORY_COUNT; index++)
  {
    armor_adc_history[index] = 0U;
  }
  armor_adc_history_count = 0U;
  armor_adc_history_write_index = 0U;
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
    armor_adc_history_count = 0U;
    armor_adc_history_write_index = 0U;
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
    if (adc_raw > armor_detector.baseline)
    {
      ArmorApp_RecordAdc((uint16_t)adc_raw);
    }
    if (hit_type == ARMOR_HIT_BIG)
    {
      armor_big_hit_count++;
    }
    else
    {
      armor_small_hit_count++;
    }
    if (!ArmorLink_IsAdcDebugActive())
    {
      ArmorProtocol_SendStatus(armor_small_hit_count,
                               armor_big_hit_count,
                               armor_reset_epoch,
                               ArmorLink_IsCounterResetAckActive(),
                               armor_reset_sequence);
    }
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
    if (ArmorLink_IsAdcDebugActive())
    {
      uint16_t samples[ARMOR_ADC_HISTORY_COUNT];
      ArmorApp_GetAdcHistory(samples);
      ArmorProtocol_SendAdcDebug(samples);
    }
    else
    {
      ArmorProtocol_SendStatus(armor_small_hit_count,
                               armor_big_hit_count,
                               armor_reset_epoch,
                               ArmorLink_IsCounterResetAckActive(),
                               armor_reset_sequence);
    }
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
