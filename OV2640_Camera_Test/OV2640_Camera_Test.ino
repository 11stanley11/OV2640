#include "esp_camera.h"
#include "img_converters.h"

// ==========================================
// GPIO Wiring Pin Configuration for ESP32-S3
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
// Globals & State Variables
// ==========================================
camera_config_t config;
pixformat_t current_format = PIXFORMAT_GRAYSCALE;
framesize_t current_framesize = FRAMESIZE_96X96;

String inputString = "";
bool stringComplete = false;

// Hardware BOOT/LOAD Button configuration
#define BOOT_BUTTON_PIN 0
int lastButtonState = HIGH;

// Base64 lookup table
const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// ==========================================
// Forward Declarations & Helpers
// ==========================================
bool initCameraWithConfig(pixformat_t format, framesize_t size);
void parseCommand(String cmd);
void captureWorkflow();
void sendToPC(camera_fb_t *fb);
void runLocalInference(camera_fb_t *fb);
void renderAsciiArt(const uint8_t* buf, int width, int height);
void print_base64(const uint8_t* data, size_t length);
const char* format_to_str(pixformat_t format);

void setup() {
    // Initialize Serial Port at high baud rate for fast image transfers
    Serial.begin(115200);
    while (!Serial) {
        ; // Wait for serial port to connect
    }
    
    delay(2000); // Give user time to open the Serial Monitor
    Serial.println("\n\n=============================================");
    Serial.println("ESP32-S3 OV2640 Camera Testing Framework");
    Serial.println("=============================================");
    
    // Configure BOOT/LOAD button pin as input with pullup
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    
    // Initialize Camera with default configuration (Grayscale, 96x96)
    if (!initCameraWithConfig(current_format, current_framesize)) {
        Serial.println("ERROR: Camera initialization failed during startup!");
    } else {
        Serial.println("Camera initialized successfully with default parameters.");
    }
    
    // Print user interface CLI menu (silenced for python controller)
    // printHelpMenu();
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

    // Check BOOT/LOAD button press (active low)
    int buttonState = digitalRead(BOOT_BUTTON_PIN);
    if (buttonState == LOW && lastButtonState == HIGH) {
        delay(50); // Simple debounce delay
        if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
            Serial.println("\n[Button] Boot/Load button pressed! Triggering capture...");
            captureWorkflow();
        }
    }
    lastButtonState = buttonState;
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
            Serial.println("WARNING: Raw buffer size may exceed SRAM capacity! High resolutions (VGA+) without PSRAM may crash.");
        }
    #endif

    // Initialise driver
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error: 0x%x\n", err);
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
            else break;
            
            if (initCameraWithConfig(new_fmt, current_framesize)) {
                current_format = new_fmt;
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
            else break;
            
            if (initCameraWithConfig(current_format, new_size)) {
                current_framesize = new_size;
            }
            break;
        }
            
        case 'e':
        case 'E':
            if (s) {
                s->set_exposure_ctrl(s, val ? 1 : 0);
            }
            break;
            
        case 'v':
        case 'V':
            if (s) {
                s->set_aec_value(s, val);
            }
            break;
            
        case 'g':
        case 'G':
            if (s) {
                s->set_gain_ctrl(s, val ? 1 : 0);
            }
            break;
            
        case 'a':
        case 'A':
            if (s) {
                s->set_agc_gain(s, val);
            }
            break;
            
        case 'b':
        case 'B':
            if (s) {
                s->set_brightness(s, val);
            }
            break;
            
        case 't':
        case 'T':
            if (s) {
                s->set_contrast(s, val);
            }
            break;
            
        case 'x':
        case 'X':
            if (s) {
                s->set_saturation(s, val);
            }
            break;
 
        case 'm':
        case 'M':
            if (s) {
                s->set_hmirror(s, val ? 1 : 0);
            }
            break;
 
        case 'p':
        case 'P':
            if (s) {
                s->set_vflip(s, val ? 1 : 0);
            }
            break;
            
        default:
            break;
    }
}

/**
 * @brief Main execution workflow. Acquires the image and channels it to two destinations.
 */
void captureWorkflow() {
    Serial.println("\n--- WORKFLOW INITIATED ---");
    Serial.println("[System] Capturing frame buffer...");
    
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("ERROR: Failed to acquire camera frame buffer.");
        Serial.println("--- WORKFLOW ABORTED ---\n");
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
    
    Serial.println("--- WORKFLOW COMPLETED ---\n");
}

/**
 * @brief Simulates transmission of the file back to a host computer.
 * Serial framing prevents raw control character collisions and enables scripting.
 */
void sendToPC(camera_fb_t *fb) {
    Serial.println("[Destination 1] Sending image file to PC Interface (USB)...");
    
    // Print metadata header
    Serial.printf("---START_IMAGE:%d:%d:%d:%d---\n", 
                  (int)fb->format, fb->width, fb->height, fb->len);
    
    // Encode and stream base64 directly to serial to conserve memory
    print_base64(fb->buf, fb->len);
    
    // Print metadata footer
    Serial.println("---END_IMAGE---");
    Serial.println("[Destination 1] File sent to PC successfully.");
}

/**
 * @brief Simulates feeding the captured buffer into a Local Embedded Machine Learning model.
 * Performs statistical evaluation on the matrix.
 */
