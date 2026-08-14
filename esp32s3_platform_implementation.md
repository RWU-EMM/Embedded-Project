# 2.x Platform Board: ESP32-S3 System Implementation

## 2.x.1 Hardware Interface Configuration

The ESP32-S3 microcontroller operates under ESP-IDF v5.x using FreeRTOS.
It functions as the communication bridge between the STM32 SPI master
and WebSocket clients over an isolated Wi-Fi Access Point (AP).

### System Architecture

``` text
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                             ESP32-S3 System Architecture                    │
 ├──────────────────────────────────────┬──────────────────────────────────────┤
 │         CPU Core 0 (Priority 3)      │        CPU Core 1 (Priority 3)      │
 │  ┌────────────────────────────────┐  │  ┌────────────────────────────────┐  │
 │  │        spi_bridge_task         │  │  │        ws_upstream_task         │  │
 │  │  • Polls SPI2_HOST Interface   │  │  │  • Adaptive Blocking Mode      │  │
 │  │  • Non-zero vTaskDelay Yield   │  │  │  • Aggregates 5 Packets/50ms    │  │
 │  └───────────────┬────────────────┘  │  └────────────────▲───────────────┘  │
 └──────────────────┼───────────────────┴───────────────────┼──────────────────┘
                    │                                       │
                    ▼                                       │
      ┌─────────────────────────────────────────────────────┴─────────┐
      │  FreeRTOS RingBuffer (RINGBUF_TYPE_NOSPLIT, 8192 Bytes)       │
      └───────────────────────────────────────────────────────────────┘
                                        ▲
 ┌──────────────────────────────────────┴──────────────────────────────────────┐
 │                      Immediate Downstream Path                              │
 │  httpd_ws_handler() ──> JSON Parse ──> spi_telemetry_send_command()         │
 └────────────────────────────────────────────────────────────────────────────┘
```

The SPI slave interface is implemented using `SPI2_HOST` with the
following pin assignment:

| Signal    |    GPIO |
|-----------|--------:|
| MOSI      | GPIO 18 |
| MISO      | GPIO 17 |
| SCLK      | GPIO 16 |
| CS        | GPIO 15 |
| Handshake | GPIO 21 |

The handshake output is asserted HIGH when a DMA receive buffer is
queued and ready to accept an SPI transaction.

The SPI payload is represented by the fixed binary structure
`spi_packet_t`. Each packet contains a protocol header, packet type,
sequence identifier (`seq`), CRC16, and ten single-precision
floating-point values, `float data[10]`.

The networking subsystem operates in `WIFI_MODE_AP`, providing an IEEE
802.11 b/g/n wireless Access Point. An `esp_http_server` instance
provides the WebSocket endpoint `/ws` together with HTTP REST endpoints
`/api/info` and `/api/control`.

## 2.x.2 FreeRTOS Inter-Task Architecture

The ESP32-S3 separates real-time SPI acquisition from variable-latency
TCP/IP processing using two FreeRTOS tasks connected through a
thread-safe RingBuffer.

The RingBuffer is configured as:

- **Type:** `RINGBUF_TYPE_NOSPLIT`
- **Capacity:** 8192 bytes

`RINGBUF_TYPE_NOSPLIT` is used so that each `spi_packet_t` is stored as
a contiguous memory block, avoiding fragmented multi-byte packet
retrieval.

### SPI Bridge Task

`spi_bridge_task` executes on CPU Core 0 with priority 3.

Its operation is:

1.  Poll `spi_telemetry_read_last_rx()` for a newly received SPI packet.
2.  Accept only packets that have passed the SPI protocol and CRC16
    validation.
3.  Write each valid `spi_packet_t` to the FreeRTOS RingBuffer.
4.  Execute a non-zero `vTaskDelay(max(pdMS_TO_TICKS(10), 1))` at each
    iteration.

The non-zero delay prevents continuous CPU occupation by the polling
task and provides scheduling opportunities for the idle task, avoiding
unnecessary Task Watchdog Timer (`task_wdt`) activation.

