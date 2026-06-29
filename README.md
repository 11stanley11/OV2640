# ESP32-S3 OV2640 Camera Testing & Live View Framework

This repository contains an optimized testing and live preview framework for the **ESP32-S3-N16R8-CAM** board equipped with an **OV2640** camera sensor. It provides dynamic hardware-level sensor tuning, high-speed image transmission over serial, real-time mock CV inference, and a live OpenCV GUI preview.

---

## Features

* **High-Speed Serial Streaming:** Configured at **2,000,000 baud** (2M) to maximize transmission frame rate, unlocking smooth live video feeds.
* **Interactive Live Preview Window:** Renders real-time camera streams using OpenCV. Small frames (like 96x96) are automatically upscaled 4x with crisp nearest-neighbor interpolation.
* **On-the-Fly Configuration Overrides:** Change camera formats, resolutions, brightness, exposure, mirror, flip, and gain controls directly from the console in real-time while streaming.
* **Dual-Destination Output Architecture:**
  1. **USB PC Interface:** Decodes Base64 packets and automatically generates both raw (`.gray`, `.rgb565`, `.jpg`) and convert-to-PNG outputs under `./captured_images`.
  2. **On-Chip CV Processing Pipeline:** Dynamic downscaling and pre-processing to a standardized `96x96` Grayscale buffer for ML tensor feeds (heap-allocated to prevent stack overflows).
* **Guaranteed Exposure Freshness:** Flushes the camera DMA queue to ensure all snapshots are captured in real-time, eliminating stale/previous frame lag.
* **Log-Suppressed Streaming:** Suppresses verbose diagnostic prints during continuous streaming to preserve serial bandwidth, while automatically enabling full reports and terminal ASCII art reviews on manual capture commands.

---

## Getting Started

### 1. Hardware Requirements
* **ESP32-S3-N16R8-CAM** Development Board (with integrated OV2640 camera connector).
* USB-C cable for programming and serial communication.

### 2. Arduino IDE Compilation Settings
Because this board relies on an **N16R8** configuration (16MB Flash, 8MB PSRAM), you must configure external memory in your IDE settings:
1. Open the Arduino IDE and load `OV2640_Camera_Test.ino`.
2. Go to **Tools** ➡️ **Board** ➡️ **ESP32S3 Dev Module** (under the ESP32 platform).
3. Go to **Tools** ➡️ **PSRAM** ➡️ Select **OPI PSRAM** (Crucial: 8MB PSRAM requires OPI mode).
4. Go to **Tools** ➡️ **Partition Scheme** ➡️ Select **16M Flash (3MB APP/9.9MB FATFS)**.
5. Compile and upload the sketch to the board.

### 3. Setting Up the Python Receiver
Install dependencies (Pillow, OpenCV, and NumPy are required for the live GUI preview):
```bash
pip install pyserial Pillow opencv-python numpy
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

## How to Run & Capture Images

Only one program can access a serial port at a time. If you leave the Arduino IDE Serial Monitor open, the Python script will fail with an *Access is denied* error.

1. **Close** the Arduino IDE Serial Monitor.
2. Run the receiver script in your terminal:
   ```bash
   python .\serial_receiver.py
   ```
3. A **Live GUI Preview window** will automatically open and initiate continuous live streaming from the camera.
4. **Trigger a capture:**
   * **Method A (GUI Window):** Focus the GUI window and press `c` or `Space` to save the current frame. This also triggers the full on-chip CV diagnostics and ASCII art preview in the terminal.
   * **Method B (CLI Terminal):** Type `c` in the terminal and press Enter.
   * **Method C (Hardware Pin):** Press the physical **BOOT** / **LOAD** button on the ESP32-S3 board.
5. **Exit Stream:** Press `q` or `Esc` in the GUI window to close the stream cleanly.

---

## Serial Command Reference

Type commands in the CLI terminal and press Enter to update configurations in real-time.

| Command | Action | Description |
|:---:|:---|:---|
| **`h`** | Help Menu | Prints the command list and current camera state. |
| **`c`** | Capture Image | Triggers a fresh acquisition workflow with full terminal reports. |
| **`d1`** / **`d0`** | Stream Mode | Start / Stop continuous streaming. |
| **`f0`** | Format: Grayscale | Raw 1 byte per pixel. |
| **`f1`** | Format: RGB565 | 2 bytes per pixel (Big Endian). |
| **`f2`** | Format: YUV422 | 2 bytes per pixel. |
| **`f3`** | Format: JPEG | Compressed format (required for high resolutions). |
| **`s0`** | Res: 96x96 | 96x96 pixels. |
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
