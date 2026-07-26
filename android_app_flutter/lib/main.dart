import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:webview_flutter/webview_flutter.dart';
import 'package:wifi_iot/wifi_iot.dart';

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

class _SmartCoolerWebViewScreenState extends State<SmartCoolerWebViewScreen> {
  late final WebViewController _controller;
  bool _loading = true;
  bool _wifiDialogOpen = false;

  @override
  void initState() {
    super.initState();

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
    if (_wifiDialogOpen || !mounted) return;

    String targetSsid = 'ESP32_Timer_Hub';
    String targetPass = '12345678';
    try {
      final data = jsonDecode(message);
      if (data is Map) {
        targetSsid = (data['ssid']?.toString().trim().isNotEmpty ?? false) ? data['ssid'].toString().trim() : targetSsid;
        targetPass = data['pass']?.toString() ?? targetPass;
      }
    } catch (_) {}

    String currentSsid = '';
    try {
      currentSsid = _cleanSsid(await WiFiForIoTPlugin.getSSID());
    } catch (_) {
      currentSsid = '';
    }

    if (_sameSsid(currentSsid, targetSsid)) {
      await _showAlreadyOnBoardWifiDialog(targetSsid);
      return;
    }

    await _showConnectWifiDialog(targetSsid, targetPass, currentSsid);
  }

  Future<void> _showAlreadyOnBoardWifiDialog(String ssid) async {
    if (_wifiDialogOpen || !mounted) return;
    _wifiDialogOpen = true;
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
    _wifiDialogOpen = false;
  }

  Future<void> _showConnectWifiDialog(String defaultSsid, String defaultPass, String currentSsid) async {
    if (_wifiDialogOpen || !mounted) return;
    _wifiDialogOpen = true;

    final ssidController = TextEditingController(text: defaultSsid);
    final passController = TextEditingController(text: defaultPass);

    final shouldConnect = await showDialog<bool>(
      context: context,
      builder: (ctx) => Directionality(
        textDirection: TextDirection.rtl,
        child: AlertDialog(
          backgroundColor: const Color(0xFF15181A),
          title: const Text('اتصال به وای‌فای برد'),
          content: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Text(
                currentSsid.isEmpty
                    ? 'ارتباط با برد برقرار نشد و شبکه فعلی گوشی قابل تشخیص نیست. اگر می‌خواهید، اپ تلاش کند به وای‌فای برد وصل شود.'
                    : 'ارتباط با برد برقرار نشد. گوشی الان به «$currentSsid» وصل است. اگر می‌خواهید، اپ تلاش کند به وای‌فای برد وصل شود.',
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
                'در Android 10 به بالا، سیستم ممکن است برای اتصال یک پنجره تأیید نشان بدهد. این محدودیت خود اندروید است و اتصال کاملاً بی‌اجازه ممکن نیست.',
                style: TextStyle(fontSize: 12, color: Colors.white60),
              ),
            ],
          ),
          actions: [
            TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('نه')),
            FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('وصل شو')),
          ],
        ),
      ),
    );

    final ssid = ssidController.text.trim();
    final pass = passController.text;
    ssidController.dispose();
    passController.dispose();
    _wifiDialogOpen = false;

    if (shouldConnect == true && ssid.isNotEmpty) {
      await _connectToWifi(ssid, pass);
    }
  }

  Future<void> _connectToWifi(String ssid, String password) async {
    try {
      final enabled = await WiFiForIoTPlugin.isEnabled();
      if (enabled != true) {
        await WiFiForIoTPlugin.setEnabled(true, shouldOpenSettings: true);
        _showSnack('اگر وای‌فای خاموش است، آن را روشن کنید و دوباره اتصال را بزنید.');
        return;
      }

      _showSnack('در حال درخواست اتصال به $ssid ...');

      final connected = await WiFiForIoTPlugin.connect(
        ssid,
        password: password,
        security: password.isEmpty ? NetworkSecurity.NONE : NetworkSecurity.WPA,
        joinOnce: false,
        withInternet: false,
      );

      // برای شبکه AP برد که اینترنت ندارد، مسیر درخواست‌های HTTP باید روی WiFi بماند.
      try {
        await WiFiForIoTPlugin.forceWifiUsage(true);
      } catch (_) {}

      if (connected == true) {
        _showSnack('درخواست اتصال به $ssid ارسال شد. چند ثانیه صبر کنید...');
        await Future<void>.delayed(const Duration(seconds: 4));
        await _controller.runJavaScript('fetchStatus(); loadSettings();');
      } else {
        _showSnack('اتصال خودکار انجام نشد. اگر پنجره تأیید اندروید نمایش داده شد، آن را تأیید کنید یا دستی به شبکه $ssid وصل شوید.', isError: true);
      }
    } catch (e) {
      _showSnack('خطا در اتصال به وای‌فای: $e', isError: true);
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
