import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';

import 'controller.dart';

void main(List<String> args) {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(
    HeadsetApp(
      smoke: args.contains('--smoke'),
      enableAccessibility: args.contains('--enable-accessibility'),
    ),
  );
}

class HeadsetApp extends StatelessWidget {
  const HeadsetApp({
    super.key,
    this.smoke = false,
    this.controller,
    this.enableAccessibility = false,
  });
  final bool smoke;
  final bool enableAccessibility;
  final HeadsetController? controller;
  @override
  Widget build(BuildContext context) => MaterialApp(
    title: 'Bluetooth HFP',
    locale: const Locale('en'),
    debugShowCheckedModeBanner: false,
    // Flutter's Windows accessibility bridge crashes while reparenting nodes
    // in this engine build (#175041). Keep a stable, empty native tree until
    // an engine with the upstream fix is available. Keyboard focus is unchanged.
    // The launch flag allows testing screen readers after an engine update.
    builder: (context, child) => ExcludeSemantics(
      excluding: Platform.isWindows && !enableAccessibility,
      child: child ?? const SizedBox.shrink(),
    ),
    theme: ThemeData(
      brightness: Brightness.dark,
      useMaterial3: true,
      scaffoldBackgroundColor: Colors.black,
      colorScheme: ColorScheme.fromSeed(
        seedColor: const Color(0xff64b5f6),
        brightness: Brightness.dark,
        primary: const Color(0xff64b5f6),
        onPrimary: Colors.black,
        secondary: const Color(0xff64b5f6),
        onSecondary: Colors.black,
        onSecondaryContainer: Colors.white,
        surface: Colors.black,
        onSurface: Colors.white,
        onSurfaceVariant: const Color(0xffbdbdbd),
        outline: const Color(0xff757575),
        outlineVariant: const Color(0xff616161),
        surfaceContainerHighest: Colors.black,
        surfaceContainerHigh: Colors.black,
        surfaceContainer: Colors.black,
        surfaceContainerLow: Colors.black,
        surfaceContainerLowest: Colors.black,
        surfaceTint: Colors.transparent,
        error: const Color(0xffef5350),
      ),
      disabledColor: const Color(0xff999999),
      iconTheme: const IconThemeData(color: Colors.white, size: 24),
      filledButtonTheme: FilledButtonThemeData(
        style: FilledButton.styleFrom(
          backgroundColor: const Color(0xff1565c0),
          foregroundColor: Colors.white,
          disabledBackgroundColor: Colors.black,
          disabledForegroundColor: const Color(0xff999999),
          minimumSize: const Size(0, 44),
        ),
      ),
      outlinedButtonTheme: OutlinedButtonThemeData(
        style: OutlinedButton.styleFrom(
          foregroundColor: const Color(0xff64b5f6),
          disabledForegroundColor: const Color(0xff999999),
          side: const BorderSide(color: Color(0xff757575)),
          minimumSize: const Size(0, 44),
        ),
      ),
      iconButtonTheme: IconButtonThemeData(
        style: ButtonStyle(
          foregroundColor: WidgetStateProperty.resolveWith(
            (states) => states.contains(WidgetState.disabled)
                ? const Color(0xff999999)
                : Colors.white,
          ),
        ),
      ),
      tooltipTheme: TooltipThemeData(
        textStyle: const TextStyle(color: Colors.white, fontSize: 12),
        decoration: BoxDecoration(
          color: Colors.black,
          border: Border.all(color: const Color(0xff757575)),
          borderRadius: BorderRadius.circular(6),
        ),
      ),
      dialogTheme: const DialogThemeData(
        backgroundColor: Colors.black,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.all(Radius.circular(20)),
          side: BorderSide(color: Color(0xff757575)),
        ),
      ),
      canvasColor: Colors.black,
      chipTheme: const ChipThemeData(backgroundColor: Colors.black),
      navigationBarTheme: NavigationBarThemeData(
        backgroundColor: Colors.black,
        indicatorColor: const Color(0xff1565c0),
        surfaceTintColor: Colors.transparent,
        iconTheme: WidgetStateProperty.resolveWith(
          (states) => IconThemeData(
            color: states.contains(WidgetState.selected)
                ? Colors.white
                : const Color(0xffbdbdbd),
            size: 24,
          ),
        ),
        labelTextStyle: WidgetStateProperty.resolveWith(
          (states) => TextStyle(
            color: states.contains(WidgetState.selected)
                ? Colors.white
                : const Color(0xffbdbdbd),
            fontSize: 12,
            fontWeight: FontWeight.w600,
          ),
        ),
      ),
      fontFamily: 'Segoe UI',
      cardTheme: const CardThemeData(
        color: Colors.black,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.all(Radius.circular(16)),
          side: BorderSide(color: Color(0xff616161)),
        ),
        elevation: 0,
        margin: EdgeInsets.only(bottom: 16),
      ),
      inputDecorationTheme: const InputDecorationTheme(
        border: OutlineInputBorder(),
        isDense: true,
      ),
    ),
    home: Directionality(
      textDirection: TextDirection.ltr,
      child: Home(controller: controller, smoke: smoke),
    ),
  );
}

