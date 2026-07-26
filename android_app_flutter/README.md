# Smart Cooler Flutter App

Flutter Android client for the ESP32 Smart Cooler JSON REST API.

## Build APK on GitHub without installing Flutter

This folder already contains a GitHub Actions workflow:

```text
.github/workflows/build-android.yml
```

Upload this folder as a GitHub repository, then:

1. Open the repository on GitHub.
2. Go to **Actions**.
3. Open **Build Android APK**.
4. Click **Run workflow** if it did not start automatically.
5. When the run is green, download the artifact named `smart-cooler-release-apk`.
6. The APK inside it is `app-release.apk`.

The workflow installs Flutter on GitHub's runner, generates the Android project files, allows cleartext HTTP for the ESP32, and builds a release APK.

Scenario backup/restore uses JSON files selected/saved from the phone storage via Android's file picker; it does not use clipboard.

## Run locally if you install Flutter later

```bash
flutter create .
flutter pub get
flutter run
```

Default ESP32 URL: `http://192.168.4.1`. You can change it from the Settings tab to the ESP32 Station IP.

## Android HTTP requirement

ESP32 serves plain HTTP, so Android must allow cleartext traffic:

```xml
<uses-permission android:name="android.permission.INTERNET" />
<application android:usesCleartextTraffic="true" ...>
```

A sample manifest is included at `android/app/src/main/AndroidManifest.xml`.

## API use

- Polls `GET /status` every second.
- Loads forms/scenarios from `GET /settings`.
- Sends JSON to `/save`, `/sync`, `/toggle-manual`, `/save-ap`, `/save-sta`, `/save-protection`, and `/save-ap-cycle`.
