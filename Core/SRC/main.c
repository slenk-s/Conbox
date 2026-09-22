/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   Barcode station: deduplication, release control and display.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "rcc.h"
#include "gpio.h"
#include "usart.h"
#include "tim.h"
/* USER CODE BEGIN Includes */
#include "barcode_app.h"
#include "barcode_port.h"
#include "barcode_view.h"
#include "oled.h"

/* USER CODE END Includes */

/* USER CODE BEGIN PTD */

/* USER CODE END PTD */
/* USER CODE BEGIN PD */

/* USER CODE END PD */
/* USER CODE BEGIN PM */

/* USER CODE END PM */
/* USER CODE BEGIN PV */

/* USER CODE END PV */
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */
/* USER CODE BEGIN ExternalFunctions */

/* USER CODE END ExternalFunctions */
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

  HAL_Init();
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  Studio_RCC_Init();
  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals. */
  Studio_GPIO_Init();
  Studio_USART2_Init();
  Studio_USART1_Init();
  Studio_TIM1_Init();
  Studio_TIM3_Init();
  /* USER CODE BEGIN 2 */
  OLED_Init();
  BarcodeView_Init();
  /* Startup rescan time zero is AFTER all blocking hardware initialization. */
  BarcodeApp_Init(HAL_GetTick());
  BarcodePort_Init();

  /* USER CODE END 2 */

  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
    BarcodePort_Poll();
    BarcodeView_Poll();

  }
  /* USER CODE END 3 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }

  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
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
