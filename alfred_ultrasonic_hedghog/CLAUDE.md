# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Building

Open in **STM32CubeIDE 1.19.0** and build with the default **Debug** configuration. There is no Makefile-based build — the IDE manages compilation via `.cproject`.

- Toolchain: `arm-none-eabi-gcc`, C11, no optimization (debug)
- Linker script: `STM32F030F4PX_FLASH.ld` (16 KB flash at `0x08000000`, 4 KB RAM at `0x20000000`)
- Key preprocessor defines: `STM32F030x6`, `USE_FULL_LL_DRIVER`, `HSE_VALUE=8000000`
- Flash with ST-Link via STM32CubeIDE's built-in programmer

## Architecture

The entire application lives in a single file: **`Core/Src/main.c`**. All configuration is done via compile-time macros at the top of that file — there is no runtime configuration.

The firmware uses the **STM32 LL (Low-Layer) API exclusively** (no HAL). All logic is interrupt-driven; the `main()` loop only polls `led_on_flag` to drive PA4.

### Hardware target

**STM32F030F4Px** — Cortex-M0, 16 KB flash, 4 KB RAM, 48 MHz (HSE 8 MHz × PLL ×12 ÷ 2).

### Pin assignments

| Pin | Function | Peripheral |
|-----|----------|-----------|
| PA9 | 190 kHz carrier TX output | TIM1_CH2, AF2 |
| PA6 | TX gate / burst enable | TIM3_CH1, AF1 |
| PB1 | Echo input capture (RX) | TIM3_CH4, AF1 |
| PA2 | Amplifier enable (blanking) | GPIO output |
| PA4 | Detection LED | GPIO output |

### Timer roles

- **TIM14** — 20 Hz trigger (prescaler = 47999, 1 ms tick). Update ISR schedules each measurement cycle.
- **TIM1** — 48 MHz clock, generates 190 kHz carrier on PA9. CC2 ISR drives the TX state machine: counts half-cycles, controls amplifier blanking (PA2 low during TX, re-enabled after `BLANK_CYCLES`).
- **TIM3** — 3 MHz tick (prescaler = 15). CH1 gates the TX burst on PA6; CH4 captures every rising echo edge on PB1. CC4 ISR validates echo frequency and measures time-of-flight.

### Detection algorithm

1. TIM14 fires at 20 Hz → starts a TX burst via TIM1.
2. TIM1 ISR emits exactly `PULSE_COUNT` half-cycles, then blanks the amplifier for `BLANK_CYCLES` half-cycles, then opens the listen window.
3. Each rising edge on PB1 is captured by TIM3_CH4. The ISR:
   - Computes the half-period and rejects edges outside ±`FREQ_TOLERANCE_PCT`% of 190 kHz.
   - Converts capture timestamp to distance (speed of sound 343 m/s); rejects echoes outside `DIST_MIN_CM`–`DIST_MAX_CM`.
   - Increments `valid_pulse_count`; sets `burst_hit` when it reaches `REQUIRED_PULSES`.
4. Confidence filter: `burst_hit` turns on the LED and resets a `CONFIDENCE_WINDOW`-miss counter. The LED clears only after `CONFIDENCE_WINDOW` consecutive miss cycles.

### Key configuration macros (top of `main.c`)

| Macro | Default | Meaning |
|-------|---------|---------|
| `FREQUENCY_HZ` | 190000 | Carrier frequency |
| `DIST_MIN_CM` / `DIST_MAX_CM` | 10 / 250 | Detection window |
| `PULSE_COUNT` | 4 | TX half-cycles per burst |
| `BLANK_CYCLES` | 100 | Amplifier blank duration (half-cycles) |
| `FREQ_TOLERANCE_PCT` | 15 | RX frequency acceptance band (±%) |
| `TRIGGER_RATE_HZ` | 20 | Measurement rate |
| `REQUIRED_PULSES` | 3 | Consecutive valid pulses for a hit |
| `CONFIDENCE_WINDOW` | 5 | Consecutive misses to clear LED |
| `LED_ON_TIME_MS` | 200 | LED hold time after hit |
| `HSE_EXT` | defined | Comment out to use internal HSI instead |
| `BUZZER_MODE` | 2 | Selects interrupt-driven detection path |
