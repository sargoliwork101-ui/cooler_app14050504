import 'dart:async';
import 'dart:convert';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:google_fonts/google_fonts.dart';
import 'package:http/http.dart' as http;

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const SmartCoolerApp());
}

class SmartCoolerApp extends StatelessWidget {
  const SmartCoolerApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'کولر هوشمند ESP32',
      debugShowCheckedModeBanner: false,
      locale: const Locale('fa', 'IR'),
      theme: ThemeData(
        useMaterial3: true,
        brightness: Brightness.dark,
        textTheme: GoogleFonts.vazirmatnTextTheme(ThemeData.dark().textTheme),
        scaffoldBackgroundColor: AppColors.bg,
        dividerColor: AppColors.line,
        colorScheme: const ColorScheme.dark(
          primary: AppColors.accent,
          secondary: AppColors.cyan,
          surface: AppColors.panel,
          error: AppColors.off,
        ),
      ),
      home: const Directionality(
        textDirection: TextDirection.rtl,
        child: DashboardScreen(),
      ),
    );
  }
}

class AppColors {
  static const bg = Color(0xFF0A0C0D);
  static const panel = Color(0xFF1B1F21);
  static const panel2 = Color(0xFF15181A);
  // دقیقاً نزدیک به CSS پنل وب: خط‌ها باید خیلی کم‌رنگ باشند، نه کادر سفید ضخیم.
  static const line = Color(0x0FFFFFFF);
  static const lineStrong = Color(0x18FFFFFF);
  static const accent = Color(0xFF1DE9C4);
  static const accentDim = Color(0x291DE9C4);
  static const onAccent = Color(0xFF00201B);
  static const on = Color(0xFF1DE9C4);
  static const onDim = Color(0x291DE9C4);
  static const off = Color(0xFFFF5468);
  static const offDim = Color(0x24FF5468);
  static const warn = Color(0xFFF2A93B);
  static const warnDim = Color(0x24F2A93B);
  static const cyan = Color(0xFF29D3C8);
  static const onCyan = Color(0xFF04201D);
  static const live = Color(0xFF3DDC84);
  static const next = Color(0xFFF5D020);
  static const text = Color(0xFFEEF0F1);
  static const dim = Color(0xFF868C90);
  static const faint = Color(0xFF585C5F);
}

class EspApi {
  EspApi(this.baseUrl);
  final String baseUrl;

  Uri uri(String path) {
    final root = baseUrl.endsWith('/') ? baseUrl.substring(0, baseUrl.length - 1) : baseUrl;
    return Uri.parse('$root$path');
  }

  Future<Map<String, dynamic>> getMap(String path) async {
    final res = await http.get(uri(path)).timeout(const Duration(seconds: 3));
    if (res.statusCode < 200 || res.statusCode >= 300) {
      throw Exception('HTTP ${res.statusCode}: ${res.body}');
    }
    final data = jsonDecode(utf8.decode(res.bodyBytes));
    if (data is Map<String, dynamic>) return data;
    return Map<String, dynamic>.from(data as Map);
  }

  Future<Map<String, dynamic>> postJson(String path, Object body) async {
    final res = await http
        .post(
          uri(path),
          headers: const {'Content-Type': 'application/json; charset=utf-8'},
          body: jsonEncode(body),
        )
        .timeout(const Duration(seconds: 4));
    if (res.statusCode < 200 || res.statusCode >= 300) {
      throw Exception('HTTP ${res.statusCode}: ${res.body}');
    }
    final decoded = jsonDecode(utf8.decode(res.bodyBytes));
    if (decoded is Map<String, dynamic>) return decoded;
    return Map<String, dynamic>.from(decoded as Map);
  }
}

class DeviceStatus {
  DeviceStatus(this.raw);
  final Map<String, dynamic> raw;

  String get time => raw['time']?.toString() ?? '--:--:--';
  int get relay => _i('relay');
  int get override => _i('override');
  int get sync => _i('sync');
  int get staState => _i('staState');
  int get internetEnabled => _i('internetEnabled');
  int get staConfigured => _i('staConfigured');
  String get staIp => raw['staIp']?.toString() ?? '';
  int get staOnMinutes => _i('staOnMinutes');
  int get staOffMinutes => _i('staOffMinutes');
  int get staPhaseOn => _i('staPhaseOn');
  int get staRemaining => _i('staRemaining');
  int get ntpLastValid => _i('ntpLastValid');
  int get ntpYear => _i('ntpYear');
  int get ntpMonth => _i('ntpMonth');
  int get ntpDay => _i('ntpDay');
  int get ntpHour => _i('ntpHour');
  int get ntpMinute => _i('ntpMinute');
  int get ntpSecond => _i('ntpSecond');
  int get switchCount => _i('switchCount');
  int get onSeconds => _i('onSeconds');
  int get weekday => _i('weekday');
  int get year => _i('year');
  int get month => _i('month');
  int get day => _i('day');
  int get protectionMinutes => _i('protectionMinutes');
  int get protectionRemaining => _i('protectionRemaining');
  int get apCycleEnabled => _i('apCycleEnabled');
  int get apOn => _i('apOn');
  int get apOnMinutes => _i('apOnMinutes');
  int get apOffMinutes => _i('apOffMinutes');
  int get apTxPowerLevel => _i('apTxPowerLevel');
  int get apRemaining => _i('apRemaining');
  int get apClientConnected => _i('apClientConnected');

  int _i(String key) {
    final v = raw[key];
    if (v is int) return v;
    if (v is num) return v.toInt();
    if (v is bool) return v ? 1 : 0;
    return int.tryParse(v?.toString() ?? '') ?? 0;
  }
}

class ScenarioModel {
  ScenarioModel({
    required this.sh,
    required this.sm,
    required this.eh,
    required this.em,
    this.enabled = true,
    this.weekdays = 127,
  });

  int sh;
  int sm;
  int eh;
  int em;
  bool enabled;
  int weekdays;

  int get startMinutes => sh * 60 + sm;
  int get endMinutes => eh * 60 + em;

  Map<String, dynamic> toJson() => {
        'sh': sh,
        'sm': sm,
        'eh': eh,
        'em': em,
        'en': enabled,
        'wd': weekdays,
      };

  static ScenarioModel fromJson(Map<String, dynamic> j) => ScenarioModel(
        sh: _i(j['sh']),
        sm: _i(j['sm']),
        eh: _i(j['eh']),
        em: _i(j['em']),
        enabled: j['en'] == null ? true : (j['en'] == true || j['en'] == 1),
        weekdays: _i(j['wd'], fallback: 127),
      );

  static int _i(dynamic v, {int fallback = 0}) {
    if (v is int) return v;
    if (v is num) return v.toInt();
    return int.tryParse(v?.toString() ?? '') ?? fallback;
  }
}

class DeviceSettings {
  DeviceSettings.fromJson(Map<String, dynamic> j) {
    programName = j['programName']?.toString() ?? 'کولر هوشمند ESP32';
    programTagline = j['programTagline']?.toString() ?? 'ESP32 · TIMER HUB';
    final ap = Map<String, dynamic>.from(j['ap'] ?? {});
    final sta = Map<String, dynamic>.from(j['sta'] ?? {});
    final protection = Map<String, dynamic>.from(j['protection'] ?? {});
    apSsid = ap['ssid']?.toString() ?? 'ESP32_Timer_Hub';
    apPass = ap['password']?.toString() ?? '';
    apCycleEnabled = ap['cycleEnabled'] == true || ap['cycleEnabled'] == 1;
    apOn = _i(ap['onMinutes'], 10);
    apOff = _i(ap['offMinutes'], 5);
    apPower = _i(ap['txPowerLevel'], 3);
    internet = sta['internetEnabled'] == true || sta['internetEnabled'] == 1;
    staSsid = sta['ssid']?.toString() ?? '';
    staPass = sta['password']?.toString() ?? '';
    staOn = _i(sta['onMinutes'], 10);
    staOff = _i(sta['offMinutes'], 0);
    protectionMin = _i(protection['minOffMinutes'], 3);
    scenarios = (j['scenarios'] is List)
        ? (j['scenarios'] as List)
            .map((e) => ScenarioModel.fromJson(Map<String, dynamic>.from(e as Map)))
            .toList()
        : <ScenarioModel>[];
  }

