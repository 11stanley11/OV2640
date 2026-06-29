import serial
import base64
import os

# Configure the serial port parameters
# Update COM_PORT to match your ESP32-S3 port (e.g., 'COM3' on Windows or '/dev/ttyACM0' on Linux)
COM_PORT = 'COM3'
BAUD_RATE = 115200
OUTPUT_DIR = './captured_images'

os.makedirs(OUTPUT_DIR, exist_ok=True)

print(f"Connecting to ESP32-S3 on {COM_PORT} at {BAUD_RATE} baud...")
try:
    ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=1)
except Exception as e:
    print(f"Error opening serial port: {e}")
    print("Please make sure the COM port is correct and not occupied by the Arduino Serial Monitor.")
    exit(1)

print("Listening for incoming image data blocks...")
print("Trigger a capture in the serial CLI (send 'c'). Press Ctrl+C to exit.")

buffer = []
receiving = False
metadata = {}

try:
    while True:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if not line:
            continue
            
        if line.startswith("---START_IMAGE:"):
            # Header format: ---START_IMAGE:format:width:height:size---
            parts = line.split(":")
            if len(parts) >= 5:
                metadata = {
                    "format": int(parts[1]),
                    "width": int(parts[2]),
                    "height": int(parts[3]),
                    "size": int(parts[4])
                }
                receiving = True
                buffer = []
                print(f"\n📥 Receiving new image frame: {metadata['width']}x{metadata['height']}, Size: {metadata['size']} bytes...")
            
        elif line.startswith("---END_IMAGE---"):
            if receiving:
                receiving = False
                base64_data = "".join(buffer)
                try:
                    # Decode base64
                    img_bytes = base64.b64decode(base64_data)
                    
                    # Decide extension based on format
                    # 0=GRAYSCALE, 1=RGB565, 2=YUV422, 3=JPEG
                    fmt_id = metadata.get("format", 3)
                    if fmt_id == 3:
                        ext = "jpg"
                    elif fmt_id == 0:
                        ext = "gray"
                    elif fmt_id == 1:
                        ext = "rgb565"
                    else:
                        ext = "yuv422"
                        
                    filename = f"image_{metadata.get('width','96')}x{metadata.get('height','96')}_{len(img_bytes)}.{ext}"
                    filepath = os.path.join(OUTPUT_DIR, filename)
                    
                    with open(filepath, "wb") as f:
                        f.write(img_bytes)
                        
                    print(f"💾 Image decoded and saved successfully to: {filepath}")
                    
                    if fmt_id == 3:
                        print("ℹ️ File is a valid JPEG, you can open it directly in any viewer.")
                    elif fmt_id == 0:
                        print(f"ℹ️ Raw Grayscale file. You can open and visualize it in Python using numpy/OpenCV:")
                        print(f"    import numpy as np, cv2")
                        print(f"    data = np.fromfile('{filepath}', dtype=np.uint8).reshape(({metadata['height']}, {metadata['width']}))")
                        print(f"    cv2.imshow('Image', data); cv2.waitKey(0)")
                except Exception as ex:
                    print(f"❌ Failed to decode Base64 image: {ex}")
            
        elif receiving:
            buffer.append(line)
        else:
            # Print standard output lines from ESP32 CLI (logs, statistics, menus)
            print(f"[ESP32] {line}")
            
except KeyboardInterrupt:
    print("\nExiting script.")
finally:
    ser.close()
