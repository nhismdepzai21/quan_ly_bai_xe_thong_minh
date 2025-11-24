// =BE_INCLUDE_THU_VIEN ========================================
#include <ESP32Servo.h>        // Thư viện điều khiển Servo cho ESP32
#include <Wire.h>              // Thư viện giao tiếp I2C (cần cho LCD)
#include <LiquidCrystal_I2C.h> // Thư viện điều khiển LCD I2C
#include <WiFi.h>              // Thư viện kết nối WiFi cho ESP32
#include <Firebase_ESP_Client.h> // Thư viện kết nối Firebase
#include <ArduinoJson.h>       // Thư viện xử lý dữ liệu JSON (cần cho Firebase)

// ================== CẤU HÌNH (CONFIGURATIONS) ==================
// --- Cấu hình WiFi ---
#define WIFI_SSID "iPhoneHiep"    // Tên mạng WiFi (SSID)
#define WIFI_PASSWORD "29V766199" // Mật khẩu WiFi

// --- Cấu hình Firebase ---
#define API_KEY "AIzaSyBqrgRtHQibGmDlXYCZyyVjNYy_x4AO2iI"      // API Key của dự án Firebase
#define DATABASE_URL "https://baidoxethongminh-f34d1-default-rtdb.firebaseio.com/" // URL của Realtime Database

// ================== KHAI BÁO PHẦN CỨNG (HARDWARE PINS) ==================
LiquidCrystal_I2C lcd(0x27, 16, 2); // Khởi tạo LCD: địa chỉ 0x27, 16 cột, 2 hàng
Servo servoIn;  // Đối tượng Servo cho cổng VÀO
Servo servoOut; // Đối tượng Servo cho cổng RA

// --- Các chân cảm biến hồng ngoại (IR) ---
#define IR_ENTER 32 // Cảm biến tại cổng VÀO
#define IR_EXIT  33 // Cảm biến tại cổng RA
#define IR_CAR1  34 // Cảm biến vị trí đỗ xe 1
#define IR_CAR2  35 // Cảm biến vị trí đỗ xe 2
#define IR_CAR3  27 // Cảm biến vị trí đỗ xe 3
#define IR_CAR4  14 // Cảm biến vị trí đỗ xe 4
#define IR_CAR5  25 // Cảm biến vị trí đỗ xe 5 (ảo)
#define IR_CAR6  24 // Cảm biến vị trí đỗ xe 6 (ảo)
#define IR_CAR7  29 // Cảm biến vị trí đỗ xe 7 (ảo)
#define IR_CAR8  30 // Cảm biến vị trí đỗ xe 8 (ảo)
#define IR_CAR9  31 // Cảm biến vị trí đỗ xe 9 (ảo)
#define IR_CAR10 29 // Cảm biến vị trí đỗ xe 10 (ảo)
// !!! LƯU Ý: IR_CAR7 và IR_CAR10 đang được định nghĩa cùng một chân (PIN 29). 
// Điều này không ảnh hưởng nếu S7 và S10 đều là "ảo" (không đọc),
// nhưng nếu dùng thật sẽ bị xung đột.

// --- Các chân điều khiển Servo ---
#define SERVO_IN_PIN  12 // Chân tín hiệu cho Servo cổng VÀO
#define SERVO_OUT_PIN 15 // Chân tín hiệu cho Servo cổng RA

// ================== BIẾN TOÀN CỤC (GLOBAL VARIABLES) ==================
// Biến lưu trạng thái các vị trí đỗ xe (0 = Trống, 1 = Có xe)
int S1 = 0, S2 = 0, S3 = 0, S4 = 0, S5 = 0, S6 = 0, S7 = 0, S8 = 0, S9 = 0, S10 = 0;
int slot = 10; // Tổng số chỗ trống (ban đầu là 10)

// Biến lưu trạng thái cũ để kiểm tra thay đổi
// Dùng để tối ưu: chỉ gửi dữ liệu lên Firebase khi có sự thay đổi
int lastS1 = -1, lastS2 = -1, lastS3 = -1, lastS4 = -1, lastS5 = -1;
int lastS6 = -1, lastS7 = -1, lastS8 = -1, lastS9 = -1, lastS10 = -1;
int lastSlot = -1;
String lastGateInStatus = "";  // Trạng thái cổng vào lần cuối gửi đi
String lastGateOutStatus = ""; // Trạng thái cổng ra lần cuối gửi đi