  late String programName;
  late String programTagline;
  late String apSsid;
  late String apPass;
  late bool apCycleEnabled;
  late int apOn;
  late int apOff;
  late int apPower;
  late bool internet;
  late String staSsid;
  late String staPass;
  late int staOn;
  late int staOff;
  late int protectionMin;
  late List<ScenarioModel> scenarios;

  static int _i(dynamic v, int fallback) {
    if (v is int) return v;
    if (v is num) return v.toInt();
    return int.tryParse(v?.toString() ?? '') ?? fallback;
  }
}

class DashboardScreen extends StatefulWidget {
  const DashboardScreen({super.key});

  @override
  State<DashboardScreen> createState() => _DashboardScreenState();
}

class _DashboardScreenState extends State<DashboardScreen> with SingleTickerProviderStateMixin {
  final baseController = TextEditingController(text: 'http://192.168.4.1');
  late EspApi api;
  Timer? poller;
  DeviceStatus? status;
  DeviceSettings? settings;
  bool connected = false;
  bool loadingSettings = false;
  int pingMs = 0;
  int tab = 0;
  String? lastError;

  final apSsid = TextEditingController();
  final apPass = TextEditingController();
  final apOn = TextEditingController();
  final apOff = TextEditingController();
  final staSsid = TextEditingController();
  final staPass = TextEditingController();
  final staOn = TextEditingController();
  final staOff = TextEditingController();
  final protection = TextEditingController();
  bool apCycleEnabled = false;
  bool internetEnabled = true;
  int txPower = 3;
  List<ScenarioModel> scenarios = [];

  @override
  void initState() {
    super.initState();
    api = EspApi(baseController.text.trim());
    loadSettings();
    fetchStatus();
    poller = Timer.periodic(const Duration(seconds: 1), (_) => fetchStatus());
  }

  @override
  void dispose() {
    poller?.cancel();
    baseController.dispose();
    apSsid.dispose();
    apPass.dispose();
    apOn.dispose();
    apOff.dispose();
    staSsid.dispose();
    staPass.dispose();
    staOn.dispose();
    staOff.dispose();
    protection.dispose();
    super.dispose();
  }

  Future<void> fetchStatus() async {
    final start = DateTime.now();
    try {
      final data = await api.getMap('/status');
      if (!mounted) return;
      setState(() {
        status = DeviceStatus(data);
        pingMs = DateTime.now().difference(start).inMilliseconds;
        connected = true;
        lastError = null;
      });
    } catch (e) {
      if (!mounted) return;
      setState(() {
        connected = false;
        lastError = e.toString();
      });
    }
  }

  Future<void> loadSettings() async {
    setState(() => loadingSettings = true);
    try {
      final data = await api.getMap('/settings');
      final s = DeviceSettings.fromJson(data);
      if (!mounted) return;
      setState(() {
        settings = s;
        apSsid.text = s.apSsid;
        apPass.text = s.apPass;
        apCycleEnabled = s.apCycleEnabled;
        apOn.text = s.apOn.toString();
        apOff.text = s.apOff.toString();
        txPower = s.apPower;
        internetEnabled = s.internet;
        staSsid.text = s.staSsid;
        staPass.text = s.staPass;
        staOn.text = s.staOn.toString();
        staOff.text = s.staOff.toString();
        protection.text = s.protectionMin.toString();
        scenarios = List<ScenarioModel>.from(s.scenarios)..sort((a, b) => a.startMinutes.compareTo(b.startMinutes));
        loadingSettings = false;
      });
    } catch (e) {
      if (!mounted) return;
      setState(() => loadingSettings = false);
      toast('خطا در دریافت تنظیمات: $e', bad: true);
    }
  }

  void applyBaseUrl() {
    final url = baseController.text.trim();
    if (!url.startsWith('http://') && !url.startsWith('https://')) {
      toast('آدرس باید با http:// شروع شود.', bad: true);
      return;
    }
    setState(() {
      api = EspApi(url);
      connected = false;
      status = null;
      settings = null;
    });
    loadSettings();
    fetchStatus();
  }

  Future<void> toggleManual() async {
    try {
      await api.postJson('/toggle-manual', {});
      await fetchStatus();
    } catch (e) {
      toast('خطا در فرمان دستی: $e', bad: true);
    }
  }

  Future<void> syncTime() async {
    final now = DateTime.now();
    int wd;
    if (now.weekday == DateTime.saturday) {
      wd = 0;
    } else if (now.weekday == DateTime.sunday) {
      wd = 1;
    } else {
      wd = now.weekday + 1;
    }
    try {
      await api.postJson('/sync', {
        'h': now.hour,
        'm': now.minute,
        's': now.second,
        'y': now.year,
        'mon': now.month,
        'd': now.day,
        'wd': wd,
      });
      await fetchStatus();
      toast('ساعت داخلی دستگاه با گوشی همگام‌سازی شد.');
    } catch (e) {
      toast('خطا در همگام‌سازی ساعت: $e', bad: true);
    }
  }

  Future<void> saveScenarios() async {
    final error = validateScenarios();
    if (error != null) {
      toast(error, bad: true);
      return;
    }
    final sorted = List<ScenarioModel>.from(scenarios)..sort((a, b) => a.startMinutes.compareTo(b.startMinutes));
    try {
      await api.postJson('/save', sorted.map((s) => s.toJson()).toList());
      setState(() => scenarios = sorted);
      toast('سناریوها ذخیره و پیاده‌سازی شدند.');
      await loadSettings();
      await fetchStatus();
    } catch (e) {
      toast('خطا در ذخیره سناریوها: $e', bad: true);
    }
  }

  String? validateScenarios() {
    if (scenarios.length > 20) return 'حداکثر ۲۰ سناریو مجاز است.';
    final timeMap = List<int?>.filled(1440, null);
    for (int i = 0; i < scenarios.length; i++) {
      final s = scenarios[i];
      if (s.startMinutes == s.endMinutes) return 'زمان روشن و خاموش سناریو ${i + 1} یکسان است.';
      if (s.weekdays < 1 || s.weekdays > 127) return 'حداقل یک روز برای سناریو ${i + 1} انتخاب کنید.';
      if (!s.enabled) continue;
      final mins = <int>[];
      if (s.startMinutes < s.endMinutes) {
        for (int m = s.startMinutes; m < s.endMinutes; m++) mins.add(m);
      } else {
        for (int m = s.startMinutes; m < 1440; m++) mins.add(m);
        for (int m = 0; m < s.endMinutes; m++) mins.add(m);
      }
      for (final m in mins) {
        if (timeMap[m] != null) return 'تداخل زمانی بین سناریوها وجود دارد.';
        timeMap[m] = i;
      }
    }
    return null;
  }

  Future<void> saveAp() async {
    if (apSsid.text.trim().isEmpty || apPass.text.length < 8) {
      toast('SSID نباید خالی و رمز AP باید حداقل ۸ کاراکتر باشد.', bad: true);
      return;
    }
    try {
      await api.postJson('/save-ap', {'ssid': apSsid.text.trim(), 'pass': apPass.text});
      toast('تنظیمات AP ذخیره شد. برد ری‌استارت می‌شود؛ پس از اتصال دوباره آدرس را بررسی کنید.');
    } catch (e) {
      toast('خطا در ذخیره AP: $e', bad: true);
    }
  }

