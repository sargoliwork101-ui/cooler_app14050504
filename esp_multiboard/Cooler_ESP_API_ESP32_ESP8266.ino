// ============================================================
//  Smart Cooler REST API — Multi-board edition
//  پشتیبانی همزمان با ESP32 و ESP8266/ESP-01 با کمترین تغییر در منطق اصلی.
// ============================================================
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>

#if defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
  #include <esp_task_wdt.h>
  #include "mbedtls/aes.h"
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #include <bearssl/bearssl.h>
  typedef ESP8266WebServer WebServer;
#else
  #error "This firmware supports only ESP32 or ESP8266."
#endif

// ============================================================
//  بخش تنظیمات و ثابت‌ها
//  تمام مقادیر قابل تغییر در اینجا جمع شده‌اند.
//  فقط همین مقادیر را تغییر دهید؛ نیازی به دست زدن به بقیه کد نیست.
// ============================================================

// نام برنامه — در تب مرورگر و نوار بالای پنل نمایش داده می‌شود.
// اگر خواستید نام دستگاه را عوض کنید، فقط این مقدار را تغییر دهید.
const char* PROGRAM_NAME    = "کولر هوشمند ESP32";

// زیرنویس/برند کوچک زیر نام برنامه در نوار بالا (مثلاً مدل برد).
const char* PROGRAM_TAGLINE = "ESP32 · TIMER HUB";

// پایه (پین) خروجی متصل به رله روی برد ESP32-WROOM.
// GPIO23 یک پایه امن برای خروجی است. اگر رله روی پایه دیگری وصل است، فقط این عدد را تغییر دهید.
// نکته: GPIO3 (پایه RX0 سریال) برای رله پیشنهاد نمی‌شود.
#if defined(ESP8266)
// ESP8266-01 فقط GPIO0 و GPIO2 را واقعاً در دسترس دارد. GPIO2 برای رله معمولاً امن‌تر است،
// اما باید هنگام بوت HIGH بماند. برای اغلب رله‌های Active-Low، LOW یعنی روشن و HIGH زمان بوت یعنی خاموش.
// اگر سخت‌افزار شما Active-High است، RELAY_ACTIVE_LEVEL را به HIGH تغییر دهید.
const int  RELAY_PIN = 2;
const int  RELAY_ACTIVE_LEVEL = LOW;
#else
const int  RELAY_PIN = 23;
const int  RELAY_ACTIVE_LEVEL = HIGH;
#endif

// اختلاف زمان محلی با UTC بر حسب ثانیه. مقدار فعلی UTC+03:30 (ایران) است.
// برای مناطق دیگر فقط این عدد را تغییر دهید (مثلاً UTC+03:30 = 12600).
const long NTP_GMT_OFFSET_SEC = 12600;

// کلید ۱۶ بایتی (۱۲۸ بیتی) برای رمزنگاری و رمزگشایی متقارن رمزهای عبور
// برای تغییر و شخصی‌سازی امنیت سیستم خود، می‌توانید مقادیر این آرایه هگز را به مقادیر تصادفی دیگر تغییر دهید.
const unsigned char AES_KEY[16] = {
  0x4F, 0xA1, 0xC3, 0x92, 0x0E, 0x77, 0xB5, 0x2D,
  0x8C, 0x1A, 0x6F, 0xE4, 0x33, 0xD9, 0x5B, 0x88
};

// نام و رمز شبکه Access Point خود دستگاه (برای اتصال مستقیم گوشی به برد).
// رمز AP طبق قوانین ESP32 باید حداقل ۸ کاراکتر باشد.
char custom_ssid[32]     = "ESP32_Timer_Hub";
char custom_password[32] = "12345678";

// تنظیمات اتصال به مودم/روتر اینترنت (اختیاری).
// اگر خالی بماند، برد فقط Access Point خودش را نگه می‌دارد.
// رمز مودم اختیاری است ولی اگر وارد شود بهتر است حداقل ۸ کاراکتر باشد.
char sta_ssid[32]     = "";
char sta_password[64] = "";

// وضعیت فعال بودن استفاده از اینترنت/NTP روی اتصال STA.
// وقتی false باشد، برد اصلاً به مودم وصل نمی‌شود و از اینترنت/NTP استفاده نمی‌کند.
bool internet_enabled = true;

// چرخه‌ی زمانی اتصال مودم اینترنت (STA).
// اگر مدت خاموش بودن ۰ باشد یعنی اتصال STA همیشه روشن می‌ماند و هیچ چرخه‌ای اعمال نمی‌شود.
int  staOnMinutes  = 10;
int  staOffMinutes = 0;
const int MIN_STA_ON_MINUTES = 1;
const int MIN_STA_OFF_MINUTES = 0;
const int MAX_STA_CYCLE_MINUTES = 1440;
bool staCurrentlyOn = true;
unsigned long staCycleLastToggleMillis = 0;

// ============ چرخه‌ی روشن/خاموش‌سازی دوره‌ای AP (فرستنده وای‌فای خود برد) ============
// هدف: هم پایین آمدن دمای برد (چون رادیوی وای‌فای یکی از منابع اصلی گرمای برد است) و هم کاهش
// تابش دائمی امواج نزدیک انسان. وقتی فعال باشد، AP به‌صورت دوره‌ای روشن و خاموش می‌شود؛
// در بازه‌ی «خاموش»، طبیعتاً گوشی موقتاً به پنل دسترسی ندارد (چون خودِ شبکه قطع است) — این رفتار
// عمدی و بخشی از هدف این قابلیت است. زمان‌بندی سناریوها و رله در تمام این مدت بدون وقفه ادامه دارد
// چون به AP هیچ وابستگی‌ای ندارد.
bool apCycleEnabled = false;          // پیش‌فرض خاموش تا رفتار فعلی کاربرهای قبلی تغییر نکند
int  apOnMinutes  = 10;               // مدت روشن بودن AP در هر دوره (دقیقه)
int  apOffMinutes = 5;                // مدت خاموش بودن AP در هر دوره (دقیقه)
const int MIN_AP_CYCLE_MINUTES = 1;
const int MAX_AP_CYCLE_MINUTES = 1440;
bool apCurrentlyOn = true;            // وضعیت واقعی فعلی AP (برای مدیریت چرخه)
unsigned long apCycleLastToggleMillis = 0;

// سطح قدرت سیگنال‌دهی AP/رادیوی وای‌فای برد (روی کل رادیو اعمال می‌شود، چون ESP32 یک تنظیم توان واحد دارد):
// 0=کم، 1=متوسط، 2=زیاد، 3=حداکثر (مقدار پیش‌فرض سازنده و رفتار قبلی برنامه)
int apTxPowerLevel = 3;
const int MAX_AP_TX_POWER_LEVEL = 3;

// حداکثر تعداد سناریوهای قابل تعریف در پنل.
const int MAX_SCENARIOS = 20;

// حداقل زمان خاموش‌بودن کمپرسور پیش از روشن‌شدن مجدد (دقیقه). صفر = غیرفعال.
int antiShortCycleMinutes = 3;
const int MAX_ANTI_SHORT_CYCLE_MINUTES = 1440;
unsigned long lastRelayOffMillis = 0;

// ظرفیت بافر JSON سناریوها (با حاشیه اطمینان محاسبه شده تا هرگز خطای حافظه ندهد).
const size_t SCENARIOS_JSON_CAPACITY = 6144;
// ============================================================================
// STORAGE / MIGRATION VERSION BANNER — CURRENT REVISION: 6
// دستور مهم برای هر توسعه‌دهنده یا هوش مصنوعی آینده:
// اگر ساختار یا معنای دادهٔ هر فایل ذخیره‌شده را تغییر می‌دهی، شمارهٔ نسخهٔ همان
// فایل را افزایش بده، CURRENT REVISION بالا را هم یک واحد زیاد کن، و مسیر خواندن
// نسخهٔ قبلی (migration) را حفظ کن. هرگز دادهٔ قدیمی کاربر را بی‌هشدار حذف نکن.
// ============================================================================
const int STORAGE_FORMAT_REVISION = 6;
const int SCENARIOS_FILE_VERSION = 2;
const int WIFI_FILE_VERSION = 5; // نسخه 5: حذف کلید جداگانهٔ STA و افزودن زمان‌بندی روشن/خاموش اتصال مودم با staOnMinutes/staOffMinutes
const int TIME_FILE_VERSION = 2;
const int RELAY_STAT_FILE_VERSION = 2;
const int OVERRIDE_FILE_VERSION = 2;
const int PROTECTION_FILE_VERSION = 1;
const int NTP_META_FILE_VERSION = 1;

// زمان‌سنج (Watchdog) اختصاصی ESP32 به ثانیه — در صورت هنگ کردن برد، خودش ری‌استارت می‌شود.
const int WDT_TIMEOUT_SEC = 8;

// فاصله ذخیره پشتیبان دوره‌ای ساعت روی حافظه (برای مقاومت در برابر قطع برق).
// مقدار فعلی هر ۵ دقیقه یک‌بار است.
const unsigned long TIME_SAVE_INTERVAL = 5UL * 60UL * 1000UL;

// فاصله تلاش برای گرفتن ساعت از اینترنت (NTP) تا وقتی که در همین روشن بودن دستگاه هنوز موفق نشده.
// همیشه بلافاصله بعد از هر روشن شدن/ریست برد یک تلاش فوری هم انجام می‌شود (فارغ از این فاصله)،
// و اگر آن تلاش موفق نبود، از همان لحظه هر چند وقت یک‌بار که اینجا مشخص شده دوباره تلاش می‌کند.
// مقدار فعلی هر ۱ دقیقه یک‌بار است. برای تغییر، فقط همین عدد را عوض کنید.
const unsigned long NTP_RETRY_INTERVAL_MS = 1UL * 60UL * 1000UL;

// فاصله بررسی مجدد ساعت از اینترنت بعد از اینکه یک‌بار در همین روشن بودن دستگاه با موفقیت سنکرون شد.
// چون دیگر ساعت درست است، نیازی به چک مکرر نیست؛ این فقط برای اطمینان از عدم انحراف ساعت داخلی برد
// در بلندمدت است. مقدار فعلی هر ۱ ساعت یک‌بار است. برای تغییر، فقط همین عدد را عوض کنید.
const unsigned long NTP_RECHECK_INTERVAL_MS = 1UL * 60UL * 60UL * 1000UL;

WebServer server(80);

// کد ثابت ریست کامل حافظه برد؛ در UI به کاربر نمایش داده نمی‌شود.
const char* FACTORY_RESET_CODE = "123456";
bool pendingFactoryReset = false;

void feedWatchdog();

// ESP32 در این نسخه هیچ HTML/CSS/JS تولید نمی‌کند؛ فقط API JSON ارائه می‌شود.

// ساختار هر سناریو
struct Scenario {
  bool active = false;   // وجود داشتن سناریو در لیست؛ حذف کردن این مقدار را false می‌کند
  bool enabled = false;  // فعال/غیرفعال بودن اجرای سناریو؛ با حذف کردن فرق دارد
  int startHour = 0;
  int startMinute = 0;
  int endHour = 0;
  int endMinute = 0;
  // بیت ۰=شنبه ... بیت ۶=جمعه؛ 127 یعنی هر روز هفته
  uint8_t weekdays = 0x7F;
};

Scenario scenarios[MAX_SCENARIOS];

// متغیرهای زمان داخلی برد
int currentHour = 0;
int currentMinute = 0;
int currentSecond = 0;
// تاریخ میلادی و روز هفته: ۰=شنبه ... ۶=جمعه
int currentYear = 2026, currentMonth = 1, currentDay = 1;
int currentWeekday = 3;
unsigned long lastTick = 0;

// وضعیت کنترل دستی رله (0 = خودکار/بر اساس سناریو، 1 = روشن دستی)
int manual_override = 0; 

// پرچم همگام‌سازی ساعت با گوشی
bool time_synchronized = false;

