# alfred_ultrasonic_hedghog

Ultrasonic proximity detector firmware for the **STM32F030F4Px** (Cortex-M0, 16 KB flash). Written entirely with the STM32 LL (Low-Layer) API — no HAL overhead.

---

## What it does

The firmware turns the MCU into a self-contained ultrasonic echo detector. It periodically fires a short burst of 190 kHz ultrasound, then listens for the echo. If a reflected burst arrives within the configured distance window (10 – 250 cm) and passes a frequency-validity check, an LED turns on. A confidence filter prevents false triggers and chattering.

---

## How it works

### Transmit (TX)
- **TIM14** (1 ms tick, prescaler = 47999) fires an update interrupt at **20 Hz** (`TRIGGER_RATE_HZ`) to schedule the next measurement cycle.
- **TIM1** runs at the full 48 MHz system clock and generates the **190 kHz carrier** on **PA9** (TIM1_CH2, AF2). Its compare-channel-2 interrupt drives the TX state machine.
- Each TX burst is exactly **4 half-cycles** (`PULSE_COUNT`), giving a short, well-defined ping.

### Amplifier blanking
- **PA2** (active-high) enables the receive amplifier. It is pulled low at the start of TX to prevent the loud transmit pulse from saturating the front end.
- After **100 carrier half-cycles** (`BLANK_CYCLES`) the amplifier is re-enabled so echoes can be received.

### Receive (RX) & time-of-flight measurement
- **TIM3** (prescaler = 15 → 3 MHz tick) handles both the TX gate (CH1 output on PA6) and echo capture (CH4 input capture on **PB1**, AF1, pull-down).
- On every rising edge captured by TIM3_CH4 the ISR computes the **half-period** of the incoming signal and checks it is within ±15 % of 190 kHz (`FREQ_TOLERANCE_PCT`). Noise or interference outside this band is rejected immediately.
- The **time-of-flight** (capture timestamp minus TX start time) is converted to distance using the speed of sound (343 m/s). Only echoes arriving in the **10 – 250 cm** window (`DIST_MIN_CM` / `DIST_MAX_CM`) are counted.

### Confidence filter
- **3 consecutive valid echo pulses** (`REQUIRED_PULSES`) must be seen in a single listen window to register a hit (`burst_hit`).
- On a hit the LED turns on and a 200 ms timer (`LED_ON_TIME_MS`) is started.
- On a miss the firmware requires **5 consecutive miss cycles** (`CONFIDENCE_WINDOW`) before it turns the LED off. This prevents flicker if an object moves slightly.

### LED output
- **PA4** (push-pull output) drives the LED. The main loop simply sets or clears it based on `led_on_flag`.

---

## Pin summary

| Pin  | Function                          | Timer / AF      |
|------|-----------------------------------|-----------------|
| PA9  | Ultrasonic transmit output        | TIM1_CH2 / AF2  |
| PA6  | TX gate / transducer enable       | TIM3_CH1 / AF1  |
| PB1  | Echo input capture (RX)           | TIM3_CH4 / AF1  |
| PA2  | Amplifier enable (blanking ctrl)  | GPIO output     |
| PA4  | Detection LED                     | GPIO output     |

---

## Configuration (top of `main.c`)

| Macro              | Default   | Description                                      |
|--------------------|-----------|--------------------------------------------------|
| `SYS_CLK`          | 48000000  | System clock in Hz (HSE × PLL)                  |
| `FREQUENCY_HZ`     | 190000    | Ultrasonic carrier frequency                     |
| `HSE_EXT`          | defined   | Use external crystal; comment out for HSI        |
| `DIST_MIN_CM`      | 10        | Minimum detection distance (cm)                  |
| `DIST_MAX_CM`      | 250       | Maximum detection distance (cm)                  |
| `PULSE_COUNT`      | 4         | Number of TX half-cycles per burst               |
| `BLANK_CYCLES`     | 100       | Amplifier blank duration (carrier half-cycles)   |
| `FREQ_TOLERANCE_PCT` | 15      | Accepted RX frequency window (±%)               |
| `TRIGGER_RATE_HZ`  | 20        | Measurement repetition rate (Hz)                 |
| `REQUIRED_PULSES`  | 3         | Consecutive valid RX pulses needed for a hit     |
| `CONFIDENCE_WINDOW`| 5         | Consecutive misses needed to clear the LED       |
| `LED_ON_TIME_MS`   | 200       | How long the LED stays on after a hit (ms)       |

---

## Building

Open the project in **STM32CubeIDE 1.19.0** and build with the default **Debug** configuration. The linker script `STM32F030F4PX_FLASH.ld` targets the 16 KB flash / 4 KB RAM of the STM32F030F4Px.

No external libraries are needed beyond the ST LL drivers already included in `Drivers/`.