  Future<void> saveSta() async {
    final on = int.tryParse(staOn.text.trim());
    final off = int.tryParse(staOff.text.trim());
    if (staPass.text.isNotEmpty && staPass.text.length < 8) {
      toast('رمز مودم اگر وارد شود باید حداقل ۸ کاراکتر باشد.', bad: true);
      return;
    }
    if (on == null || on < 1 || on > 1440 || off == null || off < 0 || off > 1440) {
      toast('زمان‌های چرخه STA معتبر نیستند.', bad: true);
      return;
    }
    try {
      await api.postJson('/save-sta', {
        'internet': internetEnabled,
        'sta_ssid': staSsid.text.trim(),
        'sta_pass': staPass.text,
        'sta_on_minutes': on,
        'sta_off_minutes': off,
      });
      toast('تنظیمات مودم/اینترنت ذخیره شد.');
      await loadSettings();
    } catch (e) {
      toast('خطا در ذخیره STA: $e', bad: true);
    }
  }

  Future<void> saveApCycle() async {
    final on = int.tryParse(apOn.text.trim());
    final off = int.tryParse(apOff.text.trim());
    if (on == null || on < 1 || on > 1440 || off == null || off < 1 || off > 1440 || txPower < 0 || txPower > 3) {
      toast('تنظیمات چرخه AP یا قدرت سیگنال معتبر نیست.', bad: true);
      return;
    }
    try {
      await api.postJson('/save-ap-cycle', {
        'cycle_enabled': apCycleEnabled,
        'on_minutes': on,
        'off_minutes': off,
        'tx_power': txPower,
      });
      toast('تنظیمات چرخه AP ذخیره شد.');
      await loadSettings();
    } catch (e) {
      toast('خطا در ذخیره چرخه AP: $e', bad: true);
    }
  }

  Future<void> saveProtection() async {
    final min = int.tryParse(protection.text.trim());
    if (min == null || min < 0 || min > 1440) {
      toast('زمان محافظت باید بین ۰ تا ۱۴۴۰ دقیقه باشد.', bad: true);
      return;
    }
    try {
      await api.postJson('/save-protection', {'min_off': min});
      toast('محافظت کمپرسور ذخیره شد.');
      await fetchStatus();
    } catch (e) {
      toast('خطا در ذخیره محافظت: $e', bad: true);
    }
  }

