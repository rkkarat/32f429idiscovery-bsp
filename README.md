# BSP STM32F429I-DISCOVERY Component

![latest tag](https://img.shields.io/github/v/tag/STMicroelectronics/stm32f429i-discovery.svg?color=brightgreen)

## FreeRTOS + LCD demo project

This repository has been extended from the standalone BSP component into a
complete, buildable firmware project for the STM32F429I-Discovery board:
FreeRTOS running two tasks, one of which drives the onboard LTDC/ILI9341 LCD.
Everything is built with free, open-source tools only — no CubeMX/.ioc file,
no paid IDE or compiler.

See [STM32F429_FreeRTOS_LCD_Explanation.md](STM32F429_FreeRTOS_LCD_Explanation.md)
for the story of the SysTick/FreeRTOS integration bug and its fix.

### What it does

- `LED_Task` toggles LED3 every 500 ms.
- `LCD_Task` initializes the LTDC/ILI9341 display (240x320) and continuously
  draws a title, a live uptime counter, and an animated bouncing box. Tap
  **Gyroscope** to open a separate screen with signed X, Y, and Z angular
  rates in degrees per second (`dps`), updated about five times a second.
  Rotate the board to see the readings change, then tap **Back** to return.
  **Calibrate** is optional: set the board on a stable surface, tap the
  button, and keep it still for about three seconds. This zeros the resting
  X, Y, and Z offsets until reset. If the readings vary too much, the screen
  reports `Motion - try again` and keeps the previous offsets.
  If the sensor is not detected, that screen displays `Gyro unavailable`.
- A dedicated `Gyro` FreeRTOS task polls for fresh sensor data once per RTOS
  tick (1 ms). The L3GD20 uses its highest output-data-rate setting; the LCD
  still refreshes at about 5 Hz. Tap **Start log** on the Gyroscope screen to
  start a new capture, and **Stop log** to freeze it. Recording continues
  until stopped even if you leave the Gyroscope screen.
- After stopping, pause the debugger and expand `gyroLog` in VS Code Watch.
  The buffer keeps the latest 50 sets in RAM. Each valid entry contains the
  RTOS `tick` and the uncalibrated `xMdps`, `yMdps`, and `zMdps` readings
  (divide by 1,000 for degrees/second). If `count` is nonzero, the oldest
  entry is at `(nextWrite + 50 - count) % 50`; read `count` entries, wrapping
  at 50. Starting another capture replaces the previous history.

### Project structure

| Path | Contents |
|---|---|
| `Core/Inc`, `Core/Src` | Application code: `main.c`, `stm32f4xx_it.c`, `stm32f4xx_hal_conf.h`, `FreeRTOSConfig.h`, `system_stm32f4xx.c` |
| `Drivers/CMSIS` | ARM CMSIS core headers + ST's `cmsis_device_f4` (device headers, startup file) |
| `Drivers/STM32F4xx_HAL_Driver` | ST's STM32F4 HAL/LL drivers |
| `Drivers/BSP/STM32F429I-Discovery` | Board BSP: LCD, touchscreen, SDRAM, IO, EEPROM, gyroscope |
| `Drivers/BSP/Components` | Legacy-API display/touch/gyro component drivers (`ili9341`, `stmpe811`, `l3gd20`, `Common`) matched to the BSP above |
| `Middlewares/Third_Party/FreeRTOS` | FreeRTOS kernel (MIT), Cortex-M4F GCC port, `heap_4` allocator |
| `Utilities/Fonts` | Font bitmaps used by the LCD driver |
| `Makefile`, `STM32F429ZITx_FLASH.ld` | Free GNU Arm Embedded Toolchain (`arm-none-eabi-gcc`) build + link setup |
| `.vscode/tasks.json`, `.vscode/launch.json` | Build task + local debug workflow (see below) |

### Dependency sourcing

All dependencies were fetched directly from their official free/open-source
GitHub repositories (not via STM32CubeMX):

- FreeRTOS kernel: [FreeRTOS/FreeRTOS-Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel) (MIT license)
- CMSIS + HAL: [STMicroelectronics/cmsis_device_f4](https://github.com/STMicroelectronics/cmsis_device_f4), [STMicroelectronics/stm32f4xx_hal_driver](https://github.com/STMicroelectronics/stm32f4xx_hal_driver)
- Fonts, and the **legacy-API** `ili9341`/`stmpe811`/`l3gd20`/`Common` component
  drivers required by this BSP's `LCD_DrvTypeDef`/`TS_DrvTypeDef` interfaces,
  pulled from an older `STM32CubeF4` tag (`v1.26.2`), since the current
  per-component GitHub repos ship an incompatible newer API.

### Building

Requires `arm-none-eabi-gcc` and GNU `make` on `PATH`.

```powershell
make            # builds build/stm32F429freertos-lcd-demo.elf/.hex/.bin
make clean
```

### Flashing / debugging (no OpenOCD, no paid tools)

VS Code uses the ST-LINK GDB server and STM32CubeProgrammer bundled with
STM32CubeIDE. The manual workflow below still uses [pyOCD](https://github.com/pyocd/pyOCD).

- In VS Code: Run and Debug → **STM32F429 Cortex-Debug (ST-LINK)** (builds
  and flashes automatically via `.vscode/tasks.json` + `launch.json`).
  Cortex-Debug discovers the ST-LINK server and programmer in the standard
  STM32CubeIDE installation; no separate GDB-server task is needed.
  The firmware starts without pausing at `main`; set a breakpoint there if
  you want to step through initialization. If it still stops at `main`, disable
  any existing breakpoint there in VS Code's Breakpoints panel and relaunch.
  On Windows, `windows.gdbPath` in [launch.json](.vscode/launch.json) selects the
  STM32CubeIDE-bundled GNU Tools for STM32 14.3 GDB to avoid the CP1252 conversion
  warning in Arm GNU Toolchain 14.2. Update that path if CubeIDE is installed
  elsewhere. If GDB reports that source is newer than the executable, stop the
  session, run `make -B` once, and relaunch.
- Manually:
  ```powershell
  pyocd gdbserver --target stm32f429xi
  arm-none-eabi-gdb build/stm32F429freertos-lcd-demo.elf
  (gdb) target extended-remote localhost:3333
  (gdb) monitor reset halt
  (gdb) load
  (gdb) monitor reset
  ```

### Known display constraint

The ILI9341 panel is 240px wide. `BSP_LCD_DisplayStringAt()`'s `CENTER_MODE`
math underflows (unsigned wraparound) for strings that don't fit the current
font's width — e.g. at `Font24` (17px/char) only 14 characters fit per line.
Longer strings must use a narrower font (`Font16` = 11px/char, fits 21 chars).

## Overview

**STM32Cube** is an STMicroelectronics original initiative to ease developers' life by reducing efforts, time and cost.

**STM32Cube** covers the overall STM32 products portfolio. It includes a comprehensive embedded software platform delivered for each STM32 series.
   * The CMSIS modules (core and device) corresponding to the ARM(tm) core implemented in this STM32 product.
   * The STM32 HAL-LL drivers, an abstraction layer offering a set of APIs ensuring maximized portability across the STM32 portfolio.
   * The BSP drivers of each evaluation, demonstration or nucleo board provided for this STM32 series.
   * A consistent set of middleware libraries such as RTOS, USB, FatFS, graphics, touch sensing library...
   * A full set of software projects (basic examples, applications, and demonstrations) for each board provided for this STM32 series.

Two models of publication are proposed for the STM32Cube embedded software:
   * The monolithic **MCU Package**: all STM32Cube software modules of one STM32 series are present (Drivers, Middleware, Projects, Utilities) in the repository (usual name **STM32Cubexx**, xx corresponding to the STM32 series).
   * The **MCU component**: each STM32Cube software module being part of the STM32Cube MCU Package, is delivered as an individual repository, allowing the user to select and get only the required software functions.

## Description

This **stm32f429i-discovery** MCU component repository is one element of the **STM32CubeF4** MCU embedded software package, providing the **STM32F429I-DISCOVERY** BSP BOARD component part.

## Release note

Details about the content of this release are available in the release note [here](https://htmlpreview.github.io/?https://github.com/STMicroelectronics/stm32f429i-discovery/blob/main/Release_Notes.html).

## Compatibility information

Below is the list of the BSP *component* drivers to be used with this BSP *board* driver. It is **crucial** that you use a consistent set of CMSIS - HAL - BSP versions, as mentioned in [this](https://htmlpreview.github.io/?https://github.com/STMicroelectronics/STM32CubeF4/blob/master/Release_Notes.html) release note.

* [stm32-bsp-common](https://github.com/STMicroelectronics/stm32-bsp-common)
* [stm32-ili9341](https://github.com/STMicroelectronics/stm32-ili9341)
* [stm32-l3gd20](https://github.com/STMicroelectronics/stm32-l3gd20)
* [stm32-stmpe811](https://github.com/STMicroelectronics/stm32-stmpe811)

## Troubleshooting

Please refer to the [CONTRIBUTING.md](CONTRIBUTING.md) guide.
