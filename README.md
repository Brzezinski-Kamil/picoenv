# Pico temperature link

Two Raspberry Pi boards: a **sender** (BME280 sensor) and a **receiver** (16×2 LCD). They talk over **UART** using a small protocol with **CRC-16 Modbus**. The sender sleeps most of the time and wakes when the receiver sends a frame.

Planned extensions: **BLE** transport (`libraries/ble/`, not in the main build yet), a single board with LCD + sensor, and reception on a **Raspberry Pi**.

## Hardware

| Role | Board (default) | Peripherals |
|------|-----------------|-------------|
| Sender | Pico 2 W (RP2350) | BME280 on I2C |
| Receiver | Pico W (RP2040) | I2C LCD (e.g. PCF8574), button |

### Pins (same on both boards)

| Function | GPIO |
|----------|------|
| I2C SDA | 4 |
| I2C SCL | 5 |
| UART TX | 0 |
| UART RX | 1 |
| Button (receiver) | 15 |

- **BME280**: address `0x76`
- **LCD**: address `0x27` (typical I2C backpack)

### UART wiring

You need **two data wires** and ground:

```
Sender GP0 (TX) ──► Receiver GP1 (RX)
Receiver GP0 (TX) ──► Sender GP1 (RX)
GND ── GND
```

115200 8N1, binary frames (not ASCII).

## Behavior

- **Sender**: starts asleep. On `GET`, measures temperature (BME280, forced mode), sends one `TEMP` frame, powers down the sensor and CYW43, returns to dormant on UART RX falling edge. On `PING`, replies with `PONG` without measuring. **30 °C** threshold — onboard LED blinks briefly after alarm (needs CYW43 on for a moment).
- **Receiver**:
  - **short press**: preamble (`0x00` bytes), `GET`, wait for `TEMP`, show result on LCD (blank after 30 s),
  - **long press** (~0.7 s): link test `PING` / `PONG`, message on LCD,
  - LED blinks in the background.

Sender logs: **USB**. Receiver logs: **USB** (UART is not used for stdio).

## Frame protocol

```
AA | VER | TYPE | LEN | PAYLOAD… | CRC_LO | CRC_HI | 55
```

| Field | Description |
|-------|-------------|
| `AA` | SOF |
| `VER` | `0x01` |
| `TYPE` | see below |
| `LEN` | payload length (bytes) |
| CRC | CRC-16 Modbus over `[VER … last payload byte]`, **little-endian** on the wire |
| `55` | EOF |

| Type | Value | Payload |
|------|-------|---------|
| PING | `0x01` | empty — link test |
| PONG | `0x02` | empty |
| TEMP | `0x10` | 2 B: `int16` temperature × 100, LE |
| GET | `0x12` | empty — request one measurement |

Implementation: `libraries/crc/`.

## Build

