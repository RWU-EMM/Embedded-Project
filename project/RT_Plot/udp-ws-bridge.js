// Bridges ESP32 UDP batches to browser WebSocket clients.
// Run: npm install, then node udp-ws-bridge.js

const dgram = require("dgram");
const { WebSocketServer } = require("ws");

const UDP_PORT = 2812;   // must match target_port in wifi_udp_handler_config on ESP32
const WS_PORT = 8080;    // browser connects here: ws://<this-pc-ip>:8080

const udpSocket = dgram.createSocket("udp4");
const wss = new WebSocketServer({ port: WS_PORT });

let clients = new Set();

wss.on("connection", (ws) => {
    clients.add(ws);
    console.log(`Browser connected. Total clients: ${clients.size}`);
    ws.on("close", () => {
        clients.delete(ws);
        console.log(`Browser disconnected. Total clients: ${clients.size}`);
    });
});

udpSocket.on("message", (msg, rinfo) => {
    // msg is the raw batch buffer: dsm_batch_hdr_t + N * dsm_packet_t
    for (const ws of clients) {
        if (ws.readyState === ws.OPEN) {
            ws.send(msg, { binary: true });
        }
    }
});

udpSocket.on("error", (err) => {
    console.error("UDP socket error:", err);
    udpSocket.close();
});

udpSocket.bind(UDP_PORT, () => {
    console.log(`UDP listening on port ${UDP_PORT}`);
    console.log(`WebSocket server on port ${WS_PORT}`);
    console.log("Set wifi_udp_handler_config.target_ip to this PC's IPv4 address.");
});