// --- وضعیت واقعی دریافت ساعت از اینترنت (NTP) در همین نوبت روشن بودن دستگاه ---
// نکته مهم: time_synchronized از روی فایل ذخیره‌شده (حتی سنکرون قدیمی با گوشی) در همان لحظه بوت لود می‌شود
// و همیشه true می‌ماند. اگر از همان متغیر برای فاصله‌ی تلاش مجدد NTP استفاده شود، در صورتی که ساعت قبلاً
// (چه با گوشی و چه از بوت قبلی) سنکرون بوده، برد فکر می‌کند NTP هم قبلاً موفق بوده و به‌جای هر ۱ دقیقه،
// هر ۱ ساعت یک‌بار تلاش می‌کند؛ در نتیجه با اینکه به مودم وصل است، ساعت واقعی اینترنت ممکن است تا یک ساعت
// دریافت نشود و کاربر فکر می‌کند "وصل شد ولی ساعت نگرفت". برای همین یک پرچم جداگانه فقط برای همین بوت داریم.
bool ntp_synced_this_boot = false;

// آیا تا به حال حداقل یک‌بار ساعت با موفقیت از اینترنت گرفته شده (برای نمایش در پنل)
bool ntpEverSucceeded = false;
// آخرین زمانِ مطلقِ دریافت موفق ساعت از اینترنت که باید حتی بعد از ریست هم باقی بماند.
bool ntpLastSuccessValid = false;
int ntpLastSuccessYear = 2026, ntpLastSuccessMonth = 1, ntpLastSuccessDay = 1;
int ntpLastSuccessHour = 0, ntpLastSuccessMinute = 0, ntpLastSuccessSecond = 0;
const char* NTP_META_FILE = "/ntp.json";

// زمان‌بندی داخلی تلاش NTP. این‌ها عمداً global هستند تا هنگام تغییر تنظیمات مودم/اینترنت
// بتوانیم تلاش بعدی را فوراً از نو برنامه‌ریزی کنیم.
unsigned long lastNtpCheckMillis = 0;
bool ntpFirstCheckPending = true;

// --- وضعیت دقیق اتصال STA (مودم اینترنت) برای نمایش سه‌حالته در پنل (نارنجی/سبز/قرمز) ---
// آیا از زمان روشن شدن دستگاه، حداقل یک‌بار به مودم اینترنت وصل شده؛ برای تشخیص «هنوز هیچ‌وقت وصل نشده»
// (حالت در حال اتصال، نارنجی) از «قبلاً وصل بوده و الان قطع شده» (حالت قطعی، قرمز).
bool staEverConnectedThisBoot = false;

// متغیرهای مدیریت ریستارت غیر بلاک کننده
unsigned long resetMillis = 0;
bool pendingReset = false;

// Rate limiting endpointهای وب: جلوگیری از اسپم لمس/درخواست و استهلاک فلش یا رله.
unsigned long lastToggleManualRequest = 0;
unsigned long lastSaveScenarioRequest = 0;
unsigned long lastSyncRequest = 0;
unsigned long lastSaveApRequest = 0;
unsigned long lastSaveStaRequest = 0;
unsigned long lastSaveProtectionRequest = 0;
unsigned long lastSaveApCycleRequest = 0;
unsigned long lastFactoryResetRequest = 0;
unsigned long lastStatusRequest = 0;

// ============ مدیریت ذخیره امن زمان (برای مقاومت در برابر نوسان برق) ============
// به‌جای یک فایل، از دو فایل به صورت چرخشی (Round-Robin) استفاده می‌شود.
// اگر برق درست وسط نوشتن یکی از فایل‌ها قطع شود، فایل دیگر همچنان سالم و قابل استفاده باقی می‌ماند.
const char* TIME_FILES[2] = {"/time0.txt", "/time1.txt"};
int timeFileSlot = 0;                 // فایلی که نوبت نوشتن در آن است
unsigned long timeSaveSeq = 0;        // شماره‌ی افزایشی برای تشخیص جدیدترین رکورد هنگام خواندن
unsigned long lastTimeSaveMillis = 0; // زمان آخرین ذخیره‌سازی دوره‌ای
// =================================================================================

// ============ آمار سوییچ و مدت‌کارکرد رله (برای پایش سلامت/عمر کمپرسور) ============
// همان تکنیک دو-فایلی چرخشی بالا برای مقاومت در برابر قطعی برق اینجا هم استفاده می‌شود.
unsigned long relaySwitchCount = 0;       // تعداد دفعاتی که کمپرسور واقعاً روشن شده (هر بار خاموش→روشن یک واحد)
unsigned long relayTotalOnSeconds = 0;    // مجموع ثانیه‌های روشن بودن که تا کنون به صورت قطعی ذخیره شده
unsigned long relayOnSinceMillis = 0;     // لحظه (millis) شروع دوره‌ی روشن بودن جاری؛ فقط وقتی relayCurrentlyOnForStats=true معتبر است
bool relayCurrentlyOnForStats = false;    // آیا هم‌اکنون یک دوره‌ی «روشن» در حال اندازه‌گیری است
int lastRelayStatState = -1;              // آخرین وضعیت منطقی رله که برای آمار ثبت شده (جدا از منطق اصلی سناریو/دستی)
const char* RELAY_STAT_FILES[2] = {"/relaystat0.txt", "/relaystat1.txt"};
int relayStatFileSlot = 0;
unsigned long relayStatSaveSeq = 0;
unsigned long lastRelayStatSaveMillis = 0; // زمان آخرین ذخیره‌سازی دوره‌ای پیشرفت مدت‌کارکرد جاری

// سقف‌های sanity برای جلوگیری از نمایش/ذخیره عددهای پرت در آمار موتور.
// اگر فایل آمار هنگام قطع برق/خرابی فلش مقدار غیرواقعی داشته باشد، هنگام خواندن رد می‌شود.
// همچنین اگر به هر دلیل زمان شروع دوره روشن بودن خراب شود، یک جهش بزرگ به مجموع کارکرد اضافه نمی‌شود.
const unsigned long MAX_RELAY_SWITCH_COUNT = 500000UL;
const unsigned long MAX_RELAY_ON_SECONDS   = 10UL * 365UL * 24UL * 60UL * 60UL; // ۱۰ سال کارکرد واقعی
const unsigned long MAX_RELAY_OPEN_PERIOD_SECONDS = (TIME_SAVE_INTERVAL / 1000UL) * 2UL + 120UL;
// =================================================================================

// تعریف توابع سیستم
void loadScenarios();
void saveScenarios();
void loadWiFiSettings();
void saveWiFiSettings();
void loadOverrideSetting();
void loadProtectionSettings();
void saveProtectionSettings();
void saveOverrideSetting();
void saveTimeSetting();
void loadTimeSetting();
bool readTimeFile(const char* path, int &h, int &m, int &s, int &sync, unsigned long &seq, int &y, int &mon, int &d, int &wd);
void advanceDate();
bool isLeapYear(int year);
void saveRelayStats();
void loadRelayStats();
bool readRelayStatFile(const char* path, unsigned long &sc, unsigned long &tos, unsigned long &seq);
unsigned long relayCurrentPeriodElapsedSeconds();
void addRelayOnSecondsSafely(unsigned long secondsToAdd);
void loadNtpSuccessInfo();
void saveNtpSuccessInfo();
void handleApiRoot();
void handleGetSettings();
void handleSaveScenario();
void handleSyncTime();
void handleGetStatus();
void handleToggleManual();
void handleSaveAP();
void handleSaveSTA();
void handleSaveProtection();
void handleSaveApCycle();
void handleFactoryReset();
bool allowRequest(unsigned long &lastRequest, unsigned long minIntervalMs);
void sendJsonMessage(int code, const char* status, const char* message);
bool parseJsonBody(JsonDocument &doc);
String fnv1aChecksum(const String &data);
JsonVariantConst verifiedPayload(JsonDocument &doc, bool &ok);
void checkScenarios();
void updateClock();
void setRelay(bool state);
void applyApTxPower();
void setApRadioState(bool on);
void manageApCycle();
void setStaConnectionState(bool on);
void manageStaCycle();
void connectToInternetWiFi();
void tryNtpSync();
String encryptPassword(const char* password, size_t bufferSize);
void loadAndDecryptPassword(const char* savedValue, char* outputBuffer, size_t bufferSize);

// بررسی با تفاضل unsigned long برای سازگاری با سرریز millis().
// پاسخ 429 به مرورگر برمی‌گردد و هیچ تغییری در رله یا فایل‌ها انجام نمی‌شود.
bool allowRequest(unsigned long &lastRequest, unsigned long minIntervalMs) {
  unsigned long now = millis();
  if (lastRequest != 0 && (unsigned long)(now - lastRequest) < minIntervalMs) {
    sendJsonMessage(429, "error", "Too Many Requests");
    return false;
  }
  lastRequest = now;
  return true;
}

// ================== Watchdog مخصوص ESP32 ==================
// در نسخه ESP8266 از ESP.wdtEnable/ESP.wdtFeed استفاده می‌شد؛
// در ESP32 باید از Task Watchdog رسمی خود ESP-IDF استفاده کنیم.

void setupWatchdog() {
#if defined(ESP32)
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_task_wdt_config_t twdt_config = {
      .timeout_ms = WDT_TIMEOUT_SEC * 1000,
      .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
      .trigger_panic = true
    };
    esp_task_wdt_init(&twdt_config);
  #else
    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
  #endif
  esp_task_wdt_add(NULL);
#elif defined(ESP8266)
  ESP.wdtEnable(WDT_TIMEOUT_SEC * 1000);
#endif
}

void feedWatchdog() {
#if defined(ESP32)
  esp_task_wdt_reset();
#elif defined(ESP8266)
  ESP.wdtFeed();
#endif
}
// ============================================================


// ارسال پاسخ استاندارد JSON برای همه endpointها
void sendJsonMessage(int code, const char* status, const char* message) {
  StaticJsonDocument<192> doc;
  doc["status"] = status;
  if (message && strlen(message) > 0) doc["message"] = message;

  String out;
  serializeJson(doc, out);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(code, "application/json", out);
}

// خواندن بدنه JSON. همه درخواست‌های تغییردهنده باید application/json و body معتبر داشته باشند.
bool parseJsonBody(JsonDocument &doc) {
  if (!server.hasArg("plain")) {
    sendJsonMessage(400, "error", "Missing JSON body");
    return false;
  }
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    sendJsonMessage(400, "error", "Invalid JSON");
    return false;
  }
  return true;
}


// checksum سبک برای handshake صحت داده بین اپ و برد.
// اپ payload را JSON.stringify می‌کند، checksum می‌سازد و به شکل {payload, checksum} می‌فرستد.
// برد قبل از اعمال عملیات، payload را دوباره serialize و checksum را مقایسه می‌کند.
String fnv1aChecksum(const String &data) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < data.length(); i++) {
    hash ^= (uint8_t)data[i];
    hash *= 16777619UL;
  }
  char out[9];
  snprintf(out, sizeof(out), "%08lx", (unsigned long)hash);
  return String(out);
}

JsonVariantConst verifiedPayload(JsonDocument &doc, bool &ok) {
  ok = true;
  if (doc.is<JsonObjectConst>()) {
    JsonObjectConst obj = doc.as<JsonObjectConst>();
    if (obj.containsKey("payload") && obj.containsKey("checksum")) {
      String normalized;
      serializeJson(obj["payload"], normalized);
      String expected = fnv1aChecksum(normalized);
      String received = String((const char*)(obj["checksum"] | ""));
      if (!expected.equalsIgnoreCase(received)) {
        sendJsonMessage(400, "error", "Checksum mismatch");
        ok = false;
        return JsonVariantConst();
      }
      return obj["payload"];
    }
  }
  // سازگاری با درخواست‌های قدیمی بدون handshake
  return doc.as<JsonVariantConst>();
}

// تابع کمکی برای سوئیچ کردن رله بر اساس منطق (Active-Low یا Active-High)
void setRelay(bool state) {
  if (state) {
    digitalWrite(RELAY_PIN, RELAY_ACTIVE_LEVEL);
  } else {
    digitalWrite(RELAY_PIN, !RELAY_ACTIVE_LEVEL);
  }
}

// ============================================================
//  چرخه‌ی روشن/خاموش دوره‌ای AP + کنترل قدرت سیگنال رادیو وای‌فای
// ============================================================

