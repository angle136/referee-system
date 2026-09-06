/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdbool.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct
{
  uint16_t adc_raw;
  uint16_t baseline;
  uint16_t tick_ms;
  uint8_t dx_level;
  uint8_t hit_event;
} ArmorDebugSample_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ARMOR_ID                  0U
#define ARMOR_DX_ACTIVE_HIGH      1U
#define ARMOR_HEARTBEAT_PERIOD_MS 50U
#define ARMOR_BASELINE_TIME_MS    500U
#define ARMOR_BASELINE_SAMPLES    64U
#define ARMOR_AX_THRESHOLD        250U
#define ARMOR_HIT_COOLDOWN_MS     100U
#define ARMOR_HIT_REPEAT_COUNT    3U
#define ARMOR_HIT_REPEAT_GAP_MS   5U
#define ARMOR_PACKET_SIZE         8U
#define ARMOR_DEBUG_SAMPLE_COUNT  256U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile uint32_t armor_run_count = 0U;
volatile uint32_t armor_adc_raw = 0U;
volatile uint32_t armor_dx_level = 0U;
volatile uint32_t armor_baseline = 0U;
volatile uint32_t armor_last_event = 0U;
volatile uint32_t armor_adc_error_count = 0U;
volatile uint32_t armor_tx_error_count = 0U;
volatile ArmorDebugSample_t armor_debug_samples[ARMOR_DEBUG_SAMPLE_COUNT];
volatile uint32_t armor_debug_write_index;
volatile uint32_t armor_debug_sample_count;

static uint8_t armor_seq;
static uint16_t armor_baseline_samples[ARMOR_BASELINE_SAMPLES];
static uint32_t armor_baseline_sum;
static uint32_t armor_baseline_count;
static uint32_t armor_baseline_index;
static uint32_t armor_baseline_start;
static uint32_t armor_last_hit_tick;
static uint32_t armor_last_heartbeat_tick;
static uint32_t armor_last_repeat_tick;
static uint8_t armor_repeat_remaining;
static uint8_t armor_repeat_event;
static bool armor_dx_was_active;
static bool armor_ax_was_high;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static uint32_t Armor_ReadAdcRaw(void);
static void Armor_SendPacket(uint8_t event);
static void Armor_UpdateLed(bool hit);
static void Armor_DebugRecord(uint32_t now, uint8_t hit_event);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
  {
    armor_adc_error_count++;
  }
  armor_baseline_start = HAL_GetTick();
  armor_last_heartbeat_tick = armor_baseline_start;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint32_t now = HAL_GetTick();
    uint32_t dx_level;
    bool dx_active;
    bool ax_high;
    uint8_t hit_event = 0U;

    armor_run_count++;
    armor_adc_raw = Armor_ReadAdcRaw();
    dx_level = (HAL_GPIO_ReadPin(DX_INPUT_GPIO_Port, DX_INPUT_Pin) == GPIO_PIN_SET) ? 1U : 0U;
    armor_dx_level = dx_level;
    dx_active = (dx_level == ARMOR_DX_ACTIVE_HIGH);

    if ((uint32_t)(now - armor_baseline_start) < ARMOR_BASELINE_TIME_MS)
    {
      if (armor_baseline_count < ARMOR_BASELINE_SAMPLES)
      {
        armor_baseline_samples[armor_baseline_index] = (uint16_t)armor_adc_raw;
        armor_baseline_sum += armor_adc_raw;
        armor_baseline_count++;
      }
      else
      {
        armor_baseline_sum -= armor_baseline_samples[armor_baseline_index];
        armor_baseline_samples[armor_baseline_index] = (uint16_t)armor_adc_raw;
        armor_baseline_sum += armor_adc_raw;
      }
      armor_baseline_index = (armor_baseline_index + 1U) % ARMOR_BASELINE_SAMPLES;
      armor_baseline = armor_baseline_sum / armor_baseline_count;
      armor_dx_was_active = dx_active;
      armor_ax_was_high = false;
    }
    else
    {
      uint32_t threshold = armor_baseline + ARMOR_AX_THRESHOLD;
      ax_high = (armor_adc_raw > threshold);
      if (armor_repeat_remaining != 0U &&
          (uint32_t)(now - armor_last_repeat_tick) >= ARMOR_HIT_REPEAT_GAP_MS)
      {
        Armor_SendPacket(armor_repeat_event);
        armor_last_repeat_tick = now;
        armor_repeat_remaining--;
      }

      if ((uint32_t)(now - armor_last_hit_tick) >= ARMOR_HIT_COOLDOWN_MS &&
          ((dx_active && !armor_dx_was_active) || (ax_high && !armor_ax_was_high)))
      {
        uint8_t event = (dx_active ? 1U : 0U) | (ax_high ? 2U : 0U);
        hit_event = event;
        armor_last_hit_tick = now;
        armor_last_event = event;
        armor_repeat_event = event;
        Armor_SendPacket(event);
        armor_repeat_remaining = ARMOR_HIT_REPEAT_COUNT - 1U;
        armor_last_repeat_tick = now;
        Armor_UpdateLed(true);
      }
      armor_dx_was_active = dx_active;
      armor_ax_was_high = ax_high;
    }

    if ((uint32_t)(now - armor_last_heartbeat_tick) >= ARMOR_HEARTBEAT_PERIOD_MS)
    {
      Armor_SendPacket(0U);
      armor_last_heartbeat_tick = now;
      Armor_UpdateLed(false);
    }

    Armor_DebugRecord(now, hit_event);

    HAL_Delay(1U);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
