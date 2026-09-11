#include "referee_packet.h"

#include <string.h>

#include "REFEREE/referee_protocol.h"
#include "crc_rm.h"

uint16_t referee_packet_build(uint8_t *frame,
                              size_t frame_capacity,
                              uint16_t command_id,
                              const void *payload,
                              uint16_t payload_length,
                              uint8_t *sequence)
{
    size_t   frame_length;
    uint16_t crc16;

    if (frame == 0 || sequence == 0 || payload_length > REFEREE_USER_DATA_MAX_LEN)
    {
        return 0;
    }

    frame_length = FRAME_HEADER_LEN + CMD_ID_LEN + payload_length + FRAME_TAIL_LEN;
    if (frame_length > frame_capacity || (payload_length > 0 && payload == 0))
    {
        return 0;
    }

    frame[0] = REFEREE_SOF;
    frame[1] = (uint8_t)(payload_length & 0xFFU);
    frame[2] = (uint8_t)(payload_length >> 8);
    frame[3] = (*sequence)++;
    frame[4] = Get_CRC8_Check_Sum(frame, 4, 0xFF);
    frame[5] = (uint8_t)(command_id & 0xFFU);
    frame[6] = (uint8_t)(command_id >> 8);

    if (payload_length > 0)
    {
        memcpy(&frame[7], payload, payload_length);
    }

    crc16 = Get_CRC16_Check_Sum(frame, frame_length - FRAME_TAIL_LEN, 0xFFFF);
    frame[frame_length - 2U] = (uint8_t)(crc16 & 0xFFU);
    frame[frame_length - 1U] = (uint8_t)(crc16 >> 8);
    return (uint16_t)frame_length;
}
