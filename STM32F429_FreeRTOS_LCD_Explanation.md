# STM32F429I-Discovery: FreeRTOS + LCD demo — how it works

## Program description

A complete, self-contained firmware project for the STM32F429I-Discovery
board, built entirely with free/open-source tools (GNU Arm Embedded Toolchain
+ GNU Make + pyOCD — no STM32CubeMX, no paid IDE).

**What it does at runtime:**
- `LED_Task` (FreeRTOS task) blinks LED3 every 500 ms.
- `LCD_Task` (FreeRTOS task) initializes the LTDC/ILI9341 display (240x320)
  and the resistive touchscreen (STMPE811), then continuously:
  - Draws a title, a live "Uptime: Ns" counter, and an animated bouncing box.
  - Draws two stacked on-screen touch buttons at the bottom of the screen:
    - **Clear** — non-destructive toggle: blanks the screen / restores the
      demo. The program keeps running.
    - **Shutdown** — one-way, permanent halt: blanks the screen, turns off
      the LCD panel and both LEDs, disables SysTick, then masks all CPU
      interrupts (`__disable_irq()`) and spins forever. Only a hardware
      reset or reflash brings the board back.

**Build variants** (`Makefile`):
- `make` → `build/stm32F429freertos-lcd-demo.elf/.hex/.bin` — the full
  program described above.
- `make clear` → `build-clear/clear-display.elf` — a minimal firmware
  (`CLEAR_DISPLAY_ONLY` build of `Core/Src/main.c`) that only blanks the LCD
  and halts. Used after a flash erase, since erasing flash doesn't clear the
  LCD panel's own display memory.

**VS Code tasks** (`.vscode/tasks.json`):
- `Build Firmware` — `make`.
- `Start pyOCD GDB Server` — background task, local pyOCD gdbserver
  (`--target stm32f429xi`), no OpenOCD.
- `Build Clear-Display Firmware` — `make clear`.
- `Erase Board` — full chip erase (with a retry, since pyOCD's erase
  occasionally reports a transient error), then flashes and runs the
  clear-display firmware so the board ends up genuinely blank.

**VS Code debug** (`.vscode/launch.json`): `STM32F429 Cortex-Debug (ST-LINK)`
config using the `cortex-debug` extension.

## How and where to access it

- **Workspace / git worktree path (this machine):**
  `C:\Users\Rudolf Krejcik\copilot-worktrees\32f429idiscovery-bsp\rkkarat-refactored-system`
