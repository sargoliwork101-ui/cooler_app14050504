# ESP32 Smart Cooler — REST API + Flutter Android App

این بسته شامل دو خروجی اصلی است:

- `esp32_api/Cooler_ESP32_WROOM_API.ino` — نسخه refactor شده ESP32 بدون HTML/CSS/JS داخلی و فقط با REST API مبتنی بر JSON.
- `android_app_flutter/` — اپ Flutter برای Android که ظاهر پنل قبلی را با کارت‌های تاریک، کولر متحرک، ساعت، اینترنت، سلامت موتور، سناریوها و تنظیمات بازسازی می‌کند.

## ESP32

فایل `Cooler_ESP32_WROOM_API.ino` را در Arduino IDE باز کنید و کتابخانه‌های قبلی پروژه را نصب نگه دارید:

- WiFi
- WebServer
- LittleFS
- ArduinoJson
- ESP32 Arduino Core
- mbedTLS داخلی ESP32

در این نسخه `#include "webpage.h"` حذف شده و هیچ HTML/CSS/JS روی ESP32 تولید نمی‌شود. منطق‌های اصلی دست‌نخورده نگه داشته شده‌اند:

- NTP: `tryNtpSync`, `updateClock`
- اجرای سناریوها: `checkScenarios`
- محافظت ضد روشن/خاموش سریع کمپرسور
- LittleFS و ذخیره‌سازی Round-Robin زمان/آمار
- Watchdog
- رمزنگاری AES رمزها قبل از ذخیره در LittleFS
- مدیریت چرخه AP/STA و قدرت سیگنال

## REST API

همه پاسخ‌ها `application/json` هستند. آدرس پیش‌فرض در حالت AP:

```text
http://192.168.4.1
```

اگر ESP32 به مودم متصل شد، می‌توانید IP حالت Station را در اپ وارد کنید.

### Endpointها

- `GET /` — معرفی API و لیست endpointها
- `GET /status` — وضعیت لحظه‌ای داشبورد؛ اپ هر ۱ ثانیه آن را poll می‌کند
- `GET /settings` — تنظیمات فعلی، رمزهای decrypt شده برای پر کردن فرم‌های اپ، و لیست سناریوها
- `POST /save` — ذخیره سناریوها
- `POST /sync` — همگام‌سازی ساعت با گوشی
- `POST /toggle-manual` — روشن/خاموش دستی
- `POST /save-ap` — ذخیره AP و ری‌استارت برد
- `POST /save-sta` — ذخیره اتصال مودم/اینترنت بدون ری‌استارت
- `POST /save-protection` — ذخیره محافظت کمپرسور
- `POST /save-ap-cycle` — ذخیره چرخه AP و قدرت سیگنال

### نمونه payloadها

```json
POST /sync
{"h":14,"m":30,"s":0,"y":2026,"mon":7,"d":26,"wd":2}
```

```json
POST /save
[
  {"sh":8,"sm":0,"eh":10,"em":30,"en":true,"wd":127}
]
```

```json
POST /save-sta
{"internet":true,"sta_ssid":"Home-WiFi","sta_pass":"12345678","sta_on_minutes":10,"sta_off_minutes":0}
```

```json
POST /save-ap-cycle
{"cycle_enabled":true,"on_minutes":10,"off_minutes":5,"tx_power":3}
```

## Flutter Android App

### اگر هیچ چیزی روی کامپیوتر نصب ندارید

بله، می‌توانید با GitHub Actions فایل APK را داخل خود GitHub بسازید. من workflow آماده را اضافه کرده‌ام:

- اگر کل پوشه `deliverables` را به عنوان repository آپلود کنید: `.github/workflows/build-android.yml` از ریشه repo اجرا می‌شود.
- اگر فقط پوشه `android_app_flutter` را به عنوان repository آپلود کنید: workflow داخل `android_app_flutter/.github/workflows/build-android.yml` اجرا می‌شود.

روش ساده:

1. در GitHub یک repository جدید بسازید.
2. فایل‌ها را آپلود/commit کنید.
3. وارد تب **Actions** شوید.
4. workflow به نام **Build Android APK** را باز کنید.
5. اگر خودکار اجرا نشد، دکمه **Run workflow** را بزنید.
6. بعد از سبز شدن build، پایین صفحه همان run در بخش **Artifacts** فایل `smart-cooler-release-apk` را دانلود کنید.
7. داخل artifact، فایل `app-release.apk` همان برنامه اندروید است.

Workflow خودش Flutter را روی سرور GitHub نصب می‌کند، فایل‌های Android project را با `flutter create --platforms=android .` می‌سازد، اجازه HTTP محلی برای ESP32 را در Manifest اضافه می‌کند و APK release خروجی می‌دهد.

### اگر بعداً خواستید روی سیستم خودتان بسازید

```bash
cd android_app_flutter
flutter create .
flutter pub get
flutter run
```

نکته Android: چون ESP32 با HTTP ساده روی شبکه محلی/AP کار می‌کند، باید cleartext HTTP مجاز باشد. فایل نمونه زیر قرار داده شده است:

```text
android/app/src/main/AndroidManifest.xml
```

اگر `flutter create .` این فایل را بازنویسی کرد، این دو مورد را در Manifest نگه دارید:

```xml
<uses-permission android:name="android.permission.INTERNET" />
<application android:usesCleartextTraffic="true" ...>
```

## نحوه ارتباط اپ با ESP32

اپ در شروع با `http://192.168.4.1` به ESP32 وصل می‌شود. در صفحه تنظیمات می‌توانید Base URL را به IP حالت Station تغییر دهید. اپ:

1. هر ۱ ثانیه `GET /status` را می‌خواند و وضعیت کولر، ساعت، اینترنت، NTP، سلامت موتور، محافظت و چرخه AP/STA را آپدیت می‌کند.
2. هنگام ورود به تنظیمات یا Refresh، `GET /settings` را می‌خواند تا فرم‌ها و سناریوها با وضعیت واقعی برد پر شوند.
3. تمام فرمان‌ها را با `Content-Type: application/json` ارسال می‌کند و انتظار پاسخ JSON استاندارد دارد.

## چک نهایی انجام‌شده

- HTML/CSS/JS و `webpage.h` از ESP32 حذف شد.
- endpointهای قدیمی حفظ شدند و خروجی آن‌ها JSON شد.
- منطق سناریو، NTP، محافظت کمپرسور، LittleFS، Round-Robin، Watchdog، AES و AP/STA باقی ماند.
- UI Flutter با پالت رنگ، خط‌های کم‌رنگ، سایه‌ها و فونت Vazirmatn نزدیک‌تر به پنل اصلی تنظیم شد. پشتیبان‌گیری/بازیابی سناریوها هم از فایل JSON داخل گوشی انجام می‌شود، نه کلیپ‌بورد.
