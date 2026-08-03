import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:webview_flutter/webview_flutter.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const SmartCoolerWebViewApp());
}

class SmartCoolerWebViewApp extends StatelessWidget {
  const SmartCoolerWebViewApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'کولر هوشمند ESP32',
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark(useMaterial3: true).copyWith(
        scaffoldBackgroundColor: const Color(0xFF0A0C0D),
      ),
      home: const SmartCoolerWebViewScreen(),
    );
  }
}

class SmartCoolerWebViewScreen extends StatefulWidget {
  const SmartCoolerWebViewScreen({super.key});

  @override
  State<SmartCoolerWebViewScreen> createState() => _SmartCoolerWebViewScreenState();
}

class _SmartCoolerWebViewScreenState extends State<SmartCoolerWebViewScreen> with WidgetsBindingObserver {
  static const MethodChannel _wifiChannel = MethodChannel('smart_cooler/wifi');

  late final WebViewController _controller;
  bool _loading = true;
  bool _wifiDialogOpen = false;

  // محافظت از race condition روی متغیرهای static-like
  static bool _isWifiDialogOpen = false;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);

    _controller = WebViewController()
      ..setJavaScriptMode(JavaScriptMode.unrestricted)
      ..setBackgroundColor(const Color(0xFF0A0C0D))
      ..setNavigationDelegate(
        NavigationDelegate(
          onPageFinished: (_) {
            if (mounted) setState(() => _loading = false);
          },
        ),
      )
      ..addJavaScriptChannel(
        'FlutterExport',
        onMessageReceived: (JavaScriptMessage message) => _exportScenarioBackup(message.message),
      )
      ..addJavaScriptChannel(
        'FlutterImport',
        onMessageReceived: (_) => _importScenarioBackup(),
      )
      ..addJavaScriptChannel(
        'FlutterWifi',
        onMessageReceived: (JavaScriptMessage message) => _handleWifiAssist(message.message),
      )
      ..loadFlutterAsset('assets/web/index.html');
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    super.didChangeAppLifecycleState(state);
    // اتصال WiFi فقط هنگام بسته‌شدن واقعی/خروج از اپ آزاد می‌شود.
    // روی paused آزاد نمی‌کنیم چون هنگام نمایش پنجره تأیید WiFi اندروید هم ممکن است paused رخ دهد.
    if (state == AppLifecycleState.detached) {
      _releaseWifiBinding();
    }
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _releaseWifiBinding();
    super.dispose();
  }

  Future<void> _releaseWifiBinding() async {
    try {
      await _wifiChannel.invokeMethod<bool>('releaseWifi');
    } catch (_) {}
  }

  Future<void> _exportScenarioBackup(String jsonText) async {
    try {
      final now = DateTime.now();
      final stamp = '${now.year}-${_pad2(now.month)}-${_pad2(now.day)}';
      final path = await FilePicker.platform.saveFile(
        dialogTitle: 'ذخیره فایل پشتیبان سناریوها',
        fileName: 'cooler-scenarios-$stamp.json',
        type: FileType.custom,
        allowedExtensions: const ['json'],
        bytes: Uint8List.fromList(utf8.encode(jsonText)),
      );
      if (path == null) return;
      _showSnack('فایل پشتیبان سناریوها ذخیره شد.');
    } catch (e) {
      _showSnack('خطا در ذخیره فایل پشتیبان: $e', isError: true);
    }
  }

  Future<void> _importScenarioBackup() async {
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
      final text = utf8.decode(bytes);

      await _controller.runJavaScript('window.receiveScenarioImport(${jsonEncode(text)});');
    } catch (e) {
      _showSnack('خطا در خواندن فایل پشتیبان: $e', isError: true);
    }
  }

  Future<void> _handleWifiAssist(String message) async {
    String action = 'connect';
    String targetSsid = 'ESP32_Timer_Hub';
    String targetPass = '12345678';
    bool force = false;
    bool autoApproved = false;
    try {
      final data = jsonDecode(message);
      if (data is Map) {
        action = data['action']?.toString() ?? action;
        targetSsid = (data['ssid']?.toString().trim().isNotEmpty ?? false) ? data['ssid'].toString().trim() : targetSsid;
        targetPass = data['pass']?.toString() ?? targetPass;
        force = data['force'] == true;
        autoApproved = data['autoApproved'] == true || data['autoApproved']?.toString() == '1';
      }
    } catch (_) {}

    if (action == 'signal') {
      try {
        final signal = await _wifiChannel.invokeMethod<String>('getSignal') ?? '';
        await _controller.runJavaScript('window.updateWifiSignal(${jsonEncode(signal)});');
      } catch (_) {}
      return;
    }

    String currentSsid = '';
    try {
      currentSsid = _cleanSsid(await _wifiChannel.invokeMethod<String>('getSsid'));
    } catch (_) {
      currentSsid = '';
    }

    if (currentSsid == '__WIFI_DISABLED__') {
      currentSsid = 'وای‌فای خاموش است';
    } else if (_sameSsid(currentSsid, targetSsid)) {
      // گوشی به وای‌فای برد وصله ولی API جواب نمیده — دوباره تلاش کن
      if (autoApproved && !force) {
        await _connectToWifi(targetSsid, targetPass, silent: true);
        return;
      }
      if (force) await _showAlreadyOnBoardWifiDialog(targetSsid);
      return;
    }

    // اگر کاربر قبلاً اجازه اتصال خودکار را داده، دیگر پنجره مزاحم نشان نده؛ خودکار تلاش کن.
    if (autoApproved && !force) {
      await _connectToWifi(targetSsid, targetPass, silent: true);
      return;
    }

    if (_isWifiDialogOpen || _wifiDialogOpen || !mounted) return;
    await _showConnectWifiDialog(targetSsid, targetPass, currentSsid);
  }

  Future<void> _showAlreadyOnBoardWifiDialog(String ssid) async {
    if (_isWifiDialogOpen || _wifiDialogOpen || !mounted) return;
    _isWifiDialogOpen = _wifiDialogOpen = true;
    await showDialog<void>(
      context: context,
      builder: (ctx) => Directionality(
        textDirection: TextDirection.rtl,
        child: AlertDialog(
          backgroundColor: const Color(0xFF15181A),
          title: const Text('برد پاسخ نمی‌دهد'),
          content: Text('گوشی به شبکه «$ssid» وصل است، اما API برد جواب نمی‌دهد. احتمالاً برد خاموش است، تازه ری‌استارت شده، AP خاموشِ چرخه‌ای است، یا آدرس API اشتباه است.'),
          actions: [
            TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('متوجه شدم')),
          ],
        ),
      ),
    );
    _isWifiDialogOpen = _wifiDialogOpen = false;
  }

  Future<void> _showConnectWifiDialog(String defaultSsid, String defaultPass, String currentSsid) async {
    if (_isWifiDialogOpen || _wifiDialogOpen || !mounted) return;
    _isWifiDialogOpen = _wifiDialogOpen = true;

    final ssidController = TextEditingController(text: defaultSsid);
    final passController = TextEditingController(text: defaultPass);

    final shouldConnect = await showDialog<bool>(
      context: context,
      builder: (ctx) => Directionality(
        textDirection: TextDirection.rtl,
        child: AlertDialog(
          backgroundColor: const Color(0xFF15181A),
          title: const Text('اجازه اتصال خودکار به برد'),
          content: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Text(
                currentSsid.isEmpty
                    ? 'ارتباط با برد برقرار نشد و شبکه فعلی گوشی قابل تشخیص نیست. اگر اجازه بدهید، اپ یک‌بار دسترسی اتصال به وای‌فای برد را می‌گیرد و از این به بعد هر وقت ارتباط قطع شد خودش دوباره برای اتصال تلاش می‌کند.'
                    : 'ارتباط با برد برقرار نشد. گوشی الان به «$currentSsid» وصل است. اگر اجازه بدهید، اپ یک‌بار دسترسی اتصال به وای‌فای برد را می‌گیرد و از این به بعد هر وقت ارتباط قطع شد خودش دوباره برای اتصال تلاش می‌کند.',
              ),
              const SizedBox(height: 14),
              TextField(
                controller: ssidController,
                textDirection: TextDirection.ltr,
                decoration: const InputDecoration(labelText: 'SSID برد'),
              ),
              TextField(
                controller: passController,
                obscureText: true,
                textDirection: TextDirection.ltr,
                decoration: const InputDecoration(labelText: 'رمز وای‌فای برد'),
              ),
              const SizedBox(height: 8),
              const Text(
                'در اندرویدهای جدید ممکن است خود گوشی یک پنجره تأیید اتصال نشان دهد. آن را تأیید کنید. بعد از آن، اپ تلاش می‌کند قطع ارتباط‌های بعدی را خودش جبران کند.',
                style: TextStyle(fontSize: 12, color: Colors.white60),
              ),
            ],
          ),
          actions: [
            TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('نه')),
            FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('اجازه می‌دهم و وصل شو')),
          ],
        ),
      ),
    );

    final ssid = ssidController.text.trim();
    final pass = passController.text;
    ssidController.dispose();
    passController.dispose();
    _isWifiDialogOpen = _wifiDialogOpen = false;

    if (shouldConnect == true && ssid.isNotEmpty) {
      await _controller.runJavaScript("localStorage.setItem('esp32WifiAutoApproved','1');");
      await _connectToWifi(ssid, pass);
    }
  }

  Future<void> _connectToWifi(String ssid, String password, {bool silent = false, int retry = 0}) async {
    try {
      if (!silent) _showSnack('در حال تلاش برای اتصال به وای‌فای برد: $ssid');

      final response = await _wifiChannel.invokeMethod<dynamic>('connect', {
        'ssid': ssid,
        'password': password,
      });
      final status = response?.toString() ?? 'failed';

      if (status == 'requested' || status == 'true') {
        if (!silent) {
          _showSnack('درخواست اتصال ارسال شد. اگر گوشی پنجره تأیید نشان داد، گزینه اتصال را تأیید کنید.');
        }
        await Future<void>.delayed(const Duration(seconds: 5));
        await _controller.runJavaScript('fetchStatus(); loadSettings();');
      } else if (status == 'wifi_disabled') {
        _showSnack('وای‌فای گوشی خاموش است. صفحه وای‌فای باز شد؛ وای‌فای را روشن کنید، سپس دوباره اتصال را بزنید.', isError: true);
      } else if (status == 'permission_requested') {
        _showSnack('گوشی اجازه دسترسی WiFi/موقعیت مکانی را می‌خواهد. اجازه را تأیید کنید؛ اپ چند ثانیه دیگر دوباره تلاش می‌کند.');
        if (retry < 2) {
          await Future<void>.delayed(const Duration(seconds: 4));
          await _connectToWifi(ssid, password, silent: silent, retry: retry + 1);
        }
      } else {
        if (!silent) {
          _showSnack(
            'اتصال خودکار انجام نشد. اگر پنجره اتصال اندروید نمایش داده شد آن را تأیید کنید، یا دستی به شبکه $ssid وصل شوید.',
            isError: true,
          );
        }
      }
    } catch (e) {
      if (!silent) _showSnack('خطا در اتصال به وای‌فای برد: $e', isError: true);
    }
  }

  String _cleanSsid(String? ssid) {
    var v = (ssid ?? '').trim();
    if (v.startsWith('"') && v.endsWith('"') && v.length >= 2) v = v.substring(1, v.length - 1);
    if (v == '<unknown ssid>') return '';
    return v;
  }

  bool _sameSsid(String a, String b) => _cleanSsid(a).toLowerCase() == _cleanSsid(b).toLowerCase();

  void _showSnack(String text, {bool isError = false}) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Directionality(textDirection: TextDirection.rtl, child: Text(text)),
        backgroundColor: isError ? const Color(0xFFFF5468) : const Color(0xFF1B1F21),
        behavior: SnackBarBehavior.floating,
      ),
    );
  }

  static String _pad2(int n) => n.toString().padLeft(2, '0');

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: Stack(
          children: [
            WebViewWidget(controller: _controller),
            if (_loading)
              const Center(
                child: CircularProgressIndicator(color: Color(0xFF1DE9C4)),
              ),
          ],
        ),
      ),
    );
  }
}
