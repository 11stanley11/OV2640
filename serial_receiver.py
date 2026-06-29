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

try:
    import cv2
    import numpy as np
    HAS_CV2 = True
except ImportError:
    HAS_CV2 = False

# Configure the serial port parameters
# Update COM_PORT to match your ESP32-S3 port (e.g., 'COM3' on Windows or '/dev/ttyACM0' on Linux)
COM_PORT = 'COM9'
BAUD_RATE = 2000000  # Set to 2,000,000 baud for high frame-rate streaming
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

# Globally shared variables for live preview and capture synchronization
latest_frame = None
latest_frame_lock = threading.Lock()

saving_next_frame = False
saving_next_frame_lock = threading.Lock()

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
    "flip": 0,        # 0 = Disabled, 1 = Enabled
    "streaming": False # True = Continuous stream active
}

def save_frame(img_bytes, metadata, fmt_id):
    # Decide extension based on Espressif's pixformat_t enum:
    # 0=RGB565, 1=YUV422, 2=YUV420, 3=GRAYSCALE, 4=JPEG
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
    
    try:
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
        print(f"❌ Failed to save image: {ex}")

# Background thread to continuously read from the serial port
def read_serial_thread():
    global receiving, buffer, metadata, latest_frame, saving_next_frame
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
                    
                    # Check if this frame is explicitly requested for saving
                    is_saving = False
                    with saving_next_frame_lock:
                        is_saving = saving_next_frame
                    
                    if is_saving or not camera_state.get("streaming", False):
                        print(f"\n📥 Receiving new image frame: {metadata['width']}x{metadata['height']}, Size: {metadata['size']} bytes...")
                
            elif line.startswith("---END_IMAGE---"):
                if receiving:
                    receiving = False
                    base64_data = "".join(buffer)
                    try:
                        # Decode base64
                        img_bytes = base64.b64decode(base64_data)
                        fmt_id = metadata.get("format", 4)
                        
                        # Decide if we save it to disk
                        is_saving = False
                        with saving_next_frame_lock:
                            if saving_next_frame:
                                is_saving = True
                                saving_next_frame = False # Reset flag after consumption
                                
                        if is_saving or not camera_state.get("streaming", False):
                            save_frame(img_bytes, metadata, fmt_id)
                            
                        # If streaming is enabled, update latest frame for GUI view
                        if camera_state.get("streaming", False):
                            with latest_frame_lock:
                                latest_frame = {
                                    "bytes": img_bytes,
                                    "metadata": metadata,
                                    "format": fmt_id
                                }
                    except Exception as ex:
                        print(f"❌ Failed to decode Base64 image: {ex}")
                
            elif receiving:
                buffer.append(line)
            else:
                # Print standard output lines from ESP32 (logs, statistics, ascii art)
                print(f"[ESP32] {line}")
        except Exception as e:
            # If serial connection is closed/broken, terminate thread
            break

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
    print("  d1 / d0  - Start/Stop Continuous live camera stream (GUI preview)")
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
    print(f"               Streaming = {'ENABLED' if camera_state['streaming'] else 'DISABLED'}")
    print("="*60 + "\n")

def handle_command(cmd_str):
    global camera_state, latest_frame, saving_next_frame
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
        with saving_next_frame_lock:
            saving_next_frame = True
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
        
    elif action == 'd':
        if val not in [0, 1]:
            print("❌ Error: Streaming mode must be 0 (Stop) or 1 (Start)")
            return False
        camera_state["streaming"] = (val == 1)
        print(f"[Python UI] Continuous streaming mode: {'ENABLED' if val else 'DISABLED'}")
        ser.write(f"d{val}\n".encode('utf-8'))
        return True
        
    else:
        print("❌ Unknown Command. Type 'h' to show the help menu.")
        return False

# Command loop running in a background daemon thread
def command_loop():
    try:
        while True:
            cmd = sys.stdin.readline().strip()
            if cmd:
                handle_command(cmd)
    except KeyboardInterrupt:
        pass