// Biến lưu nội dung hiển thị trên LCD để tránh nhấp nháy
// Chỉ cập nhật LCD khi nội dung thực sự thay đổi
String lastLine1 = "";
String lastLine2 = "";

// === BIẾN TRẠNG THÁI CỔNG TRÊN FIREBASE ===
// Lưu trạng thái cổng (dạng String) để gửi lên Firebase
String gateInStatus = "Khong co xe";
String gateOutStatus = "Khong co xe";

// --- Các đối tượng Firebase ---
FirebaseData fbdo;   // Đối tượng xử lý dữ liệu Firebase
FirebaseAuth auth;   // Đối tượng xác thực Firebase
FirebaseConfig config; // Đối tượng cấu hình Firebase

// === BIẾN CHO LOGIC KHÔNG CHẶN (NON-BLOCKING) ===
// Các biến này dùng để điều khiển cổng mà không dùng hàm delay()

// Biến trạng thái cổng (true = Mở, false = Đóng)
bool isGateInOpen = false;
bool isGateOutOpen = false;

// Thời gian giữ cổng mở sau khi xe đi (3000ms = 3 giây)
const long gateCloseDelay = 3000;
// Thời gian chờ 1 giây khi xe đến (1000ms = 1 giây)
const long gateDetectDuration = 1000;

// --- Biến theo dõi cổng VÀO ---
unsigned long irEnterDetectTime = 0; // Thời điểm (millis) bắt đầu phát hiện xe ở cổng VÀO
unsigned long irEnterClearTime = 0;  // Thời điểm (millis) xe rời khỏi cảm biến VÀO
bool isDetectingEnter = false;       // Cờ: Báo hiệu đang trong 1 giây chờ (VÀO)
bool isClosingEnter = false;         // Cờ: Báo hiệu đang trong 3 giây chờ đóng (VÀO)

// --- Biến theo dõi cổng RA ---
unsigned long irExitDetectTime = 0;  // Thời điểm (millis) bắt đầu phát hiện xe ở cổng RA
unsigned long irExitClearTime = 0;   // Thời điểm (millis) xe rời khỏi cảm biến RA
bool isDetectingExit = false;        // Cờ: Báo hiệu đang trong 1 giây chờ (RA)
bool isClosingExit = false;          // Cờ: Báo hiệu đang trong 3 giây chờ đóng (RA)


// ================== KHAI BÁO HÀM (FUNCTION PROTOTYPES) ==================
void connectWiFi();          // Hàm kết nối WiFi
void updateSensorStatus();   // Hàm đọc cảm biến và cập nhật số chỗ
void sendDataToFirebase();   // Hàm gửi dữ liệu lên Firebase
void handleGatesNonBlocking(); // Hàm xử lý logic cổng (non-blocking)
void updateLcdDisplay();     // Hàm cập nhật màn hình LCD

