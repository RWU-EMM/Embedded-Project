# Task-3

###  **Author:** Yash Sangale
### **Email:** yashbalu.sangale@rwu.de

---

## Overview

Task-3 implements MQTT-based communication exchange between the **ESP32-S3** and **Raspberry Pi Pico W**, extending the UART-based communication from Task-2 into a modular, distributed MQTT architecture.

| Device | Role |
|---|---|
| ESP32-S3 | UART-to-MQTT bridge · Authentication manager · Device manager · MQTT controller |
| Raspberry Pi Pico W | Remote execution device · LED controller · Potentiometer reader · Telemetry publisher |

---

## Communication Flow

```
PC  <──UART──>  ESP32-S3  <──MQTT──>  Raspberry Pi Pico W
```

## Features

- UART-to-MQTT command bridge
- JSON-based structured communication
- Multi-device scalable architecture
- MQTT wildcard topic routing
- Device authentication handshake
- ACK synchronization with sequence tracking
- Dynamic device registry
- Remote telemetry reporting
- WiFi & MQTT reconnect handling
- RGB LED and potentiometer control
- Modular FreeRTOS-based software architecture


## Project Test Setup

### Prerequisites

Before starting, install the following VS Code extensions:

- **[Wokwi for VS Code](https://marketplace.visualstudio.com/items?itemName=wokwi.wokwi-vscode)** — hardware simulator
- **[Serial Monitor](https://marketplace.visualstudio.com/items?itemName=ms-vscode.vscode-serial-monitor)** by Microsoft — serial/TCP log viewer

---

### Step 1 — Launch the ESP32-S3 Simulator

1. Open the ESP32-S3 project in VS Code:
   ```
   D:\RWU\Embedded-Project\task_3\esp32s3
   ```
2. Open `esp32s3/wokwi/diagram.json` — this automatically triggers the Wokwi simulator.
3. In the Terminal panel, select the **Serial Monitor** tab.
4. Set **Monitor Mode** to `TCP`, then set:
   - **Host:** `localhost`
   - **Port:** `4000`
5. Click **Start Monitoring**.

> **Tip:** If logs don't appear, click the **Re-simulate** button in the Wokwi toolbar.

---

### Step 2 — Launch the Pico W Simulator

1. Open the Pico W project in VS Code (`Ctrl+K, O` or manually):
   ```
   D:\RWU\Embedded-Project\task_3\pico_w
   ```
2. Open `pico_w/wokwi/diagram.json` — this automatically triggers the Wokwi simulator.
3. In the Terminal panel, select the **Serial Monitor** tab.
4. Set **Monitor Mode** to `TCP`, then set:
   - **Host:** `localhost`
   - **Port:** `4010`
5. Click **Start Monitoring**.

> **Tip:** If logs don't appear, click the **Re-simulate** button in the Wokwi toolbar.

---

### Step 3 — Monitor MQTT Topics with Postman *(Recommended)*

**MQTT Broker:** `141.69.95.10`

1. Download and install **[Postman](https://www.postman.com/downloads/)**.
2. Click **New** (top left) → select **MQTT**.
3. Enter the broker URL: `broker.emqx.io`
4. Click the **Topics** tab (below the URL input).
5. Subscribe to the following topics:

   | Topic | Purpose |
   |---|---|
   | `/heartbeat` | Device heartbeat |
   | `/connect` | Authentication requests |
   | `/ack` | Authentication responses |
   | `/cmd` | Commands to Pico W |
   | `/ack/PICO` | Pico W ACK responses |
   | `/data/PICO` | Pico W telemetry |

6. Return to the Serial Monitor terminals to send UART commands and observe logs.


## UART Commands

### Connection

| Command | Description |
|---|---|
| `CONN=PICO:BYPASS=0` | Connect device with authentication , here `PICO` is token|
| `CONN=PICO:BYPASS=1` | Connect with authentication bypass *(testing only)* |

---

### LED Commands

| Command | Description |
|---|---|
| `PICO:LED:EN=1` | Turn LED on |
| `PICO:LED:EN=0` | Turn LED off |
| `PICO:LED:EN=1:BLINK=500` | Enable LED blinking with interval in ms |
| `PICO:LED:BLINK=1000` | Change LED blink interval only |

---

<!-- ### RGB Commands

| Command | Description |
|---|---|
| `PICO:RGB:EN=1` | Enable RGB LED |
| `PICO:RGB:EN=0` | Disable RGB LED |
| `PICO:RGB:R=255:G=0:B=0` | Set RGB color to red |
| `PICO:RGB:R=0:G=255:B=0` | Set RGB color to green |
| `PICO:RGB:R=0:G=0:B=255` | Set RGB color to blue |
| `PICO:RGB:BLINK=500` | RGB blink interval |
| `PICO:RGB:LED_IDX=1:R=255:G=255:B=0` | Set indexed RGB LED color |

> If `RGB_LED_USE_RGBW=1`

| Command | Description |
|---|---|
| `PICO:RGB:W=255` | Set white channel |

--- -->

### POT Commands

| Command | Description |
|---|---|
| `PICO:POT=1` | Enable potentiometer mode |
| `PICO:POT=0` | Disable potentiometer mode |

---

### Special Commands

| Command | Description |
|---|---|
| `PICO:CMD:STATUS` | Request device status |
| `PICO:CMD:STATS` | Request runtime statistics |
| `PICO:CMD:HELP` | Print supported commands |

---

### Valid Multi-Block Combinations

The parser supports combining multiple command blocks in a single UART command.


```text
PICO:LED:EN=1:BLINK=500:POT=0:CMD:HELP
```

### MQTT Topics

| Topic | Purpose |
|---|---|
| `/connect` | Authentication requests |
| `/ack` | Authentication responses |
| `/ack/<token>` | Command acknowledgements |
| `/cmd` | Commands sent to Pico W |
| `/data/<token>` | Telemetry and device responses |

---

## MQTT Examples

### 1. Authentication Request
**Topic:** `/connect`
```json
{ "in_key": "POST", "bypass": 1 }
```

### 2. Authentication Response
**Topic:** `/ack`
```json
{ "status": "ok" }
```

### 3. Command Message
**Topic:** `/cmd`
```json
{
  "token": "POST",
  "seq": 1,
  "led": { "en": 1 }
}
```

### 4. ACK Response
**Topic:** `/ack/POST`
```json
{ "ack": 1 }
```

### 5. Telemetry Response
**Topic:** `/data/POST`
```json
{
  "uptime_ms": 25000,
  "wifi": 1,
  "mqtt": 1
}
```

---

## Testing with Postman / MQTTX

**Subscribe to:**
```
/ack/#
/data/#
/connect
```

**Publish test command** → `/cmd`
```json
{ "token": "POST", "seq": 1, "led": { "en": 1 } }
```

**Publish test ACK** → `/ack/POST`
```json
{ "ack": 1 }
```

---

## System Architecture

The ESP firmware is separated into multiple modules for modularity and scalability.

### Modules

| Module | Responsibility |
|---|---|
| `mqtt_service.c` | Low-level MQTT implementation |
| `mqtt_manager.c` | High-level MQTT abstraction layer |
| `auth_handler.c` | Authentication handling |
| `device_registry.c` | Device storage and tracking |
| `device_handler.c` | Device communication handling |
| `uart_handler.c` | UART command processing |
| `wifi_driver.c` | WiFi connection management |

### MQTT Service Layer (`mqtt_service.c`)

- MQTT initialization, publish/subscribe handling
- Topic routing with wildcard matching
- MQTT reconnection handling with subscription restoration
- Thread-safe access using FreeRTOS mutexes

```c
mqtt_service_publish(...)
mqtt_service_subscribe(...)
mqtt_service_register_handler(...)
```

### MQTT Manager Layer (`mqtt_manager.c`)

Acts as a wrapper over `mqtt_service.c` to simplify upper-layer code and centralize initialization.

```c
mqtt_manager_publish(...)
mqtt_manager_subscribe(...)
mqtt_manager_register_handler(...)
```

---

## Authentication

This implementation extends the original Task-3 spec with a custom authentication handshake.

### Authentication Flow

```
Step 1  ESP publishes /connect
        { "in_key": "POST" }

Step 2  Pico generates out_key → /ack
        { "out_key": "ABCD1234" }

Step 3  ESP computes hash(in_key + out_key + DEVICE_SECRET)
        and publishes → /connect
        { "out_key": "ABCD1234", "reg": "23948234", "token": "POST" }

Step 4  Pico validates registration → /ack
        { "status": "ok" }
        ESP marks device as authenticated.
```

---

## Device Registry (`device_registry.c`)

Each registered device tracks:

- Token
- Authentication state
- Sequence TX / Sequence ACK
- Last TX timestamp

**Features:** dynamic device allocation, auth tracking, sequence tracking, ACK tracking, multi-device support.

---

## Wildcard MQTT Routing

Wildcard subscriptions enable scalable multi-device support:

```c
mqtt_manager_register_handler("/ack/#",  handle_ack);
mqtt_manager_register_handler("/data/#", handle_data);
```

The token is extracted directly from the topic, allowing a single handler to serve multiple devices (e.g. `/ack/POST`, `/ack/DEV1`, `/data/POST`).

---

## ACK Mechanism

Each transmitted command increments `seq_tx`. The Pico responds with the matching sequence number:

```json
{ "ack": 5 }
```

The ESP validates with:
```c
if (seq == dev->seq_tx) { /* valid */ }
```

This provides packet synchronization, ACK verification, replay protection, and communication robustness.

---

## Pico W Implementation

The Pico W firmware handles WiFi/MQTT connectivity, authentication responses, command execution, LED control, potentiometer reading, and telemetry publishing.

**Subscribes to:** `/cmd`  
**Publishes to:** `/ack/<token>`, `/data/<token>`

---

## Robustness Features

- Authentication tokens & sequence numbers
- ACK verification
- MQTT & WiFi reconnect handling
- Dynamic subscriptions with wildcard topic routing
- Thread-safe MQTT access (FreeRTOS mutexes)
- Device authentication state tracking

---

## Conclusion

This implementation extends the original Task-3 MQTT + JSON communication system into a modular, scalable multi-device architecture. The ESP32-S3 acts as an intelligent UART-to-MQTT gateway while the Pico W serves as the remote execution device, with structured authentication, ACK synchronization, and full multi-device scalability.


# Task-3 — MQTT-Based Distributed Communication System
> **ESP32-S3 + Raspberry Pi Pico W**

**Author:** Yash Sangale

---

## Overview

Task-3 implements MQTT-based communication exchange between the **ESP32-S3** and **Raspberry Pi Pico W**, extending the UART-based communication from Task-2 into a modular, distributed MQTT architecture.

| Device | Role |
|---|---|
| ESP32-S3 | UART-to-MQTT bridge · Authentication manager · Device manager · MQTT controller |
| Raspberry Pi Pico W | Remote execution device · LED controller · Potentiometer reader · Telemetry publisher |

---

## Communication Flow

```
PC  <──UART──>  ESP32-S3  <──MQTT──>  Raspberry Pi Pico W
```

---

## UART Commands

| # | Command | Description |
|---|---|---|
| 1 | `CONN=POST:BYPASS=0` | Connect device with authentication |
| 2 | `CONN=POST:BYPASS=1` | Connect with authentication bypass *(testing only)* |
| 3 | `POST:LED:EN=1` | Turn LED on |
| 4 | `POST:LED:EN=0` | Turn LED off |
| 5 | `POST:LED:EN=1:BLINK=500` | LED blink with interval (ms) |
| 6 | `POST:STATUS` | Device status |
| 7 | `POST:STATS` | Device stats |
| 8 | `POST:HELP` | Device help |

---

## MQTT Topics

| Topic | Purpose |
|---|---|
| `/connect` | Authentication requests |
| `/ack` | Authentication responses |
| `/ack/<token>` | Command acknowledgements |
| `/cmd` | Commands sent to Pico W |
| `/data/<token>` | Telemetry and device responses |

---

## MQTT Examples

### 1. Authentication Request
**Topic:** `/connect`
```json
{ "in_key": "POST", "bypass": 1 }
```

### 2. Authentication Response
**Topic:** `/ack`
```json
{ "status": "ok" }
```

### 3. Command Message
**Topic:** `/cmd`
```json
{
  "token": "POST",
  "seq": 1,
  "led": { "en": 1 }
}
```

### 4. ACK Response
**Topic:** `/ack/POST`
```json
{ "ack": 1 }
```

### 5. Telemetry Response
**Topic:** `/data/POST`
```json
{
  "uptime_ms": 25000,
  "wifi": 1,
  "mqtt": 1
}
```

---

## Testing with Postman / MQTTX

**Subscribe to:**
```
/ack/#
/data/#
/connect
```

**Publish test command** → `/cmd`
```json
{ "token": "POST", "seq": 1, "led": { "en": 1 } }
```

**Publish test ACK** → `/ack/POST`
```json
{ "ack": 1 }
```

---

## System Architecture

The ESP firmware is separated into multiple modules for modularity and scalability.

### Modules

| Module | Responsibility |
|---|---|
| `mqtt_service.c` | Low-level MQTT implementation |
| `mqtt_manager.c` | High-level MQTT abstraction layer |
| `auth_handler.c` | Authentication handling |
| `device_registry.c` | Device storage and tracking |
| `device_handler.c` | Device communication handling |
| `uart_handler.c` | UART command processing |
| `wifi_driver.c` | WiFi connection management |

### MQTT Service Layer (`mqtt_service.c`)

- MQTT initialization, publish/subscribe handling
- Topic routing with wildcard matching
- MQTT reconnection handling with subscription restoration
- Thread-safe access using FreeRTOS mutexes

```c
mqtt_service_publish(...)
mqtt_service_subscribe(...)
mqtt_service_register_handler(...)
```

### MQTT Manager Layer (`mqtt_manager.c`)

Acts as a wrapper over `mqtt_service.c` to simplify upper-layer code and centralize initialization.

```c
mqtt_manager_publish(...)
mqtt_manager_subscribe(...)
mqtt_manager_register_handler(...)
```

---

## Authentication

This implementation extends the original Task-3 spec with a custom authentication handshake.

### Authentication Flow

```
Step 1  ESP publishes /connect
        { "in_key": "POST" }

Step 2  Pico generates out_key → /ack
        { "out_key": "ABCD1234" }

Step 3  ESP computes hash(in_key + out_key + DEVICE_SECRET)
        and publishes → /connect
        { "out_key": "ABCD1234", "reg": "23948234", "token": "POST" }

Step 4  Pico validates registration → /ack
        { "status": "ok" }
        ESP marks device as authenticated.
```

---

## Device Registry (`device_registry.c`)

Each registered device tracks:

- Token
- Authentication state
- Sequence TX / Sequence ACK
- Last TX timestamp

**Features:** dynamic device allocation, auth tracking, sequence tracking, ACK tracking, multi-device support.

---

## Wildcard MQTT Routing

Wildcard subscriptions enable scalable multi-device support:

```c
mqtt_manager_register_handler("/ack/#",  handle_ack);
mqtt_manager_register_handler("/data/#", handle_data);
```

The token is extracted directly from the topic, allowing a single handler to serve multiple devices (e.g. `/ack/POST`, `/ack/DEV1`, `/data/POST`).

---

## ACK Mechanism

Each transmitted command increments `seq_tx`. The Pico responds with the matching sequence number:

```json
{ "ack": 5 }
```

The ESP validates with:
```c
if (seq == dev->seq_tx) { /* valid */ }
```

This provides packet synchronization, ACK verification, replay protection, and communication robustness.

---

## Pico W Implementation

The Pico W firmware handles WiFi/MQTT connectivity, authentication responses, command execution, LED control, potentiometer reading, and telemetry publishing.

**Subscribes to:** `/cmd`  
**Publishes to:** `/ack/<token>`, `/data/<token>`

---

## Robustness Features

- Authentication tokens & sequence numbers
- ACK verification
- MQTT & WiFi reconnect handling
- Dynamic subscriptions with wildcard topic routing
- Thread-safe MQTT access (FreeRTOS mutexes)
- Device authentication state tracking

---

