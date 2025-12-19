# Smart Plant Watering – ESP32 + Blynk (Stable Heartbeat)

## Mô tả
Hệ thống tưới cây tự động sử dụng ESP32 và Blynk IoT, được thiết kế để hoạt động ổn định trong thời gian dài:
- Tưới tự động theo độ ẩm đất (phù hợp sen đá)
- Điều khiển bơm thủ công qua Blynk (Manual mode) có timeout tự tắt
- Bảo vệ cạn nước và lỗi cảm biến
- Chống nhiễu DHT khi bơm đang chạy
- Tối ưu heartbeat, hạn chế mất kết nối khi WiFi yếu

## Phần cứng
- ESP32 Dev Module  
- DHT22 (nhiệt độ, độ ẩm)  
- Cảm biến độ ẩm đất (Analog)  
- Cảm biến mực nước (Analog)  
- Relay điều khiển bơm (Active HIGH)

## Pinout
- DHT22: GPIO 32  
- Relay / Pump: GPIO 33 (Active HIGH)  
- Soil moisture (ADC): GPIO 34  
- Water level (ADC): GPIO 35  

## Blynk Virtual Pins
- V0: Humidity  
- V1: Temperature  
- V2: Soil moisture (%)  
- V3: Pump button (Manual)  
- V4: Water level (%)  
- V5: Mode (0 = AUTO, 1 = MANUAL)  
- V10: Terminal log  

## Nguyên lý hoạt động
### AUTO mode
- Soil ≤ 20% → Bật bơm  
- Soil ≥ 40% → Tắt bơm  

### MANUAL mode
- Bật bơm bằng nút trên Blynk  
- Tự động tắt sau 8 giây để tránh quên  

### Bảo vệ
- Mực nước < 10% → Ngắt bơm + cảnh báo  
- DHT lỗi liên tiếp → Ngắt bơm + cảnh báo  
- DHT chỉ đọc mỗi 10 giây để giảm block và tránh heartbeat timeout  

## Cách sử dụng
1. Mở file code `.ino`
2. Điền thông tin WiFi và Blynk token:
```cpp
const char* WIFI_SSID  = "YOUR_WIFI_SSID";
const char* WIFI_PASS  = "YOUR_WIFI_PASS";
const char* BLYNK_AUTH = "YOUR_BLYNK_AUTH_TOKEN";


---

## 📌 PHẦN 4 – TỐI ƯU + GIỚI HẠN + THÔNG TIN ĐỒ ÁN
```md
## Tối ưu & độ ổn định
- Sử dụng `Blynk.begin()` kết hợp `BlynkTimer` để tránh block loop
- Tăng `BLYNK_HEARTBEAT` và `BLYNK_TIMEOUT_MS` để chống mất kết nối khi WiFi yếu
- Giảm tần suất đọc DHT xuống 10 giây/lần nhằm hạn chế heartbeat timeout
- Tạm ngắt relay khi đọc DHT để giảm nhiễu do bơm gây ra

## Hiệu chỉnh cảm biến
- Độ ẩm đất được chuẩn hóa từ giá trị ADC thô sang phần trăm (%)
- Các mốc `SOIL_RAW_DRY` và `SOIL_RAW_WET` có thể cần tinh chỉnh lại theo từng loại cảm biến
- Cảm biến mực nước được ánh xạ từ ADC sang % để dễ theo dõi trên Blynk

## Nhật ký & giám sát
- Log trạng thái hệ thống theo chu kỳ (WiFi, Blynk, Soil, Water, DHT, Pump, Mode)
- Hiển thị log trên Serial Monitor và Blynk Terminal (V10)
- Gửi cảnh báo Blynk Event khi xảy ra lỗi nghiêm trọng

## Giới hạn & hướng phát triển
- Chưa lưu cấu hình vào flash (EEPROM/NVS)
- Chưa hỗ trợ nhiều vùng tưới
- Có thể mở rộng: thêm cảm biến mưa/ánh sáng, lưu lịch sử tưới, nhiều relay

## Thông tin đồ án
- Môn học: **Thiết kế Hệ thống Nhúng (CE224)**
- Ngành: **Computer Engineering – UIT**
- Nền tảng: **ESP32 + Blynk IoT**


