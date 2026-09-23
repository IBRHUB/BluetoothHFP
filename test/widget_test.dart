import 'package:bluetooth_hfp/main.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
      .setMockMethodCallHandler(
        const MethodChannel('bluetooth_hfp/windows'),
        (call) async => {
          'phones': [
            {'id': 'phone-1', 'name': 'iPhone'},
          ],
          'inputs': [
            {'id': 'mic-1', 'name': 'Microphone'},
          ],
          'outputs': [
            {'id': 'out-1', 'name': 'Headphones'},
          ],
          'phoneId': null,
          'phoneConnected': false,
          'inputId': 'mic-1',
          'outputId': 'out-1',
          'routeActive': false,
          'message': 'Select an iPhone.',
        },
      );

  testWidgets('shows the three device controls', (tester) async {
    await tester.pumpWidget(const BluetoothHfpApp());
    await tester.pumpAndSettle();
    expect(find.byType(PopupMenuButton<String?>), findsNWidgets(3));
    expect(find.text('Bluetooth'), findsOneWidget);
    expect(find.text('Input'), findsOneWidget);
    expect(find.text('Output'), findsOneWidget);
  });
}
