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
|   |  * Streams 256-byte chunks with synchronous ACK/NACK flow control         |   |
|   +---------------------------------------------------------------------------+   |
+------------------------------------------|----------------------------------------+
                                           | TCP Socket (Port 8080)
                                           v
+-----------------------------------------------------------------------------------+
|                         WIRELESS GATEWAY (ESP32 Node)                             |
|   +---------------------------------------------------------------------------+   |
|   |  esp32_wifi_bridge.ino / PlatformIO firmware                              |   |
|   |  * Connects to WLAN / Hosts TCP Server on Port 8080                       |   |
|   |  * Bi-directional non-blocking TCP socket <---> UART2 bridge (115200 8N1) |   |
|   +---------------------------------------------------------------------------+   |
+------------------------------------------|----------------------------------------+
                                           | Full-Duplex UART (115200 Baud)
                                           v
+-----------------------------------------------------------------------------------+
|                     TARGET EMBEDDED SYSTEM (STM32F446RE)                          |
|   +---------------------------------------------------------------------------+   |
|   |  BARE-METAL BOOTLOADER (0x08000000)                                       |   |
|   |  * Evaluates RTC->BKP0R & RCC_CSR_IWDGRSTF reset flags                    |   |
|   |  * Automatic Rollback to Golden Image if candidate hangs                  |   |
|   |  * De-initializes peripherals, masks NVIC, relocates SCB->VTOR & sets MSP |   |
|   +---------------------------------------------------------------------------+   |
|                        |                                 |                        |
|                        v                                 v                        |
|       +---------------------------------+  +----------------------------------+   |
|       |  SLOT 0: GOLDEN / FACTORY IMAGE |  |  SLOT 1: CANDIDATE UPDATE IMAGE  |   |
|       |  (Address: 0x08020000)          |  |  (Address: 0x08040000)           |   |
|       |  * Known-good recovery firmware |  |  * New deployment under trial    |   |
|       |  * Active OTA Flash & Decryption|  |  * Distinct Double-Blink PA5 LED |   |
|       |  * Arms RTC->BKP0R & Reboots    |  |  * Watchdog probation check      |   |
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
* **Health Check & Confirmation:** The candidate application must successfully complete startup self-tests and feed/refresh the watchdog (`IWDG_KR = 0xAAAA`).
* **Failure Interception:** If the candidate application hard-faults, hangs, or fails to service the watchdog, a hardware reset triggers. Upon reboot, the bootloader reads `RCC->CSR & RCC_CSR_IWDGRSTF`, clears the flag, atomically updates `RTC->BKP0R = 0`, and immediately falls back to the **Slot 0 Golden Image**.

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
* **CRC-32 Checksum:** Validates frame transmission and on-chip Flash integrity (IEEE 802.3 standard).
* **SHA-256 Digest:** 32-byte pre-boot cryptographic validation comparing calculated payload digest against deployment header.
* **Chunked Handshaking:** Synchronous 256-byte chunk transfer with `READY`, `HDR_OK`, and `ACK`/`NACK` packet flow control.

---

## 🔌 Hardware Pinout & Wiring

| ESP32 DevKit Pin | STM32 Nucleo Pin | Signal Name | Description |
| :---: | :---: | :---: | :--- |
| **GPIO 26 (RX2)** | **D8 (PA9 / USART1_TX)** | UART Data (STM32 -> ESP32) | Status logs, ACK, Ready signals |
| **GPIO 25 (TX2)** | **D2 (PA10 / USART1_RX)**| UART Data (ESP32 -> STM32) | Downstream encrypted firmware chunks |
| **GND** | **GND** (CN5 pin 4) | Common Ground | Required for clean signal reference |

> [!IMPORTANT]
> **Nucleo Solder Bridge Note:** On factory NUCLEO-F446RE boards, Arduino headers `D0` (PA3) and `D1` (PA2) have solder bridges `SB62` and `SB63` **OPEN** by default, routing `USART2` strictly to the onboard ST-LINK USB VCP (`COM9`). 
> Wiring the ESP32 to **`D8` (PA9)** and **`D2` (PA10)** connects directly to **`USART1`**, enabling dual-UART operation with zero soldering required!

---

## 🚀 Getting Started

