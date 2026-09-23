import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

void main() => runApp(const BluetoothHfpApp());

const black = Color(0xFF000000);
const card = black;
const white = Color(0xFFFFFFFF);
const muted = Color(0xFF9A9FA9);
const border = Color(0xFF2B2E36);
const green = Color(0xFF22C55E);
const blue = Color(0xFF3B82F6);
const red = Color(0xFFEF4444);
const yellow = Color(0xFFFACC15);

class BluetoothHfpApp extends StatelessWidget {
  const BluetoothHfpApp({super.key});

  @override
  Widget build(BuildContext context) => MaterialApp(
    title: 'Bluetooth HFP',
    debugShowCheckedModeBanner: false,
    theme: ThemeData(
      brightness: Brightness.dark,
      scaffoldBackgroundColor: black,
      colorScheme: const ColorScheme.dark(
        surface: card,
        primary: blue,
        onPrimary: white,
        onSurface: white,
        outline: border,
      ),
      fontFamily: 'Segoe UI',
      textButtonTheme: TextButtonThemeData(
        style: TextButton.styleFrom(
          foregroundColor: white,
          minimumSize: const Size(0, 44),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(10),
          ),
        ),
      ),
      outlinedButtonTheme: OutlinedButtonThemeData(
        style: OutlinedButton.styleFrom(
          foregroundColor: white,
          minimumSize: const Size(0, 44),
          side: const BorderSide(color: border),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(10),
          ),
        ),
      ),
      filledButtonTheme: FilledButtonThemeData(
        style: FilledButton.styleFrom(
          minimumSize: const Size(0, 44),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(10),
          ),
        ),
      ),
      popupMenuTheme: PopupMenuThemeData(
        color: card,
        surfaceTintColor: Colors.transparent,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(12),
          side: const BorderSide(color: border),
        ),
      ),
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
    } on PlatformException {
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
    } on PlatformException {
      if (mounted) setState(() => error = 'Request failed');
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
          constraints: const BoxConstraints(maxWidth: 540),
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(24, 12, 24, 24),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Align(
                  alignment: Alignment.centerRight,
                  child: TextButton.icon(
                    onPressed: () => Navigator.of(context).push(
                      MaterialPageRoute<void>(
                        builder: (_) => SettingsPage(
                          onAction: (method) => change(method, null),
                        ),
                      ),
                    ),
                    icon: const Icon(Icons.settings_outlined, size: 20),
                    label: const Text('Settings'),
                  ),
                ),
                const SizedBox(height: 12),
                SelectRow(
                  title: 'Phone',
                  icon: Icons.smartphone_rounded,
                  selectedId: state?.phoneId,
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
                const SizedBox(height: 12),
                SelectRow(
                  title: 'Microphone',
                  icon: Icons.mic_none_rounded,
                  selectedId: state?.inputId,
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
                const SizedBox(height: 12),
                SelectRow(
                  title: 'Headphones',
                  icon: Icons.headphones_outlined,
                  selectedId: state?.outputId,
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
                const SizedBox(height: 16),
                Panel(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      SwitchListTile(
                        contentPadding: EdgeInsets.zero,
                        dense: true,
                        title: const Text('Voice recording'),
                        secondary: const Icon(
                          Icons.graphic_eq_rounded,
                          color: muted,
                          size: 22,
                        ),
                        value: state?.voiceMode ?? false,
                        onChanged: state?.phoneId == null
                            ? null
                            : (enabled) => change('setVoiceMode', enabled),
                      ),
                      const SizedBox(height: 8),
                      OutlinedButton.icon(
                        icon: const Icon(Icons.mic_none_rounded, size: 18),
                        onPressed:
                            state?.inputId == null ||
                                state?.outputId == null ||
                                state?.testActive == true ||
                                active
                            ? null
                            : () => change('testAudio', null),
                        label: Text(
                          state?.testActive == true
                              ? 'Speak now'
                              : 'Test microphone',
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
                          borderRadius: BorderRadius.circular(4),
                          minHeight: 5,
                        ),
                      ],
                    ],
                  ),
                ),
                const SizedBox(height: 20),
                LayoutBuilder(
                  builder: (context, constraints) {
                    final stacked =
                        constraints.maxWidth < 380 ||
                        MediaQuery.textScalerOf(context).scale(14) > 18;
                    final width = stacked
                        ? constraints.maxWidth
                        : (constraints.maxWidth - 12) / 2;
                    return Wrap(
                      spacing: 12,
                      runSpacing: 8,
                      children: [
                        SizedBox(
                          width: width,
                          child: OutlinedButton.icon(
                            icon: const Icon(Icons.refresh_rounded, size: 18),
                            onPressed: state?.phoneId == null
                                ? null
                                : () => change('reconnect', null),
                            label: const Text('Reconnect'),
                          ),
                        ),
                        SizedBox(
                          width: width,
                          child: FilledButton.icon(
                            icon: const Icon(
                              Icons.phone_forwarded_rounded,
                              size: 18,
                            ),
                            onPressed:
                                state?.phoneId == null ||
                                    state?.voiceMode == true
                                ? null
                                : () => change('requestPcAudio', null),
                            label: const Text('Use PC for call'),
                          ),
                        ),
                      ],
                    );
                  },
                ),
                const SizedBox(height: 20),
                Material(
                  color: black,
                  clipBehavior: Clip.antiAlias,
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(12),
                    side: const BorderSide(color: border),
                  ),
                  child: ExpansionTile(
                    key: const PageStorageKey('connection-status'),
                    initiallyExpanded: false,
                    tilePadding: const EdgeInsets.symmetric(horizontal: 16),
                    childrenPadding: const EdgeInsets.fromLTRB(16, 0, 16, 12),
                    shape: const Border(),
                    collapsedShape: const Border(),
                    iconColor: blue,
                    collapsedIconColor: muted,
                    textColor: white,
                    collapsedTextColor: muted,
                    leading: const Icon(Icons.monitor_heart_outlined, size: 20),
                    title: const Text(
                      'Bluetooth',
                      style: TextStyle(fontSize: 13),
                    ),
                    children: [
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
                      StatusRow(
                        title: 'Microphone',
                        value: microphoneLabel(state),
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class SettingsPage extends StatelessWidget {
  const SettingsPage({required this.onAction, super.key});
  final ValueChanged<String> onAction;

  @override
  Widget build(BuildContext context) => Scaffold(
    body: Align(
      alignment: Alignment.topCenter,
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 540),
        child: ListView(
          padding: const EdgeInsets.fromLTRB(24, 12, 24, 24),
          children: [
            const Row(
              children: [
                BackButton(),
                SizedBox(width: 8),
                Text(
                  'Settings',
                  style: TextStyle(fontSize: 18, fontWeight: FontWeight.w600),
                ),
              ],
            ),
            const SizedBox(height: 20),
            Panel(
              child: Column(
                children: [
                  SettingsRow(
                    title: 'Sound',
                    icon: Icons.volume_up_outlined,
                    onTap: () => onAction('openSoundSettings'),
                  ),
                  const Divider(color: border, height: 16),
                  SettingsRow(
                    title: 'Permissions',
                    icon: Icons.shield_outlined,
                    onTap: () => onAction('openCallPermissions'),
                  ),
                  const Divider(color: border, height: 16),
                  SettingsRow(
                    title: 'Phone Link',
                    icon: Icons.phonelink_rounded,
                    onTap: () => onAction('openPhoneLink'),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    ),
  );
}

class Panel extends StatelessWidget {
  const Panel({required this.child, super.key});
  final Widget child;

  @override
  Widget build(BuildContext context) => Material(
    color: card,
    shape: RoundedRectangleBorder(
      side: const BorderSide(color: border),
      borderRadius: BorderRadius.circular(14),
    ),
    child: Padding(padding: const EdgeInsets.all(12), child: child),
  );
}

class SettingsRow extends StatelessWidget {
  const SettingsRow({
    required this.title,
    required this.icon,
    required this.onTap,
    super.key,
  });
  final String title;
  final IconData icon;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => ListTile(
    contentPadding: const EdgeInsets.symmetric(horizontal: 8),
    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
    leading: Icon(icon, color: muted, size: 22),
    title: Text(title, style: const TextStyle(fontSize: 14)),
    trailing: const Icon(Icons.open_in_new_rounded, color: muted, size: 18),
    onTap: onTap,
  );
}

class StatusRow extends StatelessWidget {
  const StatusRow({required this.title, required this.value, super.key});

  final String title;
  final String value;

  @override
  Widget build(BuildContext context) {
    final color = switch (value) {
      'Connected' || 'Active' => green,
      'Failed' ||
      'Blocked' ||
      'Timed out' ||
      'Request failed' ||
      'Request timed out' => red,
      'Connecting' ||
      'Waiting' ||
      'Unavailable' ||
      'Disconnected' ||
      'Status unavailable' ||
      'Connection unavailable' => yellow,
      'Checking' => blue,
      _ => muted,
    };
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        children: [
          Icon(
            switch (title) {
              'Bluetooth' => Icons.bluetooth_rounded,
              'Media' => Icons.music_note_outlined,
              'Calls' => Icons.call_outlined,
              _ => Icons.mic_none_rounded,
            },
            size: 16,
            color: muted,
          ),
          const SizedBox(width: 10),
          Flexible(
            fit: FlexFit.tight,
            child: Text(
              title,
              style: const TextStyle(color: white, fontSize: 12),
            ),
          ),
          Container(
            width: 6,
            height: 6,
            decoration: BoxDecoration(color: color, shape: BoxShape.circle),
          ),
          const SizedBox(width: 7),
          Text(
            value,
            textAlign: TextAlign.end,
            style: TextStyle(color: color, fontSize: 12),
          ),
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
    required this.icon,
    this.selectedId,
    this.valueColor = muted,
    super.key,
  });

  final String title;
  final String value;
  final IconData icon;
  final String? selectedId;
  final Color valueColor;
  final List<Choice> choices;
  final ValueChanged<String?> onSelected;

  @override
  Widget build(BuildContext context) => PopupMenuButton<String?>(
    borderRadius: BorderRadius.circular(12),
    enabled: choices.isNotEmpty,
    tooltip: title,
    color: card,
    surfaceTintColor: card,
    elevation: 12,
    position: PopupMenuPosition.under,
    offset: const Offset(0, 4),
    constraints: BoxConstraints(
      minWidth: 220,
      maxWidth: (MediaQuery.sizeOf(context).width - 48).clamp(220.0, 460.0),
    ),
    onSelected: onSelected,
    itemBuilder: (_) => choices
        .map(
          (choice) => PopupMenuItem<String?>(
            value: choice.id,
            child: Row(
              children: [
                Icon(
                  choice.id == '__pair__'
                      ? Icons.add_rounded
                      : choice.id == '__stop__'
                      ? Icons.stop_circle_outlined
                      : choice.id == selectedId
                      ? Icons.check_rounded
                      : icon,
                  size: 18,
                  color: choice.id == selectedId ? blue : muted,
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: Text(
                    choice.label,
                    style: TextStyle(
                      color: choice.id == selectedId ? blue : white,
                      fontSize: 13,
                    ),
                  ),
                ),
              ],
            ),
          ),
        )
        .toList(),
    child: Ink(
      width: double.infinity,
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      decoration: BoxDecoration(
        color: card,
        border: Border.all(color: border),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Row(
        children: [
          Icon(icon, color: muted, size: 22),
          const SizedBox(width: 14),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  title,
                  style: const TextStyle(
                    color: muted,
                    fontSize: 11,
                    fontWeight: FontWeight.w500,
                  ),
                ),
                const SizedBox(height: 4),
                Tooltip(
                  message: value,
                  child: Text(
                    value,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      color: choices.isEmpty
                          ? muted
                          : valueColor == muted
                          ? white
                          : valueColor,
                      fontSize: 13,
                    ),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(width: 14),
          const Icon(Icons.keyboard_arrow_down, color: muted, size: 18),
        ],
      ),
    ),
  );
}
