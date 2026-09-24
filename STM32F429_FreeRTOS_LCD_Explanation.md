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
