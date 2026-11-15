# 🖐️ Real-time AI Vision Bionic Hand Controller

Hệ thống điều khiển bàn tay robot mô phỏng 5 ngón tay độc lập theo thời gian thực (Real-time Hand Tracking) sử dụng mô hình AI Vision (MediaPipe + OpenCV) qua Camera laptop kết hợp vi điều khiển **ESP32-S3 (Yolo UNO)** qua giao thức HTTP REST API.

---

## 📌 Tính năng nổi bật (Key Features)
- **Computer Vision (Laptop):** Theo dõi 21 khớp xương bàn tay (Hand Landmarks) với MediaPipe ở độ trễ cực thấp.
- **2 Chế độ điều khiển:**
  - *Binary Mode:* Gập (0°) / Duỗi (180°) dứt khoát theo cử chỉ tay.
  - *Proportional Smoothing:* Nội suy góc cong ngón tay mượt mà (0° - 45° - 90° - 135° - 180°).
- **Embedded Firmware (ESP32-S3):**
  - Xử lý PWM độc lập 5 kênh thông qua bộ định thời phần cứng LEDC (Hardware PWM Timer) với độ phân giải 14-bit, loại bỏ hoàn toàn hiện tượng rung giật hoặc xung đột xung điều khiển.
  - Tích hợp Web Server trực quan với 5 Block điều khiển riêng lẻ và chế độ đồng bộ toàn bộ ngón.
  - Cơ chế gửi lệnh bất đồng bộ (Multi-threading HTTP Client) trên Python giúp khung hình Camera duy trì ổn định 30–60 FPS.

---

## 🛠️ Kiến trúc Hệ thống (System Architecture)
```text
[Laptop Webcam] 
      │ (Raw Frames)
      ▼
[OpenCV + MediaPipe Hands] 
      │ (Tính toán khoảng cách Euclid & nội suy góc 5 ngón)
      ▼
[Python HTTP Client (Threaded)]
      │ (HTTP GET /servo?s1=..&s2=..&s3=..&s4=..&s5=..) via Wi-Fi 2.4GHz
      ▼
[Yolo UNO - ESP32-S3 WebServer]
      │ (Hardware LEDC PWM Channels 0 -> 4)
      ▼
[5x RC Servos] (Ngón cái, Ngón trỏ, Ngón giữa, Ngón áp út, Ngón út)

```
---

## 🔌 Sơ đồ đấu nối chân 5 Servo (Pinout Mapping)

|  STT  | Ngón tay điều khiển  | Tên chân (Yolo UNO)  | ESP32-S3 GPIO | Kênh PWM (LEDC) | Vị trí cắm trên mạch   |
| :---: | :------------------- | :------------------: | :-----------: | :-------------: | :--------------------- |
| **1** | Ngón cái             |         `D2`         |   `GPIO 5`    |     Kênh 0      | Cụm header servo đáy   |
| **2** | Ngón trỏ             |        `D13`         |   `GPIO 48`   |     Kênh 1      | Cụm header servo đáy   |
| **3** | Ngón giữa            |        `D12`         |   `GPIO 47`   |     Kênh 2      | Cụm header servo đáy   |
| **4** | Ngón áp út           |        `D11`         |   `GPIO 38`   |     Kênh 3      | Cụm header servo đáy   |
| **5** | Ngón út              |        `D10`         |   `GPIO 21`   |     Kênh 4      | Hàng chân mở rộng phải |

## 🚀 Hướng dẫn cài đặt & Vận hành

# 1. Nạp Firmware cho Yolo UNO (ESP32-S3)

```text

1. Mở mã nguồn firmware trên **Arduino IDE**.
2. Cập nhật thông tin Wi-Fi phòng/nhà bạn:
   ```cpp
   const char* ssid = "TEN_WIFI_CUA_BAN";
   const char* password = "MAT_KHAU_WIFI";

```

1.Cấu hình nạp trong menu Tools:

    - Board: Yolo UNO (hoặc ESP32S3 Dev Module)
    - USB CDC On Boot: Enabled (bắt buộc để in IP ra Serial)
    - Port: Chọn đúng cổng COM của mạch Yolo UNO.

2.Nhấn nút Upload ($\rightarrow$).

3.Mở Serial Monitor (tốc độ 115200 baud), nhấn nút RST trên mạch một lần để lấy địa chỉ IP cục bộ (dạng: http://192.168.1.xxx).

# 2. Cài đặt môi trường AI Vision trên Laptop
Khởi chạy cửa sổ Terminal tại thư mục D:\bionicarm:

Bash
#1. Tạo môi trường ảo Python

    python -m venv venv

#2. Kích hoạt môi trường ảo

    # Windows (CMD):
        venv\Scripts\activate
    # Windows (PowerShell):
        .\venv\Scripts\Activate.ps1

#3. Cài đặt các thư viện phụ thuộc

    pip install -r requirements.txt

# 3. Vận hành hệ thống theo thời gian thực
#1. Mở file hand_servo_control.py, cập nhật biến IP bằng địa chỉ IP mạch vừa nhận:

        Python
        YOLO_UNO_IP = "192.168.1.xxx"  
        # Điền IP từ Serial Monitor vào đây

#2. Khởi chạy ứng dụng AI Vision:

        Bashpython hand_servo_control.py

#3. Đưa bàn tay lên trước Camera laptop để điều khiển trực tiếp 5 ngón tay servo.

## 🎮 Bảng phím tắt tương tác:

|        Phím        |       Chức năng       | Mô tả hoạt động                                                                      |
| :----------------: | :-------------------- | :----------------------------------------------------------------------------------- |
|      **`M`**       | Đổi chế độ điều khiển | Chuyển đổi qua lại giữa **Nhị phân (0° / 180°)** và **Bám góc mượt (0° - 45° - 90° - 135° - 180°)** |
|      **`0`**       | Gập toàn bộ           | Ép tức thì cả 5 servo về `0°`                                                        |
|      **`9`**       | Xòe toàn bộ           | Ép tức thì cả 5 servo lên `180°`                                                     |
| **`Q`** / **`ESC`**| Thoát                 | Tắt Camera và dừng chương trình                                                      |