- **Git branch:** `freertos-lcd-demo`, latest commit `670d768`
  ("Add touch menu (Clear/Shutdown buttons), Erase Board task, clear-display
  build variant, and updated docs") — local only, not pushed to any remote;
  `origin` points to ST's official upstream repo, not a personal fork.
  ```powershell
  git checkout freertos-lcd-demo
  git log --oneline
  ```
- **Build** (requires `arm-none-eabi-gcc` + GNU `make` on `PATH`):
  ```powershell
  make          # build/stm32F429freertos-lcd-demo.elf/.hex/.bin
  make clear    # build-clear/clear-display.elf
  make clean
  ```
- **Flash / run / debug on real hardware** — no OpenOCD, no paid tools:
  - In VS Code: **Run and Debug** → `STM32F429 Cortex-Debug (ST-LINK)`, or
    run the **Erase Board** task (Terminal → Run Task…) to wipe and re-blank
    the board.
  - Manually with pyOCD + local gdb:
    ```powershell
    pyocd gdbserver --target stm32f429xi
    arm-none-eabi-gdb build/stm32F429freertos-lcd-demo.elf
    (gdb) target extended-remote localhost:3333
    (gdb) monitor reset halt
    (gdb) load
    (gdb) monitor reset
    ```
- **On the board itself:** once flashed and running, use the two on-screen
  touch buttons ("Clear" / "Shutdown") described above — no PC connection
  needed to operate them.

## Where this program lives

- Git worktree (workspace folder):
  `C:\Users\Rudolf Krejcik\copilot-worktrees\32f429idiscovery-bsp\rkkarat-refactored-system`
- Branch: **`freertos-lcd-demo`**
- Remote `origin` points to ST's official upstream repo
  (`https://github.com/STMicroelectronics/32f429idiscovery-bsp.git`); this
  branch has **not been pushed** anywhere — it only exists locally.
- Key paths inside the repo: `Core/Src/main.c` (application logic),
  `Makefile` (build), `.vscode/` (build/flash/debug tasks), `README.md`
  (full project structure and dependency sourcing).

## The bug that caused "only LED blinks, no display"

FreeRTOS's Cortex-M port takes over `SysTick_Handler` to drive its scheduler
tick. Nothing was updating HAL's own tick counter, so `HAL_GetTick()` /
`HAL_Delay()` stayed frozen at 0 forever. The LCD init routine
(`stm32f429i_discovery_lcd.c`) uses `HAL_Delay()` during the ILI9341 startup
sequence, so `LCD_Task` hung on its very first delay call — while `LED_Task`
(which only uses FreeRTOS's own `vTaskDelay`, no HAL timing) kept blinking
fine. That's why only the LED worked and the display stayed blank.

## The fix

`Core/Src/stm32f4xx_it.c` now defines its own `SysTick_Handler` that:
1. Calls `HAL_IncTick()` — keeps `HAL_Delay()` / `HAL_GetTick()` alive.
2. Calls FreeRTOS's real tick handler (`xPortSysTickHandler`, remapped in
   `FreeRTOSConfig.h`) — but only once `vTaskStartScheduler()` has actually
   started the scheduler, so FreeRTOS's internal tick count isn't touched
   before it's initialized.

## How the system runs

- `main()` configures the clock (HSI → PLL → 180 MHz core, with the PLLSAI
  clock the LCD driver derives from it), initializes FreeRTOS, and starts two
  tasks.
- `LED_Task` toggles LED3 every 500 ms via `vTaskDelay`.
- `LCD_Task` initializes the LTDC/ILI9341 display (now that `HAL_Delay()`
  works correctly), then loops drawing a live uptime counter and an animated
  bouncing box.
- Both tasks run cooperatively/preemptively under the FreeRTOS scheduler, with
  SysTick now correctly serving both HAL timing and the RTOS tick.

## Related fix: Makefile stale-object bug

The Makefile originally had no header-dependency tracking, so editing a
shared header (like `FreeRTOSConfig.h`) didn't force dependent `.c` files
(like FreeRTOS's `port.c`) to recompile — stale `.o` files got silently
relinked. Fixed by adding `-MMD -MP` to the compiler flags and including the
generated `.d` dependency files in the Makefile.

## Project structure

- `Core/Inc`, `Core/Src` — application code: `main.c`, `stm32f4xx_it.c`,
  `stm32f4xx_hal_conf.h`, `FreeRTOSConfig.h`, `system_stm32f4xx.c`.
- `Drivers/CMSIS`, `Drivers/STM32F4xx_HAL_Driver` — ST's CMSIS device support
  and HAL, fetched from ST's official free GitHub repos.
- `Drivers/BSP/STM32F429I-Discovery`, `Drivers/BSP/Components` — the board's
  BSP (LCD/touch/SDRAM/etc.) and the legacy-API display/touch/gyro component
  drivers matched to it.
- `Middlewares/Third_Party/FreeRTOS` — the FreeRTOS kernel (MIT license).
- `Utilities/Fonts` — font bitmaps used by the LCD driver.
- `Makefile`, `STM32F429ZITx_FLASH.ld` — free GNU Arm toolchain build/link
  setup (no CubeMX/.ioc, no paid IDE).
- `.vscode/tasks.json`, `.vscode/launch.json` — build + local
  pyOCD/`arm-none-eabi-gdb` debug workflow (no OpenOCD).

## Git status note

As of this writing, only this explanation file and the `Drivers/BSP/STM32F429I-Discovery`
file renames have been committed. Everything else listed above (`Core/`,
`Drivers/CMSIS/`, `Drivers/STM32F4xx_HAL_Driver/`, `Drivers/BSP/Components/`,
`Middlewares/`, `Utilities/`, `Makefile`, `STM32F429ZITx_FLASH.ld`, `.vscode/`,
`.gitignore`) is still untracked/uncommitted — run `git add -A && git commit`
to save the full project into version control.