// ================== HÀM SETUP - CHẠY 1 LẦN KHI KHỞI ĐỘNG ==================
void setup() {
  Serial.begin(115200); // Khởi động Serial Monitor để debug

  // --- Khởi tạo các chân cảm biến là INPUT ---
  pinMode(IR_CAR1, INPUT);
  pinMode(IR_CAR2, INPUT);
  pinMode(IR_CAR3, INPUT);
  pinMode(IR_CAR4, INPUT);
  pinMode(IR_CAR5, INPUT);
  pinMode(IR_CAR6, INPUT);
  pinMode(IR_CAR7, INPUT); // Vẫn khởi tạo dù là "ảo"
  pinMode(IR_CAR8, INPUT);
  pinMode(IR_CAR9, INPUT);
  pinMode(IR_CAR10, INPUT);
  pinMode(IR_ENTER, INPUT);
  pinMode(IR_EXIT, INPUT);

  // --- Khởi tạo Servo ---
  servoIn.attach(SERVO_IN_PIN);   // Gắn servo vào chân đã định nghĩa
  servoOut.attach(SERVO_OUT_PIN);
  servoIn.write(90);  // Đặt servo cổng VÀO ở vị trí 90 độ (Đóng)
  servoOut.write(90); // Đặt servo cổng RA ở vị trí 90 độ (Đóng)

  // --- Khởi tạo LCD ---
  lcd.init();      // Bắt đầu LCD
  lcd.backlight(); // Bật đèn nền
  lcd.setCursor(0, 0);
  lcd.print("Bai Do Xe TM");
  lcd.setCursor(0, 1);
  lcd.print("Khoi Dong...");
  delay(2000); // Chờ 2 giây để hiển thị thông báo chào
  lcd.clear(); // Xóa màn hình

  // --- Kết nối WiFi ---
  connectWiFi(); // Gọi hàm kết nối WiFi

  // --- Cấu hình và kết nối Firebase ---
  Serial.println("Dang cau hinh Firebase...");
  config.api_key = API_KEY;         // Gán API Key
  config.database_url = DATABASE_URL; // Gán URL Database

  // Đăng nhập ẩn danh (Anonymous) vào Firebase
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("=> Dang nhap Firebase thanh cong!");
  } else {
    Serial.printf("=> Loi dang nhap Firebase: %s\n", config.signer.signupError.message.c_str());
  }

  Firebase.begin(&config, &auth); // Bắt đầu kết nối Firebase
  Firebase.reconnectWiFi(true);   // Tự động kết nối lại WiFi nếu mất

  // Cập nhật trạng thái và gửi lên Firebase lần đầu
  updateSensorStatus();   // Đọc cảm biến
  sendDataToFirebase();   // Gửi lên Firebase

  // Gán giá trị ban đầu cho các biến 'last...' 
  // để đồng bộ với trạng thái vừa gửi đi
  lastS1 = S1; lastS2 = S2; lastS3 = S3; lastS4 = S4; lastS5 = S5;
  lastS6 = S6; lastS7 = S7; lastS8 = S8; lastS9 = S9; lastS10 = S10;
  lastSlot = slot;
  lastGateInStatus = gateInStatus;
  lastGateOutStatus = gateOutStatus;
}

// ================== HÀM LOOP - CHẠY LIÊN TỤC ==================
void loop() {
  connectWiFi(); // Kiểm tra kết nối WiFi, nếu mất sẽ tự kết nối lại

  updateSensorStatus(); // 1. Đọc trạng thái các cảm biến vị trí
  updateLcdDisplay();   // 2. Cập nhật màn hình LCD
  
  // 3. Điều khiển cổng vào/ra theo logic mới (non-blocking)
  handleGatesNonBlocking();

  // 4. Kiểm tra nếu có sự thay đổi trạng thái thì mới gửi dữ liệu lên Firebase
  // Đây là logic quan trọng để TRÁNH GỬI DỮ LIỆU LIÊN TỤC gây tốn băng thông và làm chậm hệ thống
  if (S1 != lastS1 || S2 != lastS2 || S3 != lastS3 || S4 != lastS4 || 
      S5 != lastS5 || S6 != lastS6 || S7 != lastS7 || S8 != lastS8 || 
      S9 != lastS9 || S10 != lastS10 || slot != lastSlot || 
      gateInStatus != lastGateInStatus || gateOutStatus != lastGateOutStatus) {
        
    Serial.println("Phat hien thay doi, cap nhat Firebase...");
    sendDataToFirebase(); // Chỉ gửi khi có thay đổi
    
    // Cập nhật lại tất cả trạng thái "last" sau khi gửi
    lastS1 = S1; lastS2 = S2; lastS3 = S3; lastS4 = S4; lastS5 = S5;
    lastS6 = S6; lastS7 = S7; lastS8 = S8; lastS9 = S9; lastS10 = S10;
    lastSlot = slot;
    lastGateInStatus = gateInStatus;
    lastGateOutStatus = gateOutStatus;
  }

  delay(50); // Delay ngắn để vòng lặp chạy ổn định, không quá nhanh
}

// ================== CÁC HÀM CHỨC NĂNG (FUNCTIONS) ==================

/**
 * @brief Kết nối vào mạng WiFi. Tự động kết nối lại nếu bị mất.
 */
void connectWiFi() {
  if (WiFi.status() != WL_CONNECTED) { // Chỉ thực hiện nếu đang mất kết nối
    Serial.print("Dang ket noi WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      delay(300);
    }
    Serial.println("\n=> WiFi da ket noi!");
    Serial.print("Dia chi IP: ");
    Serial.println(WiFi.localIP());
  }
}

/**
 * @brief Đọc trạng thái từ các cảm biến và cập nhật số chỗ trống.
 * ĐÃ SỬA ĐỔI: S5 đến S10 luôn báo trống (giá trị = 0).
 */
