import serial
import base64
import os
import threading
import sys
import time

try:
    from PIL import Image
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

# Configure the serial port parameters
# Update COM_PORT to match your ESP32-S3 port (e.g., 'COM3' on Windows or '/dev/ttyACM0' on Linux)
COM_PORT = 'COM9'
BAUD_RATE = 115200
OUTPUT_DIR = './captured_images'

os.makedirs(OUTPUT_DIR, exist_ok=True)

print(f"Connecting to ESP32-S3 on {COM_PORT} at {BAUD_RATE} baud...")
try:
    # Use a short timeout so serial reading is non-blocking and responsive
    ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=0.1)
except Exception as e:
    print(f"Error opening serial port: {e}")
    print("Please make sure the COM port is correct and not occupied by the Arduino Serial Monitor.")
    exit(1)

buffer = []
receiving = False
metadata = {}

# Background thread to continuously read from the serial port
def read_serial_thread():
    global receiving, buffer, metadata
    while True:
        try:
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
                        "size": int(parts[4].rstrip("-"))
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
                        
                        # Decide extension based on Espressif's pixformat_t enum:
                        # 0=RGB565, 1=YUV422, 2=YUV420, 3=GRAYSCALE, 4=JPEG
                        fmt_id = metadata.get("format", 4)
                        if fmt_id == 4:
                            ext = "jpg"
                        elif fmt_id == 3:
                            ext = "gray"
                        elif fmt_id == 0:
                            ext = "rgb565"
                        elif fmt_id == 1:
                            ext = "yuv422"
                        else:
                            ext = "bin"
                            
                        timestamp = time.strftime("%Y%m%d_%H%M%S")
                        filename = f"image_{timestamp}_{metadata.get('width','96')}x{metadata.get('height','96')}_{len(img_bytes)}.{ext}"
                        filepath = os.path.join(OUTPUT_DIR, filename)
                        
                        with open(filepath, "wb") as f:
                            f.write(img_bytes)
                            
                        print(f"Image decoded and saved successfully to: {filepath}")
                        
                        if fmt_id == 4:
                            print("File is a valid JPEG, you can open it directly in any viewer.")
                        else:
                            if HAS_PIL:
                                png_filename = filename.rsplit(".", 1)[0] + ".png"
                                png_filepath = os.path.join(OUTPUT_DIR, png_filename)
                                
                                try:
                                    if fmt_id == 3:  # Grayscale
                                        img = Image.frombytes('L', (metadata['width'], metadata['height']), img_bytes)
                                        img.save(png_filepath)
                                        print(f"🖼️ PNG version automatically generated and saved to: {png_filepath}")
                                    elif fmt_id == 0:  # RGB565
                                        # Convert RGB565 (16-bit) to RGB888
                                        rgb888 = bytearray(metadata['width'] * metadata['height'] * 3)
                                        idx = 0
                                        for i in range(0, len(img_bytes), 2):
                                            if i + 1 < len(img_bytes):
                                                val = (img_bytes[i] << 8) | img_bytes[i+1]
                                                r = ((val >> 11) & 0x1F) << 3
                                                g = ((val >> 5) & 0x3F) << 2
                                                b = (val & 0x1F) << 3
                                                rgb888[idx] = r
                                                rgb888[idx+1] = g
                                                rgb888[idx+2] = b
                                                idx += 3
                                        img = Image.frombytes('RGB', (metadata['width'], metadata['height']), bytes(rgb888))
                                        img.save(png_filepath)
                                        print(f"🖼️ PNG version automatically generated and saved to: {png_filepath}")
                                except Exception as pil_ex:
                                    print(f"Failed to generate PNG image: {pil_ex}")
                            else:
                                print("Install Pillow ('pip install Pillow') to automatically convert raw images to PNG.")
                    except Exception as ex:
                        print(f"❌ Failed to decode Base64 image: {ex}")
                
            elif receiving:
                buffer.append(line)
            else:
                # Print standard output lines from ESP32 (logs, statistics)
                print(f"[ESP32] {line}")
        except Exception as e:
            # If serial connection is closed/broken, terminate thread
            break

