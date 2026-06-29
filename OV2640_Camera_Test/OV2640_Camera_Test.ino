#include "esp_camera.h"

// ==========================================
// 📸 GPIO Wiring Pin Configuration for ESP32-S3
// ==========================================
#define PWDN_GPIO_NUM    -1  // Tie to GND to keep camera enabled
#define RESET_GPIO_NUM   -1  // Tie to 3.3V to keep camera enabled
#define XCLK_GPIO_NUM    15  // Master Clock
#define SIOD_GPIO_NUM     4  // SCCB Data (I2C SDA) - Needs 4.7k Ohm Pull-up
#define SIOC_GPIO_NUM     5  // SCCB Clock (I2C SCL) - Needs 4.7k Ohm Pull-up

#define Y9_GPIO_NUM      16  // MSB
#define Y8_GPIO_NUM      17
#define Y7_GPIO_NUM      18
#define Y6_GPIO_NUM      12
#define Y5_GPIO_NUM      10
#define Y4_GPIO_NUM       8
#define Y3_GPIO_NUM       9
#define Y2_GPIO_NUM      11  // LSB

#define VSYNC_GPIO_NUM    6  // Vertical Sync
#define HREF_GPIO_NUM     7  // Horizontal Reference
#define PCLK_GPIO_NUM    13  // Pixel Clock

// ==========================================
// 🛠️ Globals & State Variables
// ==========================================
camera_config_t config;
pixformat_t current_format = PIXFORMAT_GRAYSCALE;
framesize_t current_framesize = FRAMESIZE_96X96;

String inputString = "";
bool stringComplete = false;

// Base64 lookup table
const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// ==========================================
// 🏷️ Forward Declarations & Helpers
// ==========================================
bool initCameraWithConfig(pixformat_t format, framesize_t size);
void printHelpMenu();
void parseCommand(String cmd);
void captureWorkflow();
void sendToPC(camera_fb_t *fb);
void runLocalInference(camera_fb_t *fb);
void renderAsciiArt(const uint8_t* buf, int width, int height);
void print_base64(const uint8_t* data, size_t length);
const char* format_to_str(pixformat_t format);
const char* framesize_to_str(framesize_t size);

void setup() {
    // Initialize Serial Port at high baud rate for fast image transfers
    Serial.begin(115200);
    while (!Serial) {
        ; // Wait for serial port to connect
    }
    
    delay(2000); // Give user time to open the Serial Monitor
    Serial.println("\n\n=============================================");
    Serial.println("🚀 ESP32-S3 OV2640 Camera Testing Framework");
    Serial.println("=============================================");
    
    // Initialize Camera with default configuration (Grayscale, 96x96)
    if (!initCameraWithConfig(current_format, current_framesize)) {
        Serial.println("❌ ERROR: Camera initialization failed during startup!");
    } else {
        Serial.println("✅ Camera initialized successfully with default parameters.");
    }
    
    // Print user interface CLI menu
    printHelpMenu();
}

void loop() {
    // Read serial inputs until newline
    while (Serial.available()) {
        char inChar = (char)Serial.read();
        if (inChar == '\n' || inChar == '\r') {
            if (inputString.length() > 0) {
                stringComplete = true;
                break;
            }
        } else {
            inputString += inChar;
        }
    }

    // Process complete commands
    if (stringComplete) {
        inputString.trim();
        parseCommand(inputString);
        inputString = "";
        stringComplete = false;
    }
}

/**
 * @brief Initialize the OV2640 camera with dynamic formats and resolutions.
 * Re-allocates frame buffers accordingly.
 */
