# ESP32-S3 OV2640 Camera Testing & Acquisition Framework

This repository provides a testing and acquisition framework for the **ESP32-S3-N16R8-CAM** development board featuring the **OV2640** camera module. 

It implements a dual-destination pipeline that captures image frames, performs statistical preprocessing/computer vision testing directly on the microcontroller, and streams base64-encoded frames to a PC via USB Serial. A companion Python script automatically receives, decodes, and saves the image files.

---

## Features
* 📷 **Integrated OV2640 Driver Setup**: Configured out of the box using the standard `CAMERA_MODEL_ESP32S3_EYE` pin layout.
* 🖱️ **Hardware Trigger Support**: Press the physical **BOOT/LOAD** button (GPIO 0) on the ESP32-S3 board to instantly capture and save a photo.
* 💻 **Interactive CLI Menu**: Send commands over Serial to dynamically modify camera configurations (contrast, format, resolution, exposure, saturation).
* ⚙️ **Dual-Destination Pipeline**:
  * **PC Stream**: Direct, memory-efficient Base64 streaming over serial.
  * **On-Chip Inference Sandbox**: Real-time luminance statistics, RGB channel averaging, JPEG validation, and gesture recognition mocks.
* 🎨 **On-Chip ASCII Preview**: Renders a live 32x16 character representation of the camera frame directly to your terminal when configured for 96x96 Grayscale.
* 🐍 **Automated Python Receiver**: Auto-detects frame boundaries, decodes Base64 data, and saves images (`.jpg`, `.gray`, `.rgb565`, `.yuv422`) locally.

---

## Repository Structure
* [OV2640_Camera_Test/OV2640_Camera_Test.ino](file:///c:/Codes/OV2640/OV2640_Camera_Test/OV2640_Camera_Test.ino) — Main C++ Arduino sketch for the ESP32-S3.
* [serial_receiver.py](file:///c:/Codes/OV2640/serial_receiver.py) — Python receiver script to capture and decode streamed images.

---

## Getting Started

### 1. Hardware Requirements
* **ESP32-S3-N16R8-CAM** Development Board (with integrated OV2640 camera connector).
* USB-C cable for programming and serial communication.

### 2. Arduino IDE Compilation Settings
Because this board relies on an **N16R8** configuration (16MB Flash, 8MB PSRAM), you must enable external memory in your IDE settings, otherwise the camera initialization will fail:
1. Open the Arduino IDE and load `OV2640_Camera_Test.ino`.
2. Go to **Tools** ➡️ **Board** ➡️ **ESP32S3 Dev Module** (under the ESP32 platform).
3. Go to **Tools** ➡️ **PSRAM** ➡️ Select **OPI PSRAM** (Crucial: 8MB PSRAM requires OPI mode).
4. Go to **Tools** ➡️ **Partition Scheme** ➡️ Select **16M Flash (3MB APP/9.9MB FATFS)**.
5. Compile and upload the sketch to the board.

### 3. Setting Up the Python Receiver
Install the standard `pyserial` dependency if you haven't already:
```bash
pip install pyserial
```

#### Identify your COM Port:
Run the built-in listing tool to find the port number of your connected ESP32-S3:
```bash
python -m serial.tools.list_ports
```

#### Configure the Script:
Open [serial_receiver.py](file:///c:/Codes/OV2640/serial_receiver.py) and change `COM_PORT` to match your port:
```python
COM_PORT = 'COM9'  # Update this to match your system
```

---

## How to Capture Images

Only one program can access a serial port at a time. If you leave the Arduino IDE Serial Monitor open, the Python script will fail with an *Access is denied* error.

1. **Close** the Arduino IDE Serial Monitor.
2. Run the receiver script in your terminal:
   ```bash
   python .\serial_receiver.py
   ```
3. Trigger a capture using one of the following methods:
   * **Method A (Hardware):** Press the physical **BOOT** / **LOAD** button on the ESP32-S3 board.
   * **Method B (CLI):** Connect using a serial terminal (when the Python script is not running) and send the `c` command.
4. The image will be processed, transmitted, and decoded under the `./captured_images` folder.

---

## Serial Command Reference

| Command | Action | Description |
|:---:|:---|:---|
| **`h`** | Help Menu | Prints the command list and current camera state. |
| **`c`** | Capture Image | Triggers the dual-destination acquisition workflow. |
| **`f0`** | Format: Grayscale | Raw 1 byte per pixel. |
| **`f1`** | Format: RGB565 | 2 bytes per pixel (Big Endian). |
| **`f2`** | Format: YUV422 | 2 bytes per pixel. |
| **`f3`** | Format: JPEG | Compressed format (required for high resolutions). |
| **`s0`** | Res: 96x96 | Grayscale mode enables the CLI ASCII art preview. |
| **`s1`** | Res: QQVGA | 160x120 pixels. |
| **`s2`** | Res: QVGA | 320x240 pixels. |
| **`s3`** | Res: VGA | 640x480 pixels. |
| **`s4`** | Res: SVGA | 800x600 pixels. |
| **`s5`** | Res: UXGA | 1600x1200 pixels (JPEG recommended to prevent SRAM crash). |
| **`e1`** / **`e0`** | Auto Exposure | Enable or disable Auto Exposure Control (AEC). |
| **`v <0-1200>`** | Manual Exposure | Set exposure index (requires AEC to be disabled). |
| **`g1`** / **`g0`** | Auto Gain | Enable or disable Auto Gain Control (AGC). |
| **`a <0-30>`** | Manual Gain | Set gain index (requires AGC to be disabled). |
| **`b <value>`** | Brightness | Brightness offset range: `-2` to `2`. |
| **`t <value>`** | Contrast | Contrast offset range: `-2` to `2`. |
| **`x <value>`** | Saturation | Saturation offset range: `-2` to `2`. |
| **`m1`** / **`m0`** | Mirror | Enable or disable horizontal mirror mode. |
| **`p1`** / **`p0`** | Flip | Enable or disable vertical flip mode. |
