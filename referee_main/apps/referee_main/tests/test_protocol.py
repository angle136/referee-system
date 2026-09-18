"""Host-side checks for the small armor/master wire formats."""


def build_c1(team: int, port: int, sequence: int) -> bytes:
    frame = bytearray((0xA5, 0xC1, team, port, 1, 0, sequence, 0))
    frame[7] = sum(frame[:7]) & 0xFF
    return bytes(frame)


def test_c1_configuration_frames_have_valid_checksum_and_port_authority():
    for port in range(4):
        frame = build_c1(team=1, port=port, sequence=port + 1)
        assert frame[:2] == bytes((0xA5, 0xC1))
        assert frame[3] == port
        assert frame[4] == 1
        assert frame[7] == (sum(frame[:7]) & 0xFF)


if __name__ == "__main__":
    test_c1_configuration_frames_have_valid_checksum_and_port_authority()
    print("protocol host test: PASS")