bool initCameraWithConfig(pixformat_t format, framesize_t size) {
    // De-initialize camera to release previous buffers
    esp_camera_deinit();
    delay(150);
    
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    
    config.xclk_freq_hz = 20000000; // 20 MHz master clock
    config.ledc_timer = LEDC_TIMER_0;
    config.ledc_channel = LEDC_CHANNEL_0;
    
    config.pixel_format = format;
    config.frame_size = size;
    config.jpeg_quality = 12; // 0-63 (lower values mean higher quality)
    
    // Dynamic buffer location allocation based on PSRAM support
    #if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.fb_count = (format == PIXFORMAT_JPEG) ? 2 : 1; 
        Serial.println("[System] PSRAM Detected. Frame buffers allocated in PSRAM.");
    #else
        config.fb_location = CAMERA_FB_IN_DRAM;
        config.fb_count = 1;
        Serial.println("[System] PSRAM NOT Detected. Frame buffers allocated in SRAM.");
        
        // Memory Warning Check
        if (format != PIXFORMAT_JPEG && size > FRAMESIZE_QVGA) {
            Serial.println("⚠️ WARNING: Raw buffer size may exceed SRAM capacity! High resolutions (VGA+) without PSRAM may crash.");
        }
    #endif

    // Initialise driver
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("❌ Camera init failed with error: 0x%x\n", err);
        return false;
    }
    
    // Apply camera sensor settings adjustments
    sensor_t * s = esp_camera_sensor_get();
    if (s) {
        // Set typical starting thresholds
        s->set_brightness(s, 0);     // -2 to 2
        s->set_contrast(s, 0);       // -2 to 2
        s->set_saturation(s, 0);     // -2 to 2
        s->set_special_effect(s, 0); // 0 = No effect, 2 = Grayscale effect
        s->set_whitebal(s, 1);       // 0 = disable , 1 = enable
        s->set_awb_gain(s, 1);       // 0 = disable , 1 = enable
        s->set_exposure_ctrl(s, 1);  // 0 = disable , 1 = enable
        s->set_gain_ctrl(s, 1);      // 0 = disable , 1 = enable
        s->set_hmirror(s, 0);        // 0 = disable , 1 = enable
        s->set_vflip(s, 0);          // 0 = disable , 1 = enable
    }
    
    return true;
}

/**
 * @brief Parse command input received via the Serial Monitor.
 */
