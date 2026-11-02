import socket
import time
import os
import struct
import binascii
import hashlib
from Crypto.Cipher import AES

# --- CONFIGURATION ---
ESP32_IP   = '10.156.71.129'
ESP32_PORT = 8080
FILE_PATH  = r"C:\Users\ayan9\OneDrive\Documents\f446_userapplication\f446_userapplication\Debug\f446_userapplication.bin"

AES_KEY = bytes([
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F
])
AES_IV = bytes([
    0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF
])

def wait_for_string(sock, target, timeout=10):
    start = time.time()
    buffer = ""
    while (time.time() - start) < timeout:
        try:
            char = sock.recv(1).decode('utf-8', errors='ignore')
            if char:
                buffer += char
                print(char, end='', flush=True)
                if target in buffer:
                    return True
        except socket.timeout:
            pass
    return False

def main():
    if not os.path.exists(FILE_PATH):
        print(f"Error: File not found at {FILE_PATH}")
        return

    # --- CONNECT TO ESP32 OVER WIFI ---
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(7.0)
    try:
        print(f"Connecting to ESP32 at {ESP32_IP}:{ESP32_PORT}...")
        sock.connect((ESP32_IP, ESP32_PORT))
        print("Connected via WiFi!")
    except Exception as e:
        print(f"WiFi Connection failed: {e}")
        return

    # Drain initial heartbeat output for 0.5s
    sock.settimeout(0.2)
    start_drain = time.time()
    while time.time() - start_drain < 0.8:
        try:
            drain = sock.recv(1024)
            if drain:
                print(drain.decode('utf-8', errors='ignore'), end='', flush=True)
        except socket.timeout:
            break

    # Reset socket timeout for protocol transactions
    sock.settimeout(1.0)

    with open(FILE_PATH, 'rb') as f:
        firmware_data = f.read()

    # --- CRYPTO & HASHING ---
    fw_size = len(firmware_data)
    fw_crc  = binascii.crc32(firmware_data) & 0xFFFFFFFF
    sha256_hash = hashlib.sha256(firmware_data).digest()

    print("\n-----------------------------------------")
    print(f"Firmware payload : {os.path.basename(FILE_PATH)}")
    print(f"Firmware size    : {fw_size} bytes")
    print(f"CRC32            : 0x{fw_crc:08X}")
    print(f"SHA-256          : {sha256_hash.hex()}")
    print("-----------------------------------------")

    chunk_size = 256
    padding_needed = chunk_size - (fw_size % chunk_size)
    if padding_needed != chunk_size:
        padded_firmware = firmware_data + (b'\xFF' * padding_needed)
    else:
        padded_firmware = firmware_data

    cipher = AES.new(AES_KEY, AES.MODE_CBC, AES_IV)
    encrypted_firmware = cipher.encrypt(padded_firmware)

    print("\n[STEP 1] Sending OTA_START trigger ('O') over WiFi...")
    sock.sendall(b"O")

    print("[STEP 2] Waiting for STM32 to erase Slot 1 Flash and reply READY...")
    if not wait_for_string(sock, "READY\r\n", timeout=15):
        print("\nError: Did not receive READY. Aborting.")
        sock.close()
        return

    # --- SEND 40-BYTE HEADER ---
    print("\n[STEP 3] Sending 40-byte Header: [size | crc32 | sha256]...")
    header = struct.pack('<II', fw_size, fw_crc) + sha256_hash
    sock.sendall(header)
    
    print("Waiting for HDR_OK...")
    if not wait_for_string(sock, "HDR_OK\r\n", timeout=5):
        print("\nError: Did not receive HDR_OK. Aborting.")
        sock.close()
        return

    print("\n[STEP 4] Device ready! Streaming AES-256 encrypted chunks over Wi-Fi...\n")

    # --- SEND CHUNKS ---
    total_chunks = len(encrypted_firmware) // chunk_size
    for i in range(total_chunks):
        chunk = encrypted_firmware[i*chunk_size : (i+1)*chunk_size]
        print(f"Sending chunk {i+1}/{total_chunks} ({len(chunk)} bytes)... ", end='', flush=True)
        sock.sendall(chunk)

        if not wait_for_string(sock, "ACK\r\n", timeout=5):
            print(f"\nError: No ACK for chunk {i+1}. Aborting.")
            sock.close()
            return

    print("\n\n[STEP 5] All chunks streamed! Live monitoring verification & reboot...\n")
    start_v = time.time()
    while (time.time() - start_v) < 6:
        try:
            data = sock.recv(1024)
            if not data:
                break
            print(data.decode('utf-8', errors='ignore'), end='', flush=True)
        except socket.timeout:
            pass
        except Exception:
            break

    sock.close()
    print("\n\n=======================================================")
    print("      🎉 WIRELESS OTA DEPLOYMENT COMPLETE! 🎉          ")
    print("=======================================================")

if __name__ == "__main__":
    main()