// اعمال سطح قدرت سیگنال انتخاب‌شده روی رادیوی وای‌فای برد.
// نکته مهم: در ESP32 این تنظیم روی کل رادیو اعمال می‌شود (هم AP هم STA)، چون فقط یک رادیوی
// فیزیکی مشترک وجود دارد؛ تفکیک جداگانه‌ی توان برای AP و STA در سخت‌افزار ESP32 ممکن نیست.
void applyApTxPower() {
#if defined(ESP32)
  wifi_power_t p;
  switch (apTxPowerLevel) {
    case 0:  p = WIFI_POWER_5dBm;    break;
    case 1:  p = WIFI_POWER_11dBm;   break;
    case 2:  p = WIFI_POWER_15dBm;   break;
    default: p = WIFI_POWER_19_5dBm; break;
  }
  WiFi.setTxPower(p);
  Serial.print("TX Power set to level "); Serial.print(apTxPowerLevel);
  Serial.print(" -> requested="); Serial.print((int)p);
  Serial.print(" actual="); Serial.println((int)WiFi.getTxPower());
#elif defined(ESP8266)
  float dbm;
  switch (apTxPowerLevel) {
    case 0:  dbm = 5.0f;  break;
    case 1:  dbm = 11.0f; break;
    case 2:  dbm = 15.0f; break;
    default: dbm = 20.5f; break;
  }
  WiFi.setOutputPower(dbm);
  Serial.print("TX Power set to level "); Serial.print(apTxPowerLevel);
  Serial.print(" -> "); Serial.print(dbm); Serial.println(" dBm");
#endif
}

// روشن/خاموش کردن واقعی رادیوی AP. خاموش کردن با پارامتر true به softAPdisconnect باعث می‌شود
// AP کاملاً از حالت پخش خارج شود (نه فقط قطع کلاینت‌ها)، و دوباره فراخوانی softAP آن را برمی‌گرداند.
// اتصال STA (اینترنت) و تمام منطق رله/سناریو/ساعت کاملاً مستقل از این وضعیت باقی می‌ماند.
void setApRadioState(bool on) {
  if (on == apCurrentlyOn) return;
  if (on) {
    WiFi.softAP(custom_ssid, custom_password, 1, 0, 3);
    applyApTxPower();
  } else {
    WiFi.softAPdisconnect(true);
  }
  apCurrentlyOn = on;
}

// مدیریت چرخه‌ی دوره‌ای AP در loop(). اگر چرخه غیرفعال باشد، فقط مطمئن می‌شویم AP روشن بماند
// (حالت عادی/فعلی برنامه). اگر فعال باشد، بین بازه‌ی روشن و خاموش بر اساس تنظیمات کاربر جابه‌جا می‌شود.
// نکته مهم: تا وقتی حداقل یک گوشی/دستگاه به AP وصل است، تایمر چرخه اصلاً پیش نمی‌رود و AP خاموش
// نمی‌شود؛ فقط بعد از قطع شدن آخرین دستگاه، شمارش مدت «روشن» از نو شروع می‌شود.
void manageApCycle() {
  if (!apCycleEnabled) {
    if (!apCurrentlyOn) {
      setApRadioState(true);
      apCycleLastToggleMillis = millis();
    }
    return;
  }

  // اگر همین الان حداقل یک کلاینت (گوشی/دستگاه) به AP وصل است، چرخه کاملاً متوقف می‌ماند:
  // AP روشن نگه داشته می‌شود و لحظه‌ی شروع شمارش هر بار به «همین الان» به‌روزرسانی می‌شود تا
  // به محض قطع شدن آخرین دستگاه، بازه‌ی «روشن» با شمارش کامل از نو آغاز شود.
  if (apCurrentlyOn && WiFi.softAPgetStationNum() > 0) {
    apCycleLastToggleMillis = millis();
    return;
  }

  unsigned long onMs  = (unsigned long)apOnMinutes  * 60000UL;
  unsigned long offMs = (unsigned long)apOffMinutes * 60000UL;
  unsigned long elapsed = millis() - apCycleLastToggleMillis;

  if (apCurrentlyOn && elapsed >= onMs) {
    setApRadioState(false);
    apCycleLastToggleMillis = millis();
  } else if (!apCurrentlyOn && elapsed >= offMs) {
    setApRadioState(true);
    apCycleLastToggleMillis = millis();
  }
}

// ============================================================
//  توابع رمزنگاری سخت‌افزاری (AES) مختص ESP32
// ============================================================

// رمزنگاری/رمزگشایی یک بلوک ۱۶ بایتی AES-128 به صورت ECB.
// ESP32: mbedTLS. ESP8266: BearSSL. خروجی هر دو یکسان است تا فرمت ENC:HEX حفظ شود.
void aesEncryptBlock16(const unsigned char* input, unsigned char* output) {
#if defined(ESP32)
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, AES_KEY, 128);
  mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, input, output);
  mbedtls_aes_free(&aes);
#elif defined(ESP8266)
  // در ESP8266 بعضی نسخه‌های BearSSL توابع داخلی br_aes_big_keysched/br_aes_big_encrypt
  // را public نمی‌کنند. برای سازگاری با Arduino-ESP8266 Core از API عمومی CBC استفاده می‌کنیم.
  // چون هر بار فقط یک بلوک ۱۶ بایتی با IV صفر پردازش می‌شود، خروجی دقیقاً معادل ECB است.
  memcpy(output, input, 16);
  unsigned char iv[16] = {0};
  br_aes_big_cbcenc_keys ctx;
  br_aes_big_cbcenc_init(&ctx, AES_KEY, 16);
  br_aes_big_cbcenc_run(&ctx, iv, output, 16);
#endif
}

void aesDecryptBlock16(const unsigned char* input, unsigned char* output) {
#if defined(ESP32)
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, AES_KEY, 128);
  mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, input, output);
  mbedtls_aes_free(&aes);
#elif defined(ESP8266)
  // معادل ECB decrypt با CBC decrypt روی یک بلوک و IV صفر.
  memcpy(output, input, 16);
  unsigned char iv[16] = {0};
  br_aes_big_cbcdec_keys ctx;
  br_aes_big_cbcdec_init(&ctx, AES_KEY, 16);
  br_aes_big_cbcdec_run(&ctx, iv, output, 16);
#endif
}

// این تابع متن ساده رمز را می‌گیرد و به یک رشته رمزگذاری‌شده (تبدیل شده به هگز) برمی‌گرداند.
String encryptPassword(const char* password, size_t bufferSize) {
  if (strlen(password) == 0) return "";

  size_t paddedSize = (bufferSize + 15) / 16 * 16;
  unsigned char* input = (unsigned char*)calloc(paddedSize, 1);
  if (!input) return "";
  strncpy((char*)input, password, bufferSize - 1);

  unsigned char* output = (unsigned char*)calloc(paddedSize, 1);
  if (!output) {
    free(input);
    return "";
  }

  for (size_t i = 0; i < paddedSize; i += 16) {
    aesEncryptBlock16(input + i, output + i);
    feedWatchdog();
  }

  String hexString = "ENC:";
  hexString.reserve(4 + paddedSize * 2);
  for (size_t i = 0; i < paddedSize; i++) {
    char hex[3];
    sprintf(hex, "%02x", output[i]);
    hexString += String(hex);
  }

  free(input);
  free(output);
  return hexString;
}

// این تابع مقادیر را از فایل می‌خواند و در صورتی که رمز شده باشد باز می‌کند.
// اگر تنظیمات از قبل (نسخه قدیمی) به صورت متن ساده ذخیره شده باشد، هوشمندانه آن را می‌فهمد.
void loadAndDecryptPassword(const char* savedValue, char* outputBuffer, size_t bufferSize) {
  String val = String(savedValue);

  if (val.length() == 0) {
    outputBuffer[0] = '\0';
    return;
  }

  if (val.startsWith("ENC:")) {
    String hexString = val.substring(4);
    size_t len = hexString.length();
    if (len % 32 != 0) {
      outputBuffer[0] = '\0';
      return;
    }

    size_t paddedSize = len / 2;
    unsigned char* input = (unsigned char*)calloc(paddedSize, 1);
    if (!input) {
      outputBuffer[0] = '\0';
      return;
    }

    for (size_t i = 0; i < paddedSize; i++) {
      char hex[3] = {hexString[i * 2], hexString[i * 2 + 1], '\0'};
      input[i] = (unsigned char)strtol(hex, NULL, 16);
    }

    unsigned char* output = (unsigned char*)calloc(paddedSize, 1);
    if (!output) {
      free(input);
      outputBuffer[0] = '\0';
      return;
    }

    for (size_t i = 0; i < paddedSize; i += 16) {
      aesDecryptBlock16(input + i, output + i);
      feedWatchdog();
    }

    size_t copySize = paddedSize < bufferSize ? paddedSize : bufferSize - 1;
    memcpy(outputBuffer, output, copySize);
    outputBuffer[copySize] = '\0';

    free(input);
    free(output);
  } else {
    strncpy(outputBuffer, savedValue, bufferSize - 1);
    outputBuffer[bufferSize - 1] = '\0';
  }
}

void setup() {
  // راه‌اندازی سریال در ESP32؛ پایه رله از RX0 جدا شده تا تداخل سریال ایجاد نشود
  Serial.begin(115200);
  
  // فعال‌سازی واچ‌داگ مخصوص ESP32 با تایم‌اوت ۸ ثانیه
  setupWatchdog(); 

  // راه‌اندازی حافظه داخلی سیستم
#if defined(ESP32)
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed even after formatting!");
  }
#elif defined(ESP8266)
  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount Failed. Formatting...");
    LittleFS.format();
    if (!LittleFS.begin()) Serial.println("LittleFS Mount Failed even after formatting!");
  }
#endif

  // لود کردن تنظیمات ذخیره شده
  loadScenarios();
  loadWiFiSettings(); // در این بخش رمزها خودکار رمزگشایی می‌شوند
  loadOverrideSetting(); // لود کردن وضعیت دستی
  loadProtectionSettings(); // لود کردن محافظت ضد روشن/خاموش شدن سریع
  loadTimeSetting();     
  loadRelayStats();      // لود کردن آمار سوییچ/مدت‌کارکرد رله ذخیره شده
  loadNtpSuccessInfo();  // لود کردن آخرین دریافت موفق ساعت از اینترنت

  // تنظیم پین رله
  pinMode(RELAY_PIN, OUTPUT);

  // *** اعمال فوری وضعیت کلید دستی بلافاصله پس از راه‌اندازی ***
  // در صورتی که قبل از قطعی برق رله روی حالت دستی روشن بوده، دوباره بی‌قید و شرط روشن می‌ماند
  if (manual_override == 1) {
    setRelay(true);
  } else {
    setRelay(false);
  }

  // *** هماهنگ کردن آمار رله با وضعیت واقعی سخت‌افزار بلافاصله بعد از بوت ***
  // مهم: اگر رله از قبل (قبل از این ریست/قطعی برق) روشن بوده و همچنان روشن می‌ماند، این ادامه‌ی همان
  // دوره‌ی کارکرد کمپرسور است، نه یک سوییچ جدید؛ پس این را در lastRelayStatState بدون افزایش شمارنده
  // ثبت می‌کنیم تا اولین بررسی در checkScenarios() آن را به اشتباه یک «سوییچ جدید» حساب نکند.
  lastRelayStatState = (manual_override == 1) ? 1 : 0;
  // بعد از بوت، اگر رله خاموش است یک دوره‌ی محافظه‌کارانه شروع می‌شود؛ چون وضعیت واقعی
  // کمپرسور درست پیش از قطع برق قابل اعتماد نیست.
  if (lastRelayStatState == 0) lastRelayOffMillis = millis();
  if (lastRelayStatState == 1) {
    relayOnSinceMillis = millis();
    relayCurrentlyOnForStats = true;
  }
  // مهم: تایمر ذخیره دوره‌ای آمار را هم‌زمان با بوت مقداردهی کن تا اگر رله از بوت روشن بود،
  // اختلاف از صفر millis حساب نشود و عدد پرت به onSeconds اضافه نشود.
  lastRelayStatSaveMillis = millis();

  // راه‌اندازی وای‌فای به صورت همزمان: AP برای اتصال مستقیم گوشی + STA برای اتصال اختیاری به اینترنت
  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(false);              // جلوگیری از نوشتن بی‌مورد تنظیمات وای‌فای روی فلش داخلی ESP32
  WiFi.setAutoReconnect(true);         // در فاز روشنِ STA اگر اتصال به مودم قطع شد، خود برد دوباره تلاش کند
  WiFi.softAP(custom_ssid, custom_password, 1, 0, 3);
  applyApTxPower();      // اعمال سطح قدرت سیگنال ذخیره‌شده (یا پیش‌فرض حداکثر)
  apCurrentlyOn = true;
  apCycleLastToggleMillis = millis();
  Serial.println("Access Point Started");

  // آغاز فاز اولیهٔ اتصال STA. اگر اینترنت فعال و تنظیمات مودم موجود باشد، اتصال برقرار می‌شود؛
  // در غیر این صورت STA خاموش می‌ماند تا بعداً از پنل فعال شود.
  staCurrentlyOn = true;
  staCycleLastToggleMillis = millis();
  connectToInternetWiFi();

  // تعریف کنترل‌کننده‌های وب‌سرور
  server.on("/", HTTP_GET, handleApiRoot);
  server.on("/settings", HTTP_GET, handleGetSettings);
  server.on("/save", HTTP_POST, handleSaveScenario);
  server.on("/sync", HTTP_POST, handleSyncTime);
  server.on("/status", HTTP_GET, handleGetStatus);
  server.on("/toggle-manual", HTTP_POST, handleToggleManual);
  server.on("/save-ap", HTTP_POST, handleSaveAP);
  server.on("/save-sta", HTTP_POST, handleSaveSTA);
  server.on("/save-protection", HTTP_POST, handleSaveProtection);
  server.on("/save-ap-cycle", HTTP_POST, handleSaveApCycle);
  server.on("/factory-reset", HTTP_POST, handleFactoryReset);

  server.onNotFound([](){
    if (server.method() == HTTP_OPTIONS) {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
      server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
      server.send(204);
      return;
    }
    sendJsonMessage(404, "error", "Endpoint not found");
  });

  server.begin();
  lastTick = millis();
  lastTimeSaveMillis = millis();
}