static uint32_t Armor_ReadAdcRaw(void)
{
  uint32_t adc_raw = armor_adc_raw;

  if (HAL_ADC_Start(&hadc1) != HAL_OK)
  {
    armor_adc_error_count++;
    return adc_raw;
  }

  if (HAL_ADC_PollForConversion(&hadc1, 10U) == HAL_OK)
  {
    adc_raw = HAL_ADC_GetValue(&hadc1);
  }
  else
  {
    armor_adc_error_count++;
  }

  if (HAL_ADC_Stop(&hadc1) != HAL_OK)
  {
    armor_adc_error_count++;
  }

  return adc_raw;
}

static void Armor_SendPacket(uint8_t event)
{
  uint8_t packet[ARMOR_PACKET_SIZE];
  uint8_t sum = 0U;

  packet[0] = 0xA5U;
  packet[1] = ARMOR_ID;
  packet[2] = event;
  packet[3] = (uint8_t)armor_dx_level;
  packet[4] = (uint8_t)(armor_adc_raw & 0xFFU);
  packet[5] = (uint8_t)((armor_adc_raw >> 8) & 0xFFU);
  packet[6] = armor_seq++;
  for (uint32_t i = 0U; i < ARMOR_PACKET_SIZE - 1U; i++)
  {
    sum = (uint8_t)(sum + packet[i]);
  }
  packet[7] = sum;

  if (HAL_UART_Transmit(&huart2, packet, sizeof(packet), 10U) != HAL_OK)
  {
    armor_tx_error_count++;
  }
}

static void Armor_UpdateLed(bool hit)
{
  HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, hit ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin, hit ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void Armor_DebugRecord(uint32_t now, uint8_t hit_event)
{
  uint32_t index = armor_debug_write_index;

  armor_debug_samples[index].adc_raw = (uint16_t)armor_adc_raw;
  armor_debug_samples[index].baseline = (uint16_t)armor_baseline;
  armor_debug_samples[index].tick_ms = (uint16_t)now;
  armor_debug_samples[index].dx_level = (uint8_t)armor_dx_level;
  armor_debug_samples[index].hit_event = hit_event;

  index++;
  if (index >= ARMOR_DEBUG_SAMPLE_COUNT)
  {
    index = 0U;
  }
  armor_debug_write_index = index;
  if (armor_debug_sample_count < ARMOR_DEBUG_SAMPLE_COUNT)
  {
    armor_debug_sample_count++;
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
