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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define UART_RX_BUFFER_SIZE 64U
#define QUEUE_SIZE          16U
#define CMD_SIZE            32U
#define CALL_TIMEOUT_MS     10000U
#define LED_MAX_CCR         1000U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim6;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
volatile uint8_t ticket_button_flag = 0;
volatile uint32_t ticket_button_tick = 0;
uint16_t ticket_number = 0;
uint8_t ticket_button_released = 1;
uint32_t ticket_release_tick = 0;
uint8_t uart_rx_byte;
volatile uint8_t uart_rx_buffer[UART_RX_BUFFER_SIZE];
volatile uint16_t uart_rx_head = 0;
volatile uint16_t uart_rx_tail = 0;

/* 排隊用環形緩衝 */
uint16_t queue_buf[QUEUE_SIZE];
uint8_t queue_head = 0;
uint8_t queue_tail = 0;
uint8_t queue_count = 0;

/* 指令列 */
char cmd_line[CMD_SIZE];
uint8_t cmd_len = 0;

/* 叫號狀態 */
uint16_t called_ticket = 0;
uint32_t called_tick = 0;
uint8_t checked_in = 0;

/* 緊急暫停（B1） */
volatile uint8_t emergency_flag = 0;
volatile uint32_t emergency_tick = 0;
uint8_t emergency_active = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM6_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
void UART_Print(const char *text);
void Take_Ticket(void);
void Call_Next(void);
void Command_Handle(const char *line);

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
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_TIM6_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  const char boot_msg[] = "QUEUE READY\r\n";
  HAL_UART_Transmit(&huart2, (const uint8_t *)boot_msg, sizeof(boot_msg) - 1, 100);
  HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);   /* 黃燈 PC7 */
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);   /* 綠燈 PC8 */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if (ticket_button_flag && (HAL_GetTick() - ticket_button_tick) >= 50U)
    {
      ticket_button_flag = 0;

      if (ticket_button_released &&
          HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_5) == GPIO_PIN_RESET)
      {
        ticket_button_released = 0;
        ticket_release_tick = HAL_GetTick();

        Take_Ticket();
      }
    }

    if (!ticket_button_released)
    {
      if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_5) == GPIO_PIN_RESET)
      {
        ticket_release_tick = HAL_GetTick();
      }
      else if ((HAL_GetTick() - ticket_release_tick) >= 50U)
      {
        ticket_button_released = 1;
      }
    }

    /* UART：一次取一個字，收到 Enter 才判斷整行指令 */
    while (uart_rx_tail != uart_rx_head)
    {
      uint8_t rx_char = uart_rx_buffer[uart_rx_tail];
      uart_rx_tail = (uart_rx_tail + 1U) % UART_RX_BUFFER_SIZE;

      if ((rx_char == '\r') || (rx_char == '\n'))
      {
        if (cmd_len > 0U)
        {
          UART_Print("\r\n");
          cmd_line[cmd_len] = '\0';
          Command_Handle(cmd_line);
          cmd_len = 0;
        }
      }
      else
      {
        HAL_UART_Transmit(&huart2, &rx_char, 1, 100);   /* 回顯 */
        if (cmd_len < (CMD_SIZE - 1U))
        {
          cmd_line[cmd_len] = (char)rx_char;
          cmd_len++;
        }
      }
    }

    /* 緊急按鈕 B1：去彈跳 50 ms，且確認仍按著才切換 */
    if (emergency_flag && (HAL_GetTick() - emergency_tick) >= 50U)
    {
      emergency_flag = 0;

      if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET)
      {
        emergency_active = !emergency_active;
        UART_Print(emergency_active ? "緊急暫停\r\n" : "恢復正常\r\n");

        if (!emergency_active && (called_ticket != 0U))
        {
          called_tick = HAL_GetTick();   /* 恢復後重新計時 */
        }
      }
    }

    /* 叫號後 10 秒沒報到就過號（暫停時不計） */
    if (!emergency_active && (called_ticket != 0U) && !checked_in &&
        (HAL_GetTick() - called_tick) >= CALL_TIMEOUT_MS)
    {
      char msg[24];
      snprintf(msg, sizeof(msg), "%03u 過號\r\n", called_ticket);
      UART_Print(msg);
      called_ticket = 0;
      Call_Next();
    }

    /* 燈號：黃燈依人數、綠燈等待時呼吸／報到恆亮、紅燈緊急 */
    {
      uint32_t yellow = (uint32_t)queue_count * LED_MAX_CCR / QUEUE_SIZE;
      uint32_t green = 0;

      if (called_ticket != 0U)
      {
        if (checked_in)
        {
          green = LED_MAX_CCR;
        }
        else
        {
          uint32_t phase = HAL_GetTick() % 2000U;
          green = (phase < 1000U) ? phase : (2000U - phase);
        }
      }

      if (emergency_active)
      {
        yellow = 0;
        green = 0;
      }

      __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, yellow);
      __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, green);
      HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, emergency_active ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
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
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_SlaveConfigTypeDef sSlaveConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_IC_InitTypeDef sConfigIC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 169;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 4294967295;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_IC_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sSlaveConfig.SlaveMode = TIM_SLAVEMODE_RESET;
  sSlaveConfig.InputTrigger = TIM_TS_TI1FP1;
  sSlaveConfig.TriggerPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
  sSlaveConfig.TriggerFilter = 0;
  if (HAL_TIM_SlaveConfigSynchro(&htim2, &sSlaveConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
  sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
  sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
  sConfigIC.ICFilter = 0;
  if (HAL_TIM_IC_ConfigChannel(&htim2, &sConfigIC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigIC.ICSelection = TIM_ICSELECTION_INDIRECTTI;
  if (HAL_TIM_IC_ConfigChannel(&htim2, &sConfigIC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 169;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 999;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  GPIO_InitStruct.Pin = GPIO_PIN_7 | GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF2_TIM3;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
  /* USER CODE END TIM3_Init 2 */

}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 169;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 999;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PC6 */
  GPIO_InitStruct.Pin = GPIO_PIN_6;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PB5 */
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 4, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == GPIO_PIN_5)
  {
    ticket_button_tick = HAL_GetTick();
    ticket_button_flag = 1;
  }
  else if (GPIO_Pin == GPIO_PIN_13)
  {
    emergency_tick = HAL_GetTick();
    emergency_flag = 1;
  }
}

void UART_Print(const char *text)
{
  HAL_UART_Transmit(&huart2, (const uint8_t *)text, (uint16_t)strlen(text), 100);
}

/* 抽號：按鈕或 take 指令都走這裡 */
void Take_Ticket(void)
{
  char msg[24];

  if (queue_count >= QUEUE_SIZE)
  {
    UART_Print("客滿\r\n");
    return;
  }

  ticket_number++;
  if (ticket_number > 999U)
  {
    ticket_number = 1;
  }

  queue_buf[queue_head] = ticket_number;
  queue_head = (queue_head + 1U) % QUEUE_SIZE;
  queue_count++;

  snprintf(msg, sizeof(msg), "%03u\r\n", ticket_number);
  UART_Print(msg);
}

/* 叫號：從 tail 取出最前面的人 */
void Call_Next(void)
{
  char msg[32];

  if (queue_count == 0U)
  {
    called_ticket = 0;
    UART_Print("目前沒有人排隊\r\n");
    return;
  }

  called_ticket = queue_buf[queue_tail];
  queue_tail = (queue_tail + 1U) % QUEUE_SIZE;
  queue_count--;
  called_tick = HAL_GetTick();
  checked_in = 0;

  snprintf(msg, sizeof(msg), "請 %03u 到櫃台\r\n", called_ticket);
  UART_Print(msg);
}

void Command_Handle(const char *line)
{
  if (strcmp(line, "take") == 0)
  {
    Take_Ticket();
  }
  else if (strcmp(line, "next") == 0)
  {
    if (emergency_active)
    {
      return;   /* 緊急暫停時 next 沒反應 */
    }

    if ((called_ticket != 0U) && !checked_in)
    {
      UART_Print("請等待目前號碼報到或過號\r\n");
      return;
    }

    Call_Next();
  }
  else if (strcmp(line, "ok") == 0)
  {
    char msg[24];
    if ((called_ticket != 0U) && !checked_in)
    {
      checked_in = 1;
      snprintf(msg, sizeof(msg), "%03u 報到\r\n", called_ticket);
      UART_Print(msg);
    }
    else
    {
      UART_Print("目前沒有等待報到的號碼\r\n");
    }
  }
  else if (strcmp(line, "list") == 0)
  {
    char msg[8];
    uint8_t idx = queue_tail;

    if (queue_count == 0U)
    {
      UART_Print("目前沒有人排隊\r\n");
      return;
    }
    UART_Print("排隊中：");
    for (uint8_t i = 0; i < queue_count; i++)
    {
      snprintf(msg, sizeof(msg), "%03u ", queue_buf[idx]);
      UART_Print(msg);
      idx = (idx + 1U) % QUEUE_SIZE;
    }
    UART_Print("\r\n");
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    uint16_t next_head = (uart_rx_head + 1U) % UART_RX_BUFFER_SIZE;

    if (next_head != uart_rx_tail)
    {
      uart_rx_buffer[uart_rx_head] = uart_rx_byte;
      uart_rx_head = next_head;
    }

    HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1);
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1);
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
