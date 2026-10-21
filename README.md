# Dual-Slot Failsafe OTA Bootloader & Wi-Fi Gateway for STM32

[![Target](https://img.shields.io/badge/Target-STM32F446RE%20(ARM%20Cortex--M4)-002B49?style=flat-square&logo=stmicroelectronics)](https://www.st.com/)
[![Co-Processor](https://img.shields.io/badge/Co--Processor-ESP32%20Wi--Fi-E7352C?style=flat-square&logo=espressif)](https://www.espressif.com/)
[![Language](https://img.shields.io/badge/Language-Embedded%20C%20%7C%20Python-blue?style=flat-square)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Integrity](https://img.shields.io/badge/Security-AES--256%20%7C%20SHA--256%20%7C%20CRC--32-green?style=flat-square)]()
[![License](https://img.shields.io/badge/License-MIT-yellow?style=flat-square)](LICENSE)

An industrial-grade, bare-metal **In-Application Programming (IAP) Dual-Slot (A/B) Bootloader** for the **STM32F446RE (ARM Cortex-M4)** microcontroller. 

The system features an automated **anti-bricking hardware rollback mechanism** using the Independent Watchdog (`IWDG`) and RTC backup domain (`RTC->BKP0R`), combined with an **ESP32 Wi-Fi to UART transparent gateway** and a Python desktop client for encrypted, chunk-based Over-The-Air (OTA) updates.

---

## 🏛 System Architecture

```
+-----------------------------------------------------------------------------------+
|                               DEPLOYMENT WORKSTATION                              |
|   +---------------------------------------------------------------------------+   |
|   |  wifi_ota_sender.py (Python Host Utility)                                 |   |
|   |  * Encrypts payload (AES-256-CBC)                                         |   |
|   |  * Generates 40-byte header: [Size(4B) | CRC32(4B) | SHA-256(32B)]        |   |
|   |  * Streams 256-byte chunks with ACK/NACK flow control                     |   |
|   +---------------------------------------------------------------------------+   |
+------------------------------------------|----------------------------------------+
                                           | TCP Socket (Port 8080)
                                           v
+-----------------------------------------------------------------------------------+
|                         WIRELESS GATEWAY (ESP32 Node)                             |
|   +---------------------------------------------------------------------------+   |
|   |  esp32_wifi_bridge.ino                                                    |   |
|   |  * Hosts SoftAP / Connects to WLAN                                        |   |
|   |  * Bi-directional non-blocking TCP socket <---> UART2 bridge              |   |
|   +---------------------------------------------------------------------------+   |
+------------------------------------------|----------------------------------------+
                                           | Full-Duplex UART (115200 Baud)
                                           v
+-----------------------------------------------------------------------------------+
|                     TARGET EMBEDDED SYSTEM (STM32F446RE)                          |
|   +---------------------------------------------------------------------------+   |
|   |  BARE-METAL BOOTLOADER (0x08000000)                                       |   |
|   |  * Interactive Boot Menu (3s timeout fallback)                            |   |
|   |  * Evaluates RTC->BKP0R & RCC_CSR_IWDGRSTF reset flags                    |   |
|   |  * Automatic Rollback to Golden Image if candidate hangs                  |   |
|   |  * Flash driver: Sector erase & chunk programming                         |   |
|   |  * De-initializes peripherals, masks NVIC, relocates SCB->VTOR & sets MSP |   |
|   +---------------------------------------------------------------------------+   |
|                        |                                 |                        |
|                        v                                 v                        |
|       +---------------------------------+  +----------------------------------+   |
|       |  SLOT 0: GOLDEN / FACTORY IMAGE |  |  SLOT 1: CANDIDATE UPDATE IMAGE  |   |
|       |  (Address: 0x08020000)          |  |  (Address: 0x08040000)           |   |
|       |  * Known-good failsafe firmware |  |  * New deployment under trial    |   |
|       +---------------------------------+  +----------------------------------+   |
+-----------------------------------------------------------------------------------+
```

---

## 💾 Flash Memory Map (512 KB On-Chip Flash)

| Sector | Base Address | Size | Purpose | Description |
| :---: | :---: | :---: | :--- | :--- |
| **0 - 3** | `0x08000000` | 64 KB | **Bootloader Core** | Vector table, IAP logic, crypto verification, rollback state machine |
| **4** | `0x08010000` | 64 KB | **Bootloader Reserved** | Bootloader data, configuration, and shared headers |
| **5** | `0x08020000` | 128 KB | **Slot 0: Golden Image** | Factory-flashed, verified, non-volatile recovery firmware |
| **6 - 7** | `0x08040000` | 256 KB | **Slot 1: Candidate Image**| Active staging slot for live wireless OTA deployments |

---

## ⚡ Key Engineering Highlights

### 1. Zero-Brick Automated Rollback Mechanism
* Newly deployed firmware in **Slot 1** is executed on probationary status.
* The bootloader activates the **Independent Watchdog (`IWDG`)** with a 3-second timeout window prior to jumping to the candidate image.
* **Health Check & Confirmation:** The application must successfully complete startup self-tests and feed/refresh the watchdog.
* **Failure Interception:** If the candidate application hard-faults, hangs, or fails to service the watchdog, the hardware reset triggers. Upon reboot, the bootloader reads `RCC->CSR & RCC_CSR_IWDGRSTF`, clears the flag, atomically updates `RTC->BKP0R = 0`, and immediately falls back to the **Slot 0 Golden Image**.

### 2. Clean Execution Handover (Bare-Metal Context Switch)
To guarantee glitch-free execution transfer from bootloader to application:
1. Global interrupts disabled (`__disable_irq()`).
2. Systick timer stopped and reset (`SysTick->CTRL = 0`).
3. Clocks and peripheral drivers deinitialized (`HAL_RCC_DeInit()`, `HAL_DeInit()`).
4. All pending NVIC interrupt enables and flags cleared (`NVIC->ICER`, `NVIC->ICPR`).
5. Vector Table Offset Register relocated to target base (`SCB->VTOR = app_address`).
6. Memory Barrier instructions issued (`__DSB()`, `__ISB()`).
7. Application initial Main Stack Pointer (`MSP`) loaded and branch executed to reset handler via inline assembly:
   ```c
   __asm volatile ("msr msp, %0 \n bx %1 \n" : : "r"(app_stack), "r"(app_reset));
   ```

### 3. Multi-Stage Cryptographic & Integrity Verification
* **AES-256-CBC Encryption:** Protects proprietary firmware binaries in transit over wireless networks.
* **CRC-32 Checksum:** Validates frame transmission and on-chip Flash integrity.
* **SHA-256 Digest:** 32-byte pre-boot cryptographic validation comparing calculated payload digest against deployment header.
* **Chunked Handshaking:** Synchronous 256-byte chunk transfer with `READY`, `HDR_OK`, and `ACK`/`NACK` packet flow control.

---

## 🔌 Hardware Pinout & Wiring

| ESP32 Pin | STM32 Nucleo Pin | Signal Name | Notes |
| :---: | :---: | :---: | :--- |
| **GPIO 17 (TX2)** | **PA3 (RX / D0)** | UART Data (ESP32 -> STM32) | Downstream firmware chunks |
| **GPIO 16 (RX2)** | **PA2 (TX / D1)** | UART Data (STM32 -> ESP32) | Status logs, ACK, Ready signals |
| **GND** | **GND** | Common Ground | Required for clean signal reference |

---

## 🚀 Getting Started

### Prerequisites
* **STM32CubeIDE** (v1.14+ or equivalent GNU Arm Embedded Toolchain)
* **Arduino IDE** (with ESP32 board support package installed)
* **Python 3.9+** with required libraries:
  ```bash
  pip install -r host_tools/requirements.txt
  ```

### Build & Deployment Steps

1. **Flash STM32 Bootloader:**
   * Open `bootloader/` in STM32CubeIDE.
   * Build project (`Release` or `Debug`).
   * Flash via ST-LINK to `0x08000000`.

2. **Flash Slot 0 (Golden Image):**
   * Open `user_application/` in STM32CubeIDE.
   * Ensure linker script `STM32F446RETX_FLASH.ld` points to `ORIGIN = 0x08020000`.
   * Flash to `0x08020000` via ST-LINK.

3. **Deploy ESP32 Wi-Fi Gateway:**
   * Open `esp32_gateway/esp32_wifi_bridge.ino` in Arduino IDE.
   * Select your ESP32 board and upload.
   * The ESP32 will broadcast the Wi-Fi AP: `STM32_OTA_SERVER` (Password: `password123`).

4. **Perform Wireless OTA Update:**
   * Connect workstation Wi-Fi to `STM32_OTA_SERVER`.
   * Generate updated binary for Slot 1 (`ORIGIN = 0x08040000`).
   * Run deployment client:
     ```bash
     python host_tools/wifi_ota_sender.py
     ```

---

## 📂 Repository Structure

```
.
├── bootloader/             # STM32F446RE IAP Bootloader firmware (C/Assembly)
│   ├── Core/               # HAL initialization, UART driver, jump routines
│   └── STM32F446RETX_FLASH.ld # Bootloader linker script (0x08000000)
├── user_application/       # User application firmware (Golden & Candidate slots)
│   ├── Core/               # Application logic, watchdog service, VTOR setup
│   └── STM32F446RETX_FLASH.ld # Application linker script (0x08020000 / 0x08040000)
├── esp32_gateway/          # ESP32 Wi-Fi TCP-to-UART bridge firmware (.ino)
├── host_tools/             # Python OTA deployment tools (Wi-Fi & Serial)
│   ├── wifi_ota_sender.py  # TCP socket chunk sender with AES & SHA-256
│   ├── ota_sender.py       # Direct serial/UART fallback uploader
│   └── requirements.txt    # Python dependencies
├── .gitignore              # Build and IDE ignore rules
└── README.md               # Complete architecture & documentation
```

---

## 📜 License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
