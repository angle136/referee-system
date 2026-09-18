#include "armor_link.h"

#include <string.h>

#include "REFEREE/referee_protocol.h"
#include "referee_config.h"
#include "tx_api.h"

typedef struct
{
    uint8_t buffer[REFEREE_MAIN_ARMOR_PACKET_SIZE];
    uint8_t length;
} armor_link_parser_t;

static armor_link_parser_t          parsers[REFEREE_MAIN_ARMOR_COUNT];
static armor_link_packet_callback_t packet_callback;
static uint8_t                      last_event_sequence[REFEREE_MAIN_ARMOR_COUNT];
static uint8_t                      last_event_type[REFEREE_MAIN_ARMOR_COUNT];
static uint8_t                      last_event_dx[REFEREE_MAIN_ARMOR_COUNT];
static uint16_t                     last_event_ax_raw[REFEREE_MAIN_ARMOR_COUNT];
static ULONG                        last_event_tick[REFEREE_MAIN_ARMOR_COUNT];
static uint8_t                      last_event_valid[REFEREE_MAIN_ARMOR_COUNT];
static volatile uint32_t            packet_count[REFEREE_MAIN_ARMOR_COUNT];
static volatile uint32_t            checksum_error_count[REFEREE_MAIN_ARMOR_COUNT];
static volatile uint32_t            duplicate_count[REFEREE_MAIN_ARMOR_COUNT];
static volatile uint32_t            hit_count[REFEREE_MAIN_ARMOR_COUNT];

static uint8_t armor_packet_sum(const uint8_t *data)
{
    uint8_t sum = 0;

    for (size_t index = 0; index < REFEREE_MAIN_ARMOR_PACKET_SIZE - 1U; index++)
    {
        sum = (uint8_t)(sum + data[index]);
    }
    return sum;
}

static void armor_link_emit(uint8_t port_id, const uint8_t *data)
{
    armor_link_packet_t packet;
    ULONG               now;

    packet.armor_id = data[1];
    packet.event = data[2];
    packet.dx = data[3];
    packet.ax_raw = (uint16_t)data[4] | ((uint16_t)data[5] << 8);
    packet.sequence = data[6];

    now = tx_time_get();
    if (packet.event != REFEREE_MAIN_ARMOR_EVENT_HEARTBEAT &&
        last_event_valid[port_id] &&
        ((last_event_sequence[port_id] == packet.sequence) ||
         (last_event_type[port_id] == packet.event &&
          last_event_dx[port_id] == packet.dx &&
          last_event_ax_raw[port_id] == packet.ax_raw)) &&
        (now - last_event_tick[port_id]) < REFEREE_MAIN_HIT_DEDUP_MS)
    {
        duplicate_count[port_id]++;
        return;
    }

    if (packet.event != REFEREE_MAIN_ARMOR_EVENT_HEARTBEAT)
    {
        hit_count[port_id]++;
        last_event_valid[port_id] = 1;
        last_event_sequence[port_id] = packet.sequence;
        last_event_type[port_id] = packet.event;
        last_event_dx[port_id] = packet.dx;
        last_event_ax_raw[port_id] = packet.ax_raw;
        last_event_tick[port_id] = now;
    }

    if (packet_callback != 0)
    {
        packet_callback(port_id, &packet);
    }
}

void armor_link_init(armor_link_packet_callback_t callback)
{
    memset(parsers, 0, sizeof(parsers));
    memset(last_event_sequence, 0, sizeof(last_event_sequence));
    memset(last_event_type, 0, sizeof(last_event_type));
    memset(last_event_dx, 0, sizeof(last_event_dx));
    memset(last_event_ax_raw, 0, sizeof(last_event_ax_raw));
    memset(last_event_tick, 0, sizeof(last_event_tick));
    memset(last_event_valid, 0, sizeof(last_event_valid));
    memset((void *)packet_count, 0, sizeof(packet_count));
    memset((void *)checksum_error_count, 0, sizeof(checksum_error_count));
    memset((void *)duplicate_count, 0, sizeof(duplicate_count));
    memset((void *)hit_count, 0, sizeof(hit_count));
    packet_callback = callback;
}

void armor_link_process(uint8_t port_id, const uint8_t *data, size_t length)
{
    armor_link_parser_t *parser;

    if (port_id >= REFEREE_MAIN_ARMOR_COUNT || data == 0)
    {
        return;
    }

    parser = &parsers[port_id];
    for (size_t index = 0; index < length; index++)
    {
        uint8_t byte = data[index];

        if (parser->length == 0)
        {
            if (byte == REFEREE_SOF)
            {
                parser->buffer[parser->length++] = byte;
            }
            continue;
        }

        parser->buffer[parser->length++] = byte;
        if (parser->length < sizeof(parser->buffer))
        {
            continue;
        }

        if (armor_packet_sum(parser->buffer) ==
            parser->buffer[REFEREE_MAIN_ARMOR_PACKET_SIZE - 1U])
        {
            packet_count[port_id]++;
            armor_link_emit(port_id, parser->buffer);
            parser->length = 0;
        }
        else
        {
            checksum_error_count[port_id]++;
            parser->length = (byte == REFEREE_SOF) ? 1U : 0U;
            if (parser->length == 1U)
            {
                parser->buffer[0] = byte;
            }
        }
    }
}

void armor_link_get_diagnostics(uint8_t port_id,
                                armor_link_diagnostics_t *diagnostics)
{
    if (port_id >= REFEREE_MAIN_ARMOR_COUNT || diagnostics == 0)
    {
        return;
    }

    diagnostics->packet_count = packet_count[port_id];
    diagnostics->checksum_error_count = checksum_error_count[port_id];
    diagnostics->duplicate_count = duplicate_count[port_id];
    diagnostics->hit_count = hit_count[port_id];
}
