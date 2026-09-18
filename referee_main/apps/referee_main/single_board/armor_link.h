#ifndef _REFEREE_MAIN_ARMOR_LINK_H_
#define _REFEREE_MAIN_ARMOR_LINK_H_

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint8_t  armor_id;
    uint8_t  event;
    uint8_t  dx;
    uint16_t ax_raw;
    uint8_t  sequence;
} armor_link_packet_t;

typedef void (*armor_link_packet_callback_t)(uint8_t port_id,
                                             const armor_link_packet_t *packet);

typedef struct
{
    uint32_t packet_count;
    uint32_t checksum_error_count;
    uint32_t duplicate_count;
    uint32_t hit_count;
} armor_link_diagnostics_t;

void armor_link_init(armor_link_packet_callback_t callback);
void armor_link_process(uint8_t port_id, const uint8_t *data, size_t length);
void armor_link_get_diagnostics(uint8_t port_id,
                                armor_link_diagnostics_t *diagnostics);

#endif
