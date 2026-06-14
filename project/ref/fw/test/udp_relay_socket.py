import socket
import time

ESP_PORT = 50001
LOCAL_PORT = 50000

DSM_PACKET_SIZE = 82

# 200 us
SAMPLE_PERIOD_SEC = 0.0002

rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

rx.bind(("0.0.0.0", ESP_PORT))

tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

print("DSM relay active")

batch_count = 0
packet_count = 0

last_print = time.perf_counter()

while True:

    data, addr = rx.recvfrom(65535)

    num_packets = len(data) // DSM_PACKET_SIZE

    batch_count += 1

    next_send_time = time.perf_counter()

    for i in range(num_packets):

        start = i * DSM_PACKET_SIZE
        end = start + DSM_PACKET_SIZE

        pkt = data[start:end]

        tx.sendto(
            pkt,
            ("127.0.0.1", LOCAL_PORT))

        packet_count += 1

        next_send_time += SAMPLE_PERIOD_SEC

        while time.perf_counter() < next_send_time:
            pass

    now = time.perf_counter()

    if (now - last_print) >= 1.0:

        print(
            f"Batches/sec={batch_count} "
            f"Packets/sec={packet_count}")

        batch_count = 0
        packet_count = 0

        last_print = now