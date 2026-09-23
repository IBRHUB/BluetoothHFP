import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

void main() => runApp(const BluetoothHfpApp());

const black = Color(0xFF000000);
const card = Color(0xFF0A0A0A);
const white = Color(0xFFFFFFFF);
const muted = Color(0xFF777777);
const border = Color(0xFF1A1A1A);
const green = Color(0xFF22C55E);

class BluetoothHfpApp extends StatelessWidget {
  const BluetoothHfpApp({super.key});

  @override
  Widget build(BuildContext context) => MaterialApp(
    title: 'Bluetooth HFP',
    debugShowCheckedModeBanner: false,
    theme: ThemeData(
      brightness: Brightness.dark,
      scaffoldBackgroundColor: black,
      colorScheme: const ColorScheme.dark(surface: black, primary: white),
      splashFactory: NoSplash.splashFactory,
      hoverColor: Colors.transparent,
      highlightColor: Colors.transparent,
    ),
    home: const HfpHome(),
  );
}

class Device {
  const Device(this.id, this.name);
  final String id;
  final String name;

  factory Device.fromMap(Map<Object?, Object?> data) =>
      Device(data['id'] as String, data['name'] as String);
}

class Snapshot {
  const Snapshot({
    required this.phones,
    required this.inputs,
    required this.outputs,
    required this.phoneId,
    required this.phoneConnected,
    required this.inputId,
    required this.outputId,
    required this.routeActive,
    required this.message,
    this.mediaActive = false,
    this.mediaMessage = 'Select an iPhone to receive media.',
    this.callsMessage = 'Select an iPhone to connect calls.',
    this.testActive = false,
    this.testPeak = 0,
    this.testMessage = '',
    this.mediaState = 'idle',
    this.callsState = 'idle',
    this.appVersion = '',
    this.packaged = false,
  });

  final List<Device> phones;
  final List<Device> inputs;
  final List<Device> outputs;
  final String? phoneId;
  final bool phoneConnected;
  final String? inputId;
  final String? outputId;
  final bool routeActive;
  final String message;
  final bool mediaActive;
  final String mediaMessage;
  final String callsMessage;
  final bool testActive;
  final double testPeak;
  final String testMessage;
  final String mediaState;
  final String callsState;
  final String appVersion;
  final bool packaged;

  factory Snapshot.fromMap(Map<Object?, Object?> data) {
    List<Device> list(String key) => (data[key] as List<Object?>? ?? [])
        .map((value) => Device.fromMap(value as Map<Object?, Object?>))
        .toList();
    return Snapshot(
      phones: list('phones'),
      inputs: list('inputs'),
      outputs: list('outputs'),
      phoneId: data['phoneId'] as String?,
      phoneConnected: data['phoneConnected'] as bool? ?? false,
      inputId: data['inputId'] as String?,
      outputId: data['outputId'] as String?,
      routeActive: data['routeActive'] as bool? ?? false,
      message: data['message'] as String? ?? '',
      mediaActive: data['mediaActive'] as bool? ?? false,
      mediaMessage:
          data['mediaMessage'] as String? ??
          'Select an iPhone to receive media.',
      callsMessage:
          data['callsMessage'] as String? ??
          'Select an iPhone to connect calls.',
      testActive: data['testActive'] as bool? ?? false,
      testPeak: (data['testPeak'] as num?)?.toDouble() ?? 0,
      testMessage: data['testMessage'] as String? ?? '',
      mediaState: data['mediaState'] as String? ?? 'idle',
      callsState: data['callsState'] as String? ?? 'idle',
      appVersion: data['appVersion'] as String? ?? '',
      packaged: data['packaged'] as bool? ?? false,
    );
  }
}

class HfpHome extends StatefulWidget {
  const HfpHome({super.key});

  @override
  State<HfpHome> createState() => _HfpHomeState();
}

class _HfpHomeState extends State<HfpHome> {
  static const channel = MethodChannel('bluetooth_hfp/windows');
  Snapshot? snapshot;
  String? error;
  Timer? timer;
  bool busy = false;

  @override
  void initState() {
    super.initState();
    refresh();
    timer = Timer.periodic(const Duration(seconds: 1), (_) => refresh());
  }

  @override
  void dispose() {
    timer?.cancel();
    super.dispose();
  }

  Future<void> refresh() async {
    if (busy) return;
    busy = true;
    try {
      final data = await channel
          .invokeMapMethod<Object?, Object?>('snapshot')
          .timeout(const Duration(seconds: 8));
      if (mounted && data != null) {
        setState(() {
          snapshot = Snapshot.fromMap(data);
          error = null;
        });
      }
    } on TimeoutException {
      if (mounted)
        setState(
          () => error = 'Windows is not responding. Device status could not be refreshed.',
        );
    } on PlatformException catch (exception) {
      if (mounted) setState(() => error = exception.message ?? exception.code);
    } on MissingPluginException {
      if (mounted) {
        setState(() => error = 'Windows audio bridge is unavailable.');
      }
    } finally {
      busy = false;
    }
  }

