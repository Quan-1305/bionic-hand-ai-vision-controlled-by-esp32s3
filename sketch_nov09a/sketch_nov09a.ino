#include <WiFi.h>
#include <WebServer.h>

// ================= 1. ĐIỀN WI-FI CỦA BẠN =================
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// ================= 2. BẢNG ÁNH XẠ CHÂN YOLO UNO =================
// Bộ 1 (Mặc định): Cụm 4 chân Servo đáy mạch (D2, D13, D12, D11) + Chân D10
// Bộ 2 (Dự phòng): Nếu bạn cắm theo thứ tự D2, D3, D4, D5, D6 bên cạnh phải
int SERVO_PINS[5] = {5, 48, 47, 38, 21}; 

// Thông số xung phần cứng LEDC chuẩn cho Servo (50Hz, độ phân giải 14-bit: 0 -> 16383)
const int PWM_FREQ = 50;
const int PWM_RES = 14; 

const char* FINGER_NAMES[5] = {
  "Servo 1 - Ngón Cái",
  "Servo 2 - Ngón Trỏ",
  "Servo 3 - Ngón Giữa",
  "Servo 4 - Ngón Áp Út",
  "Servo 5 - Ngón Út"
};

int currentAngles[5] = {90, 90, 90, 90, 90};
WebServer server(80);

// Hàm đổi góc (0 - 180 độ) sang độ rộng xung (500us - 2400us) trong chu kỳ 20,000us (50Hz)
uint32_t angleToDuty(int angle) {
  angle = constrain(angle, 0, 180);
  uint32_t pulseWidthUs = map(angle, 0, 180, 500, 2400);
  return (pulseWidthUs * ((1 << PWM_RES) - 1)) / 20000;
}

// Hàm khởi tạo 5 kênh PWM phần cứng độc lập hoàn toàn
void initServosHardwarePWM() {
  for (int i = 0; i < 5; i++) {
    // Tự động tương thích với cả bản ESP32 Arduino Core 2.x và 3.x
    #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
      ledcDetach(SERVO_PINS[i]);
      ledcAttachChannel(SERVO_PINS[i], PWM_FREQ, PWM_RES, i);
    #else
      ledcSetup(i, PWM_FREQ, PWM_RES);
      ledcAttachPin(SERVO_PINS[i], i);
    #endif
  }
}

// Hàm xuất xung điều khiển ĐÚNG 1 SERVO duy nhất
void moveSingleServo(int index, int angle) {
  if (index < 0 || index >= 5) return;
  angle = constrain(angle, 0, 180);
  currentAngles[index] = angle;
  uint32_t duty = angleToDuty(angle);

  #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcWrite(SERVO_PINS[index], duty);
  #else
    ledcWrite(index, duty);
  #endif
}

// Hàm điều khiển TẤT CẢ 5 SERVO - cập nhật đồng thời, không delay
void moveAllServos(int angle) {
  angle = constrain(angle, 0, 180);
  uint32_t duty = angleToDuty(angle);

  for (int i = 0; i < 5; i++) {
    currentAngles[i] = angle;
    #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
      ledcWrite(SERVO_PINS[i], duty);
    #else
      ledcWrite(i, duty);
    #endif
  }
}

void sendCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");
}

