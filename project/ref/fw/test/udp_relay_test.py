import socket

ESP_PORT = 50001
LOCAL_PORT = 50000

DSM_PACKET_SIZE = 82

rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

rx.bind(("0.0.0.0", ESP_PORT))

tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

print("Relay active")

while True:

    data, addr = rx.recvfrom(65535)

    num_packets = len(data) // DSM_PACKET_SIZE

    print(f"RX batch {num_packets}")

    for i in range(num_packets):

        start = i * DSM_PACKET_SIZE
        end = start + DSM_PACKET_SIZE

        pkt = data[start:end]

        ret = tx.sendto(
            pkt,
            ("127.0.0.1", LOCAL_PORT))

        print(f"TX {ret} bytes")