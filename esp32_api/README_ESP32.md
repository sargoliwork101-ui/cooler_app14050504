# Firmware اختصاصی ESP32

فایل اصلی:

```text
Cooler_ESP32_WROOM_API.ino
```

این نسخه مخصوص ESP32 است و برای استفاده از قابلیت‌های بومی ESP32 نگه‌داری می‌شود:

- `WiFi.h` و `WebServer.h`
- Watchdog رسمی ESP32 (`esp_task_wdt`)
- AES بومی/mbedTLS برای رمزنگاری رمزهای AP/STA
- توان CPU و RAM بیشتر نسبت به ESP8266
- API JSON بدون HTML/CSS/JS داخلی

## API مشترک با اپ Android

endpointها با نسخه ESP8266 یکسان هستند:

```text
GET  /
GET  /settings
GET  /status
POST /save
POST /sync
POST /toggle-manual
POST /save-ap
POST /save-sta
POST /save-protection
POST /save-ap-cycle
POST /factory-reset
```

## Handshake صحت داده

درخواست‌های POST از اپ به صورت envelope شامل `payload` و `checksum` ارسال می‌شوند. برد قبل از اجرای عملیات checksum را بررسی می‌کند. درخواست‌های قدیمی بدون envelope هم برای سازگاری پذیرفته می‌شوند.
