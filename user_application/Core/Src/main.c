/* USER CODE BEGIN Header */
/* USER CODE END Header */
#include "main.h"
#include "usart.h"
#include <string.h>

/* USER CODE BEGIN PD */
#define RCC_AHB1ENR   (*(volatile uint32_t*)0x40023830)
#define GPIOA_MODER   (*(volatile uint32_t*)0x40020000)
#define GPIOA_ODR     (*(volatile uint32_t*)0x40020014)
#define IWDG_KR       (*(volatile uint32_t*)0x40003000)
/* USER CODE END PD */

/* USER CODE BEGIN PV */
extern uint32_t g_pfnVectors;
/* USER CODE END PV */

/* USER CODE BEGIN PFP */
void delay(volatile uint32_t count);
void feed_watchdog(void);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */
void uart_print(const char *msg)
{
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
}

void delay(volatile uint32_t count) { while(count--); }

void feed_watchdog(void) { IWDG_KR = 0xAAAA; }
/* USER CODE END 0 */

int main(void)
{
  /* USER CODE BEGIN 1 */
  SCB->VTOR = (uint32_t)&g_pfnVectors;   // point VTOR at this app's own table
  /* USER CODE END 1 */

  HAL_Init();

  /* USER CODE BEGIN Init */
  __enable_irq();                        // only after VTOR is correct
  /* USER CODE END Init */

  /* USER CODE BEGIN 2 */
  MX_USART2_UART_Init();
  RCC_AHB1ENR |= (1 << 0);
  GPIOA_MODER |= (1 << 10);
  /* USER CODE END 2 */

  while (1)
  {
    /* USER CODE BEGIN 3 */
    GPIOA_ODR ^= (1 << 5);
    delay(3000000);
    feed_watchdog();
    uart_print("[APP B - Slot 0] Alive, feeding watchdog\r\n");
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
