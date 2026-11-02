/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : STM32F446RE Slot 0 Golden Application with Wireless OTA
  *                   - Dual-UART: USART2 (PC USB COM9) & USART1 (ESP32 Wi-Fi)
  *                   - AES-256-CBC Decryption + SHA-256 + CRC-32 Verification
  *                   - Flash Sector Erase & Chunk Programming for Slot 1
  *                   - Failsafe Watchdog Rollback & Bootloader Handoff
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"
#include "gpio.h"
#include "aes.h"
#include "sha256.h"
#include <string.h>
#include <stdio.h>

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct {
    uint32_t size;
    uint32_t crc32;
    uint8_t  sha256[32];
} __attribute__((packed)) ota_header_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define RCC_AHB1ENR       (*(volatile uint32_t*)0x40023830)
#define GPIOA_MODER       (*(volatile uint32_t*)0x40020000)
#define GPIOA_ODR         (*(volatile uint32_t*)0x40020014)
#define IWDG_KR           (*(volatile uint32_t*)0x40003000)

#define SLOT1_BASE_ADDR   0x08040000
#define MAX_FW_SIZE       (256 * 1024)
#define CHUNK_SIZE        256
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
extern uint32_t g_pfnVectors;

static const uint8_t AES_KEY[32] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F
};