void loop() {
  feedWatchdog();

  // ============================================================
  //  زمان‌بندی چندوظیفه‌ای سبک و قابل‌حمل برای ESP32 و ESP8266
  // ============================================================
  // ESP8266-01 تک‌هسته‌ای است و FreeRTOS واقعی ندارد؛ بنابراین «مولتی‌ترد واقعی» روی هر دو برد
  // به شکل یکسان ممکن نیست. برای اینکه رفتار روی ESP32 و ESP8266 یکی بماند، کارها به taskهای
  // کوتاه و غیرمسدودکننده تقسیم شده‌اند تا وب‌سرور، ساعت، رله، NTP و چرخه‌های WiFi همزمان و روان‌تر
  // جلو بروند. روی ESP32 هم این روش عمداً حفظ شده تا منطق مشترک بماند و race condition ایجاد نشود.

  const unsigned long now = millis();

  // وب‌سرور باید در هر دور loop سرویس بگیرد تا API کند نشود.
  server.handleClient();

  // ساعت داخلی همچنان با while جبران عقب‌افتادگی می‌کند و باید خیلی مکرر اجرا شود.
  updateClock();

  // مدیریت ریستارت نرم‌افزاری امن
  if (pendingReset && (now - resetMillis > 2000UL)) {
    if (!pendingFactoryReset) saveTimeSetting();
    ESP.restart();
  }

  // بررسی کمبود RAM، اما نه در هر میکروثانیه؛ هر ۱ ثانیه کافی است و روی ESP8266 هم فشار کمتر دارد.
  static unsigned long lastHeapCheck = 0;
  static int lowHeapStreak = 0;
  if (now - lastHeapCheck >= 1000UL) {
    lastHeapCheck = now;
    if (ESP.getFreeHeap() < 2000) {
      lowHeapStreak++;
      if (lowHeapStreak >= 5) {
        Serial.println("Memory too low! Restarting safely to prevent freeze.");
        saveTimeSetting();
        ESP.restart();
      }
    } else {
      lowHeapStreak = 0;
    }
  }

  // سناریوها و رله: رزولوشن برنامه دقیقه‌ای است؛ ۲۰۰ms کاملاً کافی و بسیار سبک‌تر از هر loop است.
  static unsigned long lastScenarioTask = 0;
  if (now - lastScenarioTask >= 200UL) {
    lastScenarioTask = now;
    checkScenarios();
  }

  // کارهای WiFi/NTP/AP دوره‌ای هستند و لازم نیست در هر loop اجرا شوند.
  static unsigned long lastNetworkTask = 0;
  if (now - lastNetworkTask >= 1000UL) {
    lastNetworkTask = now;
    manageStaCycle();
    tryNtpSync();
    manageApCycle();
  }

  // ذخیره پشتیبان دوره‌ای زمان.
  if (time_synchronized && (now - lastTimeSaveMillis >= TIME_SAVE_INTERVAL)) {
    saveTimeSetting();
  }

  // ذخیره پیشرفت دوره‌ی جاری «روشن بودن» رله.
  if (relayCurrentlyOnForStats && (now - lastRelayStatSaveMillis >= TIME_SAVE_INTERVAL)) {
    unsigned long elapsedSec = relayCurrentPeriodElapsedSeconds();
    if (elapsedSec > 0) {
      addRelayOnSecondsSafely(elapsedSec);
      relayOnSinceMillis = millis();
    } else {
      relayOnSinceMillis = millis();
    }
    saveRelayStats();
  }

  // محافظت در برابر بازنشانی خاموش/بی‌صدای قدرت سیگنال توسط استک وای‌فای.
  static unsigned long lastTxPowerRefresh = 0;
  if (now - lastTxPowerRefresh >= 30000UL) {
    applyApTxPower();
    lastTxPowerRefresh = now;
  }

  // محافظت اضافه در برابر نویز رله (EMI): جهت پین رله هر ۳۰ ثانیه refresh می‌شود.
  static unsigned long lastPinModeRefresh = 0;
  if (now - lastPinModeRefresh >= 30000UL) {
    pinMode(RELAY_PIN, OUTPUT);
    lastPinModeRefresh = now;
  }

  // در ESP8266 این yield برای WiFi stack حیاتی است؛ در ESP32 هم بی‌ضرر است.
  yield();
}

void updateClock() {
  // از while به‌جای if استفاده شده: اگر loop() به هر دلیلی (نوشتن روی فلش، تاخیر وای‌فای و ...)
  // بیش از ۱ ثانیه بلاک شود، ساعت باید همان لحظه جبران شود، نه اینکه طی چند اجرای بعدی loop()
  // به‌تدریج جمع شود.
  while (millis() - lastTick >= 1000) {
    lastTick += 1000;
    currentSecond++;
    if (currentSecond >= 60) {
      currentSecond = 0;
      currentMinute++;
      if (currentMinute >= 60) {
        currentMinute = 0;
        currentHour++;
        if (currentHour >= 24) {
          currentHour = 0;
          advanceDate();
        }
      }
    }
  }
}

bool isLeapYear(int year) { return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0); }

void advanceDate() {
  const int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int maxDay = daysInMonth[currentMonth - 1];
  if (currentMonth == 2 && isLeapYear(currentYear)) maxDay = 29;
  currentDay++;
  if (currentDay > maxDay) { currentDay = 1; currentMonth++; if (currentMonth > 12) { currentMonth = 1; currentYear++; } }
  currentWeekday = (currentWeekday + 1) % 7;
}

void checkScenarios() {
  // جلوگیری از ذخیره‌سازی/لاگ بی‌دلیل با کش کردن وضعیت منطقی قبلی
  static int lastKnownState = -1; 
  bool desiredState;

  // اگر کنترل دستی روشن است، هیچ سناریویی بررسی نمی‌شود
  if (manual_override == 1) {
    desiredState = true;
    if (lastKnownState != 1) {
      if (time_synchronized) saveTimeSetting(); // ذخیره زمان قبل از فرمان قطع/وصل رله
      lastKnownState = 1;
    }
  }
  // اگر پس از قطع برق هنوز ساعت همگام‌سازی نشده باشد، رله خاموش می‌ماند
  else if (!time_synchronized) {
    desiredState = false;
    if (lastKnownState != 0) {
      lastKnownState = 0;
    }
  }
  else {
    desiredState = false;
    int currentMinutesSinceMidnight = currentHour * 60 + currentMinute;

    for (int i = 0; i < MAX_SCENARIOS; i++) {
      // سناریوی حذف‌شده یا غیرفعال فقط نمایش داده/ذخیره می‌شود و اجرا نمی‌شود
      if (!scenarios[i].active || !scenarios[i].enabled) continue;

      int startMinutes = scenarios[i].startHour * 60 + scenarios[i].startMinute;
      int endMinutes = scenarios[i].endHour * 60 + scenarios[i].endMinute;
      uint8_t selectedDays = scenarios[i].weekdays;

      if (startMinutes == endMinutes || selectedDays == 0) continue;
      // در سناریوی عبور از نیمه‌شب، بخش بعد از نیمه‌شب متعلق به روز شروع سناریو است.
      int scenarioDay = currentWeekday;
      if (startMinutes > endMinutes && currentMinutesSinceMidnight < endMinutes) scenarioDay = (currentWeekday + 6) % 7;
      if ((selectedDays & (1 << scenarioDay)) == 0) continue;

      if (startMinutes < endMinutes) {
        if (currentMinutesSinceMidnight >= startMinutes && currentMinutesSinceMidnight < endMinutes) {
          desiredState = true;
          break;
        }
      } else {
        if (currentMinutesSinceMidnight >= startMinutes || currentMinutesSinceMidnight < endMinutes) {
          desiredState = true;
          break;
        }
      }
    }

    int newKnownState = desiredState ? 1 : 0;
    if (lastKnownState != newKnownState) {
      saveTimeSetting(); // ذخیره زمان قبل از فرمان قطع/وصل رله
      lastKnownState = newKnownState;
    }
  }

  // --- محافظت در برابر نویز رله مجاور (EMI) ---
  // پین رله در هر اجرای loop() دوباره نوشته می‌شود، نه فقط زمانی که وضعیت منطقی عوض شده باشد.
  // دلیل: اگر نویز الکتریکی حاصل از قطع/وصل رله کناری باعث برگشتن اتفاقی رجیستر خروجی GPIO شود،
  // قبلاً چون فقط با تغییر lastKnownState دوباره digitalWrite زده می‌شد، فریمور متوجه این "پرش" پین
  // نمی‌شد و رله تا تغییر منطقی بعدی در وضعیت غلط می‌ماند. دیگیتال‌رایت هزینه‌ی محاسباتی ناچیزی دارد،
  // پس نوشتن مکرر آن هیچ ضرری ندارد ولی از ماندگاری اثر نویز روی پین جلوگیری می‌کند.
  // توجه: ذخیره‌سازی فلش (saveTimeSetting) و لاگ فقط روی تغییر واقعی state انجام می‌شود، نه هر بار.

  // --- Anti Short-Cycle Protection ---
  // هر فرمان روشن‌شدن، از جمله حالت دستی، تا سپری‌شدن حداقل زمان خاموشی نگه داشته می‌شود.
  if (desiredState && antiShortCycleMinutes > 0 && lastRelayStatState == 0) {
    unsigned long requiredMs = (unsigned long)antiShortCycleMinutes * 60UL * 1000UL;
    if (millis() - lastRelayOffMillis < requiredMs) desiredState = false;
  }

  // --- آمار سوییچ و مدت‌کارکرد رله (برای پایش سلامت/عمر کمپرسور) ---
  // این بخش کاملاً جدا از lastKnownState بالا کار می‌کند تا به منطق اصلی سناریو/دستی دست زده نشود؛
  // فقط تغییر واقعی وضعیت خروجی رله را (فارغ از دلیل: سناریو یا دستی) برای آمار ثبت می‌کند.
  int newRelayStatState = desiredState ? 1 : 0;
  if (lastRelayStatState != newRelayStatState) {
    if (newRelayStatState == 1) {
      // کمپرسور تازه روشن شد: یک سوییچ جدید و شروع اندازه‌گیری این دوره
      relaySwitchCount++;
      relayOnSinceMillis = millis();
      relayCurrentlyOnForStats = true;
    } else {
      // کمپرسور تازه خاموش شد: مدت این دوره به مجموع اضافه می‌شود
      if (relayCurrentlyOnForStats) {
        addRelayOnSecondsSafely(relayCurrentPeriodElapsedSeconds());
      }
      relayCurrentlyOnForStats = false;
      lastRelayOffMillis = millis();
    }
    lastRelayStatState = newRelayStatState;
    saveRelayStats(); // ذخیره فوری هر رویداد واقعی سوییچ، مشابه ذخیره فوری ساعت قبل از فرمان رله
  }

  setRelay(desiredState);
}

