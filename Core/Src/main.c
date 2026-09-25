/**
  * FreeRTOS + onboard LCD demo for the STM32F429I-Discovery board.
  * - Blinks LED3/LED4 from a FreeRTOS task.
  * - Draws a live counter and a bouncing rectangle on the LTDC/ILI9341 LCD
  *   from a second FreeRTOS task.
  */
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f429i_discovery.h"
#include "stm32f429i_discovery_lcd.h"
#include "stm32f429i_discovery_sdram.h"
#include "stm32f429i_discovery_ts.h"
#include <stdio.h>

/* On-screen touch menu buttons, in LCD pixel coordinates (panel is 240x320).
   Stacked vertically, centered, "Clear" above "Shutdown". */
#define CLEAR_BTN_X      60
#define CLEAR_BTN_Y      230
#define CLEAR_BTN_WIDTH  120
#define CLEAR_BTN_HEIGHT 35

#define SHUTDOWN_BTN_X      60
#define SHUTDOWN_BTN_Y      275
#define SHUTDOWN_BTN_WIDTH  120
#define SHUTDOWN_BTN_HEIGHT 35

static void SystemClock_Config(void);
static void LCD_Task(void *argument);
static void LED_Task(void *argument);

#ifdef CLEAR_DISPLAY_ONLY
/* Minimal firmware variant: init the LCD only far enough to blank it, then halt.
   Used after erasing the board, since a flash erase alone doesn't clear the LCD's
   own display memory - it just stops the program that was driving it. */
