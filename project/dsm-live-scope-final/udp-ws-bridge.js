// Bridges ESP32 UDP batches to browser WebSocket clients.
// Run: npm install
// Then: npm start

const dgram = require("dgram");
const { WebSocketServer } = require("ws");

const UDP_PORT = 2812;
const WS_PORT = 8080;

const udpSocket = dgram.createSocket("udp4");
const wss = new WebSocketServer({
    port: WS_PORT
});

const clients = new Set();

wss.on("connection", ws => {
    clients.add(ws);

    console.log(
        `Browser connected. Total clients: ${clients.size}`
    );

    ws.on("close", () => {
        clients.delete(ws);

        console.log(
            `Browser disconnected. Total clients: ${clients.size}`
        );
    });
});

udpSocket.on("message", msg => {
    // msg is the raw UDP batch:
    //
    // dsm_batch_hdr_t +
    // N * dsm_packet_t
    //
    // The bridge intentionally does not parse the packet.
    // Parsing remains in worker.js.

    for (const ws of clients) {
        if (ws.readyState === ws.OPEN) {
            ws.send(msg, {
                binary: true
            });
        }
    }
});

udpSocket.on("error", err => {
    console.error("UDP socket error:", err);
    udpSocket.close();
});

udpSocket.bind(UDP_PORT, () => {
    console.log(`UDP listening on port ${UDP_PORT}`);
    console.log(`WebSocket server on port ${WS_PORT}`);
    console.log(
        "Set the ESP32 target IP to this PC's IPv4 address."
    );
});