void loadScenarios() {
  if (!LittleFS.exists("/scenarios.json")) return;
  
  File configFile = LittleFS.open("/scenarios.json", "r");
  if (!configFile) return;

  // تغییر به DynamicJsonDocument برای جلوگیری از Stack Overflow
  DynamicJsonDocument doc(SCENARIOS_JSON_CAPACITY);
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) {
    Serial.print("loadScenarios JSON error: ");
    Serial.println(error.c_str());
    return;
  }

  // Migration سناریوها: نسخه ۱ آرایهٔ مستقیم بود؛ نسخه ۲ یک شیء شامل version و items است.
  JsonArray array = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc["items"].as<JsonArray>();
  int i = 0;
  for (JsonObject v : array) {
    if (i >= MAX_SCENARIOS) break;
    bool itemActive = v["active"] | false;
    scenarios[i].active = itemActive;
    // برای سازگاری با فایل‌های قدیمی: اگر کلید en وجود نداشت، سناریوهای موجود فعال فرض می‌شوند
    scenarios[i].enabled = v.containsKey("en") ? (bool)v["en"] : itemActive;
    scenarios[i].startHour = v["sh"] | 0;
    scenarios[i].startMinute = v["sm"] | 0;
    scenarios[i].endHour = v["eh"] | 0;
    scenarios[i].endMinute = v["em"] | 0;
    // سازگاری با سناریوهای قدیمی: نبودن wd یعنی اجرا در تمام روزها
    scenarios[i].weekdays = v.containsKey("wd") ? (uint8_t)(v["wd"] | 0x7F) : 0x7F;
    i++;
  }
}

void saveScenarios() {
  // تغییر به DynamicJsonDocument برای جلوگیری از Stack Overflow
  DynamicJsonDocument doc(SCENARIOS_JSON_CAPACITY);
  JsonObject root = doc.to<JsonObject>();
  root["version"] = SCENARIOS_FILE_VERSION;
  JsonArray array = root.createNestedArray("items");
  
  for (int i = 0; i < MAX_SCENARIOS; i++) {
    JsonObject obj = array.createNestedObject();
    obj["active"] = scenarios[i].active;
    obj["en"] = scenarios[i].enabled; // فعال بودن اجرای سناریو، جدا از حذف شدن آن
    obj["sh"] = scenarios[i].startHour;
    obj["sm"] = scenarios[i].startMinute;
    obj["eh"] = scenarios[i].endHour;
    obj["em"] = scenarios[i].endMinute;
    obj["wd"] = scenarios[i].weekdays;
  }

  File configFile = LittleFS.open("/scenarios.json", "w");
  if (!configFile) return;
  size_t written = serializeJson(doc, configFile);
  configFile.close();

  // اگر چیزی نوشته نشد یعنی فلش پر بوده یا خرابی رخ داده؛ فایل خراب را حذف می‌کنیم
  // تا در بوت بعدی به‌جای دیتای ناقص، به‌درستی رد شود.
  if (written == 0) {
    Serial.println("saveScenarios write failed!");
    LittleFS.remove("/scenarios.json");
  }
}

void loadWiFiSettings() {
  if (!LittleFS.exists("/wifi.json")) return;
  
  File configFile = LittleFS.open("/wifi.json", "r");
  if (!configFile) return;

  // بافر به دلیل تبدیل متن به هگزادسیمال رمزنگاری‌شده افزایش یافته است
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) return;
  // wifi.json نسخهٔ ۱ فاقد کلید version بود؛ هر دو فرمت همچنان خوانده می‌شوند.

  if (doc.containsKey("ssid")) { strncpy(custom_ssid, doc["ssid"], 31); custom_ssid[31] = '\0'; }
  if (doc.containsKey("pass")) {
    // بارگذاری و رمزگشایی هوشمند رمز عبور فرستنده (AP)
    loadAndDecryptPassword(doc["pass"], custom_password, 32);
  }
  if (doc.containsKey("sta_ssid")) { strncpy(sta_ssid, doc["sta_ssid"], 31); sta_ssid[31] = '\0'; }
  if (doc.containsKey("sta_pass")) {
    // بارگذاری و رمزگشایی هوشمند رمز عبور اینترنت (STA)
    loadAndDecryptPassword(doc["sta_pass"], sta_password, 64);
  }

  // در نسخه‌های جدید فقط همین کلید تعیین می‌کند که اصلاً از مودم/اینترنت استفاده شود یا نه.
  if (doc.containsKey("internet")) { internet_enabled = doc["internet"] | true; }

  // مهاجرت از نسخهٔ ۴: اگر کاربر قبلاً کلید جداگانهٔ STA را خاموش کرده بود و هنوز فیلدهای جدید چرخه وجود ندارند،
  // همان رفتار به «اینترنت غیرفعال» تبدیل می‌شود تا انتخاب قبلی کاربر از بین نرود.
  if (doc.containsKey("staEnabled") && !doc.containsKey("staOnMinutes") && !doc.containsKey("staOffMinutes")) {
    bool legacyStaEnabled = doc["staEnabled"] | true;
    if (!legacyStaEnabled) internet_enabled = false;
  }

  if (doc.containsKey("staOnMinutes")) {
    int v = doc["staOnMinutes"] | staOnMinutes;
    staOnMinutes = constrain(v, MIN_STA_ON_MINUTES, MAX_STA_CYCLE_MINUTES);
  }
  if (doc.containsKey("staOffMinutes")) {
    int v = doc["staOffMinutes"] | staOffMinutes;
    staOffMinutes = constrain(v, MIN_STA_OFF_MINUTES, MAX_STA_CYCLE_MINUTES);
  }

  // فیلدهای نسخه ۳: چرخه‌ی AP و قدرت سیگنال. فایل‌های قدیمی‌تر (نسخه ۲ و پایین‌تر) فاقد این کلیدها هستند
  // و به همین دلیل مقدار پیش‌فرض تعریف‌شده در بالای برنامه (چرخه غیرفعال، توان حداکثر) حفظ می‌شود.
  if (doc.containsKey("apCycleEnabled")) { apCycleEnabled = doc["apCycleEnabled"] | false; }
  if (doc.containsKey("apOnMinutes")) {
    int v = doc["apOnMinutes"] | apOnMinutes;
    apOnMinutes = constrain(v, MIN_AP_CYCLE_MINUTES, MAX_AP_CYCLE_MINUTES);
  }
  if (doc.containsKey("apOffMinutes")) {
    int v = doc["apOffMinutes"] | apOffMinutes;
    apOffMinutes = constrain(v, MIN_AP_CYCLE_MINUTES, MAX_AP_CYCLE_MINUTES);
  }
  if (doc.containsKey("apTxPowerLevel")) {
    int v = doc["apTxPowerLevel"] | apTxPowerLevel;
    apTxPowerLevel = constrain(v, 0, MAX_AP_TX_POWER_LEVEL);
  }
}

void saveWiFiSettings() {
  StaticJsonDocument<1024> doc;
  doc["version"] = WIFI_FILE_VERSION;
  doc["ssid"] = custom_ssid;
  
  // اعمال رمزنگاری سخت‌افزاری بر روی پسورد شبکه محلی بورد
  doc["pass"] = encryptPassword(custom_password, 32);
  
  doc["sta_ssid"] = sta_ssid;
  
  // اعمال رمزنگاری سخت‌افزاری بر روی پسورد مودم کاربر قبل از نگارش در فلش مموری بورد
  doc["sta_pass"] = encryptPassword(sta_password, 64);
  
  doc["internet"] = internet_enabled;
  doc["staOnMinutes"] = staOnMinutes;
  doc["staOffMinutes"] = staOffMinutes;

  doc["apCycleEnabled"] = apCycleEnabled;
  doc["apOnMinutes"] = apOnMinutes;
  doc["apOffMinutes"] = apOffMinutes;
  doc["apTxPowerLevel"] = apTxPowerLevel;

  File configFile = LittleFS.open("/wifi.json", "w");
  if (!configFile) return;
  size_t written = serializeJson(doc, configFile);
  configFile.close();

  // مطابق همان الگوی محافظتی سایر توابع ذخیره‌سازی (saveScenarios/saveTimeSetting/...):
  // اگر نوشتن ناقص بود (مثلاً فلش پر بود)، فایل نیمه‌نوشته را حذف می‌کنیم تا در بوت بعدی
  // به‌جای خواندن دیتای خراب (که می‌تواند شامل SSID/رمز رمزنگاری‌شده باشد)، به‌درستی رد شود.
  if (written == 0) {
    Serial.println("saveWiFiSettings write failed!");
    LittleFS.remove("/wifi.json");
  }
}

void loadOverrideSetting() {
  if (!LittleFS.exists("/override.txt")) {
    manual_override = 0;
    return;
  }
  File f = LittleFS.open("/override.txt", "r");
  if (f) {
    String val = f.readString();
    // سازگاری با فایل قدیمی که فقط 0 یا 1 بود.
    manual_override = val.startsWith("V") ? val.substring(val.indexOf(':') + 1).toInt() : val.toInt();
    manual_override = (manual_override == 1) ? 1 : 0;
    f.close();
  }
}

void saveOverrideSetting() {
  File f = LittleFS.open("/override.txt", "w");
  if (f) {
    f.print("V"); f.print(OVERRIDE_FILE_VERSION); f.print(":"); f.print(manual_override);
    f.close();
  }
}

// تنظیمات محافظ ضد استارت مکرر جدا از Wi-Fi نگهداری می‌شود تا توسعه‌ی آینده مستقل باشد.
void loadProtectionSettings() {
  if (!LittleFS.exists("/protection.json")) return;
  File f = LittleFS.open("/protection.json", "r"); if (!f) return;
  StaticJsonDocument<128> doc; DeserializationError err = deserializeJson(doc, f); f.close();
  if (!err && doc.containsKey("minOffMinutes")) {
    int value = doc["minOffMinutes"] | 3;
    antiShortCycleMinutes = constrain(value, 0, MAX_ANTI_SHORT_CYCLE_MINUTES);
  }
}
void saveProtectionSettings() {
  StaticJsonDocument<128> doc;
  doc["version"] = PROTECTION_FILE_VERSION; doc["minOffMinutes"] = antiShortCycleMinutes;
  File f = LittleFS.open("/protection.json", "w"); if (!f) return;
  size_t written = serializeJson(doc, f); f.close();
  // مطابق همان الگوی محافظتی سایر توابع ذخیره‌سازی: نوشتن ناقص را با حذف فایل خراب مشخص می‌کنیم.
  if (written == 0) {
    Serial.println("saveProtectionSettings write failed!");
    LittleFS.remove("/protection.json");
  }
}

// نوشتن مقدار فعلی ساعت روی یکی از دو فایل به نوبت (Round-Robin)
// این کار باعث می‌شود اگر برق درست حین نوشتن قطع شود، نسخه ذخیره شده در فایل دیگر (که دست نخورده مانده) در دسترس بماند
void saveTimeSetting() {
  timeSaveSeq++;
  timeFileSlot = 1 - timeFileSlot; // جابجایی بین فایل ۰ و ۱

  File f = LittleFS.open(TIME_FILES[timeFileSlot], "w");
  if (f) {
    size_t written = f.printf("V%d:%d:%d:%d:%d:%lu:%d:%d:%d:%d", TIME_FILE_VERSION, currentHour, currentMinute, currentSecond, time_synchronized ? 1 : 0, timeSaveSeq, currentYear, currentMonth, currentDay, currentWeekday);
    f.close();
    // اگر نوشتن ناقص بود، این فایل را حذف می‌کنیم تا readTimeFile آن را نامعتبر تشخیص دهد
    // و به‌جای یک رکورد خراب، سراغ فایل زوجش (که سالم مانده) برود.
    if (written == 0) {
      Serial.println("saveTimeSetting write failed!");
      LittleFS.remove(TIME_FILES[timeFileSlot]);
    }
  }
  lastTimeSaveMillis = millis();
}

