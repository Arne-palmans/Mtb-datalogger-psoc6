# 📊 Datalogger — CY8CKIT-062-WIFI-BT

A datalogger for the **Infineon CY8CKIT-062-WIFI-BT** board that periodically samples an ADC, stores samples in a circular ring buffer, and prints them via UART. The system is controlled by a **Finite State Machine (FSM)** with 3 states.

---
<img width="952" height="566" alt="image" src="https://github.com/user-attachments/assets/7a21ab2f-99f5-489a-be44-a300e1c2d42b" />


## 📋 Features

- **ADC sampling** every 1000ms on pin `P10_0`
- **Circular ring buffer** — stores the last 32 samples
- **Minimum tracker** — keeps track of the lowest value over the last 10 samples
- **FSM control** via UART commands or physical button
- **LED indicator** — LED on during logging

---

## 🔌 Hardware

| Component | Pin |
|---|---|
| ADC input | P10_0 |
| User button | CYBSP_USER_BTN |
| Debug UART TX | CYBSP_DEBUG_UART_TX |
| Debug UART RX | CYBSP_DEBUG_UART_RX |
| LED | CYBSP_LED8 |

---

## 🧠 FSM — Finite State Machine

```
        [s / button]              [d / button]
ST_IDLE ────────────→ ST_LOGGING ──────────────→ ST_DUMPING
   ↑                       │                          │
   │          [p]          │                          │
   └───────────────────────┘           (after dump)   │
   └──────────────────────────────────────────────────┘
```

### States

| State | Description |
|---|---|
| `ST_IDLE` | Waiting for command. No sampling. |
| `ST_LOGGING` | Samples are read and stored in the buffer every second. |
| `ST_DUMPING` | Buffer is printed via UART, then returns to IDLE. |

### Events

| Event | Trigger |
|---|---|
| `EVT_TIMER_TICK` | Sampling interval (1000ms) elapsed |
| `EVT_BTN_PRESS` | User button pressed |
| `EVT_CMD_S` | UART command `s` (start) |
| `EVT_CMD_P` | UART command `p` (pause) |
| `EVT_CMD_D` | UART command `d` (dump) |

---

## 🗄️ Ring Buffer

The circular buffer stores up to **32 samples** of type `sample_t`:

```c
typedef struct {
    uint32_t timestamp_ms;
    int16_t  value;
    uint8_t  source_id;
} sample_t;
```

- If the buffer is **full**, the oldest sample is automatically overwritten
- `buffer_push()` — add a sample
- `buffer_pop()` — retrieve the oldest sample
- `buffer_flush()` — print all samples via UART and empty the buffer

---

## 🖥️ UART Commands

Connect via a serial terminal (e.g. PuTTY) at **115200 baud**.

| Command | Action |
|---|---|
| `s` | Start logging (IDLE → LOGGING) |
| `p` | Pause logging (LOGGING → IDLE) |
| `d` | Dump buffer (→ DUMPING → IDLE) |

### PuTTY settings

```
Connection type:  Serial
Serial line:      COMx    (check Device Manager)
Speed:            115200
Local echo:       Force on
Local line edit:  Force off
```

---

## 📦 Output format

```
--- DUMP START (10 samples) ---
[    1000 ms] src=0 val=2048
[    2000 ms] src=0 val=2050
[    2345 ms] src=1 val=1
...
--- DUMP END ---
```

| Field | Description |
|---|---|
| `timestamp_ms` | Time in ms since boot |
| `src=0` | ADC sample |
| `src=1` | Button event |
| `val` | Measured value |

---

## 🛠️ Build & Flash

Built with **ModusToolbox**.

```bash
make build
make program
```

---

## 📁 Project Structure

```
EX2/
├── local/
│   └── main.c          # Main application
├── bsps/
│   └── APP_CY8CKIT-062-WIFI-BT/
├── build/
└── Makefile
```

---

## 📚 Dependencies

- [ModusToolbox](https://www.infineon.com/modustoolbox)
- `cy_retarget_io` — UART via USB
- `cyhal` — Hardware Abstraction Layer
- `cybsp` — Board Support Package

---

## 👤 Author

Arne Palmans - 1eai