int main(void)
{
  HAL_Init();
  SystemClock_Config();

  BSP_LCD_Init();
  BSP_LCD_LayerDefaultInit(0, LCD_FRAME_BUFFER);
  BSP_LCD_SelectLayer(0);
  BSP_LCD_Clear(LCD_COLOR_BLACK);
  BSP_LCD_DisplayOff();

  while (1) { }
}
#else
int main(void)
{
  HAL_Init();
  SystemClock_Config();

  BSP_LED_Init(LED3);
  BSP_LED_Init(LED4);

  xTaskCreate(LED_Task, "LED", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
  xTaskCreate(LCD_Task, "LCD", configMINIMAL_STACK_SIZE * 4, NULL, tskIDLE_PRIORITY + 1, NULL);

  vTaskStartScheduler();

  /* Only reached if there is insufficient heap to start the scheduler. */
  while (1) { }
}
#endif /* CLEAR_DISPLAY_ONLY */

static void LED_Task(void *argument)
{
  (void)argument;
  for (;;)
  {
    BSP_LED_Toggle(LED3);
    BSP_LED_Toggle(LED4);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

static void LCD_DrawClearButton(uint8_t cleared)
{
  BSP_LCD_SetTextColor(LCD_COLOR_DARKGRAY);
  BSP_LCD_FillRect(CLEAR_BTN_X, CLEAR_BTN_Y, CLEAR_BTN_WIDTH, CLEAR_BTN_HEIGHT);
  BSP_LCD_SetBackColor(LCD_COLOR_DARKGRAY);
  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
  BSP_LCD_SetFont(&Font16);
  BSP_LCD_DisplayStringAt(0, CLEAR_BTN_Y + 10, cleared ? (uint8_t *)"Show" : (uint8_t *)"Clear", CENTER_MODE);
  BSP_LCD_SetFont(&Font24);
}

static void LCD_DrawShutdownButton(void)
{
  BSP_LCD_SetTextColor(LCD_COLOR_DARKRED);
  BSP_LCD_FillRect(SHUTDOWN_BTN_X, SHUTDOWN_BTN_Y, SHUTDOWN_BTN_WIDTH, SHUTDOWN_BTN_HEIGHT);
  BSP_LCD_SetBackColor(LCD_COLOR_DARKRED);
  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
  BSP_LCD_SetFont(&Font16);
  BSP_LCD_DisplayStringAt(0, SHUTDOWN_BTN_Y + 10, (uint8_t *)"Shutdown", CENTER_MODE);
  BSP_LCD_SetFont(&Font24);
}

static void LCD_DrawDemoScreen(void)
{
  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
  BSP_LCD_SetTextColor(LCD_COLOR_BLUE);
  BSP_LCD_Clear(LCD_COLOR_WHITE);
  /* Font16 (11px/char) is required here: the panel is 240px wide, and BSP_LCD_DisplayStringAt's
     centering math underflows (uint32_t wraparound) for strings that don't fit at Font24 (17px/char). */
  BSP_LCD_SetFont(&Font16);
  BSP_LCD_DisplayStringAt(0, 10, (uint8_t *)"FreeRTOS + LTDC Demo", CENTER_MODE);
  BSP_LCD_DisplayStringAt(0, 30, (uint8_t *)"STM32F429I-Discovery", CENTER_MODE);
  BSP_LCD_SetFont(&Font24);
  LCD_DrawClearButton(0);
  LCD_DrawShutdownButton();
}

static void LCD_Task(void *argument)
{
  (void)argument;
  char line[32];
  uint32_t seconds = 0;
  uint16_t x = 0;
  int8_t dx = 4;
  const uint16_t boxSize = 20;
  uint8_t cleared = 0;
  uint8_t prevClearTouch = 0;
  TS_StateTypeDef tsState;

  BSP_LCD_Init();
  BSP_LCD_LayerDefaultInit(0, LCD_FRAME_BUFFER);
  BSP_LCD_SelectLayer(0);
  BSP_TS_Init(BSP_LCD_GetXSize(), BSP_LCD_GetYSize());
  /* A fixed handful of throwaway reads wasn't enough: the STMPE811 can keep
     reporting a stale/phantom touch for longer than that right after init,
     which swallows the very first real tap (typically "Shutdown", since
     "Clear" happening to be tapped first "used up" the stale reading instead).
     Instead, actively wait until the controller reports a clean run of
     consecutive "no touch" polls before trusting it, with a timeout in case
     a finger is already resting on the panel at boot. */
  for (uint16_t settled = 0, tries = 0; settled < 10 && tries < 100; tries++)
  {
    BSP_TS_GetState(&tsState);
    settled = tsState.TouchDetected ? 0 : (settled + 1);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  LCD_DrawDemoScreen();

  for (;;)
  {
    BSP_TS_GetState(&tsState);
    uint8_t touchOnShutdown = tsState.TouchDetected
        && tsState.X >= SHUTDOWN_BTN_X && tsState.X < (SHUTDOWN_BTN_X + SHUTDOWN_BTN_WIDTH)
        && tsState.Y >= SHUTDOWN_BTN_Y && tsState.Y < (SHUTDOWN_BTN_Y + SHUTDOWN_BTN_HEIGHT);
    uint8_t touchOnClear = tsState.TouchDetected
        && tsState.X >= CLEAR_BTN_X && tsState.X < (CLEAR_BTN_X + CLEAR_BTN_WIDTH)
        && tsState.Y >= CLEAR_BTN_Y && tsState.Y < (CLEAR_BTN_Y + CLEAR_BTN_HEIGHT);

    /* Halt the whole system permanently when the "Shutdown" menu button is tapped. */
    if (touchOnShutdown)
    {
      BSP_LCD_Clear(LCD_COLOR_BLACK);
      BSP_LCD_DisplayOff();
      BSP_LED_Off(LED3);
      BSP_LED_Off(LED4);
      /* taskDISABLE_INTERRUPTS() only masks interrupts up to FreeRTOS's syscall
         priority (via BASEPRI) - SysTick still fires and keeps other tasks
         (like LED_Task) running. Fully stop the CPU: disable SysTick itself,
         then mask ALL exceptions (PRIMASK) so nothing can preempt this loop. */
      SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
      __disable_irq();
      for (;;) { }
    }

    /* Toggle between the demo screen and a blank display when the "Clear" menu button is tapped. */
    if (touchOnClear && !prevClearTouch)
    {
      cleared = !cleared;
      if (cleared)
      {
        BSP_LCD_Clear(LCD_COLOR_BLACK);
        LCD_DrawClearButton(1);
        LCD_DrawShutdownButton();
      }
      else
      {
        LCD_DrawDemoScreen();
      }

      /* Wait for the finger to fully lift before returning to the loop, so a
         lingering/held touch from this tap can't be misread as (or block) the
         next distinct tap - e.g. on the "Shutdown" button right below it. */
      do
      {
        vTaskDelay(pdMS_TO_TICKS(20));
        BSP_TS_GetState(&tsState);
      } while (tsState.TouchDetected);
      touchOnClear = 0;
    }
    prevClearTouch = touchOnClear;

    if (!cleared)
    {
      snprintf(line, sizeof(line), "Uptime: %lus", (unsigned long)seconds);
      BSP_LCD_SetTextColor(LCD_COLOR_BLUE);
      BSP_LCD_ClearStringLine(3);
      BSP_LCD_DisplayStringAt(0, 90, (uint8_t *)line, CENTER_MODE);

      /* Erase previous box, move it, draw it again to animate the screen. */
      BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
      BSP_LCD_FillRect(x, 150, boxSize, boxSize);
      x = (uint16_t)(x + dx);
      if (x >= (BSP_LCD_GetXSize() - boxSize) || x == 0)
      {
        dx = (int8_t)-dx;
      }
      BSP_LCD_SetTextColor(LCD_COLOR_RED);
      BSP_LCD_FillRect(x, 150, boxSize, boxSize);

      seconds++;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

/* Configure the system clock from the internal HSI oscillator: 16 MHz / 16 * 180 / 2 = 90 MHz.
   Using HSI keeps the board runnable regardless of whether the HSE crystal is populated. */
static void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 180;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 8;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

void Error_Handler(void)
{
  __disable_irq();
  while (1) { }
}

void vApplicationMallocFailedHook(void)
{
  Error_Handler();
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;
  (void)pcTaskName;
  Error_Handler();
}
