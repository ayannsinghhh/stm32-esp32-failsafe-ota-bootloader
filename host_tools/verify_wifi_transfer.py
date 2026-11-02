import socket
import serial
import time
import threading

ESP32_IP = "10.156.71.129"
ESP32_PORT = 8080
STM32_COM = "COM9"

print("=" * 65)
print("     STM32 <-> ESP32 WI-FI & UART BI-DIRECTIONAL TEST       ")
print("=" * 65)

# Thread to monitor STM32 COM9 console
stm32_logs = []
stop_event = threading.Event()

def monitor_stm32():
    try:
        s9 = serial.Serial(STM32_COM, 115200, timeout=0.1)
        while not stop_event.is_set():
            if s9.in_waiting:
                chunk = s9.read(s9.in_waiting).decode('utf-8', errors='replace')
                stm32_logs.append(chunk)
            time.sleep(0.01)
        s9.close()
    except Exception as e:
        print(f"[COM9 Monitor Error]: {e}")

mon_thread = threading.Thread(target=monitor_stm32)
mon_thread.daemon = True
mon_thread.start()

print(f"\n[1] Connecting to ESP32 Wi-Fi bridge at {ESP32_IP}:{ESP32_PORT}...")
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.settimeout(5.0)
sock.connect((ESP32_IP, ESP32_PORT))
print("  --> CONNECTED over Wi-Fi TCP!\n")

print("[2] Listening to Wi-Fi stream from STM32 (D8 -> ESP32 G26 -> TCP) for 3 seconds:")
stream_data = b""
start = time.time()
while time.time() - start < 3.0:
    try:
        chunk = sock.recv(1024)
        if chunk:
            stream_data += chunk
            print(f"  [Wi-Fi IN]: {repr(chunk.decode('utf-8', errors='replace'))}")
    except socket.timeout:
        pass
    time.sleep(0.05)

print(f"\n  --> Total bytes received from STM32 over Wi-Fi: {len(stream_data)}")

print("\n[3] Sending 'O' trigger over Wi-Fi (TCP -> ESP32 G25 -> STM32 D2)...")
sock.sendall(b"O")

# Listen for response (e.g., READY)
response = b""
start = time.time()
while time.time() - start < 3.0:
    try:
        chunk = sock.recv(1024)
        if chunk:
            response += chunk
            if b"READY" in response:
                break
    except socket.timeout:
        pass
    time.sleep(0.05)

stop_event.set()
mon_thread.join(timeout=1.0)
sock.close()

print(f"  --> Response received over Wi-Fi: {repr(response.decode('utf-8', errors='replace'))}")
print(f"\n[4] STM32 Internal Console Log (COM9):")
print("".join(stm32_logs))

print("=" * 65)
if len(stream_data) > 0:
    print("  RESULT: STM32 -> ESP32 direction is WORKING! [PASS]")
if "READY" in "".join(stm32_logs) or b"READY" in response:
    print("  RESULT: ESP32 -> STM32 direction is WORKING! [PASS]")
    print("  STATUS: COMPLETE BI-DIRECTIONAL OTA HANDSHAKE VERIFIED!")
print("=" * 65)
