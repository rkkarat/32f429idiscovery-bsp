/**
  * FreeRTOS + onboard LCD demo for the STM32F429I-Discovery board.
  * - Blinks LED3/LED4 from a FreeRTOS task.
  * - Draws a live counter and a bouncing rectangle on the LTDC/ILI9341 LCD
  *   from a second FreeRTOS task.
  * - Offers a gyroscope screen with live X/Y/Z angular rates.
  */
#include "main.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "stm32f429i_discovery.h"
#include "stm32f429i_discovery_gyroscope.h"
#include "stm32f429i_discovery_lcd.h"
#include "stm32f429i_discovery_sdram.h"
#include "stm32f429i_discovery_ts.h"
#include <stdio.h>

/* Centered buttons on the 240x320 LCD. */
#define MENU_BTN_X       60
#define MENU_BTN_WIDTH   120
#define MENU_BTN_HEIGHT  35
#define GYRO_BTN_Y       185
#define CLEAR_BTN_Y      230
#define SHUTDOWN_BTN_Y   275
#define BACK_BTN_Y       275
#define CALIBRATE_BTN_Y  230
#define LOG_BTN_Y        48

#define GYRO_TEXT_Y         90
#define GYRO_TEXT_ROW_STEP  28
#define GYRO_STATUS_Y       185
#define GYRO_CAL_SAMPLES    10
#define GYRO_CAL_SPAN_MDPS  3000.0f
#define GYRO_CAL_LIMIT_MDPS 10000.0f
#define GYRO_LOG_CAPACITY   50U

typedef struct
{
  float sum[3];
  float min[3];
  float max[3];
  uint8_t count;
} GyroCalibration;

static void SystemClock_Config(void);

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
typedef struct
{
  TickType_t tick;
  float xMdps;
  float yMdps;
  float zMdps;
} GyroSample;

typedef struct
{
  GyroSample samples[GYRO_LOG_CAPACITY];
  uint8_t count;
  uint8_t nextWrite;
  uint8_t recording;
} GyroLog;

static GyroLog gyroLog;
static GyroSample gyroLatest;
static uint8_t gyroLatestValid;
static SemaphoreHandle_t lcdGyroSpiMutex;

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
    BSP_LED_Toggle(LED4);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

static void LCD_DrawButton(uint16_t y, uint32_t color, const char *label)
{
  BSP_LCD_SetTextColor(color);
  BSP_LCD_FillRect(MENU_BTN_X, y, MENU_BTN_WIDTH, MENU_BTN_HEIGHT);
  BSP_LCD_SetBackColor(color);
  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
  BSP_LCD_SetFont(&Font16);
  BSP_LCD_DisplayStringAt(0, y + 10, (uint8_t *)label, CENTER_MODE);
  BSP_LCD_SetFont(&Font24);
}

static uint8_t LCD_TouchInButton(const TS_StateTypeDef *touch, uint16_t y)
{
  return touch->X >= MENU_BTN_X && touch->X < MENU_BTN_X + MENU_BTN_WIDTH
      && touch->Y >= y && touch->Y < y + MENU_BTN_HEIGHT;
}

static void LCD_DrawDemoScreen(void)
{
  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
  BSP_LCD_SetTextColor(LCD_COLOR_BLUE);
  BSP_LCD_Clear(LCD_COLOR_WHITE);
  /* Font16 (11px/char) is required here: the panel is 240px wide, and BSP_LCD_DisplayStringAt's
     centering math underflows (uint32_t wraparound) for strings that don't fit at Font24 (17px/char). */
  BSP_LCD_SetFont(&Font12 );
  BSP_LCD_DisplayStringAt(0, 10, (uint8_t *)"FreeRTOS-LTDC-GyroscopeDemo", CENTER_MODE);
  BSP_LCD_DisplayStringAt(0, 30, (uint8_t *)"STM32F429I-Discovery", CENTER_MODE);
  BSP_LCD_SetFont(&Font24);
  LCD_DrawButton(GYRO_BTN_Y, LCD_COLOR_BLUE, "Gyroscope");
  LCD_DrawButton(CLEAR_BTN_Y, LCD_COLOR_DARKGRAY, "Clear");
  LCD_DrawButton(SHUTDOWN_BTN_Y, LCD_COLOR_DARKRED, "Shutdown");
}