  void toast(String msg, {bool bad = false}) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text(msg, textDirection: TextDirection.rtl),
        backgroundColor: bad ? AppColors.off : AppColors.panel,
        behavior: SnackBarBehavior.floating,
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(14)),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final title = settings?.programName ?? 'کولر هوشمند ESP32';
    final sub = settings?.programTagline ?? 'ESP32 · TIMER HUB';
    return Scaffold(
      body: SafeArea(
        child: Stack(
          children: [
            Column(
              children: [
                TopBar(title: title, subtitle: sub, connected: connected, pingMs: pingMs),
                Expanded(
                  child: RefreshIndicator(
                    color: AppColors.accent,
                    onRefresh: () async {
                      await fetchStatus();
                      if (tab == 2 || settings == null) await loadSettings();
                    },
                    child: ListView(
                      padding: const EdgeInsets.fromLTRB(18, 20, 18, 112),
                      children: [
                        if (tab == 0) homePage(),
                        if (tab == 1) scenariosPage(),
                        if (tab == 2) settingsPage(),
                      ],
                    ),
                  ),
                ),
              ],
            ),
            Positioned(
              left: 22,
              right: 22,
              bottom: 16,
              child: OrbitNav(
                index: tab,
                onChanged: (i) {
                  setState(() => tab = i);
                  if (i == 2) loadSettings();
                },
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget homePage() {
    final s = status;
    final relayOn = connected && s?.relay == 1;
    final manual = s?.override == 1;
    return Column(
      children: [
        NeuPanel(
          child: Column(
            children: [
              CoolerHero(on: relayOn, disconnected: !connected),
              Text(
                !connected ? 'ارتباط با برد قطع شده است' : (relayOn ? 'کولر روشن است' : 'کولر خاموش است'),
                style: TextStyle(fontWeight: FontWeight.w900, fontSize: 16, color: !connected ? AppColors.off : AppColors.text),
              ),
              const SizedBox(height: 5),
              Text(
                !connected
                    ? (lastError ?? 'در حال تلاش برای اتصال...')
                    : (s?.protectionRemaining ?? 0) > 0
                        ? 'محافظت کمپرسور: ${((s!.protectionRemaining) / 60).ceil()} دقیقه تا روشن‌شدن'
                        : manual
                            ? 'حالت: دستی (روشن)'
                            : 'حالت: خودکار (سناریو)',
                textAlign: TextAlign.center,
                style: const TextStyle(color: AppColors.dim, fontSize: 12),
              ),
              const SizedBox(height: 14),
              WideButton(
                label: manual ? 'خاموش کردن دستی کولر' : 'روشن کردن دستی کولر',
                foreground: manual ? AppColors.off : AppColors.on,
                background: manual ? AppColors.offDim : AppColors.onDim,
                onPressed: connected ? toggleManual : null,
              ),
            ],
          ),
        ),
        clockPanel(),
        internetPanel(),
        healthPanel(),
      ],
    );
  }

  Widget clockPanel() {
    final s = status;
    final dayNames = ['شنبه', 'یکشنبه', 'دوشنبه', 'سه‌شنبه', 'چهارشنبه', 'پنجشنبه', 'جمعه'];
    final date = s == null ? '--' : '${dayNames[s.weekday.clamp(0, 6).toInt()]} — ${formatJalaliDate(s.year, s.month, s.day)}';
    return NeuPanel(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const PanelLabel('ساعت داخلی برد'),
          Row(
            children: [
              Expanded(
                child: Text(s?.time ?? '--:--:--', textAlign: TextAlign.center, style: const TextStyle(color: AppColors.cyan, fontSize: 24, fontWeight: FontWeight.w800, letterSpacing: 1.2)),
              ),
              SmallButton(label: 'همگام‌سازی', onPressed: connected ? syncTime : null),
            ],
          ),
          const SizedBox(height: 6),
          Text('امروز: $date', textAlign: TextAlign.center, style: const TextStyle(color: AppColors.dim, fontSize: 12)),
          const SizedBox(height: 10),
          Text(
            s?.sync == 1 ? 'ساعت همگام‌سازی شده و سناریوها فعال هستند.' : '⚠️ ساعت برد همگام نیست! سناریوها تا زمان همگام‌سازی اجرا نمی‌شوند.',
            style: TextStyle(color: s?.sync == 1 ? AppColors.on : AppColors.off, fontSize: 12, height: 1.7),
          ),
        ],
      ),
    );
  }

  Widget internetPanel() {
    final s = status;
    final state = staStateText(s);
    final cycle = staCycleText(s);
    return NeuPanel(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const PanelLabel('اتصال اینترنت'),
          StatusLine(label: 'وضعیت اتصال مودم (STA)', value: state.$1, color: state.$2),
          const SizedBox(height: 8),
          StatusLine(label: 'چرخه اتصال STA', value: cycle.$1, color: cycle.$2),
          const SizedBox(height: 8),
          StatusLine(label: 'وضعیت اینترنت/NTP', value: internetModeText(s).$1, color: internetModeText(s).$2),
          const SizedBox(height: 8),
          StatusLine(label: 'آخرین دریافت موفق ساعت', value: formatNtpSuccessStamp(s), color: s?.ntpLastValid == 1 ? AppColors.live : AppColors.warn),
        ],
      ),
    );
  }

  Widget healthPanel() {
    final s = status;
    return NeuPanel(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const PanelLabel('سلامت موتور'),
          Row(
            children: [
              Expanded(child: StatBox(value: s == null ? '--' : '${s.switchCount} بار', label: 'تعداد روشن‌شدن')),
              const SizedBox(width: 8),
              Expanded(child: StatBox(value: s == null ? '--' : formatDuration(s.onSeconds), label: 'مجموع مدت کارکرد')),
            ],
          ),
          const SizedBox(height: 12),
          const Text('برای سلامت و عمر بیشتر کمپرسور، بهتر است بین هر خاموش و روشن شدن حداقل چند دقیقه فاصله باشد.', style: TextStyle(color: AppColors.dim, fontSize: 12, height: 1.8)),
        ],
      ),
    );
  }

  Widget scenariosPage() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        if (status?.override == 1)
          const BannerBox(text: 'تنظیمات سناریو در حالت دستی کولر غیرفعال است؛ پس از خاموش کردن حالت دستی فعال می‌شود.', danger: true),
        const BannerBox(text: 'تا زمانی که دکمهٔ «ذخیره و پیاده‌سازی برنامه‌ها» را نزنید، سناریوها اجرا نمی‌شوند.'),
        Row(
          children: [
            Expanded(child: WideButton(label: '↓ پشتیبان‌گیری سناریوها', foreground: AppColors.accent, background: AppColors.accentDim, onPressed: exportScenariosToFile)),
            const SizedBox(width: 8),
            Expanded(child: WideButton(label: '↑ بازیابی از فایل', foreground: AppColors.onCyan, background: AppColors.cyan, onPressed: importScenariosFromFile)),
          ],
        ),
        const SizedBox(height: 8),
        const Text('فایل پشتیبان با فرمت JSON از حافظه گوشی انتخاب یا در گوشی ذخیره می‌شود؛ کلیپ‌بورد استفاده نمی‌شود.', textAlign: TextAlign.center, style: TextStyle(color: AppColors.faint, fontSize: 11)),
        const SizedBox(height: 14),
        NeuPanel(
          child: Column(
            children: [
              Row(
                children: [
                  const Expanded(child: PanelLabel('برنامه‌ریزی سناریوها')),
                  IconButton.filled(
                    style: IconButton.styleFrom(backgroundColor: AppColors.accentDim, foregroundColor: AppColors.accent, side: const BorderSide(color: AppColors.accent)),
                    onPressed: scenarios.length >= 20
                        ? null
                        : () => setState(() => scenarios.add(ScenarioModel(sh: 0, sm: 0, eh: 1, em: 0))),
                    icon: const Icon(Icons.add),
                    tooltip: 'افزودن سناریو',
                  ),
                ],
              ),
              if (loadingSettings) const Padding(padding: EdgeInsets.all(18), child: CircularProgressIndicator(color: AppColors.accent)),
              for (int i = 0; i < scenarios.length; i++)
                ScenarioCard(
                  index: i,
                  scenario: scenarios[i],
                  currentStatus: status,
                  onChanged: () => setState(() {}),
                  onRemove: () => setState(() => scenarios.removeAt(i)),
                ),
              const SizedBox(height: 4),
              WideButton(label: 'ذخیره و پیاده‌سازی برنامه‌ها', foreground: AppColors.onAccent, background: AppColors.accent, onPressed: connected ? saveScenarios : null),
            ],
          ),
        ),
      ],
    );
  }

  Future<void> exportScenariosToFile() async {
    final now = DateTime.now();
    final stamp = '${now.year}-${pad2(now.month)}-${pad2(now.day)}';
    final backup = {
      'version': 1,
      'type': 'esp32-cooler-scenarios',
      'exportedAt': now.toIso8601String(),
      'scenarios': scenarios.map((s) => s.toJson()).toList(),
    };
    final jsonText = const JsonEncoder.withIndent('  ').convert(backup);
    try {
      final savedPath = await FilePicker.platform.saveFile(
        dialogTitle: 'ذخیره فایل پشتیبان سناریوها',
        fileName: 'cooler-scenarios-$stamp.json',
        type: FileType.custom,
        allowedExtensions: const ['json'],
        bytes: Uint8List.fromList(utf8.encode(jsonText)),
      );
      if (savedPath == null) return;
      toast('فایل پشتیبان سناریوها در گوشی ذخیره شد.');
    } catch (e) {
      toast('خطا در ذخیره فایل پشتیبان: $e', bad: true);
    }
  }

  Future<void> importScenariosFromFile() async {
    try {
      final result = await FilePicker.platform.pickFiles(
        dialogTitle: 'انتخاب فایل پشتیبان سناریوها',
        type: FileType.custom,
        allowedExtensions: const ['json'],
        withData: true,
      );
      if (result == null || result.files.isEmpty) return;
      final bytes = result.files.single.bytes;
      if (bytes == null) throw Exception('فایل خوانده نشد');
      final parsed = jsonDecode(utf8.decode(bytes));
      final list = parsed is List ? parsed : parsed['scenarios'];
      if (list is! List || list.length > 20) throw Exception('فرمت نامعتبر');
      final imported = list.map((e) => ScenarioModel.fromJson(Map<String, dynamic>.from(e as Map))).toList();
      setState(() => scenarios = imported);
      toast('سناریوها از فایل خوانده شدند. برای اعمال روی برد، ذخیره را بزنید.');
    } catch (_) {
      toast('فایل پشتیبان معتبر نیست یا با این برنامه سازگار نیست.', bad: true);
    }
  }

  Widget settingsPage() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        NeuPanel(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const PanelLabel('آدرس دستگاه'),
              const Text('برای اتصال مستقیم AP معمولاً http://192.168.4.1 است؛ اگر برد به مودم وصل است، IP حالت Station را وارد کنید.', style: TextStyle(color: AppColors.dim, fontSize: 12, height: 1.7)),
              const SizedBox(height: 10),
              AppTextField(controller: baseController, label: 'Base URL', ltr: true),
              WideButton(label: 'اتصال به این آدرس', foreground: AppColors.onAccent, background: AppColors.accent, onPressed: applyBaseUrl),
            ],
          ),
        ),
        NeuPanel(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const PanelLabel('تنظیمات فرستنده (AP)'),
              const Text('این شبکه برای اتصال مستقیم گوشی به خود برد است. ذخیره این بخش باعث ری‌استارت برد می‌شود.', style: TextStyle(color: AppColors.dim, fontSize: 12, height: 1.7)),
              AppTextField(controller: apSsid, label: 'نام شبکه (SSID)', ltr: true),
              AppTextField(controller: apPass, label: 'گذرواژه', obscure: true, ltr: true),
              WideButton(label: 'ذخیره تنظیمات فرستنده (ری‌استارت می‌شود)', foreground: AppColors.onCyan, background: AppColors.cyan, onPressed: connected ? saveAp : null),
            ],
          ),
        ),
        NeuPanel(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const PanelLabel('چرخه روشن/خاموش فرستنده (AP)'),
              SwitchListTile(
                value: apCycleEnabled,
                activeColor: AppColors.on,
                contentPadding: EdgeInsets.zero,
                title: const Text('فعال‌سازی چرخه دوره‌ای'),
                onChanged: (v) => setState(() => apCycleEnabled = v),
              ),
              const Text('وقتی فعال باشد، فرستنده وای‌فای برد به‌صورت دوره‌ای خاموش و روشن می‌شود؛ تا وقتی گوشی به AP وصل است چرخه متوقف می‌ماند.', style: TextStyle(color: AppColors.dim, fontSize: 12, height: 1.7)),
              Row(children: [Expanded(child: AppTextField(controller: apOn, label: 'روشن بودن AP / دقیقه', number: true)), const SizedBox(width: 8), Expanded(child: AppTextField(controller: apOff, label: 'خاموش بودن AP / دقیقه', number: true))]),
              const SizedBox(height: 8),
              const Text('قدرت سیگنال‌دهی', style: TextStyle(color: AppColors.dim, fontSize: 12)),
              const SizedBox(height: 7),
              SegmentedButton<int>(
                style: ButtonStyle(
                  side: MaterialStateProperty.all(const BorderSide(color: AppColors.lineStrong, width: 1)),
                  backgroundColor: MaterialStateProperty.resolveWith((states) => states.contains(MaterialState.selected) ? AppColors.accentDim : AppColors.panel2),
                  foregroundColor: MaterialStateProperty.resolveWith((states) => states.contains(MaterialState.selected) ? AppColors.accent : AppColors.dim),
                ),
                segments: const [
                  ButtonSegment(value: 0, label: Text('کم')),
                  ButtonSegment(value: 1, label: Text('متوسط')),
                  ButtonSegment(value: 2, label: Text('زیاد')),
                  ButtonSegment(value: 3, label: Text('حداکثر')),
                ],
                selected: {txPower},
                onSelectionChanged: (s) => setState(() => txPower = s.first),
              ),
              const SizedBox(height: 12),
              StatusLine(label: 'وضعیت چرخه', value: apCycleText(status), color: AppColors.dim),
              const SizedBox(height: 12),
              WideButton(label: 'ذخیره تنظیمات چرخه AP', foreground: AppColors.onCyan, background: AppColors.cyan, onPressed: connected ? saveApCycle : null),
            ],
          ),
        ),
        NeuPanel(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const PanelLabel('اتصال به مودم اینترنت'),
              SwitchListTile(
                value: internetEnabled,
                activeColor: AppColors.on,
                contentPadding: EdgeInsets.zero,
                title: const Text('فعال‌سازی اینترنت / NTP'),
                onChanged: (v) => setState(() => internetEnabled = v),
              ),
              const Text('وقتی روشن باشد، برد از مودم/روتر برای اینترنت و دریافت خودکار ساعت استفاده می‌کند. اگر خاموشی STA را ۰ بگذارید، اتصال دائماً روشن می‌ماند.', style: TextStyle(color: AppColors.dim, fontSize: 12, height: 1.7)),
              AppTextField(controller: staSsid, label: 'SSID مودم/روتر اینترنت', ltr: true),
              AppTextField(controller: staPass, label: 'رمز وای‌فای مودم/روتر', obscure: true, ltr: true),
              Row(children: [Expanded(child: AppTextField(controller: staOn, label: 'روشن بودن STA / دقیقه', number: true)), const SizedBox(width: 8), Expanded(child: AppTextField(controller: staOff, label: 'خاموش بودن STA / دقیقه', number: true))]),
              const SizedBox(height: 10),
              StatusLine(label: 'وضعیت STA', value: staStateText(status).$1, color: staStateText(status).$2),
              const SizedBox(height: 6),
              StatusLine(label: 'وضعیت چرخه STA', value: staCycleText(status).$1, color: staCycleText(status).$2),
              const SizedBox(height: 12),
              WideButton(label: 'ذخیره تنظیمات مودم / اینترنت', foreground: AppColors.onCyan, background: AppColors.cyan, onPressed: connected ? saveSta : null),
            ],
          ),
        ),
        NeuPanel(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const PanelLabel('محافظت کمپرسور'),
              const Text('حداقل مدت خاموش‌بودن کولر پیش از روشن‌شدن دوباره. مقدار ۰ یعنی غیرفعال.', style: TextStyle(color: AppColors.dim, fontSize: 12, height: 1.7)),
              AppTextField(controller: protection, label: 'حداقل فاصله خاموش تا روشن / دقیقه', number: true),
              StatusLine(label: 'وضعیت محافظت', value: protectionText(status), color: (status?.protectionRemaining ?? 0) > 0 ? AppColors.warn : AppColors.live),
              const SizedBox(height: 12),
              WideButton(label: 'ذخیره تنظیمات محافظت', foreground: AppColors.onCyan, background: AppColors.cyan, onPressed: connected ? saveProtection : null),
            ],
          ),
        ),
      ],
    );
  }
}

