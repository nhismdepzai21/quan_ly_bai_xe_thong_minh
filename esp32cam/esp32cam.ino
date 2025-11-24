// ======================================================
//  ESP32-CAM STREAMING QUA WEBSOCKET (kèm Base64 + JSON)
// ======================================================

#include "esp_camera.h"          // Thư viện điều khiển camera
#include <WiFi.h>                // WiFi ESP32
#include <WebSocketsServer.h>    // WebSocket server
#include <Base64.h>              // Encode ảnh JPEG thành Base64 để gửi đi

// ===== Wi-Fi credentials =====
const char* ssid     = "iPhoneHiep";
const char* password = "29V766199";

// ===== Camera pins for AI-Thinker (ĐÚNG CHUẨN BOARD AI-THINKER) =====
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

// ===== WebSocket Server chạy trên cổng 81 =====
WebSocketsServer webSocket(81);

unsigned long lastReconnectAttempt = 0;

// ===== Function Declarations =====
void connectWiFi();
void configCamera();
void checkWiFiConnection();
void sendCameraFrame();

// ======================================================
//  SETUP — chạy duy nhất 1 lần
// ======================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n🚀 ESP32-CAM Booting...");

  connectWiFi();      // Kết nối Wi-Fi
  configCamera();     // Cấu hình camera

  webSocket.begin();  // Khởi động WebSocket server
  webSocket.onEvent([](uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
      case WStype_CONNECTED:
        Serial.printf("📡 Client [%u] connected\n", num);
        break;
      case WStype_DISCONNECTED:
        Serial.printf("❌ Client [%u] disconnected\n", num);
        break;
      case WStype_TEXT:  // Nhận message từ client
        Serial.printf("💬 Message from [%u]: %s\n", num, payload);
        break;
    }
  });

  Serial.println("✅ WebSocket server started on port 81");
}

// ======================================================
//  LOOP — chạy lặp liên tục (stream video)
// ======================================================
void loop() {
  webSocket.loop();        // Xử lý WebSocket (phải gọi liên tục)
  checkWiFiConnection();   // Nếu mất Wi-Fi → tự động reconnect

  if (WiFi.status() == WL_CONNECTED) {
    sendCameraFrame();     // Chụp ảnh và gửi qua WebSocket
  }

  delay(30);   // delay nhỏ để giảm tải CPU (tầm 30ms ~ 30FPS)
}

// ======================================================
//  ⚙️ Kết nối Wi-Fi + Retry nếu thất bại
// ======================================================
void connectWiFi() {
  WiFi.mode(WIFI_STA);     // ESP32 chạy ở chế độ Station (kết nối vào router)
  WiFi.begin(ssid, password);

  Serial.print("Connecting to Wi-Fi");

  int retryCount = 0;
  while (WiFi.status() != WL_CONNECTED && retryCount < 30) {  // Tối đa 30 lần thử
    delay(500);
    Serial.print(".");
    retryCount++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("✅ Connected! IP Address: ");
    Serial.println(WiFi.localIP());

    WiFi.setTxPower(WIFI_POWER_19_5dBm);     // Tăng công suất phát Wi-Fi => ổn định hơn
  } else {
    Serial.println("❌ Wi-Fi connect failed (will retry later)");
  }
}

// ======================================================
//  ⚙️ Auto Reconnect nếu mất Wi-Fi
// ======================================================
void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt >= 5000) {  // 5 giây thử reconnect 1 lần
      lastReconnectAttempt = now;
      Serial.println("⚠️ Wi-Fi lost, reconnecting...");
      WiFi.disconnect();
      WiFi.reconnect();
    }
  }
}

// ======================================================
//  ⚙️ Configure Camera (ưu tiên độ ổn định, tránh crash PSRAM)
// ======================================================
void configCamera() {
  camera_config_t config;

  // Tham số clock và DMA
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;

  // Gán đúng chân Data
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;

  // Chân clock + VSYNC + HREF
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;

  // Chân điều khiển camera
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;

  config.xclk_freq_hz = 16000000;  // ⚙️ giảm từ 20MHz → 16MHz để tránh crash PSRAM
  config.pixel_format = PIXFORMAT_JPEG; // Ảnh dạng JPEG

  if (psramFound()) {
    config.frame_size   = FRAMESIZE_VGA; // 640x480 (OCR biển số rõ)
    config.jpeg_quality = 15;            // giảm dung lượng ảnh nhưng vẫn rõ
    config.fb_count     = 1;             // 1 Frame Buffer tránh tràn PSRAM
  } else {
    config.frame_size   = FRAMESIZE_QQVGA; // fallback nếu thiếu PSRAM
    config.jpeg_quality = 20;
    config.fb_count     = 1;
  }

  // Khởi tạo camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Camera init failed 0x%x\n", err);
    while (true); // Dừng lại luôn nếu camera lỗi
  }
  Serial.println("✅ Camera initialized");

  // ===== Tùy chỉnh sensor sau khi init =====
  sensor_t * s = esp_camera_sensor_get();

  s->set_vflip(s, 1);     // Lật dọc nếu camera ngược
  s->set_hmirror(s, 1);   // Lật ngang (phản gương)

  s->set_brightness(s, 0); // chỉnh sáng (-2 ~ 2)
  s->set_contrast(s, 0);   // tương phản (-2 ~ 2)
  s->set_saturation(s, 0); // bão hòa (-2 ~ 2)

  s->set_framesize(s, FRAMESIZE_QVGA);  // ĐẶT FRAME STREAM QVGA (320x240)
  s->set_quality(s, 15);                // chất lượng ảnh
}

// ======================================================
//  📤 Chụp ảnh → Base64 → gửi qua WebSocket dạng JSON
// ======================================================
void sendCameraFrame() {
  camera_fb_t *fb = esp_camera_fb_get();   // Chụp frame JPEG

  if (!fb) {
    Serial.println("❌ Camera capture failed");
    delay(100);
    return;
  }

  // Mã hóa ảnh JPEG sang Base64
  String base64Image = base64::encode(fb->buf, fb->len);

  // Đóng gói JSON gửi tới app/web
  String json = "{\"type\":\"camera_frame\",\"image\":\"" + base64Image + "\"}";

  // Gửi cho tất cả client đang kết nối WebSocket
  webSocket.broadcastTXT(json);

  Serial.printf("📸 Sent frame (%d bytes)\n", fb->len);

  esp_camera_fb_return(fb); // Trả buffer về camera để dùng lại
}