### WebSocket Upstream Task

`ws_upstream_task` executes on CPU Core 1 with priority 3.

The task uses adaptive blocking to minimize CPU utilization:

- When no packets are pending (`N = 0`), it blocks indefinitely on
  `xRingbufferReceive()` using `portMAX_DELAY`.
- After the first packet is received, the task switches to a 50 ms
  collection window.
- Packets are accumulated until either five packets have been collected
  or the 50 ms timeout expires.

The collected packets are serialized into a single unformatted JSON
object:

``` json
{
  "pkts": [
    {
      "seq": 0,
      "data": [f0, f1, f2, f3, f4, f5, f6, f7, f8, f9]
    }
  ]
}
```

The resulting batch is broadcast asynchronously to all registered
WebSocket client file descriptors through `http_server_broadcast_ws()`.

The aggregation strategy reduces the number of WebSocket transmissions
compared with sending every SPI packet individually while maintaining
bounded transmission latency through the 50 ms flush interval.

## 2.x.3 Downstream Command Pipeline and Memory Safety

Commands transmitted from WebSocket clients to the STM32 follow a direct
downstream path:

``` text
Client WebSocket Frame
        │
        ▼
httpd_ws_recv_frame()
        │
        ▼
Two-pass frame retrieval
        │
        ▼
Dynamic buffer
        │
        ▼
cJSON parsing
        │
        ▼
spi_telemetry_send_command()
        │
        ▼
SPI DMA transmission queue
```

### Two-Pass WebSocket Frame Retrieval

The WebSocket frame is retrieved in two stages.

First, `httpd_ws_recv_frame(req, &ws_pkt, 0)` is called with zero
payload length to obtain the incoming frame size from `ws_pkt.len`.

Second, a buffer of `ws_pkt.len + 1` bytes is allocated using `calloc()`
and the complete frame is retrieved using
`httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len)`.

The additional byte provides space for a null terminator when the
received payload is interpreted as a C string.

### Command Processing

The received text frame is parsed as JSON using `cJSON`. The expected
command structure contains a sequence identifier and ten floating-point
values:

``` json
{
  "seq": 0,
  "data": [f0, f1, f2, f3, f4, f5, f6, f7, f8, f9]
}
```

After validation and parsing, `spi_telemetry_send_command()` is called
directly. The command therefore bypasses the upstream RingBuffer and
additional inter-task scheduling, providing a low-latency path from the
WebSocket client to the SPI transmission mechanism.

### Memory Management

The dynamically allocated WebSocket buffer remains valid throughout
frame parsing, command processing, and frame echo. The buffer is
released once processing is complete.

The lifecycle is therefore:

``` text
calloc()
   ↓
WebSocket frame retrieval
   ↓
JSON parsing
   ↓
SPI command processing
   ↓
WebSocket echo
   ↓
free()
```

This ensures a single ownership and release point for the dynamically
allocated frame buffer and prevents memory leaks, double-free
conditions, and use-after-free errors.

## 2.x.4 Embedded Web Interface and Client-Side Statistical Processing

The ESP32-S3 hosts an embedded single-page HTML5/JavaScript dashboard
and serves it through the HTTP server at `/main`.

To reduce ESP32-S3 computational and memory requirements, raw
floating-point telemetry is transmitted without statistical
preprocessing. Statistical calculations are performed by the client-side
JavaScript runtime.

For each of the ten telemetry channels, the dashboard calculates:

- Current value
- Minimum value
- Maximum value
- Running average
- Instantaneous difference

For channel (k), the running average over (M) samples is calculated as:

\[ *k = rac{1}{M} *{i=1}^{M} x\_{k,i} \]

The instantaneous difference is:

\[ *k = x*{k,i} - x\_{k,i-1} \]

This offloads non-real-time statistical processing from the ESP32-S3 and
leaves the microcontroller resources primarily dedicated to SPI
communication, packet buffering, WebSocket transport, and command
forwarding.