  Future<void> change(String method, String? id) async {
    if (busy) return;
    busy = true;
    try {
      if (method == 'selectPhone' && id == '__pair__') {
        await channel
            .invokeMethod<void>('openBluetoothSettings')
            .timeout(const Duration(seconds: 8));
      } else {
        await channel
            .invokeMethod<void>(method, id == '__stop__' ? null : id)
            .timeout(const Duration(seconds: 8));
      }
      final data = await channel
          .invokeMapMethod<Object?, Object?>('snapshot')
          .timeout(const Duration(seconds: 8));
      if (mounted && data != null) {
        setState(() {
          snapshot = Snapshot.fromMap(data);
          error = null;
        });
      }
    } on TimeoutException {
      if (mounted)
        setState(
          () => error =
              'Windows did not finish the request. Refreshing status...',
        );
    } on MissingPluginException {
      if (mounted)
        setState(
          () => error =
              'Windows audio bridge is unavailable. Restart the installed app.',
        );
    } on PlatformException catch (exception) {
      if (mounted) setState(() => error = exception.message ?? exception.code);
    } finally {
      busy = false;
    }
  }

  String label(List<Device> devices, String? id, String fallback) {
    for (final device in devices) {
      if (device.id == id) return device.name;
    }
    return fallback;
  }

