#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "armor_link.h"

static unsigned callback_count;
static armor_link_packet_t last_packet;

static void on_packet(uint8_t port_id, const armor_link_packet_t *packet)
{
    assert(port_id == 0U);
    assert(packet != 0);
    callback_count++;
    last_packet = *packet;
}

static void make_frame(uint8_t frame[8], uint8_t armor_id,
                       uint16_t small_count, uint16_t big_count,
                       uint8_t reset_epoch, uint8_t reset_ack)
{
    uint8_t i;
    frame[0] = 0xA5U;
    frame[1] = (uint8_t)(armor_id | reset_ack);
    frame[2] = (uint8_t)small_count;
    frame[3] = (uint8_t)(small_count >> 8U);
    frame[4] = (uint8_t)big_count;
    frame[5] = (uint8_t)(big_count >> 8U);
    frame[6] = reset_epoch;
    frame[7] = 0U;
    for (i = 0U; i < 7U; ++i) frame[7] = (uint8_t)(frame[7] + frame[i]);
}

int main(void)
{
    uint8_t frame[8];
    uint8_t bad_frame[8];
    armor_link_diagnostics_t diagnostics;

    armor_link_init(on_packet);
    make_frame(frame, 3U, 0x1234U, 0x5678U, 9U, 0U);
    armor_link_process(0U, frame, 3U);
    assert(callback_count == 0U);
    armor_link_process(0U, &frame[3], sizeof(frame) - 3U);
    assert(callback_count == 1U);
    assert(last_packet.armor_id == 3U);
    assert(last_packet.small_hit_count == 0x1234U);
    assert(last_packet.big_hit_count == 0x5678U);
    assert(last_packet.reset_epoch == 9U);
    assert(last_packet.reset_ack == 0U);
    assert(last_packet.reset_sequence == 0U);

    make_frame(frame, 3U, 0x1234U, 0x5678U, 9U, 0U);
    armor_link_process(0U, frame, sizeof(frame));
    assert(callback_count == 2U);

    memcpy(bad_frame, frame, sizeof(bad_frame));
    bad_frame[7]++;
    armor_link_process(0U, bad_frame, sizeof(bad_frame));
    armor_link_get_diagnostics(0U, &diagnostics);
    assert(diagnostics.packet_count == 2U);
    assert(diagnostics.checksum_error_count == 1U);
    assert(diagnostics.small_hit_count == 0x1234U);
    assert(diagnostics.big_hit_count == 0x5678U);
    assert(diagnostics.reset_epoch == 9U);

    puts("armor_link host test: PASS");
    return 0;
}
