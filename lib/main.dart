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
    this.testActive = false,
    this.testPeak = 0,
    this.testMessage = '',
    this.mediaState = 'idle',
    this.callsState = 'idle',
    this.voiceMode = false,
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
  final bool testActive;
  final double testPeak;
  final String testMessage;
  final String mediaState;
  final String callsState;
  final bool voiceMode;

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
      testActive: data['testActive'] as bool? ?? false,
      testPeak: (data['testPeak'] as num?)?.toDouble() ?? 0,
      testMessage: data['testMessage'] as String? ?? '',
      mediaState: data['mediaState'] as String? ?? 'idle',
      callsState: data['callsState'] as String? ?? 'idle',
      voiceMode: data['voiceMode'] as bool? ?? false,
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
      if (mounted) {
        setState(() => error = 'Status unavailable');
      }
    } on PlatformException catch (exception) {
      if (mounted) setState(() => error = 'Request failed');
    } on MissingPluginException {
      if (mounted) {
        setState(() => error = 'Connection unavailable');
      }
    } finally {
      busy = false;
    }
  }

  Future<void> change(String method, Object? id) async {
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
      if (mounted) {
        setState(() => error = 'Request timed out');
      }
    } on MissingPluginException {
      if (mounted) {
        setState(() => error = 'Connection unavailable');
      }
    } on PlatformException catch (exception) {
      if (mounted) setState(() => error = 'Request failed');
    } finally {
      busy = false;
    }
  }

  String connectionLabel(String? value) => switch (value) {
    'connected' => 'Connected',
    'connecting' => 'Connecting',
    'waiting' => 'Waiting',
    'blocked' => 'Blocked',
    'unavailable' => 'Unavailable',
    'timeout' => 'Timed out',
    'error' => 'Failed',
    'disconnected' => 'Disconnected',
    _ => 'Not connected',
  };

  String microphoneLabel(Snapshot? state) {
    if (error != null) return error!;
    if (state == null) return 'Checking';
    if (state.phoneId == null) return 'Choose iPhone';
    if (!state.phoneConnected) return 'Connect iPhone';
    if (state.routeActive) return 'Active';
    final message = state.message.toLowerCase();
    if (message.contains('not routing') || message.contains('no usable')) {
      return 'Unavailable';
    }
    if (message.contains('opening') || message.contains('progressing')) {
      return 'Connecting';
    }
    return 'Waiting';
  }

  @override
  Widget build(BuildContext context) {
    final state = snapshot;
    final active = state?.routeActive ?? false;
    return Scaffold(
      body: Align(
        alignment: Alignment.topCenter,
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 480),
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(24, 32, 24, 24),
            child: Column(
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
                const SizedBox(height: 18),
                StatusRow(
                  title: 'Bluetooth',
                  value: state?.phoneId == null
                      ? 'Not connected'
                      : state!.phoneConnected
                      ? 'Connected'
                      : 'Disconnected',
                ),
                StatusRow(
                  title: 'Media',
                  value: connectionLabel(state?.mediaState),
                ),
                StatusRow(
                  title: 'Calls',
                  value: connectionLabel(state?.callsState),
                ),
                StatusRow(title: 'Microphone', value: microphoneLabel(state)),
                const SizedBox(height: 16),
                SelectRow(
                  title: 'Phone',
                  value: state == null
                      ? 'Checking'
                      : label(state.phones, state.phoneId, 'Select iPhone'),
                  valueColor: state?.phoneConnected == true ? green : muted,
                  choices: [
                    if (state?.phoneId != null)
                      const Choice('__stop__', 'Stop'),
                    ...?state?.phones.map(
                      (device) => Choice(device.id, device.name),
                    ),
                    const Choice('__pair__', 'Pair iPhone'),
                  ],
                  onSelected: (id) => change('selectPhone', id),
                ),
                const SizedBox(height: 8),
                SelectRow(
                  title: 'Microphone',
                  value: state == null
                      ? 'Checking'
                      : label(state.inputs, state.inputId, 'Select microphone'),
                  choices: [
                    ...?state?.inputs.map(
                      (device) => Choice(device.id, device.name),
                    ),
                  ],
                  onSelected: (id) => change('selectInput', id),
                ),
                const SizedBox(height: 8),
                SelectRow(
                  title: 'Headphones',
                  value: state == null
                      ? 'Checking'
                      : label(
                          state.outputs,
                          state.outputId,
                          'Select headphones',
                        ),
                  choices: [
                    ...?state?.outputs.map(
                      (device) => Choice(device.id, device.name),
                    ),
                  ],
                  onSelected: (id) => change('selectOutput', id),
                ),
                const SizedBox(height: 10),
                SwitchListTile(
                  contentPadding: EdgeInsets.zero,
                  dense: true,
                  title: const Text('Voice recording'),
                  value: state?.voiceMode ?? false,
                  onChanged: state?.phoneId == null
                      ? null
                      : (enabled) => change('setVoiceMode', enabled),
                ),
                OutlinedButton(
                  onPressed:
                      state?.inputId == null ||
                          state?.outputId == null ||
                          state?.testActive == true ||
                          active
                      ? null
                      : () => change('testAudio', null),
                  child: Text(
                    state?.testActive == true ? 'Speak now' : 'Test microphone',
                  ),
                ),
                if (state?.testMessage.isNotEmpty == true) ...[
                  const SizedBox(height: 6),
                  Text(
                    state!.testActive
                        ? 'Speak now'
                        : state.testPeak > 0.001
                        ? 'Signal detected'
                        : 'No signal',
                    style: const TextStyle(color: muted, fontSize: 11),
                  ),
                  const SizedBox(height: 6),
                  LinearProgressIndicator(
                    value: state.testPeak.clamp(0.0, 1.0),
                    color: green,
                    backgroundColor: border,
                    semanticsLabel: 'Microphone level',
                  ),
                ],
                const SizedBox(height: 10),
                Wrap(
                  spacing: 4,
                  children: [
                    TextButton(
                      onPressed: state?.phoneId == null
                          ? null
                          : () => change('reconnect', null),
                      child: const Text('Reconnect'),
                    ),
                    TextButton(
                      onPressed:
                          state?.phoneId == null || state?.voiceMode == true
                          ? null
                          : () => change('requestPcAudio', null),
                      child: const Text('Use PC for call'),
                    ),
                    PopupMenuButton<String>(
                      tooltip: 'More settings',
                      onSelected: (value) => change(value, null),
                      itemBuilder: (context) => const [
                        PopupMenuItem(
                          value: 'openSoundSettings',
                          child: Text('Sound'),
                        ),
                        PopupMenuItem(
                          value: 'openCallPermissions',
                          child: Text('Permissions'),
                        ),
                        PopupMenuItem(
                          value: 'openPhoneLink',
                          child: Text('Phone Link'),
                        ),
                      ],
                      child: const Padding(
                        padding: EdgeInsets.all(12),
                        child: Text('Settings'),
                      ),
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

class StatusRow extends StatelessWidget {
  const StatusRow({required this.title, required this.value, super.key});

  final String title;
  final String value;

  @override
  Widget build(BuildContext context) {
    final color = value == 'Connected' || value == 'Active'
        ? green
        : value == 'Blocked' || value == 'Failed'
        ? const Color(0xFFFBBF24)
        : muted;
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        children: [
          Expanded(
            child: Text(
              title,
              style: const TextStyle(color: white, fontSize: 12),
            ),
          ),
          Text(value, style: TextStyle(color: color, fontSize: 12)),
        ],
      ),
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
      height: 60,
      padding: const EdgeInsets.symmetric(horizontal: 14),
      decoration: BoxDecoration(
        color: card,
        border: Border.all(color: border),
        borderRadius: BorderRadius.circular(8),
      ),
      child: Row(
        children: [
          SizedBox(
            width: 112,
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