void updateSensorStatus() {
  // Đọc 4 cảm biến thật: Cảm biến hồng ngoại thường trả về LOW khi có vật cản
  S1 = (digitalRead(IR_CAR1) == LOW) ? 1 : 0; // Nếu LOW (có xe) -> S1 = 1
  S2 = (digitalRead(IR_CAR2) == LOW) ? 1 : 0; // Nếu HIGH (trống) -> S2 = 0
  S3 = (digitalRead(IR_CAR3) == LOW) ? 1 : 0;
  S4 = (digitalRead(IR_CAR4) == LOW) ? 1 : 0;
  
  // Yêu cầu của người dùng: S5 đến S10 là "ảo" và luôn báo "co xe"
  // "Trống" tương ứng với giá trị 0 (vì 1 là "Đã có xe")
  S5 = 1;
  S6 = 1;
  S7 = 1;
  S8 = 1;
  S9 = 1;
  S10 = 1;
  
  // Tính toán lại tổng số chỗ trống
  // slot = (Tổng 10) - (Số xe đang đỗ)
  slot = 10 - (S1 + S2 + S3 + S4 + S5 + S6 + S7 + S8 + S9 + S10); 
  // Do S5-S10 luôn = 0, phép tính này tương đương 10 - (S1+S2+S3+S4)
}

/**
 * @brief Gửi toàn bộ dữ liệu trạng thái bãi xe lên Firebase Realtime Database.
 */
void sendDataToFirebase() {
  if (Firebase.ready()) { // Kiểm tra xem Firebase đã sẵn sàng nhận dữ liệu chưa
    FirebaseJson json; // Tạo một đối tượng JSON
    
    // Thêm các cặp key-value vào đối tượng JSON
    json.set("slots", slot); // Gửi số chỗ trống (dạng số)
    // Chuyển đổi 0/1 thành chuỗi "Con Trong" / "Da co xe" bằng toán tử 3 ngôi
    json.set("S1", S1 ? "Da co xe" : "Con Trong"); 
    json.set("S2", S2 ? "Da co xe" : "Con Trong");
    json.set("S3", S3 ? "Da co xe" : "Con Trong");
    json.set("S4", S4 ? "Da co xe" : "Con Trong");
    json.set("S5", S5 ? "Da co xe" : "Con Trong"); // Sẽ luôn là "Còn Trống"
    json.set("S6", S6 ? "Da co xe" : "Con Trong"); // Sẽ luôn là "Còn Trống"
    json.set("S7", S7 ? "Da co xe" : "Con Trong"); // Sẽ luôn là "Còn Trống"
    json.set("S8", S8 ? "Da co xe" : "Con Trong"); // Sẽ luôn là "Còn Trống"
    json.set("S9", S9 ? "Da co xe" : "Con Trong"); // Sẽ luôn là "Còn Trống"
    json.set("S10", S10 ? "Da co xe" : "Con Trong"); // Sẽ luôn là "Còn Trống"
    
    // Gửi dữ liệu trạng thái cổng
    json.set("CongVao", gateInStatus);
    json.set("CongRa", gateOutStatus);

    // Gửi đối tượng JSON lên Firebase tại đường dẫn "/parking"
    if (Firebase.RTDB.setJSON(&fbdo, "/parking", &json)) {
      Serial.println("=> Gui du lieu len Firebase thanh cong!");
    } else {
      Serial.println("=> Loi khi gui du lieu: " + fbdo.errorReason());
    }
  } else {
    Serial.println("=> Firebase chua san sang, vui long kiem tra lai ket noi.");
  }
}

/**
 * @brief Điều khiển cổng không chặn (non-blocking) VÀ ĐÃ VIẾT LẠI HOÀN TOÀN
 * Logic mới:
 * 1. Mở cổng: Chỉ mở khi xe đứng ở cảm biến liên tục 1 giây.
 * 2. Đóng cổng: Chỉ đóng sau khi xe đã rời khỏi cảm biến 3 giây.
 */