static void GyroLog_Toggle(void)
{
  taskENTER_CRITICAL();
  gyroLog.recording = !gyroLog.recording;
  if (gyroLog.recording)
  {
    gyroLog.count = 0;
    gyroLog.nextWrite = 0;
  }
  taskEXIT_CRITICAL();
}

static void Gyro_Publish(const GyroSample *sample)
{
  taskENTER_CRITICAL();
  gyroLatest = *sample;
  gyroLatestValid = 1;
  if (gyroLog.recording)
  {
    gyroLog.samples[gyroLog.nextWrite] = *sample;
    gyroLog.nextWrite = (gyroLog.nextWrite + 1U) % GYRO_LOG_CAPACITY;
    if (gyroLog.count < GYRO_LOG_CAPACITY)
    {
      gyroLog.count++;
    }
  }
  taskEXIT_CRITICAL();
}

static uint8_t Gyro_GetLatest(GyroSample *sample)
{
  uint8_t valid;
  taskENTER_CRITICAL();
  valid = gyroLatestValid;
  *sample = gyroLatest;
  taskEXIT_CRITICAL();
  return valid;
}

static void Gyro_Task(void *argument)
{
  (void)argument;
  for (;;)
  {
    /* LCD panel commands also use SPI5; framebuffer drawing does not. */
    xSemaphoreTake(lcdGyroSpiMutex, portMAX_DELAY);
    const uint8_t ready = BSP_GYRO_DataReady();
    float rateMdps[3];
    if (ready)
    {
      BSP_GYRO_GetXYZ(rateMdps);
    }
    xSemaphoreGive(lcdGyroSpiMutex);

    if (ready)
    {
      const GyroSample sample = {xTaskGetTickCount(), rateMdps[0], rateMdps[1], rateMdps[2]};
      Gyro_Publish(&sample);
    }
    vTaskDelay(1);
  }
}

static void LCD_DrawLogButton(void)
{
  /* Only the LCD task changes recording. */
  const uint8_t recording = gyroLog.recording;
  LCD_DrawButton(LOG_BTN_Y, recording ? LCD_COLOR_DARKRED : LCD_COLOR_BLUE,
                 recording ? "Stop log" : "Start log");
}

static void LCD_DrawGyroscopeScreen(uint8_t gyroStatus)
{
  BSP_LCD_Clear(LCD_COLOR_WHITE);
  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
  BSP_LCD_SetTextColor(LCD_COLOR_BLUE);
  BSP_LCD_SetFont(&Font24);
  BSP_LCD_DisplayStringAt(0, 20, (uint8_t *)"Gyroscope", CENTER_MODE);
  if (gyroStatus == GYRO_OK)
  {
    LCD_DrawLogButton();
    LCD_DrawButton(CALIBRATE_BTN_Y, LCD_COLOR_BLUE, "Calibrate");
  }
  LCD_DrawButton(BACK_BTN_Y, LCD_COLOR_DARKGRAY, "Back");
}

static void LCD_DrawGyroStatus(const char *message)
{
  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
  BSP_LCD_FillRect(0, GYRO_STATUS_Y, BSP_LCD_GetXSize(), Font16.Height);
  BSP_LCD_SetTextColor(LCD_COLOR_BLUE);
  BSP_LCD_SetFont(&Font16);
  BSP_LCD_DisplayStringAt(0, GYRO_STATUS_Y, (uint8_t *)message, CENTER_MODE);
  BSP_LCD_SetFont(&Font24);
}

