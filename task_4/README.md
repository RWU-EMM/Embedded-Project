# Task-4 — MQTT + UART XPLORER Bridge

### Author: Yash Sangale

### Email: [yashbalu.sangale@rwu.de](mailto:yashbalu.sangale@rwu.de)

---

# Overview

Task-4 implements a complete bidirectional MQTT + UART communication chain between:

* Raspberry Pi Pico W Simulator
* ESP32-S3 MQTT ↔ UART Bridge
* Raspberry Pi Pico on Joy-IT XPLORER Board

The simulator acts as the remote HMI/controller.
The ESP32-S3 acts as the MQTT ↔ UART bridge.
The XPLORER Pico controls the real hardware peripherals.

Communication flow:

```text
Pico W Simulator <----MQTT----> ESP32-S3 <----UART----> XPLORER Pico
```

---

# Setup

## 1. Pico W Simulator

Flash/run the simulator firmware on the Pico W Wokwi project.

Responsibilities:

* Publish commands over MQTT
* Mirror XPLORER RGB LED state
* Mirror servo angle feedback
* Display UART feedback logs
* Send LED and servo commands

MQTT Topics:

```text
/cmd/PICOE
/ack/PICOE
```

---

## 2. ESP32-S3 Bridge

Flash the ESP32-S3 bridge firmware.

Responsibilities:

* Subscribe to MQTT command topic
* Forward MQTT payload to UART
* Receive UART feedback from XPLORER
* Wrap UART feedback into JSON
* Publish feedback to MQTT ACK topic

UART Wiring:

| ESP32-S3    | XPLORER Pico |
| ----------- | ------------ |
| GPIO17 (TX) | GP5 (RX)     |
| GPIO18 (RX) | GP4 (TX)     |
| GND         | GND          |

UART Configuration:

```text
115200 baud
8 data bits
1 stop bit
No parity
```

---

## 3. XPLORER Pico Hardware

Flash the XPLORER firmware on the physical Raspberry Pi Pico mounted on the Joy-IT XPLORER board.

Responsibilities:

* Execute UART commands
* Control WS2812 RGB LEDs
* Control servo motor
* Read buttons
* Send ACK/status feedback over UART

Hardware Used:

| Peripheral      | GPIO |
| --------------- | ---- |
| WS2812 RGB LEDs | GP1  |
| Servo           | GP7  |
| Button TOP      | GP10 |
| Button RIGHT    | GP11 |
| Button BOTTOM   | GP14 |
| Button LEFT     | GP15 |
| UART TX         | GP4  |
| UART RX         | GP5  |

---

# MQTT JSON Format

## Command Packet

```json
{
    "token":"iem2026",
    "source":"pico-sim",
    "seq":0,
    "cmd":"LED:1:NEXT"
}
```

## Feedback Packet

```json
{
    "token":"iem2026",
    "source":"esp32",
    "seq":10,
    "uart":"ACK:LED:1:RED"
}
```

---

# UART Commands

## LED Control

```text
LED:1:NEXT
LED:2:NEXT
LED:3:NEXT
LED:4:NEXT
```

Cycles LED color sequence:

```text
OFF -> RED -> GREEN -> BLUE -> YELLOW -> CYAN -> PURPLE -> WHITE -> OFF
```

---

## Servo Control

```text
SERVO:0
SERVO:90
SERVO:180
```

Valid range:

```text
0-180 degrees
```

---

# UART Feedback

## LED ACK

```text
ACK:LED:1:RED
ACK:LED:2:GREEN
```

---

## Servo ACK

```text
ACK:SERVO:90
```

---

## Button Events

```text
BTN:TOP
BTN:RIGHT
BTN:BOTTOM
BTN:LEFT
```

Physical XPLORER button presses generate UART feedback and are mirrored back on the simulator.

---

# Behaviour

## Simulator → Hardware

| Simulator Action       | Hardware Behaviour                   |
| ---------------------- | ------------------------------------ |
| LED button press       | Cycles corresponding XPLORER RGB LED |
| Potentiometer movement | Moves XPLORER servo                  |

---

## Hardware → Simulator

| Hardware Action       | Simulator Behaviour            |
| --------------------- | ------------------------------ |
| Physical button press | Virtual RGB LED mirror updates |
| Servo ACK             | Servo angle mirror updates     |
| UART feedback         | Printed on simulator console   |
