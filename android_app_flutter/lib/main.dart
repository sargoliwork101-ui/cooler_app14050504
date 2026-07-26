import 'dart:convert';
import 'dart:typed_data';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
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

class _SmartCoolerWebViewScreenState extends State<SmartCoolerWebViewScreen> {
  late final WebViewController _controller;
  bool _loading = true;

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

      // مقدار فایل به صورت string literal امن وارد JS می‌شود.
      await _controller.runJavaScript('window.receiveScenarioImport(${jsonEncode(text)});');
    } catch (e) {
      _showSnack('خطا در خواندن فایل پشتیبان: $e', isError: true);
    }
  }

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