void handleGatesNonBlocking() {
  // Lấy thời gian hiện tại (tính bằng mili giây từ khi ESP32 khởi động)
  // Đây là biến thời gian "chuẩn" cho toàn bộ hàm này
  unsigned long currentTime = millis(); 
  
  // Đọc trạng thái 2 cảm biến cổng 1 LẦN duy nhất ở đầu hàm
  int irEnterState = digitalRead(IR_ENTER); // LOW = có xe, HIGH = không có xe
  int irExitState = digitalRead(IR_EXIT);

  // ==========================
  // === XỬ LÝ CỔNG VÀO (IN) ===
  // ==========================

  // --- TRƯỜNG HỢP 1: CÓ XE TẠI CỔNG VÀO (irEnterState == LOW) ---
  if (irEnterState == LOW) { 
    // 1. Logic MỞ cổng (Yêu cầu: Chờ 1 giây)
    if (!isDetectingEnter) {
      // Nếu cờ isDetectingEnter = false (tức là xe vừa mới đến)
      // => Bật cờ lên
      isDetectingEnter = true; 
      // => Ghi lại thời điểm bắt đầu phát hiện
      irEnterDetectTime = currentTime; 
      Serial.println("Phat hien xe Vao, bat dau dem 1s...");
    }

    // Kiểm tra xem đã đủ 1 giây chưa (kể từ lúc irEnterDetectTime được ghi)
    if (isDetectingEnter && (currentTime - irEnterDetectTime >= gateDetectDuration)) {
      // ĐÃ ĐỦ 1 GIÂY
      
      if (!isGateInOpen && slot > 0) {
        // Nếu cổng đang ĐÓNG (isGateInOpen = false) VÀ bãi CÒN CHỖ (slot > 0)
        // => Mở cổng
        Serial.println("=> Da du 1s, MO CONG VAO");
        servoIn.write(0); // Quay servo về 0 độ (Mở)
        isGateInOpen = true; // Cập nhật trạng thái cổng
        gateInStatus = "Co xe vao"; // Cập nhật trạng thái cho Firebase
      
      } else if (slot <= 0 && !isGateInOpen) {
        // Nếu BÃI ĐẦY (slot <= 0) và cổng đang ĐÓNG
        // => Báo lỗi trên LCD (không mở cổng)
        Serial.println("Bai xe da day!");
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("Bai da day,");
        lcd.setCursor(0, 1); lcd.print("Vui long quay lai!");
        delay(2000); // Chấp nhận delay nhỏ ở đây vì là trường hợp đặc biệt
        lcd.clear();
        lastLine1 = ""; lastLine2 = ""; // Reset biến lastLine để LCD cập nhật lại
      }
    }
    
    // 2. Logic ĐÓNG cổng (Yêu cầu: Không đóng khi có xe)
    // Nếu xe vẫn còn ở cảm biến, HỦY mọi tiến trình đang chờ đóng (nếu có)
    if (isClosingEnter) {
      // isClosingEnter = true có nghĩa là xe đã đi qua và cổng đang đếm 3s để đóng
      // nhưng bỗng nhiên xe lùi lại (hoặc xe khác tới)
      Serial.println("Xe Vao van con, huy lenh dong cong.");
      isClosingEnter = false; // Hủy cờ chờ đóng
      irEnterClearTime = 0;   // Reset thời gian chờ đóng
    }

  // --- TRƯỜNG HỢP 2: KHÔNG CÓ XE TẠI CỔNG VÀO (irEnterState == HIGH) ---
  } else { 
    // Reset cờ phát hiện 1 giây (vì không có xe thì không cần phát hiện)
    isDetectingEnter = false;

    // 2. Logic ĐÓNG cổng (Yêu cầu: Xe đi qua mới đóng)
    if (isGateInOpen && !isClosingEnter) {
      // Nếu cổng đang MỞ (isGateInOpen = true) 
      // và xe vừa rời đi (chưa có lệnh đóng: !isClosingEnter)
      // => Bắt đầu đếm 3 giây để đóng
      isClosingEnter = true;       // Bật cờ "đang chờ đóng"
      irEnterClearTime = currentTime; // Ghi lại thời điểm xe rời đi
      Serial.println("Xe Vao da roi cam bien, bat dau dem 3s de dong...");
    }
  }

  // 3. Thi hành ĐÓNG cổng VÀO (Code này chạy độc lập)
  // Luôn kiểm tra xem có cờ isClosingEnter không
  if (isClosingEnter && (currentTime - irEnterClearTime >= gateCloseDelay)) {
    // Nếu cờ "đang chờ đóng" BẬT VÀ đã qua 3 giây (kể từ lúc irEnterClearTime)
    Serial.println("=> Da du 3s, DONG CONG VAO");
    servoIn.write(90);     // Đóng cổng
    isGateInOpen = false;  // Cập nhật trạng thái
    isClosingEnter = false; // Reset cờ
    gateInStatus = "Khong co xe"; // Cập nhật trạng thái cho Firebase
  }


  // =========================
  // === XỬ LÝ CỔNG RA (OUT) ===
  // =========================
  // Logic tương tự cổng VÀO, nhưng không cần kiểm tra 'slot'

  // --- TRƯỜNG HỢP 1: CÓ XE TẠI CỔNG RA (irExitState == LOW) ---
  if (irExitState == LOW) { 
    // 1. Logic MỞ cổng (Áp dụng 1 giây chờ cho nhất quán)
    if (!isDetectingExit) {
      // Xe vừa đến
      isDetectingExit = true;
      irExitDetectTime = currentTime;
      Serial.println("Phat hien xe Ra, bat dau dem 1s...");
    }

    // Kiểm tra xem đã đủ 1 giây chờ chưa
    if (isDetectingExit && (currentTime - irExitDetectTime >= gateDetectDuration)) {
      if (!isGateOutOpen) {
        // Nếu đủ 1s và cổng đang đóng -> Mở cổng
        Serial.println("=> Da du 1s, MO CONG RA");
        servoOut.write(0); // Mở cổng ra
        isGateOutOpen = true;
        gateOutStatus = "Co xe ra";
      }
    }
    
    // 2. Logic ĐÓNG cổng (Không đóng khi có xe)
    // Nếu xe vẫn còn, hủy lệnh chờ đóng (nếu có)
    if (isClosingExit) {
      Serial.println("Xe Ra van con, huy lenh dong cong.");
      isClosingExit = false;
      irExitClearTime = 0;
    }
  
  // --- TRƯỜNG HỢP 2: KHÔNG CÓ XE TẠI CỔNG RA (irExitState == HIGH) ---
  } else { 
    // Reset cờ phát hiện 1 giây
    isDetectingExit = false;

    // 2. Logic ĐÓNG cổng (Xe đi qua mới đóng)
    if (isGateOutOpen && !isClosingExit) {
      // Cổng đang mở và xe vừa rời đi
      // => Bắt đầu đếm 3s
      isClosingExit = true;
      irExitClearTime = currentTime;
      Serial.println("Xe Ra da roi cam bien, bat dau dem 3s de dong...");
    }
  }

  // 3. Thi hành ĐÓNG cổng RA
  if (isClosingExit && (currentTime - irExitClearTime >= gateCloseDelay)) {
    // Nếu đang chờ đóng VÀ đã đủ 3s
    Serial.println("=> Da du 3s, DONG CONG RA");
    servoOut.write(90);    // Đóng cổng
    isGateOutOpen = false;
    isClosingExit = false; // Reset cờ
    gateOutStatus = "Khong co xe";
  }
}