class TopBar extends StatelessWidget {
  const TopBar({super.key, required this.title, required this.subtitle, required this.connected, required this.pingMs});
  final String title;
  final String subtitle;
  final bool connected;
  final int pingMs;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 16),
      decoration: const BoxDecoration(color: AppColors.bg, border: Border(bottom: BorderSide(color: AppColors.line))),
      child: Row(
        children: [
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(title, style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w900)),
                Text(subtitle, style: const TextStyle(fontSize: 10, color: AppColors.faint, letterSpacing: .5)),
              ],
            ),
          ),
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 7),
            decoration: BoxDecoration(
              borderRadius: BorderRadius.circular(999),
              color: connected ? AppColors.onDim : AppColors.offDim,
            ),
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Container(width: 7, height: 7, decoration: BoxDecoration(shape: BoxShape.circle, color: connected ? AppColors.on : AppColors.off, boxShadow: [BoxShadow(color: connected ? AppColors.on : AppColors.off, blurRadius: 8)])),
                const SizedBox(width: 6),
                Text(connected ? 'متصل · ${pingMs}ms' : 'قطع ارتباط!', style: TextStyle(color: connected ? AppColors.on : AppColors.off, fontWeight: FontWeight.w800, fontSize: 11)),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class NeuPanel extends StatelessWidget {
  const NeuPanel({super.key, required this.child});
  final Widget child;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      margin: const EdgeInsets.only(bottom: 14),
      padding: const EdgeInsets.all(18),
      decoration: BoxDecoration(
        color: AppColors.panel,
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: AppColors.line),
        boxShadow: const [BoxShadow(color: Colors.black54, offset: Offset(7, 7), blurRadius: 15), BoxShadow(color: Color(0x08FFFFFF), offset: Offset(-5, -5), blurRadius: 11)],
      ),
      child: Stack(
        children: [
          Positioned(top: -8, right: -8, child: _rivet()),
          Positioned(top: -8, left: -8, child: _rivet()),
          child,
        ],
      ),
    );
  }

  Widget _rivet() => Container(width: 4, height: 4, decoration: const BoxDecoration(shape: BoxShape.circle, color: AppColors.lineStrong));
}

class PanelLabel extends StatelessWidget {
  const PanelLabel(this.text, {super.key});
  final String text;
  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 14),
      child: Row(
        children: [
          Container(width: 3, height: 14, decoration: BoxDecoration(color: AppColors.accent, borderRadius: BorderRadius.circular(2))),
          const SizedBox(width: 8),
          Text(text, style: const TextStyle(color: AppColors.dim, fontSize: 13, fontWeight: FontWeight.w800)),
        ],
      ),
    );
  }
}

class WideButton extends StatelessWidget {
  const WideButton({super.key, required this.label, required this.foreground, required this.background, required this.onPressed});
  final String label;
  final Color foreground;
  final Color background;
  final VoidCallback? onPressed;
  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: double.infinity,
      child: ElevatedButton(
        style: ElevatedButton.styleFrom(
          elevation: 0,
          backgroundColor: background,
          foregroundColor: foreground,
          disabledBackgroundColor: AppColors.panel2,
          disabledForegroundColor: AppColors.faint,
          padding: const EdgeInsets.symmetric(vertical: 14, horizontal: 10),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12), side: BorderSide(color: foreground.withOpacity(.25))),
        ),
        onPressed: onPressed,
        child: Text(label, textAlign: TextAlign.center, style: const TextStyle(fontWeight: FontWeight.w900, fontSize: 13)),
      ),
    );
  }
}

