"""Host-side checks for armor counter frames and reset synchronization."""


MODE_ENABLE = 0x01
MODE_RESET_COUNTERS = 0x02
STATUS_RESET_ACK = 0x80


def build_c1(team: int, port: int, mode: int, reset_epoch: int,
             sequence: int) -> bytes:
    frame = bytearray((0xA5, 0xC1, team, port, mode, reset_epoch, sequence, 0))
    frame[7] = sum(frame[:7]) & 0xFF
    return bytes(frame)


def build_status(armor_id: int, small: int, big: int,
                 reset_epoch: int, reset_ack: bool = False,
                 reset_sequence: int = 0) -> bytes:
    status_id = ((reset_sequence & 0x7F) if reset_ack else armor_id)
    status_id |= STATUS_RESET_ACK if reset_ack else 0
    frame = bytearray((0xA5, status_id, small & 0xFF, small >> 8,
                       big & 0xFF, big >> 8, reset_epoch, 0))
    frame[7] = sum(frame[:7]) & 0xFF
    return bytes(frame)


def test_c1_configuration_frames_have_valid_checksum_and_port_authority():
    for port in range(4):
        frame = build_c1(team=1, port=port,
                         mode=MODE_ENABLE | MODE_RESET_COUNTERS,
                         reset_epoch=7, sequence=port + 1)
        assert frame[:2] == bytes((0xA5, 0xC1))
        assert frame[3] == port
        assert frame[4] == 3
        assert frame[5] == 7
        assert frame[7] == (sum(frame[:7]) & 0xFF)


def test_status_frame_layout_and_checksum():
    frame = build_status(armor_id=3, small=0x1234, big=0x5678,
                         reset_epoch=9)
    assert frame == bytes((0xA5, 3, 0x34, 0x12, 0x78, 0x56, 9,
                           sum(frame[:7]) & 0xFF))
    ack = build_status(armor_id=3, small=0, big=0,
                       reset_epoch=9, reset_ack=True, reset_sequence=0x35)
    assert ack[1] == (0x35 | STATUS_RESET_ACK)
    assert ack[7] == (sum(ack[:7]) & 0xFF)


if __name__ == "__main__":
    test_c1_configuration_frames_have_valid_checksum_and_port_authority()
    test_status_frame_layout_and_checksum()
    print("protocol host test: PASS")
