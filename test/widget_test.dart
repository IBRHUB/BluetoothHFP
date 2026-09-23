import 'package:bluetooth_hfp/main.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  const channel = MethodChannel('bluetooth_hfp/windows');
  final commands = <MethodCall>[];
  var voiceMode = false;
  setUp(() {
    commands.clear();
    voiceMode = false;
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
          if (call.method != 'snapshot') {
            commands.add(call);
            if (call.method == 'setVoiceMode') {
              voiceMode = call.arguments as bool;
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

  testWidgets('shows the three device controls', (tester) async {
    await tester.pumpWidget(const BluetoothHfpApp());
    await tester.pumpAndSettle();
    expect(find.byType(PopupMenuButton<String?>), findsNWidgets(3));
    expect(find.text('Bluetooth'), findsOneWidget);
    expect(find.text('Input'), findsOneWidget);
    expect(find.text('Output'), findsOneWidget);
    expect(find.text('Media · Connected'), findsOneWidget);
    expect(find.text('Calls in this app · Access denied'), findsOneWidget);
    expect(find.text('Bluetooth link · Connected'), findsOneWidget);
    expect(tester.widget<Text>(find.text('iPhone')).style?.color, green);
    expect(find.text('v1.0.1+2 · Installed app'), findsOneWidget);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('Stop routing dispatches null instead of dismissing the menu', (
    tester,
  ) async {
    await tester.pumpWidget(const BluetoothHfpApp());
    await tester.pumpAndSettle();
    await tester.tap(find.text('Bluetooth'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Stop routing'));
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
      final button = find.text('Use PC for active call');
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
    final button = find.text('Test mic through headphones (5 seconds)');
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
    final transfer = find.widgetWithText(TextButton, 'Use PC for active call');
    expect(tester.widget<TextButton>(transfer).onPressed, isNull);
    expect(
      find.text('Bluetooth microphone: Select an iPhone.'),
      findsOneWidget,
    );
    await tester.ensureVisible(toggle);
    await tester.tap(toggle);
    await tester.pumpAndSettle();
    expect(commands.last.arguments, false);
    expect(tester.widget<TextButton>(transfer).onPressed, isNotNull);
    expect(commands.where((call) => call.method == 'requestPcAudio'), isEmpty);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('timeouts are displayed as failure instead of connecting', (
    tester,
  ) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(
          body: ConnectionStatusLine(
            title: 'Media',
            state: 'timeout',
            message: 'Opening media did not respond.',
          ),
        ),
      ),
    );
    expect(find.text('Media · Timed out'), findsOneWidget);
    expect(find.text('Opening media did not respond.'), findsOneWidget);
    expect(find.textContaining('In progress'), findsNothing);
  });
}
