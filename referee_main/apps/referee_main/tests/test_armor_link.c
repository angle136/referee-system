#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "armor_link.h"
#include "tx_api.h"

static ULONG fake_tick;
static unsigned callback_count;

ULONG tx_time_get(void)
{
    return fake_tick;
}

static void on_packet(uint8_t port_id, const armor_link_packet_t *packet)
{
    assert(port_id == 0U);
    assert(packet != 0);
    callback_count++;
}

static void make_frame(uint8_t frame[8], uint8_t event, uint8_t dx,
                       uint16_t ax, uint8_t sequence)
{
    uint8_t i;
    frame[0] = 0xA5U;
    frame[1] = 0U;
    frame[2] = event;
    frame[3] = dx;
    frame[4] = (uint8_t)ax;
    frame[5] = (uint8_t)(ax >> 8U);
    frame[6] = sequence;
    frame[7] = 0U;
    for (i = 0U; i < 7U; ++i) frame[7] = (uint8_t)(frame[7] + frame[i]);
}

int main(void)
{
    uint8_t frame[8];
    uint8_t bad_frame[8];
    armor_link_diagnostics_t diagnostics;

    armor_link_init(on_packet);
    make_frame(frame, 3U, 1U, 512U, 10U);
    armor_link_process(0U, frame, sizeof(frame));
    assert(callback_count == 1U);

    /* The armor board retransmits with a new sequence number. The payload
     * signature must still be deduplicated inside the hit window. */
    make_frame(frame, 3U, 1U, 512U, 11U);
    armor_link_process(0U, frame, sizeof(frame));
    assert(callback_count == 1U);

    fake_tick = 101U;
    make_frame(frame, 3U, 1U, 512U, 12U);
    armor_link_process(0U, frame, sizeof(frame));
    assert(callback_count == 2U);

    memcpy(bad_frame, frame, sizeof(bad_frame));
    bad_frame[7]++;
    armor_link_process(0U, bad_frame, sizeof(bad_frame));
    armor_link_get_diagnostics(0U, &diagnostics);
    assert(diagnostics.packet_count == 3U);
    assert(diagnostics.duplicate_count == 1U);
    assert(diagnostics.checksum_error_count == 1U);
    assert(diagnostics.hit_count == 2U);

    puts("armor_link host test: PASS");
    return 0;
}
