/* USER CODE BEGIN Header */
/* USER CODE END Header */
#include "main.h"
#include "usart.h"
#include <string.h>

/* USER CODE BEGIN PD */
#define APP_SLOT_0    0x08020000
#define APP_SLOT_1    0x08040000
#define BUTTON_PIN    (1 << 13)
#define RCC_AHB1ENR   (*(volatile uint32_t*)0x40023830)
#define GPIOC_MODER   (*(volatile uint32_t*)0x40020800)
#define GPIOC_IDR     (*(volatile uint32_t*)0x40020810)
/* USER CODE END PD */

/* USER CODE BEGIN PV */
typedef void (*app_entry_t)(void);
/* USER CODE END PV */

/* USER CODE BEGIN PFP */
void jump_to_app(uint32_t app_address);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */
uint8_t is_button_pressed(void)
{
    RCC_AHB1ENR |= (1 << 2);
    GPIOC_MODER &= ~(0x3 << 26);
    return ((GPIOC_IDR & BUTTON_PIN) == 0);
}

void uart_print(const char *msg)
{
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
}

void start_watchdog(void)
{
    IWDG->KR = 0xCCCC;   // Start FIRST — makes SR flags behave correctly
    IWDG->KR = 0x5555;   // Unlock write access

    IWDG->PR  = 0x04;    // prescaler /64
    IWDG->RLR = 3000;    // reload value

    while (IWDG->SR & 0x03);   // Now safely clears

    IWDG->KR = 0xAAAA;   // Reload
}

uint8_t was_watchdog_reset(void)
{
    uint8_t flag = (RCC->CSR & RCC_CSR_IWDGRSTF) ? 1 : 0;
    RCC->CSR |= RCC_CSR_RMVF;
    return flag;
}

uint32_t get_boot_slot(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_RTC_ENABLE();

    if (was_watchdog_reset())
    {
        uart_print("\r\n[BOOTLOADER] Watchdog reset detected! Falling back to Slot 0.\r\n");
        RTC->BKP0R = 0;
        return APP_SLOT_0;
    }

    uint32_t flag = RTC->BKP0R;
    uart_print(flag == 1 ? "\r\n[BOOTLOADER] Flag=1, booting Slot 1 (candidate)\r\n"
                          : "\r\n[BOOTLOADER] Flag=0, booting Slot 0 (known good)\r\n");
    return (flag == 1) ? APP_SLOT_1 : APP_SLOT_0;
}

void jump_to_app(uint32_t app_address)
{
    uint32_t app_stack = *((volatile uint32_t*)app_address);
    uint32_t app_reset = *((volatile uint32_t*)(app_address + 4));

    __disable_irq();

    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    HAL_RCC_DeInit();
    HAL_DeInit();

    for (uint8_t i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    SCB->VTOR = app_address;
    __DSB();
    __ISB();

    __asm volatile ("msr msp, %0 \n bx %1 \n" : : "r"(app_stack), "r"(app_reset));
}
/* USER CODE END 0 */

int main(void)
{
  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  HAL_Init();

  /* USER CODE BEGIN 2 */
  MX_USART2_UART_Init();
  uart_print("\r\n[BOOTLOADER] Starting up...\r\n");

  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_RTC_ENABLE();

  if (is_button_pressed())
  {
      uart_print("\r\n[BOOTLOADER] Button held — simulating new update, forcing Slot 1.\r\n");
      RTC->BKP0R = 1;
  }

  uint32_t target_slot = get_boot_slot();

  if (target_slot == APP_SLOT_1)
  {
      start_watchdog();
  }
  else
  {
      IWDG->KR = 0xAAAA;
  }

  jump_to_app(target_slot);

  /* USER CODE END 2 */

  while (1) {}
}

void Error_Handler(void)
{
  __disable_irq();
  while (1) {}
}
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif
