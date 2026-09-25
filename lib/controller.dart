import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

class HeadsetController extends ChangeNotifier {
  static const platform = MethodChannel('bluetooth_hfp/platform');
  final String bundle;
  final Directory data;
  HeadsetController({String? bundlePath, String? dataPath})
    : bundle = bundlePath ?? File(Platform.resolvedExecutable).parent.path,
      data = Directory(
        dataPath ?? '${Platform.environment['LOCALAPPDATA']}\\BluetoothHFP',
      );
  Process? _engine;
  Timer? _retry;
  Completer<void>? _ready;
  bool _disposed = false;
  bool busy = false,
      ready = false,
      connected = false,
      muted = false,
      scanning = false;
  bool hfpReady = false;
  bool autoReconnect = true;
  String message = 'Checking adapter…',
      error = '',
      peer = '',
      call = '',
      media = '',
      mediaCodec = '';
  String capture = '', render = '', codec = 'Auto';
  int volume = 100, micLevel = 100, retries = 0;
  Map<String, dynamic> inventory = {};
  List<Map<String, dynamic>> endpoints = [];
  final Set<String> phones = {};
  final List<String> logs = [];
  bool get running => _engine != null;
  String get enginePath => '$bundle\\engine';
  String get service => devices.isEmpty ? '' : '${devices.first['service']}';
  List<Map<String, dynamic>> get devices =>
      (inventory['devices'] as List? ?? [])
          .map((e) => Map<String, dynamic>.from(e as Map))
          .toList();
  bool get supported => inventory['supported'] == true;
  void changed() {
    if (!_disposed) notifyListeners();
  }

  void reportError(Object exception) {
    error = '$exception';
    _log('ERROR: $exception');
    changed();
  }

  void _log(String text) {
    logs.add(text);
    if (logs.length > 350) logs.removeRange(0, logs.length - 350);
  }

  Future<void> initialize() async {
    await data.create(recursive: true);
    final settings = File('${data.path}\\settings.json');
    if (await settings.exists()) {
      try {
        final s = jsonDecode(
          (await settings.readAsString()).replaceFirst('\ufeff', ''),
        ) as Map;
        capture = s['capture'] as String? ?? '';
        render = s['render'] as String? ?? '';
        codec = ['Auto', 'mSBC', 'CVSD'].contains(s['codec'])
            ? s['codec'] as String
            : 'Auto';
        peer = s['peer'] as String? ?? '';
        autoReconnect = s['autoReconnect'] != false;
        phones.addAll(
          (s['phones'] as List? ?? []).whereType<String>().where(validAddress),
        );
        volume = ((s['volume'] as num?)?.toInt() ?? 100).clamp(0, 127);
        micLevel = ((s['micLevel'] as num?)?.toInt() ?? 100).clamp(0, 100);
      } catch (_) {
        error = 'Could not read settings. Defaults have been applied.';
      }
    }
    await refresh();
  }

  static bool validAddress(String address) =>
      RegExp(r'^([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}$').hasMatch(address);
  Future<void> _saveQueue = Future<void>.value();
  Future<void> save() {
    _saveQueue = _saveQueue
        .catchError((Object _) {})
        .then((_) => _saveCurrent());
    return _saveQueue;
  }

  Future<void> saveInBackground() async {
    try {
      await save();
    } catch (e) {
      reportError('Could not save settings: $e');
    }
  }

  Future<void> _saveCurrent() async {
    final file = File('${data.path}\\settings.json.tmp');
    await file.writeAsString(
      jsonEncode({
        'capture': capture,
        'render': render,
        'codec': codec,
        'peer': peer,
        'autoReconnect': autoReconnect,
        'phones': phones.toList(),
        'volume': volume,
        'micLevel': micLevel,
      }),
      flush: true,
    );
    await file.rename('${data.path}\\settings.json');
  }

  Future<void> refresh() async {
    try {
      final result = await Process.run('powershell.exe', [
        '-NoProfile',
        '-NonInteractive',
        '-ExecutionPolicy',
        'Bypass',
        '-File',
        '$enginePath\\tools\\Controller.ps1',
        '-Action',
        'Inspect',
      ]).timeout(const Duration(seconds: 45));
      if (result.exitCode != 0) throw Exception('${result.stderr}');
      inventory = Map<String, dynamic>.from(
        jsonDecode('${result.stdout}'.trim().replaceFirst('\ufeff', '')) as Map,
      );
      endpoints = (await platform.invokeListMethod<dynamic>('endpoints') ?? [])
          .map((e) => Map<String, dynamic>.from(e as Map))
          .toList();
      message = supported
          ? (service == 'WINUSB'
                ? 'Adapter is in headset mode'
                : 'Normal Windows Bluetooth')
          : '${inventory['reason']}';
    } catch (e) {
      error = 'Inspection failed: $e';
    }
    changed();
  }

  Future<void> perform(Future<void> Function() action) async {
    if (busy) return;
    busy = true;
    error = '';
    changed();
    try {
      await action();
    } catch (e) {
      error = '$e';
      _log('ERROR: $e');
    } finally {
      busy = false;
      changed();
    }
  }

  Future<void> _driver(String action) async {
    message = 'Switching driver and verifying recovery…';
    changed();
    await platform.invokeMethod<void>('driverStart', action);
    for (var i = 0; i < 600; i++) {
      await Future<void>.delayed(const Duration(milliseconds: 500));
      final status = await platform.invokeMethod<int>('driverPoll');
      if (status == -1) continue;
      final resultFile = File(
        '${Platform.environment['ProgramData']}\\BluetoothHFP\\operation.json',
      );
      String detail = 'Driver operation did not complete (code=$status)';
      if (await resultFile.exists()) {
        final info = jsonDecode(
          (await resultFile.readAsString()).replaceFirst('\ufeff', ''),
        ) as Map;
        detail = '${info['message']}';
      }
      await refresh();
      if (status != 0) throw Exception(detail);
      _log(detail);
      return;
    }
    throw Exception(
      'The driver operation is still running. Keep the adapter connected, wait, then refresh.',
    );
  }

  Future<void> enable() => perform(() async {
    await refresh();
    if (!supported) throw Exception('${inventory['reason']}');
    if (running) return;
    if (service != 'WINUSB') await _driver('Headset');
    var probe = await Process.run('$enginePath\\ax201_probe.exe', [
      '--hci',
    ]).timeout(const Duration(seconds: 30));
    if (probe.exitCode != 0) {
      await _driver('Bootstrap');
      probe = await Process.run('$enginePath\\ax201_probe.exe', [
        '--hci',
      ]).timeout(const Duration(seconds: 30));
      if (probe.exitCode != 0) {
        throw Exception(
          'HCI is not ready after reinitialization. Use Restore Windows Bluetooth.',
        );
      }
    }
    await save();
    _ready = Completer<void>();
    final process = await Process.start(
      '$enginePath\\ax201_headset.exe',
      [],
      workingDirectory: data.path,
      environment: {
        'AX201_IPC': '1',
        'AX201_USB_INSTANCE': '${devices.first['id']}',
        'AX201_CAPTURE': capture,
        'AX201_RENDER': render,
        'AX201_HFP_CODEC': codec,
      },
    );
    _engine = process;
    // Broken pipes surface asynchronously through IOSink.done.
    unawaited(
      process.stdin.done.catchError((Object e) {
        if (_engine == process) reportError('Engine connection closed: $e');
      }),
    );
    retries = 0;
    process.stdout
        .transform(const Utf8Decoder(allowMalformed: true))
        .transform(const LineSplitter())
        .listen(
          consume,
          onError: (Object e) {
            error = '$e';
            changed();
          },
        );
    process.stderr
        .transform(const Utf8Decoder(allowMalformed: true))
        .transform(const LineSplitter())
        .listen(_log, onError: (Object e) => reportError(e));
    unawaited(
      process.exitCode.then((code) {
        if (_engine != process) return;
        _engine = null;
        ready = connected = scanning = hfpReady = false;
        call = media = '';
        _retry?.cancel();
        if (!(_ready?.isCompleted ?? true)) {
          _ready!.completeError(
            Exception('Engine exited before becoming ready (code=$code)'),
          );
        }
        if (code != 0) {
          error =
              'Engine stopped (code=$code). Restart it or restore Windows Bluetooth.';
        }
        changed();
      }),
    );
    try {
      await _ready!.future.timeout(const Duration(seconds: 35));
    } catch (_) {
      await stop();
      rethrow;
    }
    send('mic ${muted ? 1 : 0} $micLevel');
    send('volume $volume');
    if (autoReconnect && validAddress(peer)) connect(peer);
  });
  void consume(String line) {
    if (!line.startsWith('@{')) {
      _log(line);
      return;
    }
    try {
      final e = jsonDecode(line.substring(1)) as Map;
      if (e['version'] != 1) {
        throw const FormatException('Unsupported engine protocol');
      }
      final value = '${e['value']}', status = e['status'] as int;
      switch (e['event']) {
        case 'ready':
          ready = true;
          message = 'Headset is ready to connect';
          if (!(_ready?.isCompleted ?? true)) _ready!.complete();
        case 'peer':
          if (status == 0) {
            connected = true;
            peer = value;
            phones.add(value);
            retries = 0;
            _retry?.cancel();
            unawaited(saveInBackground());
          } else {
            _log('Connection status=$status');
            _scheduleReconnect();
          }
        case 'hfp':
          hfpReady = status == 0 && value == 'connected';
          if (hfpReady) {
            message = 'Call connection is ready';
            error = '';
            retries = 0;
            _retry?.cancel();
          } else if (status != 0) {
            error =
                'Call connection did not complete ($status). Open Bluetooth settings on your iPhone and reconnect to the headset.';
            _scheduleReconnect();
          }
        case 'disconnected':
          connected = false;
          hfpReady = false;
          call = media = '';
          message = 'Disconnected';
          _scheduleReconnect();
        case 'call':
          call = value == 'stopped'
              ? ''
              : {
                      1: 'CVSD · 8 kHz',
                      2: 'mSBC · 16 kHz',
                      3: 'LC3-SWB · 32 kHz',
                    }[status] ??
                    'Connection failed ($status)';
        case 'mediaCodec':
          mediaCodec = value;
        case 'media':
          media = value == 'playing'
              ? '$mediaCodec · ${(status / 1000).toStringAsFixed(1)} kHz'
              : '';
        case 'device':
          if (validAddress(value)) phones.add(value);
        case 'scanComplete':
          scanning = false;
          _scheduleReconnect();
        case 'error':
          error = value;
        case 'audioError':
          error = value;
        case 'forgotten':
          phones.remove(value);
          if (peer == value) peer = '';
          unawaited(saveInBackground());
        case 'command':
          if (status != 0) {
            error = 'Command failed ($status): $value';
            if (value == 'scan') scanning = false;
          }
      }
      changed();
    } catch (e) {
      _log('IPC: $e');
    }
  }

  void _scheduleReconnect() {
    if (_disposed ||
        !autoReconnect ||
        !ready ||
        !validAddress(peer) ||
        retries >= 6 ||
        _retry?.isActive == true) {
      return;
    }
    _retry = Timer(const Duration(seconds: 15), () {
      if ((!connected || !hfpReady) && ready && !scanning) {
        retries++;
        send('connect $peer');
        _scheduleReconnect();
      }
    });
  }

  void send(String command) {
    try {
      _engine?.stdin.writeln(command);
    } catch (e) {
      reportError('Engine connection closed: $e');
    }
  }

  void connect(String address) {
    if (!validAddress(address)) {
      error = 'Invalid Bluetooth address';
      changed();
      return;
    }
    peer = address.toUpperCase();
    phones.add(peer);
    unawaited(saveInBackground());
    send('connect $peer');
    changed();
  }

  void disconnect() {
    _retry?.cancel();
    autoReconnect = false;
    unawaited(saveInBackground());
    send('disconnect');
    changed();
  }

  void scan() {
    if (connected || !ready) return;
    scanning = true;
    send('scan');
    changed();
  }

  void forget(String address) {
    if (validAddress(address) && !connected) send('forget $address');
  }

  void setMute(bool value) {
    muted = value;
    send('mic ${muted ? 1 : 0} $micLevel');
    changed();
  }

  void setMic(int value) {
    micLevel = value;
    send('mic ${muted ? 1 : 0} $micLevel');
    changed();
  }

  void setVolume(int value) {
    volume = value;
    send('volume $volume');
    changed();
  }

  Future<void> stop() async {
    _retry?.cancel();
    final process = _engine;
    if (process == null) return;
    send('quit');
    try {
      await process.exitCode.timeout(const Duration(seconds: 12));
    } on TimeoutException {
      throw Exception(
        'The engine did not stop safely. The driver has not been changed. Try again, or close the stalled process before restoring Windows Bluetooth.',
      );
    }
    _engine = null;
    ready = connected = scanning = hfpReady = false;
    call = media = '';
    changed();
  }

  Future<void> restore() => perform(() async {
    await stop();
    await _driver('Native');
  });
  Future<String> exportDiagnostics() async {
    final path =
        '${data.path}\\diagnostics-${DateTime.now().millisecondsSinceEpoch}.txt';
    final report = jsonEncode({
      'version': '2.0.0',
      'inventory': {
        'supported': supported,
        'devices': devices
            .map(
              (device) => {
                'status': device['status'],
                'service': device['service'],
                'inf': device['inf'],
              },
            )
            .toList(),
      },
      'call': call,
      'media': media,
      'codec': codec,
      'error': redactDiagnostic(error),
    });
    await File(path)
        .writeAsString('$report\n${logs.map(redactDiagnostic).join('\n')}');
    return path;
  }

  static String redactDiagnostic(String text) {
    var result = text.replaceAll(
      RegExp(r'\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\b', caseSensitive: false),
      '[REDACTED-BLUETOOTH-ADDRESS]',
    );
    result = result.replaceAll(
      RegExp(
        r'USB\\VID_[0-9a-f]{4}&PID_[0-9a-f]{4}\\[^\s"]+',
        caseSensitive: false,
      ),
      '[REDACTED-USB-INSTANCE]',
    );
    result = result.replaceAll(
      RegExp(
        r'\{[0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12}\}',
        caseSensitive: false,
      ),
      '{REDACTED-GUID}',
    );
    final profile = Platform.environment['USERPROFILE'];
    if (profile != null && profile.isNotEmpty) {
      result = result
          .replaceAll(profile, '%USERPROFILE%')
          .replaceAll(profile.replaceAll('\\', '/'), '%USERPROFILE%');
    }
    return result;
  }

  @override
  void dispose() {
    _disposed = true;
    _retry?.cancel();
    super.dispose();
  }
}