# Camera settings state tracking (local representation of ESP32 state)
camera_state = {
    "format": "GRAYSCALE",
    "resolution": "96x96",
    "aec": 1,         # 1 = Auto, 0 = Manual
    "exposure": 0,    # 0 to 1200
    "agc": 1,         # 1 = Auto, 0 = Manual
    "gain": 0,        # 0 to 30
    "brightness": 0,  # -2 to 2
    "contrast": 0,    # -2 to 2
    "saturation": 0,  # -2 to 2
    "mirror": 0,      # 0 = Disabled, 1 = Enabled
    "flip": 0         # 0 = Disabled, 1 = Enabled
}

def print_help_menu():
    print("\n" + "="*60)
    print("💻 INTERACTIVE CONTROL INTERFACE (Python Host)")
    print("="*60)
    print("Type commands directly here and press Enter.")
    print("Commands are validated on the PC before sending to ESP32.")
    print("-"*60)
    print("📁 General commands:")
    print("  h        - Display this Help menu")
    print("  c        - Trigger image acquisition (saved to PC & CV processed on ESP32)")
    print("\n🎨 Pixel Format (f <0-3>):")
    print("  f0       - GRAYSCALE (1 byte per pixel)")
    print("  f1       - RGB565    (2 bytes per pixel)")
    print("  f2       - YUV422    (2 bytes per pixel)")
    print("  f3       - JPEG      (Compressed, variable size)")
    print("\n📐 Resolution (s <0-5>):")
    print("  s0       - 96x96")
    print("  s1       - QQVGA (160x120)")
    print("  s2       - QVGA  (320x240)")
    print("  s3       - VGA   (640x480)")
    print("  s4       - SVGA  (800x600)")
    print("  s5       - UXGA  (1600x1200) (Use JPEG format for RAM safety)")
    print("\nExposure & Sensor Controls:")
    print("  e1 / e0  - Enable/Disable Auto Exposure Control (AEC)")
    print("  v <0-1200>- Manual Exposure index (Only active when AEC is disabled)")
    print("  g1 / g0  - Enable/Disable Auto Gain Control (AGC)")
    print("  a <0-30> - Manual Gain index (Only active when AGC is disabled)")
    print("  b <value>- Set Brightness (-2 to 2)")
    print("  t <value>- Set Contrast (-2 to 2)")
    print("  x <value>- Set Saturation (-2 to 2)")
    print("  m1 / m0  - Enable/Disable Horizontal Mirror")
    print("  p1 / p0  - Enable/Disable Vertical Flip")
    print("-"*60)
    print(f"Current State: Format = {camera_state['format']} | Resolution = {camera_state['resolution']}")
    print(f"               AEC = {'AUTO' if camera_state['aec'] else 'MANUAL'} | Exposure = {camera_state['exposure']}")
    print(f"               AGC = {'AUTO' if camera_state['agc'] else 'MANUAL'} | Gain = {camera_state['gain']}")
    print(f"               Brightness = {camera_state['brightness']} | Contrast = {camera_state['contrast']} | Saturation = {camera_state['saturation']}")
    print(f"               Mirror = {'ENABLED' if camera_state['mirror'] else 'DISABLED'} | Flip = {'ENABLED' if camera_state['flip'] else 'DISABLED'}")
    print("="*60 + "\n")