void parseCommand(String cmd) {
    if (cmd.length() == 0) return;
    
    char action = cmd.charAt(0);
    String argStr = cmd.substring(1);
    argStr.trim();
    int val = argStr.toInt();
    
    sensor_t * s = esp_camera_sensor_get();
    
    switch (action) {
        case 'h':
        case 'H':
            printHelpMenu();
            break;
            
        case 'c':
        case 'C':
            captureWorkflow();
            break;
            
        case 'f':
        case 'F': {
            pixformat_t new_fmt;
            if (val == 0) new_fmt = PIXFORMAT_GRAYSCALE;
            else if (val == 1) new_fmt = PIXFORMAT_RGB565;
            else if (val == 2) new_fmt = PIXFORMAT_YUV422;
            else if (val == 3) new_fmt = PIXFORMAT_JPEG;
            else {
                Serial.println("❌ Error: Invalid format (0=Grayscale, 1=RGB565, 2=YUV422, 3=JPEG)");
                break;
            }
            Serial.printf("🔄 Changing pixel format to %s...\n", format_to_str(new_fmt));
            if (initCameraWithConfig(new_fmt, current_framesize)) {
                current_format = new_fmt;
                Serial.println("✅ Format updated.");
            }
            break;
        }
            
        case 's':
        case 'S': {
            framesize_t new_size;
            if (val == 0) new_size = FRAMESIZE_96X96;
            else if (val == 1) new_size = FRAMESIZE_QQVGA;
            else if (val == 2) new_size = FRAMESIZE_QVGA;
            else if (val == 3) new_size = FRAMESIZE_VGA;
            else if (val == 4) new_size = FRAMESIZE_SVGA;
            else if (val == 5) new_size = FRAMESIZE_UXGA;
            else {
                Serial.println("❌ Error: Invalid resolution (0=96x96, 1=QQVGA, 2=QVGA, 3=VGA, 4=SVGA, 5=UXGA)");
                break;
            }
            Serial.printf("🔄 Changing resolution to %s...\n", framesize_to_str(new_size));
            if (initCameraWithConfig(current_format, new_size)) {
                current_framesize = new_size;
                Serial.println("✅ Resolution updated.");
            }
            break;
        }
            
        case 'e':
        case 'E':
            if (s) {
                s->set_exposure_ctrl(s, val ? 1 : 0);
                Serial.printf("⚙️ Auto Exposure Control (AEC) set to %s\n", val ? "AUTO" : "MANUAL");
            } else {
                Serial.println("❌ Error: Sensor interface not available.");
            }
            break;
            
        case 'v':
        case 'V':
            if (s) {
                if (val < 0) val = 0;
                if (val > 1200) val = 1200;
                s->set_aec_value(s, val);
                Serial.printf("⚙️ Manual Exposure set to %d (AEC must be disabled for this to take effect)\n", val);
            } else {
                Serial.println("❌ Error: Sensor interface not available.");
            }
            break;
            
        case 'g':
        case 'G':
            if (s) {
                s->set_gain_ctrl(s, val ? 1 : 0);
                Serial.printf("⚙️ Auto Gain Control (AGC) set to %s\n", val ? "AUTO" : "MANUAL");
            } else {
                Serial.println("❌ Error: Sensor interface not available.");
            }
            break;
            
        case 'a':
        case 'A':
            if (s) {
                if (val < 0) val = 0;
                if (val > 30) val = 30;
                s->set_agc_value(s, val);
                Serial.printf("⚙️ Manual Gain set to %d (AGC must be disabled for this to take effect)\n", val);
            } else {
                Serial.println("❌ Error: Sensor interface not available.");
            }
            break;
            
        case 'b':
        case 'B':
            if (s) {
                if (val < -2) val = -2;
                if (val > 2) val = 2;
                s->set_brightness(s, val);
                Serial.printf("⚙️ Brightness set to %d\n", val);
            } else {
                Serial.println("❌ Error: Sensor interface not available.");
            }
            break;
            
        case 't':
        case 'T':
            if (s) {
                if (val < -2) val = -2;
                if (val > 2) val = 2;
                s->set_contrast(s, val);
                Serial.printf("⚙️ Contrast set to %d\n", val);
            } else {
                Serial.println("❌ Error: Sensor interface not available.");
            }
            break;
            
        case 'x':
        case 'X':
            if (s) {
                if (val < -2) val = -2;
                if (val > 2) val = 2;
                s->set_saturation(s, val);
                Serial.printf("⚙️ Saturation set to %d\n", val);
            } else {
                Serial.println("❌ Error: Sensor interface not available.");
            }
            break;

        case 'm':
        case 'M':
            if (s) {
                s->set_hmirror(s, val ? 1 : 0);
                Serial.printf("⚙️ Horizontal Mirror: %s\n", val ? "ENABLED" : "DISABLED");
            } else {
                Serial.println("❌ Error: Sensor interface not available.");
            }
            break;

        case 'p':
        case 'P':
            if (s) {
                s->set_vflip(s, val ? 1 : 0);
                Serial.printf("⚙️ Vertical Flip: %s\n", val ? "ENABLED" : "DISABLED");
            } else {
                Serial.println("❌ Error: Sensor interface not available.");
            }
            break;
            
        default:
            Serial.println("❓ Unknown Command. Send 'h' to show the help menu.");
            break;
    }
}

/**
 * @brief Prints the interactive CLI instructions and current status.
 */