// خواندن و اعتبارسنجی یکی از دو فایل زمان. در صورت معتبر بودن مقادیر، true برمی‌گرداند
bool readTimeFile(const char* path, int &h, int &m, int &s, int &sync, unsigned long &seq, int &y, int &mon, int &d, int &wd) {
  if (!LittleFS.exists(path)) return false;

  File f = LittleFS.open(path, "r");
  if (!f) return false;

  String val = f.readString();
  f.close();

  int th = -1, tm = -1, ts = -1, tsync = -1, ty = 2026, tmon = 1, td = 1, twd = 3, fileVersion = 1;
  unsigned long tseq = 0;
  int parsed;
  if (val.startsWith("V")) {
    parsed = sscanf(val.c_str(), "V%d:%d:%d:%d:%d:%lu:%d:%d:%d:%d", &fileVersion, &th, &tm, &ts, &tsync, &tseq, &ty, &tmon, &td, &twd);
  } else {
    parsed = sscanf(val.c_str(), "%d:%d:%d:%d:%lu:%d:%d:%d:%d", &th, &tm, &ts, &tsync, &tseq, &ty, &tmon, &td, &twd);
  }

  // بررسی صحت مقادیر خوانده شده تا در صورت خراب شدن فایل (نوشتن ناقص هنگام قطع برق) استفاده نشود
  if (parsed < 4 || th < 0 || th > 23 || tm < 0 || tm > 59 || ts < 0 || ts > 59 || (tsync != 0 && tsync != 1)) {
    return false;
  }

  if ((fileVersion >= 2 || parsed >= 9) && (ty < 2024 || tmon < 1 || tmon > 12 || td < 1 || td > 31 || twd < 0 || twd > 6)) return false;
  h = th; m = tm; s = ts; sync = tsync; y = ty; mon = tmon; d = td; wd = twd;
  seq = ((fileVersion >= 2 && parsed >= 10) || (fileVersion == 1 && parsed >= 9)) ? tseq : 0; // فایل‌های قدیمیِ بدون تاریخ باید دوباره همگام‌سازی شوند
  return true;
}

// بارگذاری آخرین زمان معتبر ذخیره شده، بدون توجه به دلیل ریست شدن برد
// به این ترتیب یک نوسان یا قطعی کوتاه برق، ساعت را کاملاً پاک نمی‌کند و همگام‌سازی مجدد با گوشی لازم نیست
void loadTimeSetting() {
  int h0, m0, s0, sync0, y0, mon0, d0, wd0; unsigned long seq0;
  int h1, m1, s1, sync1, y1, mon1, d1, wd1; unsigned long seq1;

  bool ok0 = readTimeFile(TIME_FILES[0], h0, m0, s0, sync0, seq0, y0, mon0, d0, wd0);
  bool ok1 = readTimeFile(TIME_FILES[1], h1, m1, s1, sync1, seq1, y1, mon1, d1, wd1);

  bool useSlot0 = false, useSlot1 = false;

  if (ok0 && ok1) {
    // هر دو فایل معتبرند: جدیدترین را بر اساس شماره توالی انتخاب کن
    if (seq1 >= seq0) useSlot1 = true; else useSlot0 = true;
  } else if (ok0) {
    useSlot0 = true;
  } else if (ok1) {
    useSlot1 = true;
  }

  if (useSlot0) {
    currentHour = h0; currentMinute = m0; currentSecond = s0; currentYear = y0; currentMonth = mon0; currentDay = d0; currentWeekday = wd0;
    time_synchronized = (sync0 == 1 && seq0 > 0); // فایل قدیمیِ بدون تاریخ باید دوباره همگام‌سازی شود
    timeFileSlot = 0;
    timeSaveSeq = seq0;
  } else if (useSlot1) {
    currentHour = h1; currentMinute = m1; currentSecond = s1; currentYear = y1; currentMonth = mon1; currentDay = d1; currentWeekday = wd1;
    time_synchronized = (sync1 == 1 && seq1 > 0); // فایل قدیمیِ بدون تاریخ باید دوباره همگام‌سازی شود
    timeFileSlot = 1;
    timeSaveSeq = seq1;
  } else {
    // هیچ رکورد معتبری پیدا نشد (اولین راه‌اندازی برد یا خرابی کامل هر دو فایل)
    currentHour = 0;
    currentMinute = 0;
    currentSecond = 0;
    time_synchronized = false;
  }
}

// نوشتن آمار سوییچ/مدت‌کارکرد رله روی یکی از دو فایل به نوبت (Round-Robin)، دقیقاً مشابه ذخیره‌سازی ساعت،
// تا در صورت قطع برق درست وسط نوشتن، نسخه‌ی فایل دیگر سالم و قابل استفاده باقی بماند.
void saveRelayStats() {
  relayStatSaveSeq++;
  relayStatFileSlot = 1 - relayStatFileSlot;

  File f = LittleFS.open(RELAY_STAT_FILES[relayStatFileSlot], "w");
  if (f) {
    size_t written = f.printf("V%d:%lu:%lu:%lu", RELAY_STAT_FILE_VERSION, relaySwitchCount, relayTotalOnSeconds, relayStatSaveSeq);
    f.close();
    if (written == 0) {
      Serial.println("saveRelayStats write failed!");
      LittleFS.remove(RELAY_STAT_FILES[relayStatFileSlot]);
    }
  }
  lastRelayStatSaveMillis = millis();
}

// بارگذاری آخرین دریافت موفق ساعت از اینترنت (NTP) از فلش.
void loadNtpSuccessInfo() {
  ntpLastSuccessValid = false;
  if (!LittleFS.exists(NTP_META_FILE)) return;
  File f = LittleFS.open(NTP_META_FILE, "r");
  if (!f) return;

  StaticJsonDocument<192> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return;

  int y   = doc["y"]   | 0;
  int mon = doc["mon"] | 0;
  int d   = doc["d"]   | 0;
  int h   = doc["h"]   | -1;
  int m   = doc["m"]   | -1;
  int s   = doc["s"]   | -1;

  if (y < 2024 || mon < 1 || mon > 12 || d < 1 || d > 31 || h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) return;

  ntpLastSuccessYear = y;
  ntpLastSuccessMonth = mon;
  ntpLastSuccessDay = d;
  ntpLastSuccessHour = h;
  ntpLastSuccessMinute = m;
  ntpLastSuccessSecond = s;
  ntpLastSuccessValid = true;
  ntpEverSucceeded = true;
}

// ذخیره آخرین دریافت موفق ساعت از اینترنت به‌صورت تاریخ/ساعت مطلق؛ آخرین تلاش ناموفق اصلاً ذخیره نمی‌شود.
void saveNtpSuccessInfo() {
  if (!ntpLastSuccessValid) return;

  StaticJsonDocument<192> doc;
  doc["version"] = NTP_META_FILE_VERSION;
  doc["y"] = ntpLastSuccessYear;
  doc["mon"] = ntpLastSuccessMonth;
  doc["d"] = ntpLastSuccessDay;
  doc["h"] = ntpLastSuccessHour;
  doc["m"] = ntpLastSuccessMinute;
  doc["s"] = ntpLastSuccessSecond;

  File f = LittleFS.open(NTP_META_FILE, "w");
  if (!f) return;
  size_t written = serializeJson(doc, f);
  f.close();
  if (written == 0) {
    Serial.println("saveNtpSuccessInfo write failed!");
    LittleFS.remove(NTP_META_FILE);
  }
}

// محاسبه امن مدت دوره جاری روشن بودن رله.
// اگر relayOnSinceMillis به هر دلیل خراب/صفر باشد یا فاصله غیرمنطقی شود، از اضافه شدن عدد پرت جلوگیری می‌کنیم.
unsigned long relayCurrentPeriodElapsedSeconds() {
  if (!relayCurrentlyOnForStats) return 0;
  if (relayOnSinceMillis == 0) {
    Serial.println("Relay stats warning: relayOnSinceMillis was zero; re-anchoring.");
    relayOnSinceMillis = millis();
    return 0;
  }

  unsigned long elapsedSec = (millis() - relayOnSinceMillis) / 1000UL;
  if (elapsedSec > MAX_RELAY_OPEN_PERIOD_SECONDS) {
    Serial.print("Relay stats warning: unrealistic open period ignored: ");
    Serial.println(elapsedSec);
    relayOnSinceMillis = millis();
    return 0;
  }
  return elapsedSec;
}

// اضافه کردن امن به مجموع کارکرد موتور بدون overflow و بدون عبور از سقف sanity.
void addRelayOnSecondsSafely(unsigned long secondsToAdd) {
  if (secondsToAdd == 0) return;
  if (relayTotalOnSeconds > MAX_RELAY_ON_SECONDS) {
    Serial.println("Relay stats warning: stored onSeconds was unrealistic; resetting to 0.");
    relayTotalOnSeconds = 0;
  }
  if (secondsToAdd > (MAX_RELAY_ON_SECONDS - relayTotalOnSeconds)) {
    Serial.println("Relay stats warning: onSeconds overflow/unrealistic add blocked.");
    relayTotalOnSeconds = MAX_RELAY_ON_SECONDS;
  } else {
    relayTotalOnSeconds += secondsToAdd;
  }
}

// خواندن و اعتبارسنجی یکی از دو فایل آمار رله
bool readRelayStatFile(const char* path, unsigned long &sc, unsigned long &tos, unsigned long &seq) {
  if (!LittleFS.exists(path)) return false;

  File f = LittleFS.open(path, "r");
  if (!f) return false;

  String val = f.readString();
  f.close();

  unsigned long tsc = 0, ttos = 0, tseq = 0;
  int fileVersion = 1;
  int parsed = val.startsWith("V") ? sscanf(val.c_str(), "V%d:%lu:%lu:%lu", &fileVersion, &tsc, &ttos, &tseq) : sscanf(val.c_str(), "%lu:%lu:%lu", &tsc, &ttos, &tseq);
  if (parsed != (val.startsWith("V") ? 4 : 3)) return false; // نوشتن ناقص هنگام قطع برق؛ این رکورد نامعتبر است
  if (tsc > MAX_RELAY_SWITCH_COUNT || ttos > MAX_RELAY_ON_SECONDS) {
    Serial.print("Invalid relay stats ignored from ");
    Serial.println(path);
    return false;
  }

  sc = tsc; tos = ttos; seq = tseq;
  return true;
}

// بارگذاری آخرین آمار معتبر سوییچ/مدت‌کارکرد رله، بدون توجه به دلیل ریست شدن برد
void loadRelayStats() {
  unsigned long sc0, tos0, seq0;
  unsigned long sc1, tos1, seq1;

  bool ok0 = readRelayStatFile(RELAY_STAT_FILES[0], sc0, tos0, seq0);
  bool ok1 = readRelayStatFile(RELAY_STAT_FILES[1], sc1, tos1, seq1);

  bool useSlot0 = false, useSlot1 = false;

  if (ok0 && ok1) {
    if (seq1 >= seq0) useSlot1 = true; else useSlot0 = true;
  } else if (ok0) {
    useSlot0 = true;
  } else if (ok1) {
    useSlot1 = true;
  }

  if (useSlot0) {
    relaySwitchCount = sc0; relayTotalOnSeconds = tos0;
    relayStatFileSlot = 0; relayStatSaveSeq = seq0;
  } else if (useSlot1) {
    relaySwitchCount = sc1; relayTotalOnSeconds = tos1;
    relayStatFileSlot = 1; relayStatSaveSeq = seq1;
  } else {
    // هیچ رکورد معتبری پیدا نشد (اولین راه‌اندازی برد یا خرابی کامل هر دو فایل)
    relaySwitchCount = 0;
    relayTotalOnSeconds = 0;
  }
}

// اتصال اختیاری برد به مودم/روتر برای گرفتن ساعت از اینترنت
// نکته: این اتصال باعث خاموش شدن Access Point برد نمی‌شود؛ گوشی همچنان می‌تواند مستقیم به خود برد وصل شود.
void setStaConnectionState(bool on) {
  if (on) {
    staCurrentlyOn = true;
    staCycleLastToggleMillis = millis();
    connectToInternetWiFi();
  } else {
    WiFi.setAutoReconnect(false); // در فاز خاموش چرخه، اجازهٔ اتصال خودکار مجدد داده نمی‌شود
    WiFi.disconnect(false);
    staCurrentlyOn = false;
    staCycleLastToggleMillis = millis();
    staEverConnectedThisBoot = false; // فاز بعدیِ روشن یک تلاش تازه محسوب می‌شود
  }
}

