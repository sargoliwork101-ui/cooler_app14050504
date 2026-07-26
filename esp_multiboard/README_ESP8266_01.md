# نسخه Multi-board برای ESP32 و ESP8266-01

فایل اصلی جدید:

```text
Cooler_ESP_API_ESP32_ESP8266.ino
```

این فایل از همان نسخه API-only ساخته شده و منطق اصلی را تغییر نمی‌دهد؛ فقط با `#if defined(ESP32)` و `#if defined(ESP8266)` بخش‌های وابسته به برد جدا شده‌اند.

## نکات مهم برای ESP8266-01

ESP8266-01 محدودیت سخت‌افزاری جدی دارد:

- فقط GPIO0 و GPIO2 در دسترس هستند.
- GPIO0 و GPIO2 هنگام بوت باید HIGH باشند، وگرنه برد درست boot نمی‌شود.
- در این نسخه برای ESP8266 پیش‌فرض رله روی GPIO2 گذاشته شده:

```cpp
const int RELAY_PIN = 2;
const int RELAY_ACTIVE_LEVEL = LOW;
```

یعنی برای بیشتر رله‌های Active-Low، هنگام بوت HIGH می‌ماند و رله خاموش است. اگر ماژول رله شما Active-High است، مقدار را به `HIGH` تغییر دهید؛ اما مراقب boot mode باشید.

## تفاوت‌های وابسته به برد که شرطی شده‌اند

- ESP32:
  - `WiFi.h`
  - `WebServer.h`
  - `esp_task_wdt.h`
  - `mbedtls/aes.h`
  - `WiFi.setTxPower(...)`
  - `LittleFS.begin(true)`

- ESP8266:
  - `ESP8266WiFi.h`
  - `ESP8266WebServer.h`
  - `bearssl/bearssl.h`
  - `ESP.wdtEnable / ESP.wdtFeed`
  - `WiFi.setOutputPower(...)`
  - `LittleFS.begin()` + format fallback

## AES

فرمت ذخیره رمزها همان `ENC:HEX` نگه داشته شده است:

- ESP32 از mbedTLS استفاده می‌کند.
- ESP8266 از API عمومی BearSSL CBC با IV صفر و پردازش تک‌بلوک استفاده می‌کند؛ این روش برای هر بلوک ۱۶ بایتی دقیقاً معادل AES-ECB است و با نسخه‌های Arduino-ESP8266 Core که توابع داخلی `br_aes_big_keysched` را expose نمی‌کنند سازگار است.

## هشدار حافظه

ESP8266-01 نسبت به ESP32 رم و فلش خیلی کمتری دارد. چون UI کاملاً از برد حذف شده و فقط JSON API مانده، شانس اجرا بهتر است؛ ولی بهتر است از ماژول ESP-01 با فلش 1MB یا بیشتر و تنظیمات LittleFS مناسب استفاده شود.

## زمان‌بندی چندوظیفه‌ای روی برد

ESP8266-01 تک‌هسته‌ای است و FreeRTOS/مولتی‌ترد واقعی مثل ESP32 ندارد. برای اینکه همین فایل روی هر دو برد رفتار یکسان و پایدار داشته باشد، `loop()` به یک scheduler سبک و غیرمسدودکننده تبدیل شده است:

- `server.handleClient()` در هر دور اجرا می‌شود تا API کند نشود.
- ساعت داخلی در هر دور به‌روزرسانی می‌شود.
- بررسی سناریو و رله هر ۲۰۰ms اجرا می‌شود.
- کارهای شبکه، NTP، چرخه STA و چرخه AP هر ۱ ثانیه اجرا می‌شوند.
- بررسی RAM، refresh توان WiFi و refresh پین رله هم دوره‌ای شده‌اند.

این روی ESP32 هم اجرا می‌شود و روی ESP8266 هم، بدون اینکه منطق اصلی سناریو/رله تغییر کند.

## روش انتخاب برد

در Arduino IDE:

- برای ESP32: همان برد ESP32 قبلی را انتخاب کنید.
- برای ESP8266-01: برد `Generic ESP8266 Module` یا `Generic ESP8285 Module` متناسب با ماژول خودتان را انتخاب کنید.

تنظیم پیشنهادی ESP8266:

- Flash Size: متناسب با ماژول، ترجیحاً گزینه‌ای که LittleFS دارد.
- CPU Frequency: 80 MHz یا 160 MHz
- Upload Speed: 115200

