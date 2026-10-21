import serial
import time
import os
import struct
import binascii
import hashlib
from Crypto.Cipher import AES
from Crypto.Util.Padding import pad

# --- CONFIGURATION ---
COM_PORT  = 'COM9'
BAUD_RATE = 115200
FILE_PATH = r"C:\Users\ayan9\OneDrive\Documents\f446_userapplication\f446_userapplication\Debug\f446_userapplication.bin"

# Must match STM32 EXACTLY
AES_KEY = bytes([
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F
])
AES_IV = bytes([
    0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF
])

def wait_for_string(ser, target, timeout=10):
    start = time.time()
    buffer = ""
    while (time.time() - start) < timeout:
        if ser.in_waiting:
            char = ser.read().decode('utf-8', errors='ignore')
            buffer += char
            print(char, end='', flush=True)
            if target in buffer:
                return True
    return False

def main():
    if not os.path.exists(FILE_PATH):
        print(f"Error: File not found at {FILE_PATH}")
        return

    try:
        ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=1)
        print(f"Connected to {COM_PORT}")
    except Exception as e:
        print(f"Error opening port: {e}")
        return

    with open(FILE_PATH, 'rb') as f:
        firmware_data = f.read()

    # --- 1. Calculate Metadata (on Plaintext) ---
    fw_size = len(firmware_data)
    fw_crc  = binascii.crc32(firmware_data) & 0xFFFFFFFF
    
    sha256_hash = hashlib.sha256(firmware_data).digest()

    print(f"Firmware size : {fw_size} bytes")
    print(f"CRC32         : 0x{fw_crc:08X}")
    print(f"SHA-256       : {sha256_hash.hex()}")

    # --- 2. Encrypt Firmware (AES-256-CBC) ---
    # We must pad the firmware to be a multiple of 256 bytes for chunking
    # But AES only requires multiples of 16. We pad with 0xFF.
    chunk_size = 256
    padding_needed = chunk_size - (fw_size % chunk_size)
    if padding_needed != chunk_size:
        padded_firmware = firmware_data + (b'\xFF' * padding_needed)
    else:
        padded_firmware = firmware_data

    cipher = AES.new(AES_KEY, AES.MODE_CBC, AES_IV)
    encrypted_firmware = cipher.encrypt(padded_firmware)

    print("\nSending OTA_START trigger...")
    ser.write(b"O")

    print("Waiting for STM32 to erase flash and reply READY...")
    if not wait_for_string(ser, "READY\r\n", timeout=15):
        print("\nError: Did not receive READY. Aborting.")
        ser.close()
        return

    # --- 3. Send 40-byte Header: [size(4)][crc(4)][sha256(32)] ---
    header = struct.pack('<II', fw_size, fw_crc) + sha256_hash
    ser.write(header)
    
    print("\nHeader sent. Waiting for HDR_OK...")
    if not wait_for_string(ser, "HDR_OK\r\n", timeout=5):
        print("\nError: Did not receive HDR_OK. Aborting.")
        ser.close()
        return

    print("\nDevice ready! Starting encrypted chunk transfer...\n")

    # --- 4. Send Encrypted Firmware in Chunks ---
    total_chunks = len(encrypted_firmware) // chunk_size

    for i in range(total_chunks):
        chunk = encrypted_firmware[i*chunk_size : (i+1)*chunk_size]
        print(f"Sending encrypted chunk {i+1}/{total_chunks}...")
        ser.write(chunk)

        if not wait_for_string(ser, "ACK\r\n", timeout=5):
            print(f"\nError: No ACK for chunk {i+1}. Aborting.")
            ser.close()
            return

    print("\n\nAll encrypted chunks sent! Waiting for device verification...\n")
    time.sleep(4)
    while ser.in_waiting:
        print(ser.read().decode('utf-8', errors='ignore'), end='')

    ser.close()
    print("\nOTA Complete!")

if __name__ == "__main__":
    main()
