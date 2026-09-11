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
static ULONG                        last_event_tick[REFEREE_MAIN_ARMOR_COUNT];
static uint8_t                      last_event_valid[REFEREE_MAIN_ARMOR_COUNT];

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
        last_event_sequence[port_id] == packet.sequence &&
        (now - last_event_tick[port_id]) < REFEREE_MAIN_HIT_DEDUP_MS)
    {
        return;
    }

    if (packet.event != REFEREE_MAIN_ARMOR_EVENT_HEARTBEAT)
    {
        last_event_valid[port_id] = 1;
        last_event_sequence[port_id] = packet.sequence;
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
    memset(last_event_tick, 0, sizeof(last_event_tick));
    memset(last_event_valid, 0, sizeof(last_event_valid));
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
            armor_link_emit(port_id, parser->buffer);
            parser->length = 0;
        }
        else
        {
            parser->length = (byte == REFEREE_SOF) ? 1U : 0U;
            if (parser->length == 1U)
            {
                parser->buffer[0] = byte;
            }
        }
    }
}