# GUI thread function to display preview
def run_gui():
    global latest_frame, saving_next_frame
    if not HAS_CV2:
        print("\n⚠️ OpenCV or NumPy is not installed. Live preview window is disabled.")
        print("Install them using: pip install opencv-python numpy")
        print("Running in CLI-only mode.")
        while True:
            time.sleep(1)
        return
        
    print("\n-------------------------------------------------------------")
    print("🎥 LIVE GUI PREVIEW WINDOW ACTIVATED")
    print("  - Focus the window and press 'c' or 'Space' to capture a frame.")
    print("  - Press 'q' or 'Esc' in the GUI window to exit preview.")
    print("-------------------------------------------------------------")
    
    try:
        cv2.namedWindow("ESP32-S3 OV2640 Live View", cv2.WINDOW_NORMAL)
    except Exception as e:
        print(f"⚠️ Failed to create GUI window (running in headless mode?): {e}")
        while True:
            time.sleep(1)
        return

    # Automatically start continuous streaming from ESP32 on launch
    camera_state["streaming"] = True
    ser.write(b"d1\n")
    
    last_displayed_frame = None
    
    while True:
        frame_to_show = None
        with latest_frame_lock:
            if latest_frame is not None and latest_frame != last_displayed_frame:
                frame_to_show = latest_frame
                last_displayed_frame = latest_frame
                
        if frame_to_show is not None:
            img_bytes = frame_to_show["bytes"]
            metadata = frame_to_show["metadata"]
            fmt_id = frame_to_show["format"]
            
            img_bgr = None
            try:
                if fmt_id == 4:  # JPEG
                    nparr = np.frombuffer(img_bytes, np.uint8)
                    img_bgr = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
                elif fmt_id == 3:  # Grayscale
                    nparr = np.frombuffer(img_bytes, np.uint8)
                    img_gray = nparr.reshape((metadata['height'], metadata['width']))
                    img_bgr = cv2.cvtColor(img_gray, cv2.COLOR_GRAY2BGR)
                elif fmt_id == 0:  # RGB565
                    nparr = np.frombuffer(img_bytes, np.uint8)
                    pixels = nparr.view(dtype=np.uint16).byteswap()
                    
                    r = ((pixels >> 11) & 0x1F) << 3
                    g = ((pixels >> 5) & 0x3F) << 2
                    b = (pixels & 0x1F) << 3
                    
                    img_bgr = np.zeros((metadata['height'], metadata['width'], 3), dtype=np.uint8)
                    img_bgr[..., 2] = r  # OpenCV uses BGR
                    img_bgr[..., 1] = g
                    img_bgr[..., 0] = b
            except Exception as e:
                print(f"Error decoding frame for preview: {e}")
                
            if img_bgr is not None:
                # Upscale small configurations (like 96x96) so they are easily viewable
                if metadata['width'] < 300:
                    img_bgr = cv2.resize(img_bgr, (metadata['width']*4, metadata['height']*4), interpolation=cv2.INTER_NEAREST)
                cv2.imshow("ESP32-S3 OV2640 Live View", img_bgr)
                
        key = cv2.waitKey(30) & 0xFF
        if key == ord('q') or key == 27:  # 'q' or Esc to quit
            break
        elif key == ord('c') or key == ord(' '):  # 'c' or Space to capture
            print("[Python UI] GUI requested frame capture. Triggering workflow on ESP32...")
            with saving_next_frame_lock:
                saving_next_frame = True
            ser.write(b"c\n")
                    
    # Clean up stream
    print("[Python UI] Stopping live stream...")
    camera_state["streaming"] = False
    ser.write(b"d0\n")
    cv2.destroyAllWindows()

# Start background serial reader thread
t = threading.Thread(target=read_serial_thread, daemon=True)
t.start()

# Start background terminal command prompt thread
cmd_thread = threading.Thread(target=command_loop, daemon=True)
cmd_thread.start()

print("Listening for incoming image data blocks...")
print_help_menu()

try:
    run_gui()
except KeyboardInterrupt:
    print("\nExiting script.")
finally:
    # Ensure camera stops streaming and serial connection closes
    ser.write(b"d0\n")
    ser.close()
