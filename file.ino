#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/rtc_io.h"

// WiFi credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Telegram BOT Token (Get from Botfather)
#define BOT_TOKEN "YOUR_TELEGRAM_BOT_TOKEN"
#define CHAT_ID "YOUR_CHAT_ID"  // Your Telegram user ID

// Pin definitions
#define MOTION_SENSOR_PIN 13  // GPIO pin connected to PIR sensor
#define BUZZER_PIN 2          // GPIO pin connected to buzzer
#define LED_PIN 4             // Built-in LED pin

// ESP32CAM Camera pins
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Object detection threshold values (simple approach)
#define OBJECT_THRESHOLD 50000  // Pixel difference threshold

WiFiClientSecure clientTCP;
UniversalTelegramBot bot(BOT_TOKEN, clientTCP);

bool motionDetected = false;
unsigned long lastMotionTime = 0;
const unsigned long motionCooldownPeriod = 10000;  // 10 seconds cooldown

// WiFi connection timeout
const unsigned long wifiConnectionTimeout = 20000; // 20 seconds timeout
unsigned long wifiConnectionStartTime = 0;

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  // Disable brownout detector
  
  Serial.begin(115200);
  Serial.println();
  
  // Initialize pins
  pinMode(MOTION_SENSOR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  
  // Initialize camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  // Initialize with high specs to pre-allocate larger buffers
  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;  // 0-63, lower is higher quality
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  
  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }
  
  // Set camera parameters
  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_VGA);  // UXGA|SXGA|XGA|SVGA|VGA|CIF|QVGA|HQVGA|QQVGA
  
  // Connect to Wi-Fi with timeout
  WiFi.begin(ssid, password);
  
  wifiConnectionStartTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    
    // Check if connection timeout has occurred
    if (millis() - wifiConnectionStartTime > wifiConnectionTimeout) {
      Serial.println("\nWiFi connection timeout! Restarting...");
      ESP.restart();
    }
  }
  
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Send startup notification
  bot.sendMessage(CHAT_ID, "ESP32-CAM Motion Detection System is online!", "");
}

void loop() {
  // Check for motion
  if (digitalRead(MOTION_SENSOR_PIN) == HIGH) {
    Serial.println("Motion detected!");
    
    // Check if cooldown period has passed
    if (millis() - lastMotionTime > motionCooldownPeriod) {
      motionDetected = true;
      lastMotionTime = millis();
      
      // Check WiFi connection
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected, attempting to reconnect...");
        WiFi.reconnect();
        delay(5000);  // Wait for reconnection
      }
      
      // Turn on LED and buzzer
      digitalWrite(LED_PIN, HIGH);
      activateBuzzer();
      
      // Take a photo
      camera_fb_t *fb = capturePhoto();
      if (fb) {
        // Perform simple object detection
        String detectedObject = detectObject(fb);
        
        // Send photo with detection result to Telegram
        sendPhotoToTelegram(fb, detectedObject);
        
        // Return the frame buffer
        esp_camera_fb_return(fb);
      }
      
      // Turn off LED
      digitalWrite(LED_PIN, LOW);
      
      // Reset motion flag after handling
      motionDetected = false;
    }
  }
  
  delay(100);  // Small delay to prevent CPU hogging
}

// Activate buzzer with a beeping pattern
void activateBuzzer() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
  }
}

// Capture a photo
camera_fb_t* capturePhoto() {
  Serial.println("Taking photo...");
  
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return NULL;
  }
  
  Serial.printf("Image captured! Size: %zu bytes\n", fb->len);
  return fb;
}

// Improved object detection with memory check
String detectObject(camera_fb_t *fb) {
  // Check if we have a valid frame buffer
  if (!fb || !fb->buf || fb->len == 0) {
    return "Error: Invalid frame buffer";
  }
  
  // Check if we have enough heap memory
  if (ESP.getFreeHeap() < 10000) {  // 10KB threshold
    Serial.println("Warning: Low memory for detection");
  }
  
  uint8_t *buf = fb->buf;
  size_t len = fb->len;
  
  // Simple heuristic - count bright pixels
  int brightPixels = 0;
  int darkPixels = 0;
  
  // Sample every 10th byte for brightness (simplified)
  for (size_t i = 0; i < len; i += 10) {
    if (buf[i] > 128) brightPixels++;
    else darkPixels++;
  }
  
  Serial.printf("Bright pixels: %d, Dark pixels: %d\n", brightPixels, darkPixels);
  
  // Very simple logic - improve this with actual computer vision
  if (brightPixels > OBJECT_THRESHOLD) {
    return "Bright object detected";
  } else if (darkPixels > OBJECT_THRESHOLD) {
    return "Dark object detected";
  } else {
    return "Motion detected, unknown object";
  }
}

// Send photo to Telegram with caption and error handling
void sendPhotoToTelegram(camera_fb_t *fb, String message) {
  Serial.println("Sending photo to Telegram...");
  
  String caption = "Motion Alert! " + message;
  
  // Check if we're connected to WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected. Cannot send photo.");
    return;
  }
  
  // Check if buffer is valid
  if (!fb || !fb->buf || fb->len == 0) {
    Serial.println("Invalid frame buffer. Cannot send photo.");
    return;
  }

  // Tell Telegram we're sending a photo
  bot.sendChatAction(CHAT_ID, "upload_photo");
  
  // Use a simpler approach to send photo to Telegram
  String sent = bot.sendPhotoByBinary(CHAT_ID, "image/jpeg", fb->len, 
    // More data available callback
    [fb](size_t currentSize) -> bool {
      return (currentSize < fb->len);
    },
    // Get next byte callback
    [fb](size_t currentSize) -> uint8_t {
      return (fb->buf)[currentSize];
    },
    // Get next buffer callback (optional, can be nullptr)
    nullptr,
    // Progress callback (optional, can be nullptr)
    nullptr,
    caption
  );
  
  if (sent) {
    Serial.println("Photo sent to Telegram successfully");
  } else {
    Serial.println("Failed to send photo to Telegram");
  }
}
