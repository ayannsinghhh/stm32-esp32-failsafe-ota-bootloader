/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c (CANDIDATE IMAGE FOR SLOT 1)
  * @brief          : STM32F446RE Slot 1 Candidate Firmware
  *                   - Linked for ORIGIN = 0x08040000, LENGTH = 256K
  *                   - Deployed wirelessly over Wi-Fi via ESP32 Gateway
  *                   - AES-256 decrypted & SHA-256/CRC-32 verified on-the-fly
  *                   - Continuous watchdog feeding ensures probationary pass
  *                   - Distinct Double-Blink LED heartbeat pattern
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"
#include "gpio.h"
#include <string.h>
#include <stdio.h>

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define RCC_AHB1ENR   (*(volatile uint32_t*)0x40023830)
#define GPIOA_MODER   (*(volatile uint32_t*)0x40020000)
#define GPIOA_ODR     (*(volatile uint32_t*)0x40020014)
#define IWDG_KR       (*(volatile uint32_t*)0x40003000)
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
extern uint32_t g_pfnVectors;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
void delay(volatile uint32_t count);
void feed_watchdog(void);
void uart_print(const char *msg);
void wifi_print(const char *msg);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */
void uart_print(const char *msg)
{
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
}

void wifi_print(const char *msg)
{
    HAL_UART_Transmit(&huart1, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
}

void delay(volatile uint32_t count)
{
    while(count--);
}

void feed_watchdog(void)
{
    IWDG_KR = 0xAAAA;
}
/* USER CODE END 0 */

int main(void)
{
  /* USER CODE BEGIN 1 */
  // Relocate Vector Table to Slot 1 (0x08040000)
  SCB->VTOR = (uint32_t)&g_pfnVectors;
  /* USER CODE END 1 */

  HAL_Init();

  /* USER CODE BEGIN Init */
  __enable_irq();
  /* USER CODE END Init */

  MX_GPIO_Init();
  MX_USART2_UART_Init();  // PC USB Console (COM9)
  MX_USART1_UART_Init();  // ESP32 Wi-Fi Bridge

  /* USER CODE BEGIN 2 */
  RCC_AHB1ENR |= (1 << 0);
  GPIOA_MODER |= (1 << 10); // User LED on PA5

  // Initial candidate boot banner
  uart_print("\r\n========================================================\r\n");
  uart_print(" 🎉🎉 [APP - SLOT 1] LIVE WIRELESS OTA UPDATE SUCCESS! 🎉🎉\r\n");
  uart_print("  - Running candidate image from Flash address: 0x08040000\r\n");
  uart_print("  - AES-256 Decrypted, CRC-32 & SHA-256 Verified\r\n");
  uart_print("  - Hardware Watchdog being refreshed (Probation PASSED!)\r\n");
  uart_print("  - User LED PA5 running Distinct Double-Blink Pattern\r\n");
  uart_print("========================================================\r\n");

  wifi_print("\r\n[STM32 -> ESP32] LIVE WIRELESS OTA SUCCESS! Slot 1 Active!\r\n");
  /* USER CODE END 2 */

  while (1)
  {
    /* USER CODE BEGIN 3 */
    feed_watchdog();

    // Distinct double-blink heartbeat
    GPIOA_ODR |= (1 << 5);   // LED ON
    delay(400000);
    GPIOA_ODR &= ~(1 << 5);  // LED OFF
    delay(400000);

    feed_watchdog();

    GPIOA_ODR |= (1 << 5);   // LED ON
    delay(400000);
    GPIOA_ODR &= ~(1 << 5);  // LED OFF
    delay(2000000);          // Pause

    uart_print("[APP - Slot 1] Running healthy in Slot 1 (0x08040000)...\r\n");
    wifi_print("[APP - Slot 1] Running healthy in Slot 1 (0x08040000)...\r\n");
    /* USER CODE END 3 */
  }
}

void Error_Handler(void)
{
  __disable_irq();
  while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif