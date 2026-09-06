#include "armor_debug.h"
#include "armor_config.h"
#include "kfifo.h"

volatile uint32_t armor_run_count;
volatile uint32_t armor_adc_raw;
volatile uint32_t armor_dx_level;
volatile uint32_t armor_baseline;
volatile uint32_t armor_last_event;
volatile uint32_t armor_adc_error_count;
volatile uint32_t armor_tx_error_count;
volatile ArmorDebugSample_t armor_debug_samples[ARMOR_DEBUG_SAMPLE_COUNT];
volatile uint32_t armor_debug_write_index;
volatile uint32_t armor_debug_sample_count;

static Kfifo_t armor_debug_fifo;

void ArmorDebug_Init(void)
{
  Kfifo_Init(&armor_debug_fifo,
             (void *)armor_debug_samples,
             sizeof(armor_debug_samples[0]),
             ARMOR_DEBUG_SAMPLE_COUNT);
}

void ArmorDebug_Record(uint32_t now, uint8_t hit_event)
{
  ArmorDebugSample_t sample = {
    (uint16_t)armor_adc_raw,
    (uint16_t)armor_baseline,
    (uint16_t)now,
    (uint8_t)armor_dx_level,
    hit_event
  };
  Kfifo_PushOverwrite(&armor_debug_fifo, &sample);
  armor_debug_write_index = (uint32_t)armor_debug_fifo.write_index;
  armor_debug_sample_count = (uint32_t)Kfifo_Count(&armor_debug_fifo);
}