// مدیریت چرخهٔ دوره‌ای اتصال مودم اینترنت (STA).
// اگر مدت خاموش بودن صفر باشد، اتصال دائماً روشن می‌ماند و اصلاً وارد فاز خاموش نمی‌شود.
void manageStaCycle() {
  if (!internet_enabled || strlen(sta_ssid) == 0) {
    // وقتی اینترنت از پنل خاموش است یا مشخصات مودم وارد نشده، خودِ STA هم بی‌دلیل روشن نگه داشته نمی‌شود.
    if (staCurrentlyOn || WiFi.status() == WL_CONNECTED) {
      WiFi.setAutoReconnect(false);
      WiFi.disconnect(false);
      staCurrentlyOn = false;
    }
    staCycleLastToggleMillis = millis();
    return;
  }

  if (staOffMinutes == 0) {
    // حالت دائماً روشن: اگر قبلاً به‌خاطر چرخه خاموش شده بود، همین حالا دوباره وصل شود.
    if (!staCurrentlyOn) setStaConnectionState(true);
    return;
  }

  unsigned long onMs  = (unsigned long)staOnMinutes  * 60000UL;
  unsigned long offMs = (unsigned long)staOffMinutes * 60000UL;
  unsigned long elapsed = millis() - staCycleLastToggleMillis;

  if (staCurrentlyOn && elapsed >= onMs) {
    setStaConnectionState(false);
  } else if (!staCurrentlyOn && elapsed >= offMs) {
    setStaConnectionState(true);
  }
}

// اتصال اختیاری برد به مودم/روتر برای گرفتن ساعت از اینترنت
// نکته: این اتصال باعث خاموش شدن Access Point برد نمی‌شود؛ گوشی همچنان می‌تواند مستقیم به خود برد وصل شود.
void connectToInternetWiFi() {
  if (!internet_enabled) {
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false); // اینترنت غیرفعال است؛ فقط AP برقرار می‌ماند
    return;
  }
  if (strlen(sta_ssid) == 0) {
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false); // اگر وای‌فای اینترنت تنظیم نشده، فقط بخش STA را آزاد کن و AP باقی می‌ماند
    return;
  }

  WiFi.setAutoReconnect(true);
  Serial.print("Connecting STA to modem WiFi: ");
  Serial.println(sta_ssid);

  if (strlen(sta_password) > 0) {
    WiFi.begin(sta_ssid, sta_password);
  } else {
    WiFi.begin(sta_ssid); // پشتیبانی از شبکه‌های بدون رمز در صورت نیاز
  }

  // تنظیم ساعت با اولویت سرورهای داخلی ایران و سپس سرور جهانی
  configTime(NTP_GMT_OFFSET_SEC, 0, "ir.pool.ntp.org", "ntp.nic.ir", "pool.ntp.org");

  // نکته مهم: WiFi.begin() در هسته آردوینوی ESP32 می‌تواند به‌صورت داخلی تنظیم قدرت سیگنال رادیو را
  // به مقدار پیش‌فرض (حداکثر) بازنشانی کند. برای همین بلافاصله بعد از هر تلاش اتصال STA، سطح
  // انتخاب‌شده‌ی کاربر دوباره روی رادیو اعمال می‌شود تا تنظیم «قدرت سیگنال» واقعاً پابرجا بماند.
  applyApTxPower();
}

// تلاش دوره‌ای و غیرمسدودکننده برای دریافت ساعت از اینترنت
void tryNtpSync() {
  // نکته مهم: اینجا از ntp_synced_this_boot استفاده می‌شود، نه time_synchronized.
  // time_synchronized ممکن است از فایل ذخیره‌شده (سنکرون قبلی با گوشی یا بوت قبلی) از همان لحظه بوت true باشد؛
  // اگر آن را ملاک فاصله‌ی تلاش قرار می‌دادیم، برد فکر می‌کرد NTP قبلاً موفق بوده و به‌جای هر ۱ دقیقه
  // هر ۱ ساعت یک‌بار تلاش می‌کرد — یعنی با اینکه به مودم وصل است، ممکن بود تا یک ساعت ساعت واقعی نگیرد.
  unsigned long interval = ntp_synced_this_boot ? NTP_RECHECK_INTERVAL_MS : NTP_RETRY_INTERVAL_MS;

  if (!internet_enabled) return;
  if (!staCurrentlyOn) return;
  if (strlen(sta_ssid) == 0) return;

  if (ntpFirstCheckPending) {
    // اولین بار بعد از این بوت یا بعد از تغییر تنظیمات مودم: فارغ از فاصله‌ی زمانی، همین الان تلاش کن
    ntpFirstCheckPending = false;
  } else if (millis() - lastNtpCheckMillis < interval) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    // تا وقتی واقعاً به مودم وصل نشده‌ایم، زمان آخرین تلاش را جلو نمی‌بریم تا
    // به محض برقراری اتصال، گرفتن ساعت بدون انتظار اضافه انجام شود.
    return;
  }

  lastNtpCheckMillis = millis();

  time_t now = time(nullptr);
  struct tm *tmInfo = localtime(&now);

  // اگر سال معتبر بود یعنی NTP زمان واقعی داده است.
  if (tmInfo && (tmInfo->tm_year + 1900) >= 2024) {
    currentHour = tmInfo->tm_hour;
    currentMinute = tmInfo->tm_min;
    currentSecond = tmInfo->tm_sec;
    currentYear = tmInfo->tm_year + 1900;
    currentMonth = tmInfo->tm_mon + 1;
    currentDay = tmInfo->tm_mday;
    currentWeekday = (tmInfo->tm_wday + 1) % 7;
    lastTick = millis();

    if (!ntp_synced_this_boot) {
      Serial.println("Time synchronized by internet NTP.");
    }

    time_synchronized = true;
    ntp_synced_this_boot = true;
    ntpEverSucceeded = true;
    ntpLastSuccessYear = currentYear;
    ntpLastSuccessMonth = currentMonth;
    ntpLastSuccessDay = currentDay;
    ntpLastSuccessHour = currentHour;
    ntpLastSuccessMinute = currentMinute;
    ntpLastSuccessSecond = currentSecond;
    ntpLastSuccessValid = true;
    saveTimeSetting();     // ساعت اینترنتی معتبر را ذخیره کن تا در قطعی برق کوتاه از دست نرود
    saveNtpSuccessInfo();  // فقط آخرین دریافت موفق NTP ذخیره می‌شود، نه تلاش‌های ناموفق
  }
}

void handleApiRoot() {
  StaticJsonDocument<768> doc;
  doc["status"] = "success";
  doc["program"] = PROGRAM_NAME;
  doc["tagline"] = PROGRAM_TAGLINE;
  doc["api"] = "Smart Cooler REST JSON API";
  doc["version"] = STORAGE_FORMAT_REVISION;
  JsonArray endpoints = doc.createNestedArray("endpoints");
  endpoints.add("GET /status");
  endpoints.add("GET /settings");
  endpoints.add("POST /save");
  endpoints.add("POST /sync");
  endpoints.add("POST /toggle-manual");
  endpoints.add("POST /save-ap");
  endpoints.add("POST /save-sta");
  endpoints.add("POST /save-protection");
  endpoints.add("POST /save-ap-cycle");
  endpoints.add("POST /factory-reset");
  String out;
  serializeJson(doc, out);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", out);
}

void handleGetSettings() {
  DynamicJsonDocument doc(8192);
  doc["status"] = "success";
  doc["programName"] = PROGRAM_NAME;
  doc["programTagline"] = PROGRAM_TAGLINE;
  doc["maxScenarios"] = MAX_SCENARIOS;

  JsonObject ap = doc.createNestedObject("ap");
  ap["ssid"] = custom_ssid;
  ap["password"] = custom_password;
  ap["cycleEnabled"] = apCycleEnabled;
  ap["onMinutes"] = apOnMinutes;
  ap["offMinutes"] = apOffMinutes;
  ap["txPowerLevel"] = apTxPowerLevel;

  JsonObject sta = doc.createNestedObject("sta");
  sta["internetEnabled"] = internet_enabled;
  sta["ssid"] = sta_ssid;
  sta["password"] = sta_password;
  sta["onMinutes"] = staOnMinutes;
  sta["offMinutes"] = staOffMinutes;

  JsonObject protection = doc.createNestedObject("protection");
  protection["minOffMinutes"] = antiShortCycleMinutes;

  JsonArray arr = doc.createNestedArray("scenarios");
  for (int i = 0; i < MAX_SCENARIOS; i++) {
    if (!scenarios[i].active) continue;
    JsonObject obj = arr.createNestedObject();
    obj["sh"] = scenarios[i].startHour;
    obj["sm"] = scenarios[i].startMinute;
    obj["eh"] = scenarios[i].endHour;
    obj["em"] = scenarios[i].endMinute;
    obj["en"] = scenarios[i].enabled;
    obj["wd"] = scenarios[i].weekdays;
  }

  String out;
  serializeJson(doc, out);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", out);
}

void handleSaveScenario() {
  if (!allowRequest(lastSaveScenarioRequest, 1200UL)) return;

  DynamicJsonDocument doc(SCENARIOS_JSON_CAPACITY);
  if (!parseJsonBody(doc)) return;
  bool payloadOk = false;
  JsonVariantConst payload = verifiedPayload(doc, payloadOk);
  if (!payloadOk) return;
  if (!payload.is<JsonArrayConst>()) {
    sendJsonMessage(400, "error", "Expected JSON array");
    return;
  }

  JsonArrayConst array = payload.as<JsonArrayConst>();
  if (array.size() > MAX_SCENARIOS) {
    sendJsonMessage(400, "error", "Too many scenarios");
    return;
  }

  Scenario temp[MAX_SCENARIOS] = {};
  int i = 0;
  for (JsonObjectConst v : array) {
    int sh = v["sh"] | -1;
    int sm = v["sm"] | -1;
    int eh = v["eh"] | -1;
    int em = v["em"] | -1;
    int wd = v.containsKey("wd") ? (int)(v["wd"] | 0x7F) : 0x7F;

    if (sh < 0 || sh > 23 || eh < 0 || eh > 23 || sm < 0 || sm > 59 || em < 0 || em > 59) {
      sendJsonMessage(400, "error", "Invalid scenario time range");
      return;
    }
    if (sh == eh && sm == em) {
      sendJsonMessage(400, "error", "Scenario start and end cannot be equal");
      return;
    }
    if (wd < 1 || wd > 127) {
      sendJsonMessage(400, "error", "Invalid weekday mask");
      return;
    }

    temp[i].active = true;
    temp[i].enabled = v["en"] | true;
    temp[i].startHour = sh;
    temp[i].startMinute = sm;
    temp[i].endHour = eh;
    temp[i].endMinute = em;
    temp[i].weekdays = (uint8_t)wd;
    i++;
  }

  bool isChanged = false;
  for (int j = 0; j < MAX_SCENARIOS; j++) {
    if (scenarios[j].active != temp[j].active ||
        scenarios[j].enabled != temp[j].enabled ||
        scenarios[j].startHour != temp[j].startHour ||
        scenarios[j].startMinute != temp[j].startMinute ||
        scenarios[j].endHour != temp[j].endHour ||
        scenarios[j].endMinute != temp[j].endMinute ||
        scenarios[j].weekdays != temp[j].weekdays) {
      isChanged = true;
      break;
    }
  }

  if (isChanged) {
    for (int j = 0; j < MAX_SCENARIOS; j++) scenarios[j] = temp[j];
    saveScenarios();
  }

  sendJsonMessage(200, "success", isChanged ? "Scenarios saved" : "No changes");
}

void handleSyncTime() {
  if (!allowRequest(lastSyncRequest, 1000UL)) return;

  StaticJsonDocument<256> doc;
  if (!parseJsonBody(doc)) return;
  bool payloadOk = false;
  JsonVariantConst payload = verifiedPayload(doc, payloadOk);
  if (!payloadOk) return;

  int h = payload["h"] | -1;
  int m = payload["m"] | -1;
  int s = payload["s"] | -1;
  int y = payload["y"] | 0;
  int mon = payload["mon"] | 0;
  int d = payload["d"] | 0;
  int wd = payload["wd"] | -1;

  if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59 ||
      y < 2024 || mon < 1 || mon > 12 || d < 1 || d > 31 || wd < 0 || wd > 6) {
    sendJsonMessage(400, "error", "Invalid time range");
    return;
  }

  currentHour = h;
  currentMinute = m;
  currentSecond = s;
  currentYear = y;
  currentMonth = mon;
  currentDay = d;
  currentWeekday = wd;
  lastTick = millis();
  time_synchronized = true;
  saveTimeSetting();

  sendJsonMessage(200, "success", "Time synchronized");
}

