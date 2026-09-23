import 'package:bluetooth_hfp/main.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  const channel = MethodChannel('bluetooth_hfp/windows');
  final commands = <MethodCall>[];
  setUp(() {
    commands.clear();
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
          if (call.method != 'snapshot') {
            commands.add(call);
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
            'message': 'Select an iPhone.',
            'mediaActive': true,
            'mediaMessage': 'Media connected.',
            'callsMessage': 'Windows denied call access.',
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
    expect(find.text('Media: Media connected.'), findsOneWidget);
    expect(find.text('Calls: Windows denied call access.'), findsOneWidget);
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
}