void printHelpMenu() {
    Serial.println("\n-------------------------------------------------------------");
    Serial.println("📖 OV2640 CLI TESTING INSTRUCTIONS");
    Serial.println("Send commands over serial in '<command><value>' format.");
    Serial.println("Example: 'c' to capture, 'f1' to change format to RGB565");
    Serial.println("-------------------------------------------------------------");
    Serial.println("📁 General commands:");
    Serial.println("  h        - Display this menu");
    Serial.println("  c        - Trigger image acquisition (Dual-Destination workflow)");
    Serial.println("\n🎨 Pixel Format (f <0-3>):");
    Serial.println("  f0       - GRAYSCALE (1 byte per pixel)");
    Serial.println("  f1       - RGB565    (2 bytes per pixel, Big Endian)");
    Serial.println("  f2       - YUV422    (2 bytes per pixel)");
    Serial.println("  f3       - JPEG      (Compressed, variable size)");
    Serial.println("\n📐 Resolution (s <0-5>):");
    Serial.println("  s0       - 96x96");
    Serial.println("  s1       - QQVGA (160x120)");
    Serial.println("  s2       - QVGA  (320x240)");
    Serial.println("  s3       - VGA   (640x480)");
    Serial.println("  s4       - SVGA  (800x600)");
    Serial.println("  s5       - UXGA  (1600x1200) (Use JPEG format for RAM safety)");
    Serial.println("\n☀️ Exposure & Sensor Controls:");
    Serial.println("  e1 / e0  - Enable/Disable Auto Exposure Control (AEC)");
    Serial.println("  v <0-1200>- Manual Exposure index (Only active when AEC is disabled)");
    Serial.println("  g1 / g0  - Enable/Disable Auto Gain Control (AGC)");
    Serial.println("  a <0-30> - Manual Gain index (Only active when AGC is disabled)");
    Serial.println("  b <value>- Set Brightness (-2 to 2)");
    Serial.println("  t <value>- Set Contrast (-2 to 2)");
    Serial.println("  x <value>- Set Saturation (-2 to 2)");
    Serial.println("  m1 / m0  - Enable/Disable Horizontal Mirror");
    Serial.println("  p1 / p0  - Enable/Disable Vertical Flip");
    Serial.println("-------------------------------------------------------------");
    Serial.printf("📊 Current State: Format = %s | Resolution = %s\n", 
                  format_to_str(current_format), framesize_to_str(current_framesize));
    Serial.println("-------------------------------------------------------------\n");
}

/**
 * @brief Main execution workflow. Acquires the image and channels it to two destinations.
 */
void captureWorkflow() {
    Serial.println("\n🎬 --- WORKFLOW INITIATED ---");
    Serial.println("[System] Capturing frame buffer...");
    
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("❌ ERROR: Failed to acquire camera frame buffer.");
        Serial.println("🎬 --- WORKFLOW ABORTED ---\n");
        return;
    }
    
    Serial.printf("[System] Frame captured! Resolution: %dx%d, Buffer size: %d bytes, Format: %s\n", 
                  fb->width, fb->height, fb->len, format_to_str(fb->format));
                  
    // -------------------------------------------------------------
    // Destination 1: Send raw file via USB Serial back to PC
    // -------------------------------------------------------------
    sendToPC(fb);
    
    // -------------------------------------------------------------
    // Destination 2: Send buffer to local CV Model
    // -------------------------------------------------------------
    runLocalInference(fb);
    
    // Always return the frame buffer to the pool
    esp_camera_fb_return(fb);
    
    Serial.println("🎬 --- WORKFLOW COMPLETED ---\n");
}

/**
 * @brief Simulates transmission of the file back to a host computer.
 * Serial framing prevents raw control character collisions and enables scripting.
 */
void sendToPC(camera_fb_t *fb) {
    Serial.println("🖥️ [Destination 1] Sending image file to PC Interface (USB)...");
    
    // Print metadata header
    Serial.printf("---START_IMAGE:%d:%d:%d:%d---\n", 
                  (int)fb->format, fb->width, fb->height, fb->len);
    
    // Encode and stream base64 directly to serial to conserve memory
    print_base64(fb->buf, fb->len);
    
    // Print metadata footer
    Serial.println("---END_IMAGE---");
    Serial.println("🖥️ [Destination 1] File sent to PC successfully.");
}

/**
 * @brief Simulates feeding the captured buffer into a Local Embedded Machine Learning model.
 * Performs statistical evaluation on the matrix.
 */
