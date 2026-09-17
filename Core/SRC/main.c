/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   工位盒: 扫码枪收条码 + SIACP 帧协议对接上位机 + 防重
  *          U1(PA2/PA3) 接扫码枪: 收 ASCII+CRLF 条码, 发 0x16 0x54 触发扫描
  *          U2(PA0/PA1) 接上位机: AA 请求帧 / AB 应答帧, 见 siacp.h
 *          PA12        继电器: 直通开吸合(低电平) / 直通关释放
  ******************************************************************************
  */

/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "rcc.h"
#include "gpio.h"
#include "usart.h"
#include "i2c.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "oled.h"
#include "stationbox.h"
#include "string.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */


/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */


/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
static uint8_t u1_rx_byte;
static uint8_t u2_rx_byte;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* External functions --------------------------------------------------------*/
/* USER CODE BEGIN ExternalFunctions */

/* USER CODE END ExternalFunctions */


/* USER CODE BEGIN 0 */

/* Echo 测试模式: U1/U2 各收字节 -> OLED 滚动显示(文本)。U1 回送同一串口, U2 不回送。
 * StationBox 业务逻辑已注释, 只跑这个诊断。
 *
 * OLED 布局:
 *   第 1 行 U1: 滚动文本, 新字节从右进, 非 ASCII 转 '.', 回送同一串口
 *   第 2 行 U2: 滚动文本, 新字节从右进, 非 ASCII 转 '.', 不回送
 *   第 3 行 1:xx 2:xx T:xx = U1/U2 累计字节数 + 0.1s 计时
 *
 * 缓冲 32 字节, 显示取最后 14 字符, 避免快速数据丢失前面的字符 */
#define BUF_SIZE 32    /* 接收缓冲 */
#define SCR_SIZE 14    /* 88px / 6px = 一屏字符数 */

static char    u1_buf[BUF_SIZE + 1] = {0};
static char    u2_buf[BUF_SIZE + 1] = {0};
static uint32_t u1_cnt = 0, u2_cnt = 0;

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    u1_cnt++;
    for (uint8_t i = 0; i < BUF_SIZE - 1; i++)
      u1_buf[i] = u1_buf[i + 1];
    u1_buf[BUF_SIZE - 1] = ((u1_rx_byte >= ' ') && (u1_rx_byte <= '~')) ? (char)u1_rx_byte : '.';
    u1_buf[BUF_SIZE] = 0;
    (void)HAL_UART_Transmit(&husart1, &u1_rx_byte, 1, 50);
    (void)HAL_UART_Receive_IT(&husart1, &u1_rx_byte, 1);
  }
  else if (huart->Instance == USART2)
  {
    u2_cnt++;
    for (uint8_t i = 0; i < BUF_SIZE - 1; i++)
      u2_buf[i] = u2_buf[i + 1];
    u2_buf[BUF_SIZE - 1] = ((u2_rx_byte >= ' ') && (u2_rx_byte <= '~')) ? (char)u2_rx_byte : '.';
    u2_buf[BUF_SIZE] = 0;
    (void)HAL_UART_Receive_IT(&husart2, &u2_rx_byte, 1);
  }
}

static void DisplayUpdate(void)
{
  static uint32_t last = 0;
  uint32_t now = HAL_GetTick();
  if ((now - last) < 50) return;
  last = now;

  uint32_t c1, c2;
  __disable_irq();
  c1 = u1_cnt; c2 = u2_cnt;
  __enable_irq();

  OLED_ScrollTextTick(0);
  OLED_ScrollTextTick(1);

  char l3[15];
  l3[0] = '1'; l3[1] = ':';
  l3[2] = (char)('0' + (c1/10)%10); l3[3] = (char)('0' + c1%10);
  l3[4] = ' ';
  l3[5] = '2'; l3[6] = ':';
  l3[7] = (char)('0' + (c2/10)%10); l3[8] = (char)('0' + c2%10);
  l3[9] = ' ';
  l3[10] = 'T'; l3[11] = ':';
  uint32_t t = (now/100)%100;
  l3[12] = (char)('0' + t/10); l3[13] = (char)('0' + t%10);
  l3[14] = 0;
  OLED_ShowString(0, 29, l3, 8, 1);

  OLED_Refresh();
}

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
  LL_PWR_EnableBkUpAccess();
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  Studio_RCC_Init();
  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  Studio_GPIO_Init();
  Studio_USART2_Init();
  Studio_USART1_Init();
  Studio_I2C1_Init();
  /* USER CODE BEGIN 2 */
  OLED_Init();
  memset(u1_buf, ' ', BUF_SIZE);
  memset(u2_buf, ' ', BUF_SIZE);
  u1_buf[BUF_SIZE] = 0;
  u2_buf[BUF_SIZE] = 0;
  /* StationBox_Init(); */
  OLED_ScrollText(0, u1_buf, BUF_SIZE, 5, 1);
  OLED_ScrollText(1, u2_buf, BUF_SIZE, 17, 1);
  OLED_ShowString(0, 5, "                ", 8, 1);
  OLED_ShowString(0, 17, "                ", 8, 1);
  OLED_ShowString(0, 29, "1:00 2:00 T:00", 8, 1);
  OLED_Refresh();
  HAL_UART_Receive_IT(&husart1, &u1_rx_byte, 1);
  HAL_UART_Receive_IT(&husart2, &u2_rx_byte, 1);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while(1)
  {

    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
    /* StationBox_Task(); */
    DisplayUpdate();
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


  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