// ================= 3. GIAO DIỆN WEB 5 BLOCK + CHỌN SƠ ĐỒ CHÂN =================
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Yolo UNO - Fix 5 Ngón Tay Servo</title>
  <style>
    body { font-family: 'Segoe UI', sans-serif; background: #0f172a; color: #f8fafc; margin: 0; padding: 20px; text-align: center; }
    h1 { color: #38bdf8; margin-bottom: 5px; font-size: 22px; }
    .pinmap-box {
      background: #1e293b; border: 1px solid #475569; border-radius: 10px;
      padding: 12px; max-width: 750px; margin: 10px auto 20px auto; font-size: 14px;
    }
    .pinmap-box button { margin: 4px; padding: 8px 12px; background: #6366f1; color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: bold; }
    .pinmap-box button:hover { background: #4f46e5; }
    .grid-container { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 16px; max-width: 1200px; margin: 0 auto; }
    .servo-block { background: #1e293b; border: 1px solid #334155; border-radius: 12px; padding: 16px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
    .finger-title { font-size: 18px; font-weight: bold; color: #facc15; margin-bottom: 4px; }
    .pin-label { font-size: 12px; color: #94a3b8; margin-bottom: 10px; }
    .angle-badge { display: inline-block; background: #0f172a; color: #4ade80; padding: 4px 12px; border-radius: 20px; font-size: 14px; margin-bottom: 14px; border: 1px solid #334155; }
    .btn-row { display: flex; gap: 8px; justify-content: center; margin-bottom: 10px; }
    button.ctrl { flex: 1; padding: 10px 8px; font-size: 14px; font-weight: 600; border: none; border-radius: 8px; cursor: pointer; }
    button.ctrl:active { transform: scale(0.95); }
    .btn-single { background: #0284c7; color: white; }
    .btn-all-0 { background: #e11d48; color: white; }
    .btn-all-180 { background: #16a34a; color: white; }
    .divider { border-top: 1px dashed #334155; margin: 10px 0; padding-top: 8px; font-size: 12px; color: #94a3b8; }
    #log-status { margin-top: 20px; color: #38bdf8; font-weight: bold; }
  </style>
</head>
<body>
  <h1>ĐIỀU KHIỂN 5 NGÓN TAY SERVO (HARDWARE LEDC 5 KÊNH ĐỘC LẬP)</h1>
  
  <div class="pinmap-box">
    <div><b>Chọn vị trí chân bạn đang cắm 5 Servo trên mạch Yolo UNO:</b></div>
    <button onclick="setPinMode(1)">Kiểu 1: Cụm đáy (D2, D13, D12, D11) + D10</button>
    <button onclick="setPinMode(2)">Kiểu 2: Hàng chân phải (D2, D3, D4, D5, D6)</button>
    <button onclick="setPinMode(3)">Kiểu 3: Hàng chân phải (D9, D10, D11, D12, D13)</button>
    <div id="pin-info" style="margin-top:6px; color:#4ade80;">Đang dùng Kiểu 1: S1=D2(GPIO5), S2=D13(GPIO48), S3=D12(GPIO47), S4=D11(GPIO38), S5=D10(GPIO21)</div>
  </div>

  <div class="grid-container" id="blocks"></div>
  <div id="log-status">Trạng thái: Sẵn sàng</div>

  <script>
    const fingers = [
      { id: 1, name: "Servo 1 - Ngón Cái" },
      { id: 2, name: "Servo 2 - Ngón Trỏ" },
      { id: 3, name: "Servo 3 - Ngón Giữa" },
      { id: 4, name: "Servo 4 - Ngón Áp Út" },
      { id: 5, name: "Servo 5 - Ngón Út" }
    ];

    const container = document.getElementById('blocks');
    fingers.forEach(f => {
      const block = document.createElement('div');
      block.className = 'servo-block';
      block.innerHTML = `
        <div class="finger-title">${f.name}</div>
        <div class="angle-badge">Góc hiện tại: <span id="val-${f.id}">90</span>°</div>
        <div class="btn-row">
          <button class="ctrl btn-single" onclick="sendCmd('${f.id}', 0)">0°</button>
          <button class="ctrl btn-single" onclick="sendCmd('${f.id}', 90)">90°</button>
          <button class="ctrl btn-single" onclick="sendCmd('${f.id}', 180)">180°</button>
        </div>
        <div class="divider">Lệnh đồng loạt (Áp dụng cả 5 Servo)</div>
        <div class="btn-row">
          <button class="ctrl btn-all-0" onclick="sendCmd('all', 0)">Tất cả về 0°</button>
          <button class="ctrl btn-all-180" onclick="sendCmd('all', 180)">Tất cả lên 180°</button>
        </div>
      `;
      container.appendChild(block);
    });

    function sendCmd(target, angle) {
      document.getElementById('log-status').innerText = `Đang gửi: Servo=${target} -> ${angle}°...`;
      fetch(`/cmd?servo=${target}&angle=${angle}`)
        .then(res => res.json())
        .then(data => {
          if (data.angles) {
            for (let i = 0; i < 5; i++) {
              document.getElementById(`val-${i+1}`).innerText = data.angles[i];
            }
          }
          document.getElementById('log-status').innerText = `Thành công: ${data.message}`;
        });
    }

    function setPinMode(mode) {
      fetch(`/pinmode?mode=${mode}`)
        .then(res => res.json())
        .then(data => {
          document.getElementById('pin-info').innerText = data.pin_desc;
          document.getElementById('log-status').innerText = "Đã chuyển sơ đồ chân thành công!";
        });
    }
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Khởi tạo 5 kênh PWM phần cứng
  initServosHardwarePWM();
  // Đưa lần lượt từng servo về 90 độ (cách nhau 100ms để không sụt nguồn lúc bật mạch)
  for (int i = 0; i < 5; i++) {
    moveSingleServo(i, 90);
    delay(100);
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Dang ket noi Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[OK] Truy cap Web: http://" + WiFi.localIP().toString());

  server.on("/", HTTP_GET, []() {
    sendCorsHeaders();
    server.send_P(200, "text/html", HTML_PAGE);
  });

  // API đổi bộ chân cắm trực tiếp trên Web để dò đúng chân bạn đã nối dây
  server.on("/pinmode", HTTP_GET, []() {
    sendCorsHeaders();
    int mode = server.arg("mode").toInt();
    String desc = "";
    
    // Ngắt PWM ở các chân cũ trước khi đổi
    for (int i = 0; i < 5; i++) {
      #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
        ledcDetach(SERVO_PINS[i]);
      #else
        ledcDetachPin(SERVO_PINS[i]);
      #endif
    }

    if (mode == 2) {
      // D2(5), D3(6), D4(7), D5(8), D6(9)
      int p[5] = {5, 6, 7, 8, 9};
      memcpy(SERVO_PINS, p, sizeof(p));
      desc = "Đang dùng Kiểu 2: S1=D2(GPIO5), S2=D3(GPIO6), S3=D4(GPIO7), S4=D5(GPIO8), S5=D6(GPIO9)";
    } else if (mode == 3) {
      // D9(18), D10(21), D11(38), D12(47), D13(48)
      int p[5] = {18, 21, 38, 47, 48};
      memcpy(SERVO_PINS, p, sizeof(p));
      desc = "Đang dùng Kiểu 3: S1=D9(GPIO18), S2=D10(GPIO21), S3=D11(GPIO38), S4=D12(GPIO47), S5=D13(GPIO48)";
    } else {
      // Mặc định Kiểu 1: Cụm đáy D2(5), D13(48), D12(47), D11(38) + D10(21)
      int p[5] = {5, 48, 47, 38, 21};
      memcpy(SERVO_PINS, p, sizeof(p));
      desc = "Đang dùng Kiểu 1: S1=D2(GPIO5), S2=D13(GPIO48), S3=D12(GPIO47), S4=D11(GPIO38), S5=D10(GPIO21)";
    }

    initServosHardwarePWM();
    for (int i = 0; i < 5; i++) {
      moveSingleServo(i, currentAngles[i]);
      delay(50);
    }
    server.send(200, "application/json", "{\"status\":\"ok\",\"pin_desc\":\"" + desc + "\"}");
  });

  server.on("/cmd", HTTP_GET, []() {
    sendCorsHeaders();
    String target = server.arg("servo");
    int angle = server.arg("angle").toInt();
    String msg = "";

    if (target == "all") {
      moveAllServos(angle);
      msg = "Tat ca 5 ngon -> " + String(angle) + " do";
    } else {
      int idx = target.toInt() - 1;
      if (idx >= 0 && idx < 5) {
        moveSingleServo(idx, angle);
        msg = String(FINGER_NAMES[idx]) + " (GPIO " + String(SERVO_PINS[idx]) + ") -> " + String(angle) + " do";
        Serial.println(msg);
      }
    }

    String json = "{\"status\":\"ok\",\"message\":\"" + msg + "\",\"angles\":[";
    for (int i = 0; i < 5; i++) {
      json += String(currentAngles[i]) + (i < 4 ? "," : "");
    }
    json += "]}";
    server.send(200, "application/json", json);
  });

  server.on("/servo", HTTP_GET, []() {
    sendCorsHeaders();
    const char* keys[5] = {"s1", "s2", "s3", "s4", "s5"};
    for (int i = 0; i < 5; i++) {
      if (server.hasArg(keys[i])) {
        moveSingleServo(i, server.arg(keys[i]).toInt());
        delay(15);
      }
    }
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server.begin();
}

void loop() {
  server.handleClient();
}