void runLocalInference(camera_fb_t *fb) {
    Serial.println("🧠 [Destination 2] Feeding image buffer to Local CV Model...");
    
    // Mock preprocessing dimensions check
    Serial.printf("  [CV Model] Input tensor target shape: %dx%dx%d\n", 
                  fb->height, fb->width, (fb->format == PIXFORMAT_GRAYSCALE) ? 1 : 3);
    
    // Extract real-time metrics based on format structure
    if (fb->format == PIXFORMAT_GRAYSCALE) {
        uint32_t sum = 0;
        uint8_t min_pixel = 255;
        uint8_t max_pixel = 0;
        
        for (size_t i = 0; i < fb->len; i++) {
            uint8_t pixel = fb->buf[i];
            sum += pixel;
            if (pixel < min_pixel) min_pixel = pixel;
            if (pixel > max_pixel) max_pixel = pixel;
        }
        
        float avg_pixel = (float)sum / fb->len;
        Serial.printf("  [CV Model] Grayscale Preprocessor -> Mean: %.2f | Min: %d | Max: %d\n", 
                      avg_pixel, min_pixel, max_pixel);
                      
        // For the 96x96 Grayscale configuration (training configuration), render ASCII visualization
        if (fb->width == 96 && fb->height == 96) {
            renderAsciiArt(fb->buf, fb->width, fb->height);
        }
    } 
    else if (fb->format == PIXFORMAT_RGB565) {
        // Extract averages for R, G, B channels from packed RGB565 (16-bit)
        uint32_t sum_r = 0, sum_g = 0, sum_b = 0;
        size_t total_pixels = fb->len / 2;
        
        for (size_t i = 0; i < fb->len; i += 2) {
            // Unpack 16-bit pixel (Big Endian)
            uint16_t pixel = (fb->buf[i] << 8) | fb->buf[i+1];
            uint8_t r = ((pixel >> 11) & 0x1F) << 3; // Scale 5-bit R to 8-bit
            uint8_t g = ((pixel >> 5) & 0x3F) << 2;  // Scale 6-bit G to 8-bit
            uint8_t b = (pixel & 0x1F) << 3;         // Scale 5-bit B to 8-bit
            sum_r += r;
            sum_g += g;
            sum_b += b;
        }
        
        Serial.printf("  [CV Model] RGB565 Preprocessor -> Average Channels: R=%.1f, G=%.1f, B=%.1f\n", 
                      (float)sum_r / total_pixels, 
                      (float)sum_g / total_pixels, 
                      (float)sum_b / total_pixels);
    } 
    else if (fb->format == PIXFORMAT_YUV422) {
        // YUV422 stores alternating bytes: Y0 U0 Y1 V0
        // Extract average luminance Y
        uint32_t sum_y = 0;
        size_t total_pixels = fb->len / 2;
        for (size_t i = 0; i < fb->len; i += 2) {
            sum_y += fb->buf[i]; // Luminance is at even indices
        }
        Serial.printf("  [CV Model] YUV422 Preprocessor -> Average Luminance (Y): %.2f\n", 
                      (float)sum_y / total_pixels);
    } 
    else if (fb->format == PIXFORMAT_JPEG) {
        // JPEG data is compressed, perform header verification
        if (fb->len >= 4 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8 && 
            fb->buf[fb->len-2] == 0xFF && fb->buf[fb->len-1] == 0xD9) {
            Serial.println("  [CV Model] JPEG Validator -> SOI (0xFFD8) and EOI (0xFFD9) markers verified.");
        } else {
            Serial.println("  ⚠️ [CV Model] JPEG Validator -> WARNING: Missing start or end markers. Data might be incomplete.");
        }
    }
    
    // Simulate inference invocation
    Serial.println("  [CV Model] Loading input tensor... OK.");
    Serial.println("  [CV Model] Running inference engine (quantized model)...");
    
    // In actual code:
    // float* model_input = tflite::GetTensorData<float>(input_tensor);
    // model_input[x] = (float)pixel / 255.0f; // normalization
    // TfLiteStatus status = interpreter.Invoke();
    
    Serial.println("  [CV Model] Inference complete. Mock Gesture Outputs: [Palm: 94.2%, Fist: 3.1%, Wave: 2.7%]");
    Serial.println("🧠 [Destination 2] Local CV processing complete.");
}

