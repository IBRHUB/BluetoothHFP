import 'package:bluetooth_hfp/main.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

// Opt-in hardware test. Exercises the REAL Win32Window::Create/OnDestroy cycle
// and method channel; a headless HfpController test misses that lifecycle.
void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();
  const channel = MethodChannel('bluetooth_hfp/windows');
  const hardware = bool.fromEnvironment('HFP_HARDWARE_TEST');

  testWidgets(
    'window startup keeps Bluetooth worker alive and local devices selectable',
    (tester) async {
      await tester.pumpWidget(const BluetoothHfpApp());
      await tester.pumpAndSettle();
      Future<Map<Object?, Object?>> read() async =>
          (await channel.invokeMapMethod<Object?, Object?>('snapshot'))!;
      var state = await read();
      expect(state['inputs'], isNotEmpty);
      expect(state['outputs'], isNotEmpty);
      final inputs = state['inputs'] as List;
      final outputs = state['outputs'] as List;
      // Optional physical-device selectors for the local hardware regression run.
      const inputName = String.fromEnvironment('HFP_TEST_INPUT');
      const outputName = String.fromEnvironment('HFP_TEST_OUTPUT');
      if (inputName.isNotEmpty) {
        final input = inputs.cast<Map>().firstWhere(
          (d) => (d['name'] as String).contains(inputName),
        );
        await channel.invokeMethod<void>('selectInput', input['id']);
      }
      if (outputName.isNotEmpty) {
        final output = outputs.cast<Map>().firstWhere(
          (d) => (d['name'] as String).contains(outputName),
        );
        await channel.invokeMethod<void>('selectOutput', output['id']);
      }
      state = await read();
      if (inputName.isNotEmpty) {
        expect(
          inputs.cast<Map>().firstWhere(
            (d) => d['id'] == state['inputId'],
          )['name'],
          contains(inputName),
        );
      }
      if (outputName.isNotEmpty) {
        expect(
          outputs.cast<Map>().firstWhere(
            (d) => d['id'] == state['outputId'],
          )['name'],
          contains(outputName),
        );
      }
      await channel.invokeMethod<void>('testAudio');
      await tester.runAsync(
        () => Future<void>.delayed(const Duration(seconds: 1)),
      );
      state = await read();
      expect(
        state['testRouteActive'],
        isTrue,
        reason: '${state['testMessage']}',
      );
      await tester.runAsync(
        () => Future<void>.delayed(const Duration(seconds: 6)),
      );
      state = await read();
      expect(state['testActive'], isFalse);
      expect(state['testRouteActive'], isFalse);
      expect(state['testMessage'], startsWith('Test finished:'));
      // ignore: avoid_print
      print(
        'LOCAL AUDIO RESULT: input and output opened; peak=${state['testPeak']}; ${state['testMessage']}',
      );
      final phones = state['phones'] as List;
      expect(
        phones,
        hasLength(1),
        reason:
            'Run with one paired test phone. Never connect an arbitrary phone.',
      );
      await channel.invokeMethod<void>(
        'selectPhone',
        (phones.single as Map)['id'],
      );
      try {
        final deadline = DateTime.now().add(const Duration(seconds: 90));
        do {
          await tester.runAsync(
            () => Future<void>.delayed(const Duration(seconds: 1)),
          );
          state = await read();
          if (!['waiting', 'connecting'].contains(state['mediaState']) &&
              !['waiting', 'connecting'].contains(state['callsState'])) {
            break;
          }
        } while (DateTime.now().isBefore(deadline));
        expect(state['mediaState'], isNot(isIn(['waiting', 'connecting'])));
        expect(state['mediaMessage'], isNot(contains('worker is stopped')));
        expect(state['callsState'], isNot(isIn(['waiting', 'connecting'])));
        // Log transport outcomes separately: denied call access is NOT a voice pass.
        // ignore: avoid_print
        print(
          'NATIVE HARDWARE RESULT: media=${state['mediaActive']}; '
          '${state['mediaMessage']}; ${state['callsMessage']}; '
          'callRoute=${state['routeActive']}',
        );
        await channel.invokeMethod<void>('selectPhone', null);
        state = await read();
        expect(state['phoneId'], isNull);
        expect(state['mediaActive'], isFalse);
        expect(state['routeActive'], isFalse);
      } finally {
        await channel.invokeMethod<void>('selectPhone', null);
      }
    },
    skip: !hardware,
  );
}