  @override
  Widget build(BuildContext context) {
    final state = snapshot;
    final active = state?.routeActive ?? false;
    final status = error ?? state?.message ?? 'Checking devices…';
    return Scaffold(
      body: Center(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 480),
          child: SingleChildScrollView(
            padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 24),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text(
                  'Bluetooth HFP',
                  style: TextStyle(
                    color: white,
                    fontSize: 26,
                    fontWeight: FontWeight.w600,
                    letterSpacing: -0.8,
                  ),
                ),
                const SizedBox(height: 8),
                if (state?.appVersion.isNotEmpty == true)
                  Text(
                    'v${state!.appVersion} · ${state.packaged ? 'Installed app' : 'Portable / development'}',
                    style: const TextStyle(color: muted, fontSize: 11),
                  ),
                const SizedBox(height: 8),
                const Text(
                  'iPhone media and calls on your PC',
                  style: TextStyle(color: muted, fontSize: 13),
                ),
                const SizedBox(height: 16),
                ConnectionStatusLine(
                  title: 'Bluetooth link',
                  state: state?.phoneId == null
                      ? 'idle'
                      : state!.phoneConnected
                      ? 'connected'
                      : 'disconnected',
                  message: state?.phoneId == null
                      ? 'Select a paired iPhone below.'
                      : state!.phoneConnected
                      ? 'Windows reports the selected iPhone is connected.'
                      : 'Paired, but Windows does not currently report a Bluetooth connection.',
                ),
                const SizedBox(height: 16),
                SelectRow(
                  title: 'Bluetooth',
                  value: state == null
                      ? 'Checking…'
                      : label(state.phones, state.phoneId, 'Select iPhone'),
                  valueColor: state?.phoneConnected == true ? green : muted,
                  choices: [
                    if (state?.phoneId != null)
                      const Choice('__stop__', 'Stop routing'),
                    ...?state?.phones.map((d) => Choice(d.id, d.name)),
                    const Choice('__pair__', 'Pair iPhone in Windows…'),
                  ],
                  onSelected: (id) => change('selectPhone', id),
                ),
                const SizedBox(height: 10),
                SelectRow(
                  title: 'Input',
                  value: state == null
                      ? 'Checking…'
                      : label(state.inputs, state.inputId, 'Select microphone'),
                  choices: [...?state?.inputs.map((d) => Choice(d.id, d.name))],
                  onSelected: (id) => change('selectInput', id),
                ),
                const SizedBox(height: 10),
                SelectRow(
                  title: 'Output',
                  value: state == null
                      ? 'Checking…'
                      : label(
                          state.outputs,
                          state.outputId,
                          'Select headphones or speakers',
                        ),
                  choices: [
                    ...?state?.outputs.map((d) => Choice(d.id, d.name)),
                  ],
                  onSelected: (id) => change('selectOutput', id),
                ),
                const SizedBox(height: 20),
                OutlinedButton(
                  onPressed:
                      state?.inputId == null ||
                          state?.outputId == null ||
                          state?.testActive == true ||
                          active
                      ? null
                      : () => change('testAudio', null),
                  child: Text(
                    state?.testActive == true
                        ? 'Testing microphone...'
                        : 'Test mic through headphones (5 seconds)',
                  ),
                ),
                if (state?.testMessage.isNotEmpty == true) ...[
                  const SizedBox(height: 6),
                  Text(
                    state!.testMessage,
                    style: const TextStyle(
                      color: muted,
                      fontSize: 11,
                      height: 1.4,
                    ),
                  ),
                  const SizedBox(height: 6),
                  LinearProgressIndicator(
                    value: state.testPeak.clamp(0.0, 1.0),
                    color: green,
                    backgroundColor: border,
                    semanticsLabel: 'Maximum microphone level',
                  ),
                ],
                const SizedBox(height: 16),
                ConnectionStatusLine(
                  title: 'Media',
                  state: state?.mediaState ?? 'idle',
                  message: state?.mediaMessage ?? 'Checking...',
                ),
                const SizedBox(height: 10),
                ConnectionStatusLine(
                  title: 'Calls in this app',
                  state: state?.callsState ?? 'idle',
                  message: state?.callsMessage ?? 'Checking...',
                ),
                const SizedBox(height: 10),
                Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Container(
                      width: 7,
                      height: 7,
                      margin: const EdgeInsets.only(top: 5, right: 9),
                      decoration: BoxDecoration(
                        shape: BoxShape.circle,
                        color: active ? green : muted,
                      ),
                    ),
                    Expanded(
                      child: Text(
                        'Call audio route: $status',
                        style: TextStyle(
                          color: active ? green : muted,
                          fontSize: 12,
                          height: 1.4,
                        ),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                const Text(
                  'Input and Output select call devices. Media plays through Windows audio output; select your headphones in Sound settings. The microphone is used during calls, not for every iPhone app.',
                  style: TextStyle(color: muted, fontSize: 11, height: 1.4),
                ),
                const SizedBox(height: 8),
                Wrap(
                  spacing: 8,
                  children: [
                    TextButton(
                      onPressed: state?.phoneId == null
                          ? null
                          : () => change('reconnect', null),
                      child: const Text('Reconnect'),
                    ),
                    TextButton(
                      onPressed: () => change('openSoundSettings', null),
                      child: const Text('Sound settings'),
                    ),
                    TextButton(
                      onPressed: () => change('openCallPermissions', null),
                      child: const Text('Call permissions'),
                    ),
                    TextButton(
                      onPressed: () => change('openPhoneLink', null),
                      child: const Text('Phone Link'),
                    ),
                  ],
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class ConnectionStatusLine extends StatelessWidget {
  const ConnectionStatusLine({
    required this.title,
    required this.state,
    required this.message,
    super.key,
  });
  final String title;
  final String state;
  final String message;

  @override
  Widget build(BuildContext context) {
    final label = switch (state) {
      'connected' => 'Connected',
      'connecting' => 'In progress',
      'waiting' => 'Waiting',
      'blocked' => 'Access denied',
      'unavailable' => 'Unavailable to this app',
      'timeout' => 'Timed out',
      'error' => 'Failed',
      'disconnected' => 'Disconnected',
      _ => 'Not started',
    };
    final color = state == 'connected'
        ? green
        : ['blocked', 'error', 'timeout'].contains(state)
        ? const Color(0xFFFBBF24)
        : muted;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          '$title · $label',
          style: TextStyle(
            color: color,
            fontSize: 12,
            fontWeight: FontWeight.w600,
          ),
        ),
        const SizedBox(height: 4),
        Text(
          message,
          style: const TextStyle(color: muted, fontSize: 12, height: 1.4),
        ),
      ],
    );
  }
}

class Choice {
  const Choice(this.id, this.label);
  final String? id;
  final String label;
}

class SelectRow extends StatelessWidget {
  const SelectRow({
    required this.title,
    required this.value,
    required this.choices,
    required this.onSelected,
    this.valueColor = muted,
    super.key,
  });

  final String title;
  final String value;
  final Color valueColor;
  final List<Choice> choices;
  final ValueChanged<String?> onSelected;

  @override
  Widget build(BuildContext context) => PopupMenuButton<String?>(
    enabled: choices.isNotEmpty,
    tooltip: title,
    color: card,
    surfaceTintColor: card,
    elevation: 0,
    offset: const Offset(0, 4),
    constraints: const BoxConstraints(minWidth: 220, maxWidth: 420),
    onSelected: onSelected,
    itemBuilder: (_) => choices
        .map(
          (choice) => PopupMenuItem<String?>(
            value: choice.id,
            child: Text(
              choice.label,
              style: const TextStyle(color: white, fontSize: 13),
            ),
          ),
        )
        .toList(),
    child: Container(
      width: double.infinity,
      height: 72,
      padding: const EdgeInsets.symmetric(horizontal: 18),
      decoration: BoxDecoration(
        color: card,
        border: Border.all(color: border),
        borderRadius: BorderRadius.circular(8),
      ),
      child: Row(
        children: [
          SizedBox(
            width: 92,
            child: Text(
              title,
              style: const TextStyle(color: white, fontSize: 14),
            ),
          ),
          Expanded(
            child: Text(
              value,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              textAlign: TextAlign.right,
              style: TextStyle(color: valueColor, fontSize: 13),
            ),
          ),
          const SizedBox(width: 14),
          const Icon(Icons.keyboard_arrow_down, color: muted, size: 18),
        ],
      ),
    ),
  );
}
