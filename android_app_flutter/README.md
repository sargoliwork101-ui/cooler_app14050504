# Smart Cooler Android App — WebView Edition

این نسخه عمداً UI را به صورت WebView ساخته است تا ظاهر پنل دقیقاً شبیه وب‌پنل قبلی بماند. HTML/CSS/SVG/JS داخل خود اپ قرار دارد و ESP32 فقط API JSON می‌دهد.

## ساخت APK در GitHub بدون نصب Flutter

این پوشه workflow آماده دارد:

```text
.github/workflows/build-android.yml
```

روش:

1. همین پوشه `android_app_flutter` را به عنوان repository در GitHub آپلود کنید.
2. وارد تب **Actions** شوید.
3. workflow به نام **Build Android APK** را باز کنید.
4. اگر خودکار اجرا نشد، **Run workflow** را بزنید.
5. بعد از سبز شدن build، artifact با نام `smart-cooler-release-apk` را دانلود کنید.
6. فایل داخل آن `app-release.apk` است.

## ساختار

```text
lib/main.dart              WebView و bridge فایل گوشی
assets/web/index.html      UI دقیق پنل قبلی + JS اتصال به REST API
pubspec.yaml               dependencies و assetها
```

## آدرس ESP32

صفحه جدا برای آدرس وجود ندارد. برای تغییر آدرس، روی نشانگر اتصال بالای صفحه بزنید؛ پنجره تنظیم آدرس باز می‌شود. پیش‌فرض:

```text
http://192.168.4.1
```

## پشتیبان‌گیری و بازیابی سناریو

- دکمه «پشتیبان‌گیری سناریوها» فایل JSON را در گوشی ذخیره می‌کند.
- دکمه «بازیابی از فایل» فایل JSON را از حافظه گوشی انتخاب می‌کند.
- کلیپ‌بورد استفاده نمی‌شود.

## Android HTTP

چون ESP32 روی HTTP محلی کار می‌کند، workflow به صورت خودکار این‌ها را در Android Manifest اعمال می‌کند:

```xml
<uses-permission android:name="android.permission.INTERNET" />
<application android:usesCleartextTraffic="true" ...>
```
