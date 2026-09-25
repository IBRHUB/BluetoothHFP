import 'package:bluetooth_hfp/main.dart';
import 'package:bluetooth_hfp/controller.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();
  testWidgets('desktop channel enumerates real audio devices', (tester) async {
    final endpoints = await HeadsetController.platform
        .invokeListMethod<dynamic>('endpoints');
    expect(endpoints, isNotNull);
    await tester.pumpWidget(const HeadsetApp());
    await tester.pump(const Duration(seconds: 8));
    expect(find.text('Bluetooth HFP'), findsOneWidget);
  }, skip: !const bool.fromEnvironment('HFP_HARDWARE_TEST'));
}