/**
 * @brief Renders a low-resolution ASCII character grid in the serial console 
 * to visually verify the captured image frames in real-time.
 */
void renderAsciiArt(const uint8_t* buf, int width, int height) {
    int cols = 32; // Characters wide
    int rows = 16; // Characters high (compensating for console font aspect ratio)
    
    int block_w = width / cols;
    int block_h = height / rows;
    
    if (block_w == 0 || block_h == 0) return;
    
    // Visual character ramp (dark to light)
    const char char_map[] = " .:-=+*#%@";
    int num_chars = sizeof(char_map) - 1;
    
    Serial.println("\n----------------- 96x96 ASCII ART PREVIEW -----------------");
    for (int r = 0; r < rows; r++) {
        Serial.print("  | ");
        for (int c = 0; c < cols; c++) {
            uint32_t block_sum = 0;
            for (int dy = 0; dy < block_h; dy++) {
                for (int dx = 0; dx < block_w; dx++) {
                    int px = c * block_w + dx;
                    int py = r * block_h + dy;
                    block_sum += buf[py * width + px];
                }
            }
            uint8_t avg_val = block_sum / (block_w * block_h);
            // Map 0-255 to 0-(num_chars-1)
            int char_idx = (avg_val * num_chars) / 256;
            Serial.print(char_map[char_idx]);
        }
        Serial.println(" |");
    }
    Serial.println("-----------------------------------------------------------");
}

/**
 * @brief Memory-efficient Base64 streaming encoder.
 * Avoids allocating a secondary string in heap memory by writing chunks directly to Serial.
 */
void print_base64(const uint8_t* data, size_t length) {
    size_t i = 0;
    uint8_t a, b, c;
    uint32_t combined;
    
    // Process main 3-byte blocks
    while (i + 3 <= length) {
        a = data[i++];
        b = data[i++];
        c = data[i++];
        combined = (a << 16) | (b << 8) | c;
        
        Serial.print(b64_table[(combined >> 18) & 0x3F]);
        Serial.print(b64_table[(combined >> 12) & 0x3F]);
        Serial.print(b64_table[(combined >> 6) & 0x3F]);
        Serial.print(b64_table[combined & 0x3F]);
    }
    
    // Process trailing padding blocks
    if (i < length) {
        a = data[i++];
        if (i < length) {
            b = data[i++];
            combined = (a << 16) | (b << 8);
            Serial.print(b64_table[(combined >> 18) & 0x3F]);
            Serial.print(b64_table[(combined >> 12) & 0x3F]);
            Serial.print(b64_table[(combined >> 6) & 0x3F]);
            Serial.print('=');
        } else {
            combined = (a << 16);
            Serial.print(b64_table[(combined >> 18) & 0x3F]);
            Serial.print(b64_table[(combined >> 12) & 0x3F]);
            Serial.print('=');
            Serial.print('=');
        }
    }
    Serial.println();
}

/**
 * @brief Helper functions to convert formats to readable strings.
 */
const char* format_to_str(pixformat_t format) {
    switch (format) {
        case PIXFORMAT_GRAYSCALE: return "GRAYSCALE";
        case PIXFORMAT_RGB565:    return "RGB565";
        case PIXFORMAT_YUV422:    return "YUV422";
        case PIXFORMAT_JPEG:      return "JPEG";
        default:                  return "UNKNOWN";
    }
}

const char* framesize_to_str(framesize_t size) {
    switch (size) {
        case FRAMESIZE_96X96:   return "96x96";
        case FRAMESIZE_QQVGA:   return "QQVGA (160x120)";
        case FRAMESIZE_QVGA:    return "QVGA (320x240)";
        case FRAMESIZE_VGA:     return "VGA (640x480)";
        case FRAMESIZE_SVGA:    return "SVGA (800x600)";
        case FRAMESIZE_UXGA:    return "UXGA (1600x1200)";
        default:                return "UNKNOWN";
    }
}