void runLocalInference(camera_fb_t *fb) {
    Serial.println("[Destination 2] Feeding image buffer to Local CV Model...");
    
    // Allocate 96x96 grayscale buffer
    uint8_t grayscale_96[96 * 96];
    bool downscale_ok = false;
    
    if (fb->format == PIXFORMAT_JPEG) {
        // Decode JPEG with downscaling if possible
        esp_jpeg_image_scale_t scale = JPG_SCALE_NONE;
        int out_width = fb->width;
        int out_height = fb->height;
        
        if (fb->width >= 1600) {
            scale = JPG_SCALE_8X;
            out_width = fb->width / 8;
            out_height = fb->height / 8;
        } else if (fb->width >= 800) {
            scale = JPG_SCALE_8X;
            out_width = fb->width / 8;
            out_height = fb->height / 8;
        } else if (fb->width >= 640) {
            scale = JPG_SCALE_4X;
            out_width = fb->width / 4;
            out_height = fb->height / 4;
        } else if (fb->width >= 320) {
            scale = JPG_SCALE_2X;
            out_width = fb->width / 2;
            out_height = fb->height / 2;
        }
        
        // Allocate temporary buffer for decoded RGB565 image
        uint8_t * rgb565_buf = (uint8_t *)malloc(out_width * out_height * 2);
        if (rgb565_buf) {
            if (jpg2rgb565(fb->buf, fb->len, rgb565_buf, scale)) {
                // Downscale out_width x out_height RGB565 to 96x96 Grayscale
                for (int y = 0; y < 96; y++) {
                    int src_y = (y * out_height) / 96;
                    for (int x = 0; x < 96; x++) {
                        int src_x = (x * out_width) / 96;
                        int src_idx = (src_y * out_width + src_x) * 2;
                        uint16_t pixel = (rgb565_buf[src_idx] << 8) | rgb565_buf[src_idx + 1];
                        
                        uint8_t r = ((pixel >> 11) & 0x1F) << 3;
                        uint8_t g = ((pixel >> 5) & 0x3F) << 2;
                        uint8_t b = (pixel & 0x1F) << 3;
                        
                        grayscale_96[y * 96 + x] = (uint8_t)(0.299f * r + 0.587f * g + 0.114f * b);
                    }
                }
                downscale_ok = true;
            } else {
                Serial.println("  [CV Model] Error: JPEG decode to RGB565 failed.");
            }
            free(rgb565_buf);
        } else {
            Serial.println("  [CV Model] Error: Failed to allocate memory for JPEG decode.");
        }
    } 
    else if (fb->format == PIXFORMAT_GRAYSCALE) {
        // Direct downscale from Grayscale
        for (int y = 0; y < 96; y++) {
            int src_y = (y * fb->height) / 96;
            for (int x = 0; x < 96; x++) {
                int src_x = (x * fb->width) / 96;
                grayscale_96[y * 96 + x] = fb->buf[src_y * fb->width + src_x];
            }
        }
        downscale_ok = true;
    } 
    else if (fb->format == PIXFORMAT_RGB565) {
        // Direct downscale from RGB565
        for (int y = 0; y < 96; y++) {
            int src_y = (y * fb->height) / 96;
            for (int x = 0; x < 96; x++) {
                int src_x = (x * fb->width) / 96;
                int src_idx = (src_y * fb->width + src_x) * 2;
                uint16_t pixel = (fb->buf[src_idx] << 8) | fb->buf[src_idx + 1];
                
                uint8_t r = ((pixel >> 11) & 0x1F) << 3;
                uint8_t g = ((pixel >> 5) & 0x3F) << 2;
                uint8_t b = (pixel & 0x1F) << 3;
                
                grayscale_96[y * 96 + x] = (uint8_t)(0.299f * r + 0.587f * g + 0.114f * b);
            }
        }
        downscale_ok = true;
    } 
    else if (fb->format == PIXFORMAT_YUV422) {
        // Direct downscale from YUV422 (Luminance Y is at even indices)
        for (int y = 0; y < 96; y++) {
            int src_y = (y * fb->height) / 96;
            for (int x = 0; x < 96; x++) {
                int src_x = (x * fb->width) / 96;
                int src_idx = (src_y * fb->width + src_x) * 2;
                grayscale_96[y * 96 + x] = fb->buf[src_idx];
            }
        }
        downscale_ok = true;
    }
    
    if (downscale_ok) {
        // Run statistical evaluations on the 96x96 grayscale buffer
        uint32_t sum = 0;
        uint8_t min_pixel = 255;
        uint8_t max_pixel = 0;
        
        for (int i = 0; i < 96 * 96; i++) {
            uint8_t pixel = grayscale_96[i];
            sum += pixel;
            if (pixel < min_pixel) min_pixel = pixel;
            if (pixel > max_pixel) max_pixel = pixel;
        }
        
        float avg_pixel = (float)sum / (96 * 96);
        Serial.printf("  [CV Model] 96x96 Grayscale Input Preprocessing -> Mean: %.2f | Min: %d | Max: %d\n", 
                      avg_pixel, min_pixel, max_pixel);
                      
        // Render the ASCII Art preview in real-time
        renderAsciiArt(grayscale_96, 96, 96);
        
        // Simulating loading tensor and inference
        Serial.println("  [CV Model] Loading input tensor... OK.");
        Serial.println("  [CV Model] Running inference engine (quantized model)...");
        Serial.println("  [CV Model] Inference complete. Mock Gesture Outputs: [Palm: 94.2%, Fist: 3.1%, Wave: 2.7%]");
    } else {
        Serial.println("  [CV Model] Warning: Unable to downscale this format for local inference.");
    }
    
    Serial.println("[Destination 2] Local CV processing complete.");
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