class SmallButton extends StatelessWidget {
  const SmallButton({super.key, required this.label, required this.onPressed});
  final String label;
  final VoidCallback? onPressed;
  @override
  Widget build(BuildContext context) {
    return OutlinedButton(
      style: OutlinedButton.styleFrom(foregroundColor: AppColors.text, side: const BorderSide(color: AppColors.lineStrong), padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10), shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(9))),
      onPressed: onPressed,
      child: Text(label, style: const TextStyle(fontWeight: FontWeight.w800, fontSize: 12)),
    );
  }
}

class StatBox extends StatelessWidget {
  const StatBox({super.key, required this.value, required this.label});
  final String value;
  final String label;
  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(vertical: 12, horizontal: 8),
      decoration: BoxDecoration(color: AppColors.panel2, borderRadius: BorderRadius.circular(9), boxShadow: const [BoxShadow(color: Colors.black38, offset: Offset(4, 4), blurRadius: 10), BoxShadow(color: Color(0x08FFFFFF), offset: Offset(-3, -3), blurRadius: 8)]),
      child: Column(children: [Text(value, textAlign: TextAlign.center, style: const TextStyle(color: AppColors.accent, fontSize: 16, fontWeight: FontWeight.w900)), const SizedBox(height: 4), Text(label, textAlign: TextAlign.center, style: const TextStyle(color: AppColors.dim, fontSize: 11))]),
    );
  }
}

class StatusLine extends StatelessWidget {
  const StatusLine({super.key, required this.label, required this.value, required this.color});
  final String label;
  final String value;
  final Color color;
  @override
  Widget build(BuildContext context) {
    return RichText(
      textDirection: TextDirection.rtl,
      text: TextSpan(
        style: const TextStyle(color: AppColors.dim, fontSize: 12, height: 1.65),
        children: [TextSpan(text: '$label: '), TextSpan(text: value, style: TextStyle(color: color, fontWeight: FontWeight.w800))],
      ),
    );
  }
}

class BannerBox extends StatelessWidget {
  const BannerBox({super.key, required this.text, this.danger = false});
  final String text;
  final bool danger;
  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 12),
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
      decoration: BoxDecoration(color: danger ? AppColors.offDim : AppColors.warnDim, border: Border.all(color: danger ? AppColors.off : AppColors.warn), borderRadius: BorderRadius.circular(10)),
      child: Text(text, textAlign: TextAlign.center, style: TextStyle(color: danger ? AppColors.off : AppColors.warn, fontWeight: FontWeight.w800, fontSize: 12, height: 1.7)),
    );
  }
}

class AppTextField extends StatelessWidget {
  const AppTextField({super.key, required this.controller, required this.label, this.obscure = false, this.number = false, this.ltr = false});
  final TextEditingController controller;
  final String label;
  final bool obscure;
  final bool number;
  final bool ltr;
  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 15),
      child: TextField(
        controller: controller,
        obscureText: obscure,
        keyboardType: number ? TextInputType.number : TextInputType.text,
        inputFormatters: number ? [FilteringTextInputFormatter.digitsOnly] : null,
        textDirection: ltr ? TextDirection.ltr : TextDirection.rtl,
        textAlign: ltr ? TextAlign.left : TextAlign.right,
        style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w600),
        decoration: InputDecoration(
          labelText: label,
          labelStyle: const TextStyle(color: AppColors.dim, fontSize: 12),
          filled: true,
          fillColor: AppColors.panel2,
          border: OutlineInputBorder(borderRadius: BorderRadius.circular(9), borderSide: const BorderSide(color: AppColors.lineStrong)),
          enabledBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(9), borderSide: const BorderSide(color: AppColors.lineStrong)),
          focusedBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(9), borderSide: const BorderSide(color: AppColors.accent)),
        ),
      ),
    );
  }
}

class CoolerHero extends StatefulWidget {
  const CoolerHero({super.key, required this.on, required this.disconnected});
  final bool on;
  final bool disconnected;
  @override
  State<CoolerHero> createState() => _CoolerHeroState();
}

class _CoolerHeroState extends State<CoolerHero> with SingleTickerProviderStateMixin {
  late final AnimationController controller = AnimationController(vsync: this, duration: const Duration(milliseconds: 1150))..repeat();

  @override
  void dispose() {
    controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (widget.on && !controller.isAnimating) controller.repeat();
    if (!widget.on && controller.isAnimating) controller.stop();
    return SizedBox(
      width: 156,
      height: 156,
      child: AnimatedBuilder(
        animation: controller,
        builder: (_, __) => CustomPaint(
          painter: CoolerPainter(progress: controller.value, on: widget.on, disconnected: widget.disconnected),
        ),
      ),
    );
  }
}

class CoolerPainter extends CustomPainter {
  CoolerPainter({required this.progress, required this.on, required this.disconnected});
  final double progress;
  final bool on;
  final bool disconnected;

  @override
  void paint(Canvas canvas, Size size) {
    final c = Offset(size.width / 2, size.height / 2);
    final active = on && !disconnected;
    final accent = disconnected ? AppColors.off : (active ? AppColors.on : AppColors.faint);
    final ring = Paint()..style = PaintingStyle.stroke..strokeWidth = 5..color = AppColors.lineStrong;
    canvas.drawCircle(c, 68, ring);
    final arcPaint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeCap = StrokeCap.round
      ..strokeWidth = 5
      ..color = accent;
    if (active) arcPaint.maskFilter = const MaskFilter.blur(BlurStyle.normal, 4);
    canvas.drawArc(
      Rect.fromCircle(center: c, radius: 68),
      -math.pi / 2,
      active ? math.pi * 1.7 : math.pi * 1.2,
      false,
      arcPaint,
    );

    canvas.save();
    canvas.translate(c.dx - 73, c.dy - 72);
    final bodyPaint = Paint()..color = active ? const Color(0xFF222A2D) : const Color(0xFF202426);
    final stroke = Paint()..style = PaintingStyle.stroke..strokeWidth = 1.3..color = active ? const Color(0xFF506065) : const Color(0xFF3B4245);
    final body = RRect.fromRectAndRadius(const Rect.fromLTWH(33, 20, 76, 104), const Radius.circular(14));
    canvas.drawRRect(body, bodyPaint);
    canvas.drawRRect(body, stroke);
    final top = Path()..moveTo(43, 20)..lineTo(99, 20)..lineTo(93, 12)..lineTo(49, 12)..close();
    canvas.drawPath(top, Paint()..color = const Color(0xFF303C40));
    canvas.drawPath(top, Paint()..style = PaintingStyle.stroke..strokeWidth = 1..color = const Color(0xFF647277));
    final grille = RRect.fromRectAndRadius(const Rect.fromLTWH(41, 37, 60, 68), const Radius.circular(10));
    canvas.drawRRect(grille, Paint()..color = const Color(0xFF111719));
    canvas.drawRRect(grille, Paint()..style = PaintingStyle.stroke..strokeWidth = 1.4..color = const Color(0xFF5C6A6D));
    final linePaint = Paint()..style = PaintingStyle.stroke..strokeWidth = 1.2..strokeCap = StrokeCap.round..color = const Color(0xDD526166);
    for (final double y in <double>[48.0, 58.0, 68.0, 78.0, 88.0, 98.0]) {
      canvas.drawLine(
        Offset(y == 98.0 ? 50.0 : 46.0, y),
        Offset(y == 98.0 ? 92.0 : 96.0, y),
        linePaint,
      );
    }

    canvas.save();
    canvas.translate(73, 72);
    if (active) canvas.rotate(progress * math.pi * 2);
    final bladePaint = Paint()..color = accent;
    for (int i = 0; i < 4; i++) {
      canvas.save();
      canvas.rotate(i * math.pi / 2);
      final blade = Path()..moveTo(0, -2)..cubicTo(-5, -22, 7, -30, 11, -18)..cubicTo(14, -10, 7, -4, 2, 1)..close();
      canvas.drawPath(blade, bladePaint);
      canvas.restore();
    }
    canvas.drawCircle(Offset.zero, 6, Paint()..color = active ? const Color(0xFFB9FFF2) : AppColors.faint);
    canvas.drawCircle(Offset.zero, 6, Paint()..style = PaintingStyle.stroke..strokeWidth = 2..color = const Color(0xFF0D8E78));
    canvas.restore();

    canvas.drawRRect(RRect.fromRectAndRadius(const Rect.fromLTWH(51, 111, 40, 4), const Radius.circular(2)), Paint()..color = const Color(0xFF101718));
    canvas.drawCircle(const Offset(73, 27), 2.6, Paint()..color = accent);

    if (active) {
      final airPaint = Paint()..style = PaintingStyle.stroke..strokeCap = StrokeCap.round..strokeWidth = 2.1..color = accent.withOpacity(.35 + .65 * (math.sin(progress * math.pi * 2).abs()));
      for (final double y in <double>[48.0, 61.0, 74.0]) {
        final path = Path()
          ..moveTo(108.0, y)
          ..cubicTo(121.0, y - 5.0, 125.0, y + 2.0, 135.0, y - 4.0);
        canvas.drawPath(path, airPaint);
      }
    }
    canvas.restore();
  }