static void GyroCalibration_AddSample(GyroCalibration *cal, const float rateMdps[3])
{
  for (uint8_t i = 0; i < 3; i++)
  {
    if (cal->count == 0 || rateMdps[i] < cal->min[i])
    {
      cal->min[i] = rateMdps[i];
    }
    if (cal->count == 0 || rateMdps[i] > cal->max[i])
    {
      cal->max[i] = rateMdps[i];
    }
    cal->sum[i] += rateMdps[i];
  }
  cal->count++;
}

static uint8_t GyroCalibration_Apply(const GyroCalibration *cal, float biasMdps[3])
{
  for (uint8_t i = 0; i < 3; i++)
  {
    if (cal->min[i] < -GYRO_CAL_LIMIT_MDPS || cal->max[i] > GYRO_CAL_LIMIT_MDPS
        || cal->max[i] - cal->min[i] > GYRO_CAL_SPAN_MDPS)
    {
      return 0;
    }
  }

  for (uint8_t i = 0; i < 3; i++)
  {
    biasMdps[i] = cal->sum[i] / cal->count;
  }
  return 1;
}

static void LCD_DrawGyroscope(const float rateMdps[3], const float biasMdps[3])
{
  static const char axes[] = "XYZ";
  char line[24];

  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
  BSP_LCD_SetTextColor(LCD_COLOR_BLUE);
  BSP_LCD_SetFont(&Font16);
  for (uint16_t i = 0; i < 3; i++)
  {
    /* The BSP returns milli-degrees/s; round to tenths without printf float support. */
    float tenths = (rateMdps[i] - biasMdps[i]) / 100.0f;
    long rate = (long)(tenths + (tenths < 0.0f ? -0.5f : 0.5f));
    long magnitude = rate < 0 ? -rate : rate;
    snprintf(line, sizeof(line), "%c: %c%3ld.%ld dps",
             axes[i], rate < 0 ? '-' : '+', magnitude / 10, magnitude % 10);
    BSP_LCD_DisplayStringAt(0, GYRO_TEXT_Y + i * GYRO_TEXT_ROW_STEP,
                           (uint8_t *)line, CENTER_MODE);
  }
  BSP_LCD_SetFont(&Font24);
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
  uint8_t showingGyroscope = 0;
  uint8_t wasTouched = 1;
  uint8_t calibrating = 0;
  TickType_t calibrationStart = 0;
  TickType_t lastCalibrationTick = 0;
  GyroCalibration calibration = {0};
  float gyroBiasMdps[3] = {0};
  TS_StateTypeDef tsState;

  BSP_LCD_Init();
  BSP_LCD_LayerDefaultInit(0, LCD_FRAME_BUFFER);
  BSP_LCD_SelectLayer(0);
  BSP_TS_Init(BSP_LCD_GetXSize(), BSP_LCD_GetYSize());
  uint8_t gyroStatus = BSP_GYRO_Init();
  if (gyroStatus == GYRO_OK)
  {
    lcdGyroSpiMutex = xSemaphoreCreateMutex();
    if (lcdGyroSpiMutex == NULL
        || xTaskCreate(Gyro_Task, "Gyro", configMINIMAL_STACK_SIZE * 2, NULL,
                       tskIDLE_PRIORITY + 2, NULL) != pdPASS)
    {
      if (lcdGyroSpiMutex != NULL)
      {
        vSemaphoreDelete(lcdGyroSpiMutex);
        lcdGyroSpiMutex = NULL;
      }
      gyroStatus = GYRO_ERROR;
    }
  }
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
    wasTouched = tsState.TouchDetected;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  LCD_DrawDemoScreen();
  const TickType_t frameInterval = pdMS_TO_TICKS(200);
  TickType_t lastFrame = xTaskGetTickCount() - frameInterval;

  for (;;)
  {
    uint8_t redraw = 0;
    BSP_TS_GetState(&tsState);
    /* A held Back press must not activate Shutdown at the same position. */
    const uint8_t newTouch = tsState.TouchDetected && !wasTouched;
    wasTouched = tsState.TouchDetected;

    if (showingGyroscope)
    {
      if (newTouch && LCD_TouchInButton(&tsState, BACK_BTN_Y))
      {
        showingGyroscope = 0;
        calibrating = 0;
        LCD_DrawDemoScreen();
        redraw = 1;
      }
      else if (gyroStatus == GYRO_OK && newTouch && LCD_TouchInButton(&tsState, LOG_BTN_Y))
      {
        GyroLog_Toggle();
        LCD_DrawLogButton();
      }
      else if (gyroStatus == GYRO_OK && !calibrating && newTouch
               && LCD_TouchInButton(&tsState, CALIBRATE_BTN_Y))
      {
        calibration = (GyroCalibration){0};
        calibrating = 1;
        calibrationStart = xTaskGetTickCount();
        LCD_DrawGyroStatus("Keep board still");
      }
      if (calibrating && calibration.count == 0 && tsState.TouchDetected)
      {
        calibrationStart = xTaskGetTickCount();
      }
    }
    else if (!cleared && newTouch && LCD_TouchInButton(&tsState, GYRO_BTN_Y))
    {
      showingGyroscope = 1;
      LCD_DrawGyroscopeScreen(gyroStatus);
      if (gyroStatus != GYRO_OK)
      {
        LCD_DrawGyroStatus("Gyro unavailable");
      }
      redraw = 1;
    }

    /* Halt the whole system permanently when the "Shutdown" menu button is tapped. */
    else if (newTouch && LCD_TouchInButton(&tsState, SHUTDOWN_BTN_Y))
    {
      BSP_LCD_Clear(LCD_COLOR_BLACK);
      if (lcdGyroSpiMutex != NULL)
      {
        xSemaphoreTake(lcdGyroSpiMutex, portMAX_DELAY);
      }
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
    else if (newTouch && LCD_TouchInButton(&tsState, CLEAR_BTN_Y))
    {
      cleared = !cleared;
      if (cleared)
      {
        BSP_LCD_Clear(LCD_COLOR_BLACK);
        LCD_DrawButton(CLEAR_BTN_Y, LCD_COLOR_DARKGRAY, "Show");
        LCD_DrawButton(SHUTDOWN_BTN_Y, LCD_COLOR_DARKRED, "Shutdown");
      }
      else
      {
        LCD_DrawDemoScreen();
        redraw = 1;
      }
    }

    /* Poll touch independently of the slower animation/sensor refresh. */
    const TickType_t now = xTaskGetTickCount();
    if (!redraw && (now - lastFrame) < frameInterval)
    {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    lastFrame = now;

    if (showingGyroscope)
    {
      GyroSample sample;
      if (gyroStatus == GYRO_OK && Gyro_GetLatest(&sample))
      {
        const float rateMdps[3] = {sample.xMdps, sample.yMdps, sample.zMdps};
        if (calibrating && (now - calibrationStart) >= pdMS_TO_TICKS(600)
            && (calibration.count == 0 || lastCalibrationTick != sample.tick))
        {
          lastCalibrationTick = sample.tick;
          GyroCalibration_AddSample(&calibration, rateMdps);
          if (calibration.count == GYRO_CAL_SAMPLES)
          {
            const uint8_t applied = GyroCalibration_Apply(&calibration, gyroBiasMdps);
            LCD_DrawGyroStatus(applied ? "Calibrated" : "Motion - try again");
            calibrating = 0;
          }
        }
        LCD_DrawGyroscope(rateMdps, gyroBiasMdps);
      }
    }
    else if (!cleared)
    {
      snprintf(line, sizeof(line), "Uptime: %lus", (unsigned long)seconds);
      BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
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

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

#endif /* CLEAR_DISPLAY_ONLY */

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
