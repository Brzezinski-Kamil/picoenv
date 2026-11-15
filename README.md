# Pico temperature link

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