/**
 * @brief Cập nhật màn hình LCD với trạng thái các vị trí và số chỗ trống.
 * Sử dụng biến 'lastLine' để chống nhấp nháy.
 */
void updateLcdDisplay() {
  // Tạo chuỗi nội dung cho 2 dòng
  String line1 = "Trong: " + String(slot) + " | S1:" + String(S1) ;
  String line2 = " S2:" + String(S2) +" S3:" + String(S3) + " S4:" + String(S4);

  // --- Cập nhật Dòng 1 ---
  if (line1 != lastLine1) { // Chỉ cập nhật nếu nội dung thay đổi
    lcd.setCursor(0, 0);
    lcd.print("                "); // Xóa dòng cũ bằng cách in 16 khoảng trắng
    lcd.setCursor(0, 0);
    lcd.print(line1);       // In nội dung mới
    lastLine1 = line1;      // Lưu lại nội dung vừa in
  }

  // --- Cập nhật Dòng 2 ---
  if (line2 != lastLine2) { // Chỉ cập nhật nếu nội dung thay đổi
    lcd.setCursor(0, 1);
    lcd.print("                "); // Xóa dòng cũ
    lcd.setCursor(0, 1);
    lcd.print(line2);       // In nội dung mới
    lastLine2 = line2;      // Lưu lại nội dung vừa in
  }
}