class Home extends StatefulWidget {
  const Home({super.key, this.controller, this.smoke = false});
  final HeadsetController? controller;
  final bool smoke;
  @override
  State<Home> createState() => _HomeState();
}

class _HomeState extends State<Home> {
  late final HeadsetController c;
  int page = 0;
  bool closing = false;
  bool resumeHeadset = false;
  @override
  void initState() {
    super.initState();
    c = widget.controller ?? HeadsetController();
    c.addListener(update);
    HeadsetController.platform.setMethodCallHandler((call) async {
      if (call.method == 'closeRequested') await close();
      if (call.method == 'suspend') {
        resumeHeadset = c.running;
        await c.perform(c.stop);
      }
      if (call.method == 'resume' && resumeHeadset) {
        resumeHeadset = false;
        await Future<void>.delayed(const Duration(seconds: 3));
        if (mounted && !closing) await c.enable();
      }
    });
    if (widget.controller == null) unawaited(boot());
  }

  Future<void> boot() async {
    await c.perform(c.initialize);
    if (widget.smoke) {
      final reconnect = c.autoReconnect;
      c.autoReconnect = false;
      await c.enable();
      if (c.ready) {
        c.scan();
        await Future<void>.delayed(const Duration(seconds: 9));
      }
      final firstReady = c.ready;
      await c.perform(c.stop);
      await c.restore();
      final restored = c.service == 'BTHUSB' && c.error.isEmpty;
      c.autoReconnect = reconnect;
      await c.enable();
      await File('${c.data.path}\\desktop-smoke.json').writeAsString(
        jsonEncode({
          'firstReady': firstReady,
          'restored': restored,
          'secondReady': c.ready,
          'error': c.error,
          'logs': c.logs,
        }),
      );
    }
  }

  void update() {
    if (mounted) setState(() {});
  }

  @override
  void dispose() {
    HeadsetController.platform.setMethodCallHandler(null);
    c.removeListener(update);
    if (widget.controller == null) c.dispose();
    super.dispose();
  }

