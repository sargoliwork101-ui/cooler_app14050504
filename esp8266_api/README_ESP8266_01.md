# Firmware اختصاصی ESP8266 / ESP-01

فایل اصلی:

```text
esp8266_api.ino
```

این نسخه مخصوص ESP8266/ESP-01 است و از کتابخانه‌های بومی ESP8266 استفاده می‌کند:

- `ESP8266WiFi.h`
- `ESP8266WebServer.h`
- `LittleFS`
- Watchdog داخلی ESP8266
- AES سازگار BearSSL برای نگه داشتن فرمت `ENC:HEX`
- API JSON بدون HTML/CSS/JS داخلی

## پایه رله روی ESP-01

ESP8266-01 فقط GPIO0 و GPIO2 را واقعاً در دسترس دارد. پیش‌فرض firmware:

```cpp
const int RELAY_PIN = 2;
const int RELAY_ACTIVE_LEVEL = LOW;
```

این تنظیم برای بیشتر رله‌های Active-Low مناسب‌تر است، چون GPIO2 هنگام boot باید HIGH بماند. اگر سخت‌افزار شما Active-High است، مقدار `RELAY_ACTIVE_LEVEL` را با احتیاط تغییر دهید.

## محدودیت ESP8266-01

ESP8266-01 نسبت به ESP32 RAM/Flash کمتر و فقط یک هسته دارد. به همین دلیل این نسخه با scheduler سبک غیرمسدودکننده کار می‌کند:

- `server.handleClient()` در هر loop
- ساعت داخلی در هر loop
- بررسی سناریو/رله هر ۲۰۰ms
- کارهای WiFi/NTP/AP هر ۱ ثانیه
- بررسی RAM و refreshهای ایمنی به صورت دوره‌ای

## API مشترک با اپ Android

endpointها با نسخه ESP32 یکسان هستند:

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

## ریست کامل حافظه برد

endpoint:

```text
POST /factory-reset
```

payload شامل کلید `code` است. کد امنیتی این عملیات در firmware ثابت است و در رابط کاربری/مستندات کاربر نمایش داده نمی‌شود. با اجرای این endpoint، `LittleFS.format()` انجام می‌شود و همه سناریوها، زمان‌بندی‌ها، تنظیمات WiFi، ساعت ذخیره‌شده، آمار موتور، تنظیمات محافظت و اطلاعات NTP پاک می‌شوند.
