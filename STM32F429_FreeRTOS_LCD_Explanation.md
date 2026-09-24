# STM32F429I-Discovery: FreeRTOS + LCD demo — how it works

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