  Future<void> close() async {
    if (closing || c.busy) return;
    closing = true;
    final choice = await showDialog<String>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Close app?'),
        content: const Text(
          'Stop the headset and choose which Bluetooth mode to keep.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('Cancel'),
          ),
          TextButton(
            onPressed: () => Navigator.pop(context, 'keep'),
            child: const Text('Keep headset mode'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, 'native'),
            child: const Text('Restore Windows and exit'),
          ),
        ],
      ),
    );
    if (choice != null) {
      if (choice == 'native') {
        await c.restore();
      } else {
        await c.perform(() async {
          await c.save();
          await c.stop();
        });
      }
      if (c.error.isEmpty) {
        await HeadsetController.platform.invokeMethod<void>('close');
      }
    }
    closing = false;
  }

  Future<void> start() async {
    if (c.service != 'WINUSB') {
      final accepted = await showDialog<bool>(
        context: context,
        builder: (context) => AlertDialog(
          title: const Text('Use adapter as a headset'),
          content: const Text(
            'This saves your current driver and switches the adapter to headset mode. Other Bluetooth devices on this adapter will disconnect. Restore Windows Bluetooth in Settings when you are done.',
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context, false),
              child: const Text('Cancel'),
            ),
            FilledButton(
              onPressed: () => Navigator.pop(context, true),
              child: const Text('Start headset'),
            ),
          ],
        ),
      );
      if (accepted != true) return;
    }
    await c.enable();
  }

  Widget section(String title, IconData icon, List<Widget> children) => Card(
    child: Padding(
      padding: const EdgeInsets.all(18),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              Icon(icon, color: Theme.of(context).colorScheme.primary),
              const SizedBox(width: 10),
              Text(
                title,
                style: const TextStyle(
                  fontSize: 19,
                  fontWeight: FontWeight.w600,
                ),
              ),
            ],
          ),
          const SizedBox(height: 18),
          ...children,
        ],
      ),
    ),
  );
  Widget audioPicker(bool capture) {
    final value = capture ? c.capture : c.render;
    final items = c.endpoints.where((e) => e['capture'] == capture).toList();
    final selected = items.any((e) => e['id'] == value) ? value : '';
    return DropdownButtonFormField<String>(
      key: ValueKey('${capture}_$selected'),
      initialValue: selected,
      isExpanded: true,
      decoration: InputDecoration(
        labelText: capture ? 'Microphone' : 'Speakers',
      ),
      items: [
        const DropdownMenuItem(value: '', child: Text('Windows default')),
        ...items.map(
          (e) => DropdownMenuItem(
            value: '${e['id']}',
            child: Text('${e['name']}', overflow: TextOverflow.ellipsis),
          ),
        ),
      ],
      onChanged: c.running || c.busy
          ? null
          : (value) {
              setState(() {
                if (capture) {
                  c.capture = value!;
                } else {
                  c.render = value!;
                }
              });
              unawaited(c.saveInBackground());
            },
    );
  }

  @override
  Widget build(BuildContext context) => Scaffold(
    appBar: AppBar(
      title: const Text('Bluetooth HFP'),
      backgroundColor: Colors.black,
      surfaceTintColor: Colors.transparent,
      actions: [
        IconButton(
          onPressed: c.busy ? null : () => c.perform(c.refresh),
          icon: const Icon(Icons.refresh),
          tooltip: 'Refresh',
        ),
        IconButton(
          onPressed: close,
          tooltip: 'Close',
          icon: const Icon(Icons.power_settings_new),
        ),
        const SizedBox(width: 12),
      ],
    ),
    bottomNavigationBar: NavigationBar(
      selectedIndex: page,
      onDestinationSelected: (value) => setState(() => page = value),
      destinations: const [
        NavigationDestination(icon: Icon(Icons.bluetooth), label: 'Connection'),
        NavigationDestination(icon: Icon(Icons.volume_up), label: 'Audio'),
        NavigationDestination(icon: Icon(Icons.settings), label: 'Settings'),
      ],
    ),
    body: IndexedStack(index: page, children: List.generate(3, buildPage)),
  );

  Widget buildPage(int page) => SingleChildScrollView(
    key: PageStorageKey(page),
    padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 16),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Text(
          c.message,
          style: TextStyle(
            color: c.connected || c.ready
                ? const Color(0xff66bb6a)
                : Colors.grey,
          ),
        ),
        const SizedBox(height: 20),
        if (c.busy)
          const Padding(
            padding: EdgeInsets.only(bottom: 16),
            child: LinearProgressIndicator(),
          ),
        if (c.error.isNotEmpty)
          Card(
            color: Colors.black,
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: SelectableText(
                c.error,
                style: const TextStyle(color: Color(0xffef5350)),
              ),
            ),
          ),
        if (page == 0)
          section('Headset', Icons.bluetooth, [
            Text(
              c.supported ? 'Intel AX201' : 'No supported adapter detected',
              style: const TextStyle(fontWeight: FontWeight.w600),
            ),
            const SizedBox(height: 8),
            Text(
              c.service == 'WINUSB' ? 'Headset mode' : 'Normal Windows mode',
            ),
            const SizedBox(height: 14),
            Wrap(
              spacing: 12,
              runSpacing: 10,
              children: [
                FilledButton.icon(
                  onPressed: c.busy || !c.supported || c.running ? null : start,
                  icon: const Icon(Icons.headset),
                  label: const Text('Start headset'),
                ),
                OutlinedButton.icon(
                  onPressed: c.busy || !c.running
                      ? null
                      : () => c.perform(c.stop),
                  icon: const Icon(Icons.stop),
                  label: const Text('Stop'),
                ),
              ],
            ),
          ]),
        if (page == 0)
          section('Phone', Icons.phone_iphone, [
            Text(
              c.connected
                  ? 'Connected to ${c.peer}'
                  : 'On your iPhone, pair with AX201 HFP Headset.',
            ),
            const SizedBox(height: 12),
            Wrap(
              spacing: 10,
              runSpacing: 8,
              children: [
                OutlinedButton.icon(
                  onPressed: !c.ready || c.scanning || c.connected || c.busy
                      ? null
                      : c.scan,
                  icon: const Icon(Icons.search),
                  label: Text(c.scanning ? 'Scanning...' : 'Scan'),
                ),
                OutlinedButton(
                  onPressed: !c.ready || c.busy ? null : manualAddress,
                  child: const Text('Add phone'),
                ),
                OutlinedButton(
                  onPressed: !c.connected || c.busy ? null : c.disconnect,
                  child: const Text('Disconnect'),
                ),
              ],
            ),
            for (final address in c.phones)
              ListTile(
                contentPadding: EdgeInsets.zero,
                title: Text(address, textDirection: TextDirection.ltr),
                subtitle: Text(
                  c.peer == address && c.connected
                      ? 'Connected'
                      : 'Saved phone',
                ),
                leading: const Icon(Icons.smartphone),
                trailing: Wrap(
                  children: [
                    IconButton(
                      tooltip: 'Connect',
                      onPressed: c.ready && !c.connected && !c.busy
                          ? () => c.connect(address)
                          : null,
                      icon: const Icon(Icons.link),
                    ),
                    IconButton(
                      tooltip: 'Forget local pairing',
                      onPressed: c.ready && !c.connected && !c.busy
                          ? () => forget(address)
                          : null,
                      icon: const Icon(Icons.delete_outline),
                    ),
                  ],
                ),
              ),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('Reconnect automatically'),
              value: c.autoReconnect,
              onChanged: (v) {
                c.autoReconnect = v;
                c.changed();
                unawaited(c.saveInBackground());
              },
            ),
            Wrap(
              spacing: 10,
              runSpacing: 8,
              children: [
                Chip(
                  label: Text('Call: ${c.call.isEmpty ? 'Inactive' : c.call}'),
                ),
                Chip(
                  label: Text(
                    'Media: ${c.media.isEmpty ? 'Inactive' : c.media}',
                  ),
                ),
              ],
            ),
          ]),
        if (page == 1)
          section('Audio', Icons.tune, [
            audioPicker(true),
            const SizedBox(height: 14),
            audioPicker(false),
            const SizedBox(height: 14),
            DropdownButtonFormField<String>(
              initialValue: c.codec,
              isExpanded: true,
              decoration: const InputDecoration(labelText: 'Call quality'),
              items: const [
                DropdownMenuItem(value: 'Auto', child: Text('Automatic')),
                DropdownMenuItem(value: 'mSBC', child: Text('mSBC (wideband)')),
                DropdownMenuItem(value: 'CVSD', child: Text('CVSD')),
              ],
              onChanged: c.running || c.busy
                  ? null
                  : (v) {
                      c.codec = v!;
                      c.changed();
                      unawaited(c.saveInBackground());
                    },
            ),
            const SizedBox(height: 8),
            const Text(
              'Stop the headset to change devices or call quality.',
              style: TextStyle(color: Color(0xffffca28), fontSize: 12),
            ),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('Mute microphone'),
              value: c.muted,
              onChanged: c.setMute,
            ),
            Text('Microphone level: ${c.micLevel}%'),
            Slider(
              value: c.micLevel.toDouble(),
              min: 0,
              max: 100,
              onChanged: (v) => c.setMic(v.round()),
              onChangeEnd: (_) => unawaited(c.saveInBackground()),
            ),
            Text('Speaker level: ${(c.volume * 100 / 127).round()}%'),
            Slider(
              value: c.volume.toDouble(),
              min: 0,
              max: 127,
              onChanged: (v) => c.setVolume(v.round()),
              onChangeEnd: (_) => unawaited(c.saveInBackground()),
            ),
            Align(
              alignment: Alignment.centerLeft,
              child: TextButton(
                onPressed: () => c.perform(
                  () => HeadsetController.platform.invokeMethod<void>(
                    'soundSettings',
                  ),
                ),
                child: const Text('Windows sound settings'),
              ),
            ),
          ]),
        if (page == 2)
          section('Settings', Icons.settings, [
            OutlinedButton.icon(
              onPressed: c.busy || !c.supported ? null : c.restore,
              icon: const Icon(Icons.settings_backup_restore),
              label: const Text('Restore Windows Bluetooth'),
            ),
            const SizedBox(height: 10),
            Align(
              alignment: Alignment.centerLeft,
              child: OutlinedButton.icon(
                onPressed: () async {
                  String? path;
                  await c.perform(() async {
                    path = await c.exportDiagnostics();
                  });
                  if (path == null) return;
                  if (!mounted) return;
                  ScaffoldMessenger.of(context).showSnackBar(
                    SnackBar(
                      content: SelectableText('Report saved: $path'),
                      duration: const Duration(seconds: 12),
                    ),
                  );
                },
                icon: const Icon(Icons.save_alt),
                label: const Text('Save report'),
              ),
            ),
            ExpansionTile(
              key: const PageStorageKey('logs-expanded'),
              title: const Text('Logs'),
              children: [
                SizedBox(
                  height: 190,
                  child: SingleChildScrollView(
                    key: const PageStorageKey('logs-scroll'),
                    padding: const EdgeInsets.all(12),
                    child: SelectableText(
                      c.logs.isEmpty
                          ? 'No logs yet.'
                          : c.logs.reversed.take(80).join('\n'),
                      textDirection: TextDirection.ltr,
                      style: const TextStyle(
                        color: Color(0xffbdbdbd),
                        fontSize: 12,
                        fontFamily: 'Consolas',
                      ),
                    ),
                  ),
                ),
              ],
            ),
          ]),
      ],
    ),
  );
  Future<void> manualAddress() async {
    final input = TextEditingController();
    final value = await showDialog<String>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Bluetooth device address'),
        content: TextField(
          controller: input,
          textDirection: TextDirection.ltr,
          decoration: const InputDecoration(hintText: 'AA:BB:CC:DD:EE:FF'),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, input.text.trim()),
            child: const Text('Connect'),
          ),
        ],
      ),
    );
    if (value != null) c.connect(value);
  }

  Future<void> forget(String address) async {
    final yes = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Forget device'),
        content: const Text(
          'The local pairing key will be deleted. Also select Forget This Device on your iPhone before pairing again.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text('Forget'),
          ),
        ],
      ),
    );
    if (yes == true) c.forget(address);
  }
}