Requires: [Pico SDK](https://github.com/raspberrypi/pico-sdk) (sender uses **`pico_low_power`** — recent SDK versions), CMake ≥ 3.13, ARM toolchain.

After cloning, point to the SDK via environment variable or CMake:

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
# or on first configure:
# cmake -DPICO_SDK_FETCH_FROM_GIT=ON ...
```

Do **not** reuse one `build/` directory for sender and receiver (different platform: RP2350 vs RP2040).

```bash
# Sender (Pico 2 W)
cmake -B build/sender -S . -DMODE=SENDER
cmake --build build/sender

# Receiver (Pico W)
cmake -B build/receiver -S . -DMODE=RECEIVER
cmake --build build/receiver
```

`.uf2` files appear under `build/`. Flash each board (hold BOOTSEL + USB).

In VS Code with the Pico extension you can set `MODE` in the CMake cache.

## Repository layout

```
sender/main.c          # sleep, respond to GET/PING
receiver/main.c        # LCD, button, polling
libraries/crc/         # protocol and CRC
libraries/uart/        # UART, preamble, stream parser
libraries/sensor/      # BME280
libraries/lcd/         # HD44780 over I2C
libraries/button/
libraries/ble/         # BLE sketch (advertising), not in CMakeLists
```

## BLE (experimental)

`libraries/ble/` mirrors the same frame format in **Manufacturer Specific Data** (`BLE_COMPANY_ID = 0xFFFF` — developer ID, not registered). To enable, add `ble.c` to the target, the include path for `btstack_config.h`, and BTstack libraries in `CMakeLists.txt` (see comment in `libraries/ble/ble.h`).

## License

MIT — see [LICENSE](LICENSE).

---

# Połączenie temperatury Pico

Dwie płytki Raspberry Pi: **nadajnik** (czujnik BME280) i **odbiornik** (wyświetlacz LCD 16×2). Łączą się po **UART** prostym protokołem z **CRC-16 Modbus**. Nadajnik większość czasu śpi i budzi się, gdy odbiornik wyśle ramkę.

Planowane rozszerzenia: transmisja po **BLE** (`libraries/ble/`, na razie poza głównym buildem), pojedyncza wyspa z LCD + czujnik, odbiór na **Raspberry Pi**.

## Sprzęt

| Rola | Płytka (domyślnie) | Peripheria |
|------|-------------------|------------|
| Nadajnik | Pico 2 W (RP2350) | BME280 na I2C |
| Odbiornik | Pico W (RP2040) | LCD I2C (np. PCF8574), przycisk |

### Piny (wspólne na obu płytkach)

| Funkcja | GPIO |
|---------|------|
| I2C SDA | 4 |
| I2C SCL | 5 |
| UART TX | 0 |
| UART RX | 1 |
| Przycisk (odbiornik) | 15 |

- **BME280**: adres `0x76`
- **LCD**: adres `0x27` (typowy backpack I2C)

### Połączenie UART

Potrzebne są **dwie żyły danych** i masa:

```
Nadajnik GP0 (TX) ──► Odbiornik GP1 (RX)
Odbiornik GP0 (TX) ──► Nadajnik GP1 (RX)
GND ── GND
```

115200 8N1, ramki binarne (nie ASCII).

## Zachowanie

- **Nadajnik**: po starcie zasypia. Na `GET` mierzy temperaturę (BME280, tryb forced), wysyła jedną ramkę `TEMP`, gasi czujnik i CYW43, wraca do dormant na zbocze opadające na RX. Na `PING` odsyła `PONG` bez pomiaru. Próg **30 °C** — dioda onboard krótko po alarmie (wymaga chwilowego włączenia CYW43).
- **Odbiornik**:
  - **krótki przycisk**: preambuła (bajty `0x00`), `GET`, oczekiwanie na `TEMP`, wynik na LCD (wygaszenie po 30 s),
  - **długi przycisk** (~0,7 s): test łącza `PING` / `PONG`, komunikat na LCD,
  - dioda miga w tle.

Logi nadajnika: **USB**. Logi odbiornika: **USB** (UART nie jest używany do stdio).

## Protokół ramek

```
AA | VER | TYPE | LEN | PAYLOAD… | CRC_LO | CRC_HI | 55
```

| Pole | Opis |
|------|------|
| `AA` | SOF |
| `VER` | `0x01` |
| `TYPE` | patrz poniżej |
| `LEN` | długość payload (bajty) |
| CRC | CRC-16 Modbus nad `[VER … ostatni bajt payload]`, na linii **little-endian** |
| `55` | EOF |

| Typ | Wartość | Payload |
|-----|---------|---------|
| PING | `0x01` | pusty — test łącza |
| PONG | `0x02` | pusty |
| TEMP | `0x10` | 2 B: `int16` temperatura × 100, LE |
| GET | `0x12` | pusty — żądanie jednego pomiaru |

Implementacja: `libraries/crc/`.

## Budowanie

Wymagane: [Pico SDK](https://github.com/raspberrypi/pico-sdk) (nadajnik używa **`pico_low_power`** — nowsze wersje SDK), CMake ≥ 3.13, toolchain ARM.

Po sklonowaniu repozytorium wskaż SDK przez zmienną środowiskową albo CMake:

```bash
export PICO_SDK_PATH=/ścieżka/do/pico-sdk
# albo przy pierwszej konfiguracji:
# cmake -DPICO_SDK_FETCH_FROM_GIT=ON ...
```

Jednego katalogu `build/` **nie przełączaj** między nadajnikiem a odbiornikiem (inna platforma: RP2350 vs RP2040).

```bash
# Nadajnik (Pico 2 W)
cmake -B build/sender -S . -DMODE=SENDER
cmake --build build/sender

# Odbiornik (Pico W)
cmake -B build/receiver -S . -DMODE=RECEIVER
cmake --build build/receiver
```

Pliki `.uf2` powstają w katalogach build. Wgraj odpowiedni obraz na każdą płytkę (przytrzymaj BOOTSEL + USB).

W Cursor / VS Code z rozszerzeniem Pico możesz ustawić `MODE` w CMake cache.

## Struktura repozytorium

```
sender/main.c          # sen, odpowiedź na GET/PING
receiver/main.c        # LCD, przycisk, odpytywanie
libraries/crc/         # protokół i CRC
libraries/uart/        # UART, preambuła, parser strumienia
libraries/sensor/      # BME280
libraries/lcd/         # HD44780 przez I2C
libraries/button/
libraries/ble/         # szkic BLE (advertising), nie w CMakeLists
```

## BLE (eksperymentalnie)

`libraries/ble/` powiela ten sam format ramki w polu **Manufacturer Specific Data** (`BLE_COMPANY_ID = 0xFFFF` — identyfikator deweloperski, nie zarejestrowany). Aby włączyć, trzeba dodać `ble.c` do targetu, ścieżkę include z `btstack_config.h` oraz biblioteki BTstack w `CMakeLists.txt` (patrz komentarz w `libraries/ble/ble.h`).

## Licencja

MIT — patrz [LICENSE](LICENSE).