void handleGetStatus() {
  // پنل هر ۱ ثانیه درخواست می‌دهد؛ حد ۱۵۰ms فقط اسپم غیرعادی را رد می‌کند.
  if (!allowRequest(lastStatusRequest, 150UL)) return;

  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", currentHour, currentMinute, currentSecond);

  int logicalState = (digitalRead(RELAY_PIN) == RELAY_ACTIVE_LEVEL) ? 1 : 0;
  int staConnected = (WiFi.status() == WL_CONNECTED) ? 1 : 0;
  int staConfigured = (strlen(sta_ssid) > 0) ? 1 : 0;
  char staIp[16] = "";
  if (staConnected) {
    IPAddress ip = WiFi.localIP();
    snprintf(staIp, sizeof(staIp), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  }

  int staState;
  if (!internet_enabled || !staConfigured) {
    staState = 0;
  } else if (staOffMinutes > 0 && !staCurrentlyOn) {
    staState = 4;
  } else if (staConnected) {
    staEverConnectedThisBoot = true;
    staState = 2;
  } else if (staEverConnectedThisBoot) {
    staState = 3;
  } else {
    staState = 1;
  }

  unsigned long liveOnSeconds = relayTotalOnSeconds;
  if (relayCurrentlyOnForStats) {
    unsigned long elapsed = relayCurrentPeriodElapsedSeconds();
    if (elapsed <= (MAX_RELAY_ON_SECONDS - liveOnSeconds)) liveOnSeconds += elapsed;
  }

  long protectionRemainingSec = 0;
  if (antiShortCycleMinutes > 0 && lastRelayStatState == 0) {
    unsigned long requiredMs = (unsigned long)antiShortCycleMinutes * 60UL * 1000UL;
    unsigned long elapsed = millis() - lastRelayOffMillis;
    if (elapsed < requiredMs) protectionRemainingSec = (long)((requiredMs - elapsed + 999UL) / 1000UL);
  }

  int apStationCount = apCurrentlyOn ? WiFi.softAPgetStationNum() : 0;
  long apRemainingSec = 0;
  if (apCycleEnabled) {
    unsigned long phaseMs = apCurrentlyOn ? ((unsigned long)apOnMinutes * 60000UL) : ((unsigned long)apOffMinutes * 60000UL);
    unsigned long elapsed = millis() - apCycleLastToggleMillis;
    if (elapsed < phaseMs) apRemainingSec = (long)((phaseMs - elapsed + 999UL) / 1000UL);
  }

  long staRemainingSec = 0;
  if (internet_enabled && staConfigured && staOffMinutes > 0) {
    unsigned long phaseMs = staCurrentlyOn ? ((unsigned long)staOnMinutes * 60000UL) : ((unsigned long)staOffMinutes * 60000UL);
    unsigned long elapsed = millis() - staCycleLastToggleMillis;
    if (elapsed < phaseMs) staRemainingSec = (long)((phaseMs - elapsed + 999UL) / 1000UL);
  }

  DynamicJsonDocument doc(2048);
  doc["status"] = "success";
  doc["time"] = timeStr;
  doc["relay"] = logicalState;
  doc["override"] = manual_override;
  doc["sync"] = time_synchronized ? 1 : 0;
  doc["sta"] = staConnected;
  doc["internetEnabled"] = internet_enabled ? 1 : 0;
  doc["staConfigured"] = staConfigured;
  doc["staState"] = staState;
  doc["staIp"] = staIp;
  doc["staOnMinutes"] = staOnMinutes;
  doc["staOffMinutes"] = staOffMinutes;
  doc["staPhaseOn"] = staCurrentlyOn ? 1 : 0;
  doc["staRemaining"] = staRemainingSec;
  doc["ntpOk"] = ntpEverSucceeded ? 1 : 0;
  doc["ntpLastValid"] = ntpLastSuccessValid ? 1 : 0;
  doc["ntpYear"] = ntpLastSuccessYear;
  doc["ntpMonth"] = ntpLastSuccessMonth;
  doc["ntpDay"] = ntpLastSuccessDay;
  doc["ntpHour"] = ntpLastSuccessHour;
  doc["ntpMinute"] = ntpLastSuccessMinute;
  doc["ntpSecond"] = ntpLastSuccessSecond;
  doc["switchCount"] = relaySwitchCount;
  doc["onSeconds"] = liveOnSeconds;
  doc["weekday"] = currentWeekday;
  doc["year"] = currentYear;
  doc["month"] = currentMonth;
  doc["day"] = currentDay;
  doc["protectionMinutes"] = antiShortCycleMinutes;
  doc["protectionRemaining"] = protectionRemainingSec;
  doc["apCycleEnabled"] = apCycleEnabled ? 1 : 0;
  doc["apOn"] = apCurrentlyOn ? 1 : 0;
  doc["apOnMinutes"] = apOnMinutes;
  doc["apOffMinutes"] = apOffMinutes;
  doc["apTxPowerLevel"] = apTxPowerLevel;
  doc["apRemaining"] = apRemainingSec;
  doc["apClientConnected"] = (apStationCount > 0) ? 1 : 0;
  doc["freeHeap"] = ESP.getFreeHeap();

  String out;
  serializeJson(doc, out);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", out);
}

void handleToggleManual() {
  if (!allowRequest(lastToggleManualRequest, 1500UL)) return;

  // برای یک API JSON خالص، body لازم نیست. اگر body ارسال شود، فقط معتبر بودن JSON چک می‌شود.
  if (server.hasArg("plain") && server.arg("plain").length() > 0) {
    StaticJsonDocument<128> ignored;
    if (!parseJsonBody(ignored)) return;
    bool payloadOk = false;
    verifiedPayload(ignored, payloadOk);
    if (!payloadOk) return;
  }

  if (manual_override == 0) {
    manual_override = 1;
    saveOverrideSetting();
    if (time_synchronized) saveTimeSetting();
    delay(100);
    checkScenarios();
  } else {
    manual_override = 0;
    saveOverrideSetting();
    if (time_synchronized) saveTimeSetting();
    delay(100);
    checkScenarios();
  }

  StaticJsonDocument<160> doc;
  doc["status"] = "success";
  doc["override"] = manual_override;
  doc["relay"] = (digitalRead(RELAY_PIN) == RELAY_ACTIVE_LEVEL) ? 1 : 0;
  String out;
  serializeJson(doc, out);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", out);
}

void handleSaveAP() {
  if (!allowRequest(lastSaveApRequest, 3000UL)) return;

  StaticJsonDocument<320> doc;
  if (!parseJsonBody(doc)) return;
  bool payloadOk = false;
  JsonVariantConst payload = verifiedPayload(doc, payloadOk);
  if (!payloadOk) return;

  const char* newSsid = payload["ssid"] | "";
  const char* newPass = payload["pass"] | "";
  String ssidStr = String(newSsid);
  String passStr = String(newPass);

  if (ssidStr.length() == 0 || ssidStr.length() > 31 || passStr.length() < 8 || passStr.length() > 31) {
    sendJsonMessage(400, "error", "Invalid AP SSID or password length");
    return;
  }

  ssidStr.toCharArray(custom_ssid, sizeof(custom_ssid));
  passStr.toCharArray(custom_password, sizeof(custom_password));
  saveWiFiSettings();
  saveTimeSetting();

  sendJsonMessage(200, "success", "AP settings saved; ESP32 will restart");
  pendingReset = true;
  resetMillis = millis();
}

void handleSaveSTA() {
  if (!allowRequest(lastSaveStaRequest, 1500UL)) return;

  StaticJsonDocument<640> doc;
  if (!parseJsonBody(doc)) return;
  bool payloadOk = false;
  JsonVariantConst payload = verifiedPayload(doc, payloadOk);
  if (!payloadOk) return;

  String newStaSsid = String((const char*)(payload["sta_ssid"] | ""));
  String newStaPass = String((const char*)(payload["sta_pass"] | ""));
  bool newInternet = payload["internet"] | false;
  int onVal = payload["sta_on_minutes"] | -1;
  int offVal = payload["sta_off_minutes"] | -1;

  if (newStaSsid.length() > 31 || newStaPass.length() > 63) {
    sendJsonMessage(400, "error", "SSID or password too long");
    return;
  }
  if (newStaPass.length() > 0 && newStaPass.length() < 8) {
    sendJsonMessage(400, "error", "STA password must be empty or at least 8 characters");
    return;
  }
  if (onVal < MIN_STA_ON_MINUTES || onVal > MAX_STA_CYCLE_MINUTES || offVal < MIN_STA_OFF_MINUTES || offVal > MAX_STA_CYCLE_MINUTES) {
    sendJsonMessage(400, "error", "Invalid STA cycle minutes");
    return;
  }

  internet_enabled = newInternet;
  staOnMinutes = onVal;
  staOffMinutes = offVal;
  newStaSsid.toCharArray(sta_ssid, sizeof(sta_ssid));
  newStaPass.toCharArray(sta_password, sizeof(sta_password));
  saveWiFiSettings();

  ntp_synced_this_boot = false;
  ntpFirstCheckPending = true;
  lastNtpCheckMillis = 0;
  staEverConnectedThisBoot = false;

  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false);
  staCurrentlyOn = true;
  staCycleLastToggleMillis = millis();
  connectToInternetWiFi();

  sendJsonMessage(200, "success", "STA settings saved");
}

void handleSaveProtection() {
  if (!allowRequest(lastSaveProtectionRequest, 1000UL)) return;

  StaticJsonDocument<192> doc;
  if (!parseJsonBody(doc)) return;
  bool payloadOk = false;
  JsonVariantConst payload = verifiedPayload(doc, payloadOk);
  if (!payloadOk) return;

  int value = payload["min_off"] | -1;
  if (value < 0 || value > MAX_ANTI_SHORT_CYCLE_MINUTES) {
    sendJsonMessage(400, "error", "Invalid protection minutes");
    return;
  }

  antiShortCycleMinutes = value;
  saveProtectionSettings();
  sendJsonMessage(200, "success", "Protection settings saved");
}

void handleFactoryReset() {
  if (!allowRequest(lastFactoryResetRequest, 5000UL)) return;

  StaticJsonDocument<192> doc;
  if (!parseJsonBody(doc)) return;
  bool payloadOk = false;
  JsonVariantConst payload = verifiedPayload(doc, payloadOk);
  if (!payloadOk) return;

  const char* code = payload["code"] | "";
  if (strlen(code) == 0) code = payload["password"] | "";
  if (String(code) != String(FACTORY_RESET_CODE)) {
    sendJsonMessage(403, "error", "Invalid reset code");
    return;
  }

  // برای ایمنی، قبل از ریست کامل حافظه رله خاموش و حالت دستی پاک می‌شود.
  manual_override = 0;
  setRelay(false);
  relayCurrentlyOnForStats = false;

  // ریست کامل حافظه فایل‌سیستم: سناریوها، زمان، آمار، تنظیمات WiFi، محافظت، override و NTP meta.
  bool ok = LittleFS.format();
  if (!ok) {
    sendJsonMessage(500, "error", "LittleFS format failed");
    return;
  }

  sendJsonMessage(200, "success", "Factory reset done; restarting");

  // مهم: در loop قبل از ریست دیگر saveTimeSetting انجام نشود، وگرنه پس از format دوباره فایل زمان ساخته می‌شود.
  pendingFactoryReset = true;
  pendingReset = true;
  resetMillis = millis();
}

void handleSaveApCycle() {
  if (!allowRequest(lastSaveApCycleRequest, 1000UL)) return;

  StaticJsonDocument<320> doc;
  if (!parseJsonBody(doc)) return;
  bool payloadOk = false;
  JsonVariantConst payload = verifiedPayload(doc, payloadOk);
  if (!payloadOk) return;

  bool enabled = payload["cycle_enabled"] | false;
  int onVal = payload["on_minutes"] | -1;
  int offVal = payload["off_minutes"] | -1;
  int powerVal = payload["tx_power"] | -1;

  if (onVal < MIN_AP_CYCLE_MINUTES || onVal > MAX_AP_CYCLE_MINUTES ||
      offVal < MIN_AP_CYCLE_MINUTES || offVal > MAX_AP_CYCLE_MINUTES ||
      powerVal < 0 || powerVal > MAX_AP_TX_POWER_LEVEL) {
    sendJsonMessage(400, "error", "Invalid AP cycle or TX power value");
    return;
  }

  apCycleEnabled = enabled;
  apOnMinutes = onVal;
  apOffMinutes = offVal;
  apTxPowerLevel = powerVal;
  applyApTxPower();

  apCycleLastToggleMillis = millis();
  if (!apCurrentlyOn) setApRadioState(true);

  saveWiFiSettings();
  sendJsonMessage(200, "success", "AP cycle settings saved");
}
