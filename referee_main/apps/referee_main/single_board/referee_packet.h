#ifndef _REFEREE_MAIN_PACKET_H_
#define _REFEREE_MAIN_PACKET_H_

#include <stddef.h>
#include <stdint.h>

uint16_t referee_packet_build(uint8_t *frame,
                              size_t frame_capacity,
                              uint16_t command_id,
                              const void *payload,
                              uint16_t payload_length,
                              uint8_t *sequence);

#endif