static const uint8_t AES_IV[16] = {
    0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF
};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
void delay(volatile uint32_t count);
void feed_watchdog(void);
void uart_print(const char *msg);
void wifi_print(const char *msg);
uint32_t crc32_calculate(uint32_t address, uint32_t length);
int sha256_verify(uint32_t address, uint32_t length, const uint8_t expected_hash[32]);
void handle_ota_update(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
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

uint32_t crc32_calculate(uint32_t address, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *data = (const uint8_t*)address;
    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

int sha256_verify(uint32_t address, uint32_t length, const uint8_t expected_hash[32])
{
    SHA256_CTX sha_ctx;
    uint8_t calc_hash[32];
    sha256_init(&sha_ctx);
    sha256_update(&sha_ctx, (const uint8_t*)address, length);
    sha256_final(&sha_ctx, calc_hash);
    return (memcmp(calc_hash, expected_hash, 32) == 0);
}

void handle_ota_update(void)
{
    char log_buf[128];
    uart_print("\r\n==================================================\r\n");
    uart_print("[OTA] Wireless OTA update requested!\r\n");
    uart_print("[OTA] Erasing Slot 1 Flash sectors (0x08040000 - 0x0807FFFF)...\r\n");

    feed_watchdog();

    // 1. Erase Slot 1 Sectors (Sector 6 and Sector 7, total 256KB)
    HAL_FLASH_Unlock();
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t SectorError = 0;
    EraseInitStruct.TypeErase    = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    EraseInitStruct.Sector       = FLASH_SECTOR_6;
    EraseInitStruct.NbSectors    = 2; // Sector 6 (128KB) + Sector 7 (128KB)

    if (HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError) != HAL_OK)
    {
        HAL_FLASH_Lock();
        uart_print("[OTA ERROR] Flash sector erase failed!\r\n");
        wifi_print("FAIL_ERASE\r\n");
        return;
    }
    HAL_FLASH_Lock();
    feed_watchdog();

    uart_print("[OTA] Slot 1 Flash erase complete.\r\n");

    // Flush any pending UART RX bytes or noise
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    __HAL_UART_CLEAR_NEFLAG(&huart1);
    __HAL_UART_CLEAR_FEFLAG(&huart1);
    uint8_t dummy;
    while (HAL_UART_Receive(&huart1, &dummy, 1, 10) == HAL_OK) {}

    // Send READY over Wi-Fi to host
    uart_print("[OTA] Sending READY over Wi-Fi...\r\n");
    wifi_print("READY\r\n");

    // 2. Receive 40-byte Header: [size(4)][crc32(4)][sha256(32)]
    ota_header_t header;
    memset(&header, 0, sizeof(header));
    HAL_StatusTypeDef status = HAL_UART_Receive(&huart1, (uint8_t*)&header, sizeof(header), 8000);
    if (status != HAL_OK)
    {
        uart_print("[OTA ERROR] Timeout waiting for header!\r\n");
        wifi_print("FAIL_HDR_TIMEOUT\r\n");
        return;
    }

    if (header.size == 0 || header.size > MAX_FW_SIZE)
    {
        snprintf(log_buf, sizeof(log_buf), "[OTA ERROR] Invalid size: %lu bytes!\r\n", header.size);
        uart_print(log_buf);
        wifi_print("FAIL_SIZE\r\n");
        return;
    }

    snprintf(log_buf, sizeof(log_buf),
             "[OTA] Header OK: Size=%lu bytes, CRC32=0x%08lX\r\n",
             header.size, header.crc32);
    uart_print(log_buf);

    uart_print("[OTA] Sending HDR_OK over Wi-Fi...\r\n");
    wifi_print("HDR_OK\r\n");

    // 3. Initialize AES-256-CBC Decryption Context
    AES_ctx aes_ctx;
    AES_init_ctx_iv(&aes_ctx, AES_KEY, AES_IV);

    // 4. Stream and program chunks
    uint32_t total_chunks = (header.size + (CHUNK_SIZE - 1)) / CHUNK_SIZE;
    uint8_t chunk_buffer[CHUNK_SIZE];

    snprintf(log_buf, sizeof(log_buf), "[OTA] Receiving %lu encrypted chunks (%d bytes each)...\r\n", total_chunks, CHUNK_SIZE);
    uart_print(log_buf);

    for (uint32_t chunk_idx = 0; chunk_idx < total_chunks; chunk_idx++)
    {
        feed_watchdog();

        // Receive 256 encrypted bytes
        status = HAL_UART_Receive(&huart1, chunk_buffer, CHUNK_SIZE, 6000);
        if (status != HAL_OK)
        {
            snprintf(log_buf, sizeof(log_buf), "[OTA ERROR] Timeout at chunk %lu/%lu!\r\n", chunk_idx + 1, total_chunks);
            uart_print(log_buf);
            wifi_print("FAIL_CHUNK_TIMEOUT\r\n");
            return;
        }

        // Decrypt in-place using AES-256-CBC
        AES_CBC_decrypt_buffer(&aes_ctx, chunk_buffer, CHUNK_SIZE);

        // Program 256 bytes into Flash at SLOT1_BASE_ADDR + chunk_idx*CHUNK_SIZE
        uint32_t write_addr = SLOT1_BASE_ADDR + (chunk_idx * CHUNK_SIZE);
        HAL_FLASH_Unlock();
        for (uint32_t offset = 0; offset < CHUNK_SIZE; offset += 4)
        {
            uint32_t word = *(uint32_t*)&chunk_buffer[offset];
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, write_addr + offset, (uint64_t)word) != HAL_OK)
            {
                HAL_FLASH_Lock();
                uart_print("[OTA ERROR] Flash write failed during chunk programming!\r\n");
                wifi_print("FAIL_PROGRAM\r\n");
                return;
            }
        }
        HAL_FLASH_Lock();

        // Send ACK for this chunk
        wifi_print("ACK\r\n");

        if ((chunk_idx + 1) % 10 == 0 || (chunk_idx + 1) == total_chunks)
        {
            snprintf(log_buf, sizeof(log_buf), "[OTA] Chunk %lu/%lu programmed and ACKed.\r\n", chunk_idx + 1, total_chunks);
            uart_print(log_buf);
        }
    }

    // Flush Flash cache to guarantee coherency before reading
    __HAL_FLASH_DATA_CACHE_DISABLE();
    __HAL_FLASH_INSTRUCTION_CACHE_DISABLE();
    __HAL_FLASH_DATA_CACHE_RESET();
    __HAL_FLASH_INSTRUCTION_CACHE_RESET();
    __HAL_FLASH_INSTRUCTION_CACHE_ENABLE();
    __HAL_FLASH_DATA_CACHE_ENABLE();

    // 5. Verification
    uart_print("[OTA] All chunks programmed. Verifying integrity on Flash...\r\n");
    feed_watchdog();

    uint32_t calc_crc = crc32_calculate(SLOT1_BASE_ADDR, header.size);
    snprintf(log_buf, sizeof(log_buf),
             "[OTA] Flash CRC32: 0x%08lX (Expected: 0x%08lX) -> %s\r\n",
             calc_crc, header.crc32, (calc_crc == header.crc32) ? "MATCH" : "MISMATCH");
    uart_print(log_buf);

    int sha_ok = sha256_verify(SLOT1_BASE_ADDR, header.size, header.sha256);
    uart_print(sha_ok ? "[OTA] Flash SHA-256: MATCH\r\n" : "[OTA] Flash SHA-256: MISMATCH\r\n");

    if (calc_crc == header.crc32 && sha_ok)
    {
        uart_print("\r\n==================================================\r\n");
        uart_print("[OTA SUCCESS] Firmware verified! Arming Slot 1 in RTC...\r\n");
        uart_print("==================================================\r\n");

        wifi_print("\r\n[STM32] FIRMWARE VERIFIED SUCCESSFULLY!\r\n");
        wifi_print("[STM32] Arming RTC Backup Register: RTC->BKP0R = 1\r\n");
        wifi_print("[STM32] Rebooting into Bootloader -> Slot 1 in 1 second...\r\n");

        // Arm RTC backup registers
        __HAL_RCC_PWR_CLK_ENABLE();
        HAL_PWR_EnableBkUpAccess();
        __HAL_RCC_RTC_ENABLE();

        RTC->BKP0R = 1;             // Flag 1 = Boot Candidate Image in Slot 1
        RTC->BKP1R = header.size;   // Candidate Firmware Size
        RTC->BKP2R = header.crc32;  // Candidate Firmware CRC32

        HAL_Delay(1000);
        uart_print("[OTA] Executing NVIC_SystemReset()...\r\n");
        NVIC_SystemReset();
    }
    else
    {
        uart_print("[OTA ERROR] Integrity check failed! Rejecting candidate image.\r\n");
        wifi_print("\r\n[STM32 ERROR] Integrity verification failed. Update rejected.\r\n");
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  SCB->VTOR = (uint32_t)&g_pfnVectors;   // point VTOR at this app's own vector table
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  __enable_irq();                        // only after VTOR is correct
  /* USER CODE END Init */

  /* Configure the system clock */
  // SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();  // PC USB Console (COM9)
  MX_USART1_UART_Init();  // ESP32 Wi-Fi Bridge (D8 = PA9 TX, D2 = PA10 RX)

  /* USER CODE BEGIN 2 */
  RCC_AHB1ENR |= (1 << 0);
  GPIOA_MODER |= (1 << 10); // User LED on PA5

  uart_print("\r\n==================================================\r\n");
  uart_print("[APP - Slot 0] Golden Image Running (Dual-UART Active)\r\n");
  uart_print("  - USART2: PC USB Terminal (115200 8N1)\r\n");
  uart_print("  - USART1: ESP32 Wi-Fi Gateway (D8=TX, D2=RX, 115200 8N1)\r\n");
  uart_print("  - Status: Listening for Wireless OTA Update ('O')...\r\n");
  uart_print("==================================================\r\n");

  wifi_print("[STM32 -> ESP32] Slot 0 Initialized! Listening for OTA...\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN 3 */
  while (1)
  {
    feed_watchdog();

    // Toggle onboard User LED (PA5)
    GPIOA_ODR ^= (1 << 5);

    // Broadcast heartbeat on both PC USB and ESP32 Wi-Fi
    uart_print("[APP - Slot 0] Running, listening for OTA...\r\n");
    wifi_print("[APP - Slot 0] Running, listening for OTA...\r\n");

    // Check for incoming byte from ESP32 on USART1
    uint8_t rx_byte = 0;
    if (HAL_UART_Receive(&huart1, &rx_byte, 1, 300) == HAL_OK)
    {
      if (rx_byte == 'O')
      {
        handle_ota_update();
      }
      else
      {
        // Echo unexpected byte to PC terminal
        HAL_UART_Transmit(&huart2, &rx_byte, 1, 10);
      }
    }

    delay(2000000);
  }
  /* USER CODE END 3 */
}

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