def handle_command(cmd_str):
    global camera_state
    cmd_str = cmd_str.strip()
    if not cmd_str:
        return False
        
    action = cmd_str[0].lower()
    arg_str = cmd_str[1:].strip()
    
    if action == 'h':
        print_help_menu()
        return False
        
    if action == 'c':
        print("[Python UI] Triggering capture workflow...")
        ser.write(b"c\n")
        return True
        
    # All other commands expect an integer argument
    try:
        val = int(arg_str)
    except ValueError:
        print(f"❌ Error: Command '{action}' requires an integer argument.")
        return False
        
    if action == 'f':
        formats = {0: "GRAYSCALE", 1: "RGB565", 2: "YUV422", 3: "JPEG"}
        if val not in formats:
            print("❌ Error: Invalid format (0=Grayscale, 1=RGB565, 2=YUV422, 3=JPEG)")
            return False
        camera_state["format"] = formats[val]
        print(f"[Python UI] Changing pixel format to {formats[val]}...")
        ser.write(f"f{val}\n".encode('utf-8'))
        return True
        
    elif action == 's':
        resolutions = {
            0: "96x96",
            1: "QQVGA (160x120)",
            2: "QVGA (320x240)",
            3: "VGA (640x480)",
            4: "SVGA (800x600)",
            5: "UXGA (1600x1200)"
        }
        if val not in resolutions:
            print("❌ Error: Invalid resolution (0=96x96, 1=QQVGA, 2=QVGA, 3=VGA, 4=SVGA, 5=UXGA)")
            return False
        camera_state["resolution"] = resolutions[val]
        print(f"[Python UI] Changing resolution to {resolutions[val]}...")
        ser.write(f"s{val}\n".encode('utf-8'))
        return True
        
    elif action == 'e':
        if val not in [0, 1]:
            print("❌ Error: AEC value must be 0 (Disable) or 1 (Enable)")
            return False
        camera_state["aec"] = val
        print(f"[Python UI] Auto Exposure Control (AEC) set to {'AUTO' if val else 'MANUAL'}")
        ser.write(f"e{val}\n".encode('utf-8'))
        return True
        
    elif action == 'v':
        if not (0 <= val <= 1200):
            print("❌ Error: Manual Exposure index must be between 0 and 1200")
            return False
        camera_state["exposure"] = val
        print(f"[Python UI] Manual Exposure set to {val} (AEC must be disabled for this to take effect)")
        ser.write(f"v{val}\n".encode('utf-8'))
        return True
        
    elif action == 'g':
        if val not in [0, 1]:
            print("❌ Error: AGC value must be 0 (Disable) or 1 (Enable)")
            return False
        camera_state["agc"] = val
        print(f"[Python UI] Auto Gain Control (AGC) set to {'AUTO' if val else 'MANUAL'}")
        ser.write(f"g{val}\n".encode('utf-8'))
        return True
        
    elif action == 'a':
        if not (0 <= val <= 30):
            print("❌ Error: Manual Gain index must be between 0 and 30")
            return False
        camera_state["gain"] = val
        print(f"[Python UI] Manual Gain set to {val} (AGC must be disabled for this to take effect)")
        ser.write(f"a{val}\n".encode('utf-8'))
        return True
        
    elif action == 'b':
        if not (-2 <= val <= 2):
            print("❌ Error: Brightness offset must be between -2 and 2")
            return False
        camera_state["brightness"] = val
        print(f"[Python UI] Brightness set to {val}")
        ser.write(f"b{val}\n".encode('utf-8'))
        return True
        
    elif action == 't':
        if not (-2 <= val <= 2):
            print("❌ Error: Contrast offset must be between -2 and 2")
            return False
        camera_state["contrast"] = val
        print(f"[Python UI] Contrast set to {val}")
        ser.write(f"t{val}\n".encode('utf-8'))
        return True
        
    elif action == 'x':
        if not (-2 <= val <= 2):
            print("❌ Error: Saturation offset must be between -2 and 2")
            return False
        camera_state["saturation"] = val
        print(f"[Python UI] Saturation set to {val}")
        ser.write(f"x{val}\n".encode('utf-8'))
        return True
        
    elif action == 'm':
        if val not in [0, 1]:
            print("❌ Error: Mirror value must be 0 (Disable) or 1 (Enable)")
            return False
        camera_state["mirror"] = val
        print(f"[Python UI] Horizontal Mirror set to {'ENABLED' if val else 'DISABLED'}")
        ser.write(f"m{val}\n".encode('utf-8'))
        return True
        
    elif action == 'p':
        if val not in [0, 1]:
            print("❌ Error: Flip value must be 0 (Disable) or 1 (Enable)")
            return False
        camera_state["flip"] = val
        print(f"[Python UI] Vertical Flip set to {'ENABLED' if val else 'DISABLED'}")
        ser.write(f"p{val}\n".encode('utf-8'))
        return True
        
    else:
        print("❌ Unknown Command. Type 'h' to show the help menu.")
        return False

# Start reader thread as a daemon thread
t = threading.Thread(target=read_serial_thread, daemon=True)
t.start()

print("Listening for incoming image data blocks...")
print_help_menu()

try:
    while True:
        # Prompt for user command
        cmd = sys.stdin.readline().strip()
        if cmd:
            handle_command(cmd)
except KeyboardInterrupt:
    print("\nExiting script.")
finally:
    ser.close()
