# CubeMX + VS Code Setup Guide - Ultrasonic Transceiver (STM32F030F4P6)

## Prerequisites

Install these before starting:

1. **STM32CubeMX** (standalone) - download from [st.com](https://www.st.com/en/development-tools/stm32cubemx.html)
2. **STM32CubeCLT** (command-line toolchain) - download from [st.com](https://www.st.com/en/development-tools/stm32cubeclt.html)
   - Includes ARM GCC, GDB, CMake, Ninja, OpenOCD, STM32CubeProgrammer
3. **VS Code** with the **STM32 VS Code Extension** by STMicroelectronics
   - Search "STM32" in the Extensions panel and install the official one
   - It will auto-install companion extensions (CMake Tools, Cortex-Debug, etc.)
4. **ST-Link V2 USB drivers** - install from within the extension:
   STM32 butterfly icon -> STM32Cube Resources -> ST-Link USB Drivers

## Part A: Create the CubeMX Project

### 1. New Project

- Open **STM32CubeMX** (standalone application)
- File -> New Project
- Search for **STM32F030F4P6** and select it
- Click "Start Project"

### 2. SYS Configuration

- System Core -> **SYS**
- Debug = **Serial Wire** (required for ST-Link SWD)
- Timebase Source = SysTick

### 3. RCC Configuration

- System Core -> **RCC**
- Set **High Speed Clock (HSE)** = Crystal/Ceramic Resonator
- Go to **Clock Configuration** tab:
  - Set HSE input = **24** MHz
  - PLL Source Mux = **HSE**
  - PREDIV = **/1**
  - PLLMUL = **x2**
  - System Clock Mux = **PLLCLK**
  - Verify SYSCLK = **48 MHz**, AHB = 48 MHz, APB1 = 48 MHz

### 4. TIM1 (PA9 - TX Frequency)

- Timers -> **TIM1**
- Clock Source = **Internal Clock**
- Channel 2 = **PWM Generation CH2** (auto-assigns PA9)
- Parameter Settings:
  - Prescaler (PSC) = **0**
  - Counter Mode = **Up**
  - Counter Period (ARR) = **799** (reconfigured by firmware)
  - Auto-reload preload = **Enable**
  - Pulse (CCR2) = **400**
  - PWM Mode = **PWM Mode 1**
  - CH Polarity = **High**
- Trigger Output (TRGO) Parameters:
  - Master/Slave Mode (MSM) = **Disable**
  - Master Mode Selection (MMS) = **OC2REF**

### 5. TIM3 (PA6 - Burst Gate)

- Timers -> **TIM3**
- Slave Mode = **External Clock Mode 1**
- Trigger Source = **ITR0**
- Channel 1 = **PWM Generation CH1** (auto-assigns PA6)
- Parameter Settings:
  - Prescaler (PSC) = **0**
  - Counter Mode = **Up**
  - Counter Period (ARR) = **8** (reconfigured by firmware)
  - Auto-reload preload = **Enable**
  - One Pulse Mode = **Enable**
  - Pulse (CCR1) = **8** (same as ARR, reconfigured by firmware)
  - PWM Mode = **PWM Mode 1**
  - CH Polarity = **High**

### 6. TIM14 (PB1 - Echo Input Capture)

- Timers -> **TIM14**
- Channel 1 = **Input Capture direct mode** (auto-assigns PB1)
- Parameter Settings:
  - Prescaler (PSC) = **0**
  - Counter Period (ARR) = **65535** (0xFFFF)
  - Polarity Selection = **Rising Edge**
  - IC Prescaler = **No division**
  - Input Filter = **0** (or 2-4 for noise filtering)

### 7. TIM17 (Burst Repetition Timer)

- Timers -> **TIM17**
- Activated = **check the box**
- Clock Source = **Internal Clock**
- Parameter Settings:
  - Prescaler (PSC) = **47999** (48 MHz / 48000 = 1 kHz tick)
  - Counter Period (ARR) = **99** (for 10 Hz burst rate)
  - Auto-reload preload = **Enable**

### 8. GPIO

- System Core -> **GPIO**
- **PA2**: GPIO_Output, output level = **High**, Push Pull, No pull, label: `TX_ENABLE`
- **PA4**: GPIO_Output, output level = **Low**, Push Pull, No pull, label: `BUZZER`

### 9. NVIC

- System Core -> **NVIC**
- Enable:
  - **TIM3 global interrupt** - Priority = **0** (highest, PA2 sync)
  - **TIM14 global interrupt** - Priority = **0** (highest, echo capture)
  - **TIM17 global interrupt** - Priority = **2** (lower, burst trigger)
- Note: STM32F030 has 2-bit priority (0-3), 0 = highest.

### 10. Project Settings & Generate

- **Project Manager** tab:
  - Project Name: `ultrasonic_transceiver` (or your choice)
  - Project Location: `c:\Users\alfre\Desktop\attendance_tracker\firmware`
  - **Toolchain / IDE: CMake** (required for VS Code workflow)
- Code Generator:
  - Check **Generate peripheral initialization as a pair of .c/.h files**
  - Check **Set all free pins as analog (to save power)**
- Click **GENERATE CODE**

## Part B: Open in VS Code and Integrate Firmware

### 1. Open the Project in VS Code

- Open VS Code
- Click the **STM32 butterfly icon** in the Activity Bar (left sidebar)
- Click **"Import CMake project"** (or open the generated folder directly)
- Navigate to the CubeMX-generated folder (where the .ioc file lives)
- VS Code will detect the CMake project and configure it automatically
- When prompted for a CMake preset, select **Debug**

### 2. Copy Firmware Files

Copy from this `firmware/Core/` directory into the generated project's `Core/`:

```
Core/Inc/ultrasonic.h     ->  <generated>/Core/Inc/ultrasonic.h
Core/Src/ultrasonic.c     ->  <generated>/Core/Src/ultrasonic.c
Core/Src/clock_config.c   ->  <generated>/Core/Src/clock_config.c
```

If you generated the project into `firmware/`, these files are already in the
right place. You just need to make sure `ultrasonic.c` and `clock_config.c` are
included in the CMake build. Check the generated `CMakeLists.txt` - if it uses
a glob for `Core/Src/*.c`, they'll be picked up automatically. If it lists files
explicitly, add the two new .c files to the source list.

### 3. Edit `main.c` (USER CODE regions only)

Open the generated `Core/Src/main.c` and paste these snippets
(see `main_user_code.c` for the full version):

**In `USER CODE BEGIN Includes`:**
```c
#include "ultrasonic.h"
```

**In `USER CODE BEGIN 2` (after all MX_xxx_Init() calls):**
```c
Ultrasonic_Init();
Ultrasonic_StartTX();
```

**In `USER CODE BEGIN 3` (inside while(1) loop):**
```c
Ultrasonic_ProcessEcho();
Ultrasonic_BuzzerUpdate();
```

### 4. Edit `stm32f0xx_it.c` (USER CODE regions only)

Open `Core/Src/stm32f0xx_it.c` and paste these snippets
(see `it_user_code.c` for details):

**In `USER CODE BEGIN Includes`:**
```c
#include "ultrasonic.h"
```

**In `USER CODE BEGIN TIM3_IRQn 0` (before HAL_TIM_IRQHandler):**
```c
Ultrasonic_TIM3_IRQHandler();
return;
```

**In `USER CODE BEGIN TIM14_IRQn 0` (before HAL_TIM_IRQHandler):**
```c
Ultrasonic_TIM14_IRQHandler();
return;
```

**In `USER CODE BEGIN TIM17_IRQn 1` (after HAL_TIM_IRQHandler):**
```c
Ultrasonic_TIM17_IRQHandler();
```

### 5. Build

- Press **Ctrl+Shift+B** (or Ctrl+Shift+P -> "CMake: Build")
- Output ELF will be in `build/Debug/`

### 6. Flash with ST-Link V2

Connect ST-Link V2 to the STM32F030F4P6:
- SWDIO -> PA13
- SWCLK -> PA14
- GND -> GND
- 3.3V -> VDD (or power the board separately)

Then in VS Code:
- **Ctrl+Shift+P** -> **"STM32: Flash STM32"**
- Or press **F5** to build, flash, and start debugging in one step

### 7. Debug

- Set breakpoints by clicking the left margin of a line
- Press **F5** to start debugging (builds + flashes + breaks at main)
- **F10** = step over, **F11** = step into, **Shift+F11** = step out
- Watch variables in the Variables panel
- View peripheral registers in the Cortex Peripherals panel

## Part C: Alternative Clock Sources

If using a different crystal or internal oscillator:

1. Change `CLOCK_SOURCE` in `ultrasonic.h`:
   ```c
   #define CLOCK_SOURCE  CLOCK_HSE_8MHZ   // for 8 MHz crystal
   // or
   #define CLOCK_SOURCE  CLOCK_HSI_8MHZ   // for internal oscillator
   ```

2. Add this in `main.c` `USER CODE BEGIN 2`, **before** `Ultrasonic_Init()`:
   ```c
   SystemClock_UserConfig();
   ```

Alternatively, just reconfigure the CubeMX clock tree for your crystal
frequency and regenerate. Then you don't need `clock_config.c` at all.

## Pin Summary

```
STM32F030F4P6 (TSSOP20)
========================

Pin 12 - PA9  -> TIM1_CH2 (AF2)   TX frequency output
Pin 10 - PA6  -> TIM3_CH1 (AF1)   TX burst gate
Pin  8 - PA2  -> GPIO Output       TX enable (inverted)
Pin 14 - PB1  -> TIM14_CH1 (AF0)  Echo input capture
Pin  9 - PA4  -> GPIO Output       Buzzer

ST-Link V2 connections:
Pin 19 - PA13 -> SWDIO
Pin 20 - PA14 -> SWCLK
```

## Troubleshooting

- **PA9 not outputting**: TIM1 is an advanced timer - needs MOE (main output enable). The firmware calls `__HAL_TIM_MOE_ENABLE()`.
- **PA6 never goes high**: Verify TIM3 slave mode = External Clock Mode 1 with ITR0. Check TIM1 MMS = OC2REF.
- **Echo not capturing**: Verify PB1 is TIM14_CH1 (AF0), not TIM3_CH4 (AF1).
- **No burst repetition**: Check TIM17 is running and its interrupt is enabled.
- **PA2 not toggling**: Check TIM3 update interrupt and TIM17 interrupt are both enabled.
- **ST-Link not detected**: Install drivers from STM32 extension -> Resources -> ST-Link USB Drivers.
- **CMake errors after CubeMX regenerate**: Check that `ultrasonic.c` and `clock_config.c` are still in the source list.