  @override
  bool shouldRepaint(covariant CoolerPainter oldDelegate) => oldDelegate.progress != progress || oldDelegate.on != on || oldDelegate.disconnected != disconnected;
}

class ScenarioCard extends StatelessWidget {
  const ScenarioCard({super.key, required this.index, required this.scenario, required this.currentStatus, required this.onChanged, required this.onRemove});
  final int index;
  final ScenarioModel scenario;
  final DeviceStatus? currentStatus;
  final VoidCallback onChanged;
  final VoidCallback onRemove;

  bool get running {
    final s = currentStatus;
    if (s == null || s.sync != 1 || !scenario.enabled) return false;
    final parts = s.time.split(':');
    if (parts.length < 2) return false;
    final cur = (int.tryParse(parts[0]) ?? 0) * 60 + (int.tryParse(parts[1]) ?? 0);
    var scenarioDay = s.weekday;
    if (scenario.startMinutes > scenario.endMinutes && cur < scenario.endMinutes) scenarioDay = (scenarioDay + 6) % 7;
    if ((scenario.weekdays & (1 << scenarioDay)) == 0) return false;
    if (scenario.startMinutes < scenario.endMinutes) return cur >= scenario.startMinutes && cur < scenario.endMinutes;
    return cur >= scenario.startMinutes || cur < scenario.endMinutes;
  }

  @override
  Widget build(BuildContext context) {
    final border = running ? AppColors.live : AppColors.lineStrong;
    return Container(
      margin: const EdgeInsets.only(bottom: 9),
      padding: const EdgeInsets.all(13),
      decoration: BoxDecoration(
        color: running ? const Color(0x243DDC84) : AppColors.panel2,
        borderRadius: BorderRadius.circular(12),
        border: Border(right: BorderSide(color: border, width: 3), top: const BorderSide(color: AppColors.line), bottom: const BorderSide(color: AppColors.line), left: const BorderSide(color: AppColors.line)),
        boxShadow: const [BoxShadow(color: Colors.black38, offset: Offset(4, 4), blurRadius: 10), BoxShadow(color: Color(0x08FFFFFF), offset: Offset(-3, -3), blurRadius: 8)],
      ),
      child: Opacity(
        opacity: scenario.enabled ? 1 : .42,
        child: Column(
          children: [
            Row(
              children: [
                Expanded(child: Wrap(spacing: 7, children: [Text('سناریو ${index + 1}', style: const TextStyle(fontWeight: FontWeight.w900, fontSize: 13)), if (running) _badge('در حال اجرا', AppColors.live)])),
                Switch(value: scenario.enabled, activeColor: AppColors.on, onChanged: (v) { scenario.enabled = v; onChanged(); }),
                IconButton(onPressed: onRemove, icon: const Icon(Icons.close, size: 18), color: AppColors.dim, tooltip: 'حذف سناریو'),
              ],
            ),
            Row(
              children: [
                Expanded(child: TimeBox(label: 'روشن', hour: scenario.sh, minute: scenario.sm, onTap: () => pickTime(context, true))),
                const SizedBox(width: 8),
                Expanded(child: TimeBox(label: 'خاموش', hour: scenario.eh, minute: scenario.em, onTap: () => pickTime(context, false))),
              ],
            ),
            const SizedBox(height: 11),
            const Align(alignment: Alignment.centerRight, child: Text('روزهای اجرا', style: TextStyle(color: AppColors.faint, fontSize: 11))),
            const SizedBox(height: 6),
            Row(
              children: List.generate(7, (d) {
                const names = ['ش', 'ی', 'د', 'س', 'چ', 'پ', 'ج'];
                final selected = (scenario.weekdays & (1 << d)) != 0;
                return Expanded(
                  child: Padding(
                    padding: const EdgeInsets.symmetric(horizontal: 2),
                    child: InkWell(
                      borderRadius: BorderRadius.circular(7),
                      onTap: () {
                        final bit = 1 << d;
                        if (selected && (scenario.weekdays & 127) == bit) return;
                        scenario.weekdays = selected ? (scenario.weekdays & ~bit) : (scenario.weekdays | bit);
                        onChanged();
                      },
                      child: Container(
                        padding: const EdgeInsets.symmetric(vertical: 7),
                        decoration: BoxDecoration(color: selected ? AppColors.accentDim : AppColors.panel, border: Border.all(color: selected ? AppColors.accent : AppColors.lineStrong), borderRadius: BorderRadius.circular(7)),
                        child: Text(names[d], textAlign: TextAlign.center, style: TextStyle(color: selected ? AppColors.accent : AppColors.dim, fontWeight: FontWeight.w900, fontSize: 11)),
                      ),
                    ),
                  ),
                );
              }),
            ),
          ],
        ),
      ),
    );
  }

  Widget _badge(String text, Color color) => Container(padding: const EdgeInsets.symmetric(horizontal: 7, vertical: 2), decoration: BoxDecoration(color: color.withOpacity(.14), borderRadius: BorderRadius.circular(99)), child: Text(text, style: TextStyle(color: color, fontSize: 10, fontWeight: FontWeight.w900)));

  Future<void> pickTime(BuildContext context, bool start) async {
    final initial = TimeOfDay(hour: start ? scenario.sh : scenario.eh, minute: start ? scenario.sm : scenario.em);
    final picked = await showTimePicker(context: context, initialTime: initial, builder: (context, child) => Directionality(textDirection: TextDirection.rtl, child: child ?? const SizedBox.shrink()));
    if (picked == null) return;
    if (start) {
      scenario.sh = picked.hour;
      scenario.sm = picked.minute;
    } else {
      scenario.eh = picked.hour;
      scenario.em = picked.minute;
    }
    onChanged();
  }
}