### Prerequisites
* **STM32CubeIDE** (v1.14+ or equivalent GNU Arm Embedded Toolchain)
* **STM32CubeProgrammer**
* **PlatformIO** or **Arduino IDE** (with ESP32 board support package)
* **Python 3.9+** with required libraries:
  ```bash
  pip install -r host_tools/requirements.txt
  ```

---

### Step-by-Step Deployment

#### 1. Flash the STM32 Bootloader
* Open `bootloader/` in STM32CubeIDE.
* Build project (`Ctrl + B`).
* Flash `bootloader.bin` via ST-LINK to address **`0x08000000`**.

#### 2. Flash Slot 0 (Golden Image)
* Open `user_application/` in STM32CubeIDE.
* Ensure linker script `STM32F446RETX_FLASH.ld` points to:
  ```ld
  FLASH (rx) : ORIGIN = 0x08020000, LENGTH = 128K
  ```
* Build project and flash `f446_userapplication.bin` to address **`0x08020000`**.

#### 3. Upload ESP32 Gateway Firmware
* In PlatformIO: Run `pio run -t upload` inside `esp32_gateway/`.
* Or in Arduino IDE: Open `esp32_gateway/esp32_wifi_bridge.ino`, configure Wi-Fi credentials, and upload.
* Note the IP address printed on the serial console (e.g., `10.156.71.129`).

#### 4. Verify Bi-Directional Wireless Link
Run the diagnostic script to ensure full end-to-end handshake:
```bash
python host_tools/verify_wifi_transfer.py
```

#### 5. Generate Candidate Binary & Deploy Over-The-Air
1. In `user_application/Core/Src/main.c`, use the candidate code from `user_application/candidate_slot1/main.c`.
2. In `user_application/STM32F446RETX_FLASH.ld`, update Flash origin to Slot 1:
   ```ld
   FLASH (rx) : ORIGIN = 0x08040000, LENGTH = 256K
   ```
3. Press `Ctrl + B` in STM32CubeIDE to compile the candidate binary (*do not flash via ST-LINK!*).
4. Run the wireless OTA deployment client:
   ```bash
   python host_tools/wifi_ota_sender.py
   ```
5. Watch the real-time progress:
   - Chunk-by-chunk encrypted transfer over Wi-Fi
   - STM32 on-chip decryption & Flash sector programming
   - CRC-32 & SHA-256 verification
   - Automatic handoff: Bootloader verifies and jumps to Slot 1 (`0x08040000`)
   - Distinct double-blink LED confirms active Slot 1 execution!

---

## 📂 Repository Structure

```
.
├── bootloader/                 # STM32F446RE IAP Bootloader firmware (C/Assembly)
│   ├── Core/                   # HAL initialization, UART driver, jump routines
│   ├── Drivers/                # CMSIS & STM32F4xx HAL Driver libraries
│   └── STM32F446RETX_FLASH.ld  # Bootloader linker script (0x08000000)
├── user_application/           # User application firmware (Golden & Candidate slots)
│   ├── Core/                   # Dual-UART, AES-256 decryption, SHA-256 digest, Flash write
│   ├── Drivers/                # CMSIS & STM32F4xx HAL Driver libraries
│   ├── candidate_slot1/        # Reference candidate image source (Slot 1 double-blink app)
│   └── STM32F446RETX_FLASH.ld  # Application linker script (0x08020000 / 0x08040000)
├── esp32_gateway/              # ESP32 Wi-Fi TCP-to-UART bridge
│   ├── esp32_wifi_bridge.ino   # Arduino IDE sketch (Pins 25/26, 115200 baud)
│   ├── platformio.ini          # PlatformIO project configuration
│   └── src/                    # PlatformIO source code
├── host_tools/                 # Python OTA deployment tools (Wi-Fi & Serial)
│   ├── wifi_ota_sender.py      # TCP socket chunk sender with AES-256 & SHA-256
│   ├── verify_wifi_transfer.py # Bi-directional hardware handshake test utility
│   ├── ota_sender.py           # Direct serial/UART fallback uploader
│   └── requirements.txt        # Python dependencies
├── .gitignore                  # Build and IDE ignore rules
└── README.md                   # Complete architecture & documentation
```

---

## 📜 License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
