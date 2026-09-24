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
#include <stdio.h>

static void SystemClock_Config(void);
static void LCD_Task(void *argument);
static void LED_Task(void *argument);

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

static void LED_Task(void *argument)
{
  (void)argument;
  for (;;)
  {
    BSP_LED_Toggle(LED3);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

static void LCD_Task(void *argument)
{
  (void)argument;
  char line[32];
  uint32_t seconds = 0;
  uint16_t x = 0;
  int8_t dx = 4;
  const uint16_t boxSize = 20;

  BSP_LCD_Init();
  BSP_LCD_LayerDefaultInit(0, LCD_FRAME_BUFFER);
  BSP_LCD_SelectLayer(0);
  BSP_LCD_SetFont(&Font24);
  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
  BSP_LCD_SetTextColor(LCD_COLOR_BLUE);
  BSP_LCD_Clear(LCD_COLOR_WHITE);
  BSP_LCD_DisplayStringAt(0, 10, (uint8_t *)"FreeRTOS + LTDC Demo", CENTER_MODE);
  BSP_LCD_DisplayStringAt(0, 45, (uint8_t *)"STM32F429I-Discovery", CENTER_MODE);

  for (;;)
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