class TimeBox extends StatelessWidget {
  const TimeBox({super.key, required this.label, required this.hour, required this.minute, required this.onTap});
  final String label;
  final int hour;
  final int minute;
  final VoidCallback onTap;
  @override
  Widget build(BuildContext context) {
    return InkWell(
      borderRadius: BorderRadius.circular(9),
      onTap: onTap,
      child: Container(
        padding: const EdgeInsets.symmetric(vertical: 9, horizontal: 10),
        decoration: BoxDecoration(color: AppColors.panel, borderRadius: BorderRadius.circular(9), boxShadow: const [BoxShadow(color: Colors.black38, offset: Offset(4, 4), blurRadius: 10), BoxShadow(color: Color(0x08FFFFFF), offset: Offset(-3, -3), blurRadius: 8)]),
        child: Column(children: [Text(label, style: const TextStyle(color: AppColors.faint, fontSize: 10)), const SizedBox(height: 3), Text('${pad2(hour)}:${pad2(minute)}', textDirection: TextDirection.ltr, style: const TextStyle(fontWeight: FontWeight.w900, fontSize: 17))]),
      ),
    );
  }
}

class OrbitNav extends StatelessWidget {
  const OrbitNav({super.key, required this.index, required this.onChanged});
  final int index;
  final ValueChanged<int> onChanged;
  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.fromLTRB(12, 10, 12, 12),
      decoration: BoxDecoration(color: const Color(0xEA0F1213), border: Border.all(color: AppColors.lineStrong), borderRadius: BorderRadius.circular(28), boxShadow: const [BoxShadow(color: Colors.black54, offset: Offset(0, 14), blurRadius: 34)]),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceAround,
        children: [
          navItem(0, Icons.home_outlined, 'خانه'),
          navItem(1, Icons.calendar_month_outlined, 'سناریوها'),
          navItem(2, Icons.settings_outlined, 'تنظیمات'),
        ],
      ),
    );
  }

  Widget navItem(int i, IconData icon, String label) {
    final active = i == index;
    return InkWell(
      borderRadius: BorderRadius.circular(999),
      onTap: () => onChanged(i),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 250),
        curve: Curves.easeOutBack,
        transform: Matrix4.translationValues(0, active ? -14 : 0, 0)..scale(active ? 1.16 : 1.0),
        padding: const EdgeInsets.all(8),
        decoration: BoxDecoration(color: active ? AppColors.accent : Colors.transparent, shape: BoxShape.circle, boxShadow: active ? const [BoxShadow(color: AppColors.accentDim, blurRadius: 22, offset: Offset(0, 10))] : null),
        child: Column(mainAxisSize: MainAxisSize.min, children: [Icon(icon, color: active ? AppColors.onAccent : AppColors.faint, size: 21), if (!active) Text(label, style: const TextStyle(color: AppColors.faint, fontWeight: FontWeight.w900, fontSize: 10))]),
      ),
    );
  }
}

(String, Color) staStateText(DeviceStatus? s) {
  if (s == null) return ('در حال بررسی...', AppColors.warn);
  if (s.internetEnabled != 1) return ('غیرفعال چون اینترنت خاموش است', AppColors.warn);
  if (s.staConfigured != 1) return ('تنظیم نشده', AppColors.warn);
  if (s.staState == 4) return ('طبق زمان‌بندی موقتاً قطع است', AppColors.warn);
  if (s.staState == 2) return ('متصل - ${s.staIp}', AppColors.live);
  if (s.staState == 3) return ('⚠️ قطع شده - در حال تلاش مجدد...', AppColors.off);
  if (s.staState == 1) return ('در حال اتصال...', AppColors.warn);
  return ('در حال بررسی...', AppColors.warn);
}

(String, Color) staCycleText(DeviceStatus? s) {
  if (s == null) return ('در حال بررسی...', AppColors.warn);
  if (s.internetEnabled != 1) return ('غیرفعال', AppColors.warn);
  if (s.staConfigured != 1) return ('تا قبل از وارد کردن SSID مودم فعال نمی‌شود', AppColors.warn);
  if (s.staOffMinutes == 0) return ('دائم روشن (زمان خاموشی = ۰)', AppColors.live);
  if (s.staPhaseOn == 1) return ('روشن — ${(s.staRemaining / 60).ceil()} دقیقه تا قطع دوره‌ای', AppColors.live);
  return ('خاموش — ${(s.staRemaining / 60).ceil()} دقیقه تا وصل مجدد', AppColors.warn);
}

(String, Color) internetModeText(DeviceStatus? s) {
  if (s == null) return ('در حال بررسی...', AppColors.warn);
  if (s.internetEnabled == 1) {
    if (s.staConfigured != 1) return ('فعال است، اما مودم تنظیم نشده', AppColors.warn);
    if (s.staOffMinutes == 0) return ('فعال — اتصال مودم دائماً روشن است', AppColors.live);
    return ('فعال — طبق زمان‌بندی STA', AppColors.live);
  }
  return ('غیرفعال', AppColors.warn);
}

String apCycleText(DeviceStatus? s) {
  if (s == null) return 'در حال بررسی...';
  if (s.apCycleEnabled != 1) return 'غیرفعال — فرستنده همیشه روشن است';
  if (s.apOn == 1 && s.apClientConnected == 1) return 'روشن — دستگاهی متصل است، چرخه متوقف مانده';
  if (s.apOn == 1) return 'روشن — ${(s.apRemaining / 60).ceil()} دقیقه تا خاموش‌شدن';
  return 'در حال خاموش بودن طبق چرخه';
}

String protectionText(DeviceStatus? s) {
  if (s == null) return 'در حال بررسی...';
  if (s.protectionRemaining > 0) return 'فعال — ${(s.protectionRemaining / 60).ceil()} دقیقه تا اجازه روشن‌شدن';
  if (s.protectionMinutes > 0) return 'فعال — فاصله تنظیم‌شده: ${s.protectionMinutes} دقیقه';
  return 'غیرفعال';
}

String formatNtpSuccessStamp(DeviceStatus? s) {
  if (s == null || s.ntpLastValid != 1) return 'هنوز دریافت نشده';
  final j = gregorianToJalali(s.ntpYear, s.ntpMonth, s.ntpDay);
  return '${j[0]}/${pad2(j[1])}/${pad2(j[2])} - ${pad2(s.ntpHour)}:${pad2(s.ntpMinute)}:${pad2(s.ntpSecond)}';
}

String formatDuration(int totalSeconds) {
  final days = totalSeconds ~/ 86400;
  final hours = (totalSeconds % 86400) ~/ 3600;
  final minutes = (totalSeconds % 3600) ~/ 60;
  if (days > 0) return '$days روز و $hours ساعت';
  if (hours > 0) return '$hours ساعت و $minutes دقیقه';
  if (minutes > 0) return '$minutes دقیقه';
  return '$totalSeconds ثانیه';
}

String formatJalaliDate(int gy, int gm, int gd) {
  final j = gregorianToJalali(gy, gm, gd);
  return '${j[0]}/${pad2(j[1])}/${pad2(j[2])}';
}

List<int> gregorianToJalali(int gy, int gm, int gd) {
  const gdm = [0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334];
  final gy2 = gm > 2 ? gy + 1 : gy;
  var days = 355666 + 365 * gy + ((gy2 + 3) ~/ 4) - ((gy2 + 99) ~/ 100) + ((gy2 + 399) ~/ 400) + gd + gdm[gm - 1];
  var jy = -1595 + 33 * (days ~/ 12053);
  days %= 12053;
  jy += 4 * (days ~/ 1461);
  days %= 1461;
  if (days > 365) {
    jy += (days - 1) ~/ 365;
    days = (days - 1) % 365;
  }
  int jm;
  int jd;
  if (days < 186) {
    jm = 1 + days ~/ 31;
    jd = 1 + days % 31;
  } else {
    jm = 7 + (days - 186) ~/ 30;
    jd = 1 + (days - 186) % 30;
  }
  return [jy, jm, jd];
}

String pad2(int n) => n.toString().padLeft(2, '0');
