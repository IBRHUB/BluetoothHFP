import 'package:bluetooth_hfp/main.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  const channel = MethodChannel('bluetooth_hfp/windows');
  final commands = <MethodCall>[];
  var voiceMode = false;
  var wiredMode = false;
  var wiredRunning = false;
  String? wiredCapture;
  String? wiredRender;
  setUp(() {
    commands.clear();
    voiceMode = false;
    wiredMode = false;
    wiredRunning = false;
    wiredCapture = null;
    wiredRender = null;
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
          if (call.method != 'snapshot') {
            commands.add(call);
            if (call.method == 'setVoiceMode') {
              voiceMode = call.arguments as bool;
            }
            if (call.method == 'setWiredMode') {
              wiredMode = call.arguments as bool;
            }
            if (call.method == 'setWiredRunning') {
              wiredRunning = call.arguments as bool;
            }
            if (call.method == 'selectWiredCapture') {
              wiredCapture = call.arguments as String;
            }
            if (call.method == 'selectWiredRender') {
              wiredRender = call.arguments as String;
            }
            return null;
          }
          return {
            'phones': [
              {'id': 'phone-1', 'name': 'iPhone'},
            ],
            'inputs': [
              {'id': 'mic-1', 'name': 'Microphone'},
            ],
            'outputs': [
              {'id': 'out-1', 'name': 'Headphones'},
            ],
            'phoneId': 'phone-1',
            'phoneConnected': true,
            'inputId': 'mic-1',
            'outputId': 'out-1',
            'routeActive': false,
            'voiceMode': voiceMode,
            'wiredMode': wiredMode,
            'wiredRunning': wiredRunning,
            'wiredAvailable': wiredCapture != null && wiredRender != null,
            'wiredCaptureId': wiredCapture,
            'wiredRenderId': wiredRender,
            'wiredInputs': [
              {'id': 'interface-in', 'name': 'Interface input'},
            ],
            'wiredOutputs': [
              {'id': 'interface-out', 'name': 'Interface output'},
            ],
            'message': 'Select an iPhone.',
            'mediaActive': true,
            'mediaMessage': 'Media connected.',
            'callsMessage': 'Windows denied call access.',
            'mediaState': 'connected',
            'callsState': 'blocked',
            'appVersion': '1.0.1+2',
            'packaged': true,
          };
        });
  });
  tearDown(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, null);
  });

  testWidgets('shows the simplified device controls', (tester) async {
    await tester.pumpWidget(const BluetoothHfpApp());
    await tester.pumpAndSettle();
    expect(find.byType(PopupMenuButton<String?>), findsNWidgets(3));
    expect(find.byType(StatusRow), findsNothing);
    await tester.ensureVisible(find.byType(ExpansionTile));
    await tester.ensureVisible(find.text('Bluetooth').first);
    await tester.tap(find.text('Bluetooth').first);
    await tester.pumpAndSettle();
    expect(find.text('Phone'), findsOneWidget);
    expect(find.text('Microphone'), findsNWidgets(3));
    expect(find.text('Headphones'), findsNWidgets(2));
    expect(find.text('Connected'), findsNWidgets(2));
    expect(find.text('Blocked'), findsOneWidget);
    expect(find.text('Bluetooth'), findsNWidgets(2));
    expect(tester.widget<Text>(find.text('iPhone')).style?.color, green);
    expect(find.text('Bluetooth HFP'), findsNothing);
    await tester.ensureVisible(find.text('Bluetooth').first);
    await tester.tap(find.text('Bluetooth').first);
    await tester.pumpAndSettle();
    expect(find.byType(StatusRow), findsNothing);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets(
    'wired bridge requires interface selection and can stop without Bluetooth',
    (tester) async {
      await tester.pumpWidget(const BluetoothHfpApp());
      await tester.pumpAndSettle();
      await tester.tap(find.text('Wired mode'));
      await tester.pumpAndSettle();
      expect(find.text('Phone'), findsNothing);
      expect(find.text('Use PC for call'), findsNothing);
      expect(find.text('Voice recording'), findsNothing);
      final start = find.widgetWithText(FilledButton, 'Start');
      expect(tester.widget<FilledButton>(start).onPressed, isNull);
      for (final entry in {
        'From phone': 'Interface input',
        'To phone': 'Interface output',
      }.entries) {
        await tester.ensureVisible(find.text(entry.key));
        await tester.tap(find.text(entry.key));
        await tester.pumpAndSettle();
        await tester.tap(find.text(entry.value));
        await tester.pumpAndSettle();
      }
      await tester.ensureVisible(start);
      await tester.tap(start);
      await tester.pumpAndSettle();
      expect(commands.last.method, 'setWiredRunning');
      expect(commands.last.arguments, true);
      expect(
        tester
            .widget<OutlinedButton>(
              find.widgetWithText(OutlinedButton, 'Test microphone'),
            )
            .onPressed,
        isNull,
      );
      await tester.tap(find.text('Stop'));
      await tester.pumpAndSettle();
      expect(commands.last.arguments, false);
      expect(
        commands.where(
          (c) =>
              c.method == 'selectPhone' ||
              c.method == 'requestPcAudio' ||
              c.method == 'reconnect',
        ),
        isEmpty,
      );
      expect(tester.takeException(), isNull);
      await tester.pumpWidget(const SizedBox());
    },
  );
  testWidgets('Stop routing dispatches null instead of dismissing the menu', (
    tester,
  ) async {
    await tester.pumpWidget(const BluetoothHfpApp());
    await tester.pumpAndSettle();
    await tester.tap(find.text('Phone'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Stop'));
    await tester.pumpAndSettle();
    expect(commands.single.method, 'selectPhone');
    expect(commands.single.arguments, isNull);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('Reconnect retries even when media is already connected', (
    tester,
  ) async {
    await tester.pumpWidget(const BluetoothHfpApp());
    await tester.pumpAndSettle();
    await tester.ensureVisible(find.text('Reconnect'));
    await tester.tap(find.text('Reconnect'));
    await tester.pumpAndSettle();
    expect(commands.single.method, 'reconnect');
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets(
    'call transfer dispatches a separate request without reconnecting',
    (tester) async {
      await tester.pumpWidget(const BluetoothHfpApp());
      await tester.pumpAndSettle();
      final button = find.text('Use PC for call');
      await tester.ensureVisible(button);
      await tester.tap(button);
      await tester.pumpAndSettle();
      expect(commands.single.method, 'requestPcAudio');
      await tester.pumpWidget(const SizedBox());
    },
  );

  testWidgets('local audio test works without an active iPhone call', (
    tester,
  ) async {
    await tester.pumpWidget(const BluetoothHfpApp());
    await tester.pumpAndSettle();
    final button = find.text('Test microphone');
    await tester.ensureVisible(button);
    await tester.tap(button);
    await tester.pumpAndSettle();
    expect(commands.single.method, 'testAudio');
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('voice mode toggles without transferring a call', (tester) async {
    await tester.pumpWidget(const BluetoothHfpApp());
    await tester.pumpAndSettle();
    final toggle = find.byType(SwitchListTile);
    await tester.ensureVisible(toggle);
    await tester.tap(toggle);
    await tester.pumpAndSettle();
    expect(commands.single.method, 'setVoiceMode');
    expect(commands.single.arguments, true);
    expect(tester.widget<SwitchListTile>(toggle).value, isTrue);
    final transfer = find.widgetWithText(FilledButton, 'Use PC for call');
    expect(tester.widget<FilledButton>(transfer).onPressed, isNull);
    expect(find.byType(StatusRow), findsNothing);
    await tester.ensureVisible(toggle);
    await tester.tap(toggle);
    await tester.pumpAndSettle();
    expect(commands.last.arguments, false);
    expect(tester.widget<FilledButton>(transfer).onPressed, isNotNull);
    expect(commands.where((call) => call.method == 'requestPcAudio'), isEmpty);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('status labels stay short', (tester) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(
          body: StatusRow(title: 'Media', value: 'Timed out'),
        ),
      ),
    );
    expect(find.text('Media'), findsOneWidget);
    expect(find.text('Timed out'), findsOneWidget);
  });

  testWidgets('settings button opens a separate page and returns to devices', (
    tester,
  ) async {
    await tester.pumpWidget(const BluetoothHfpApp());
    await tester.pumpAndSettle();
    await tester.tap(find.text('Settings'));
    await tester.pumpAndSettle();
    expect(find.text('Select iPhone'), findsNothing);
    expect(find.byType(SelectRow), findsNothing);
    for (final entry in {
      'Sound': 'openSoundSettings',
      'Permissions': 'openCallPermissions',
      'Phone Link': 'openPhoneLink',
    }.entries) {
      await tester.ensureVisible(find.text(entry.key));
      await tester.tap(find.text(entry.key));
      await tester.pumpAndSettle();
      expect(commands.last.method, entry.value);
    }
    await tester.tap(find.byType(BackButton));
    await tester.pumpAndSettle();
    expect(find.byType(SelectRow), findsNWidgets(3));
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('controls fit a narrow window with large text', (tester) async {
    tester.view.physicalSize = const Size(360, 740);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData.dark(),
        home: MediaQuery(
          data: const MediaQueryData(
            size: Size(360, 740),
            textScaler: TextScaler.linear(1.3),
          ),
          child: const HfpHome(),
        ),
      ),
    );
    await tester.pumpAndSettle();
    await tester.ensureVisible(find.text('Use PC for call'));
    expect(tester.takeException(), isNull);
    await tester.ensureVisible(find.text('Phone'));
    await tester.tap(find.text('Phone'));
    await tester.pumpAndSettle();
    expect(find.byIcon(Icons.check_rounded), findsOneWidget);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });
}
