#ifndef _REFEREE_MAIN_ARMOR_LINK_H_
#define _REFEREE_MAIN_ARMOR_LINK_H_

#include <stddef.h>
#include <stdint.h>

#include "referee_config.h"

typedef struct
{
    uint8_t  frame_type;
    uint8_t  armor_id;
    uint16_t small_hit_count;
    uint16_t big_hit_count;
    uint8_t  reset_epoch;
    uint8_t  reset_ack;
    uint8_t  reset_sequence;
    uint16_t adc_samples[REFEREE_MAIN_ARMOR_ADC_HISTORY_COUNT];
} armor_link_packet_t;

typedef void (*armor_link_packet_callback_t)(uint8_t port_id,
                                             const armor_link_packet_t *packet);

typedef struct
{
    uint32_t packet_count;
    uint32_t checksum_error_count;
    uint16_t small_hit_count;
    uint16_t big_hit_count;
    uint8_t  reset_epoch;
    uint8_t  reset_ack;
    uint8_t  reset_sequence;
    uint8_t  adc_debug;
    uint16_t adc_samples[REFEREE_MAIN_ARMOR_ADC_HISTORY_COUNT];
} armor_link_diagnostics_t;

void armor_link_init(armor_link_packet_callback_t callback);
void armor_link_set_debug_mode(uint8_t enabled);
void armor_link_process(uint8_t port_id, const uint8_t *data, size_t length);
void armor_link_get_diagnostics(uint8_t port_id,
                                armor_link_diagnostics_t *diagnostics);

#endif
