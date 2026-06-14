import serial
import struct

# =========================================================
# CONFIG
# =========================================================

PORT = "COM4"

BAUDRATE = 115200

DSM_PACKET_SIZE = 82

DSM_SIGNATURE = 0xAD

# =========================================================
# SERIAL
# =========================================================

ser = serial.Serial(
    PORT,
    BAUDRATE,
    timeout=0)

# =========================================================
# XOR16
# =========================================================

def calc_xor16(data):

    crc = 0

    for i in range(0, len(data), 2):

        word = (
            data[i] |
            (data[i + 1] << 8))

        crc ^= word

    return crc & 0xFFFF

# =========================================================
# RX BUFFER
# =========================================================

buf = bytearray()

packet_counter = 0
crc_error_counter = 0
sync_error_counter = 0

print("================================================")
print(" DSM USB CDC RECEIVER")
print("================================================")
print(f"Port        : {PORT}")
print(f"Baudrate    : {BAUDRATE}")
print(f"Packet Size : {DSM_PACKET_SIZE}")
print("================================================")

# =========================================================
# MAIN LOOP
# =========================================================

while True:

    chunk = ser.read(4096)

    if chunk:
        buf.extend(chunk)

    while len(buf) >= DSM_PACKET_SIZE:

        idx = buf.find(bytes([DSM_SIGNATURE]))

        if idx < 0:

            sync_error_counter += 1

            print("[SYNC LOST]")

            buf.clear()

            break

        if idx > 0:

            print(f"[RESYNC] dropped {idx} bytes")

            del buf[:idx]

            sync_error_counter += 1

            continue

        if len(buf) < DSM_PACKET_SIZE:
            break

        packet = bytes(buf[:DSM_PACKET_SIZE])

        if packet[0] != DSM_SIGNATURE:

            del buf[0]

            sync_error_counter += 1

            continue

        rx_crc = struct.unpack_from(
            "<H",
            packet,
            80)[0]

        calc_crc = calc_xor16(packet[:80])

        if rx_crc != calc_crc:

            crc_error_counter += 1

            print(
                f"[CRC ERROR] "
                f"rx=0x{rx_crc:04X} "
                f"calc=0x{calc_crc:04X}")

            del buf[0]

            continue

        # =================================================
        # VALID PACKET
        # =================================================

        packet_counter += 1

        sig = packet[0]

        n = packet[1]

        seq = (n >> 6) & 0x03

        adc_in = struct.unpack_from(
            "<h",
            packet,
            2)[0]

        fgen_out = struct.unpack_from(
            "<h",
            packet,
            4)[0]

        filt_x = struct.unpack_from(
            "<h",
            packet,
            6)[0]

        filt_y = struct.unpack_from(
            "<h",
            packet,
            8)[0]

        dt = struct.unpack_from(
            "<h",
            packet,
            14)[0]

        print(
            f"pkt={packet_counter:06d} "
            f"sig=0x{sig:02X} "
            f"seq={seq} "
            f"dt={dt:4d}us "
            f"filt_x={filt_x:+6d}"
        )

        del buf[:DSM_PACKET_SIZE]