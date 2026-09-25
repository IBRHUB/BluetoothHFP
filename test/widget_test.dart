import 'dart:io';
import 'dart:convert';

import 'package:bluetooth_hfp/controller.dart';
import 'package:bluetooth_hfp/main.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  late Directory data;
  late HeadsetController controller;
  setUp(() async {
    data = await Directory.systemTemp.createTemp('hfp-test-');
    controller = HeadsetController(dataPath: data.path, bundlePath: data.path);
    controller.autoReconnect = false;
  });
  tearDown(() async {
    await controller.save();
    controller.dispose();
    await data.delete(recursive: true);
  });
  test('only valid Bluetooth addresses reach connection commands', () {
    expect(HeadsetController.validAddress('02:00:00:00:00:02'), isTrue);
    for (final bad in ['', '02:00:00', 'connect\nquit', 'GG:00:00:00:00:00']) {
      expect(HeadsetController.validAddress(bad), isFalse);
    }
  });
  test(
    'structured events track codecs and clear stale routes on disconnect',
    () async {
      controller.consume(
        '@{"version":1,"event":"peer","value":"02:00:00:00:00:02","status":0}',
      );
      controller.consume(
        '@{"version":1,"event":"call","value":"codec","status":2}',
      );
      controller.consume(
        '@{"version":1,"event":"mediaCodec","value":"AAC","status":0}',
      );
      controller.consume(
        '@{"version":1,"event":"media","value":"playing","status":44100}',
      );
      expect(controller.connected, isTrue);
      expect(controller.call, contains('16 kHz'));
      expect(controller.media, contains('AAC'));
      controller.consume(
        '@{"version":1,"event":"disconnected","value":"","status":0}',
      );
      expect(controller.connected, isFalse);
      expect(controller.call, isEmpty);
      expect(controller.media, isEmpty);
      await controller.save();
      expect(
        jsonDecode(
          await File('${data.path}/settings.json').readAsString(),
        )['peer'],
        '02:00:00:00:00:02',
      );
    },
  );
  test('overlapping saves produce a complete settings document', () async {
    await Future.wait(List.generate(15, (_) => controller.save()));
    expect(
      jsonDecode(
        await File('${data.path}/settings.json').readAsString(),
      )['codec'],
      'Auto',
    );
  });
  test('text logs cannot invent a connected state', () {
    controller.consume('[HFP] SLC established');
    expect(controller.connected, isFalse);
    controller.consume('@not json');
    expect(controller.connected, isFalse);
  });
  test(
    'diagnostic exports omit device identity and redact logged addresses',
    () async {
      controller.inventory = {
        'supported': true,
        'backupPath': 'private-backup-path',
        'devices': [
          {
            'id': r'USB\VID_8087&PID_0026\PRIVATE_INSTANCE',
            'name': 'private-device-name',
            'status': 'OK',
            'service': 'WINUSB',
          },
        ],
      };
      controller.consume('[BT] Local address: 02:00:00:00:00:01');
      controller.consume(r'[USB] USB\VID_8087&PID_0026\PRIVATE_INSTANCE');
      final report = await File(await controller.exportDiagnostics())
          .readAsString();
      for (final private in [
        '02:00:00:00:00:01',
        'PRIVATE_INSTANCE',
        'private-backup-path',
        'private-device-name',
      ]) {
        expect(report, isNot(contains(private)));
      }
      expect(report, contains('WINUSB'));
      expect(report, contains('REDACTED-BLUETOOTH-ADDRESS'));
    },
  );
  testWidgets('unsupported adapter blocks enable but preserves diagnosis UI', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(540, 960);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    await tester.pumpWidget(HeadsetApp(controller: controller));
    expect(find.text('Bluetooth HFP'), findsOneWidget);
    final button = tester.widget<FilledButton>(
      find.widgetWithText(FilledButton, 'Start headset'),
    );
    expect(button.onPressed, isNull);
    await tester.tap(find.text('Audio').last);
    await tester.pumpAndSettle();
    expect(find.text('Microphone'), findsOneWidget);
    expect(tester.takeException(), isNull);
    await tester.tap(find.text('Settings').last);
    await tester.pumpAndSettle();
    expect(find.text('Save report'), findsOneWidget);
    await tester.tap(find.text('Logs'));
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
    expect(find.text('No logs yet.'), findsOneWidget);
    controller.consume('Headset ready');
    controller.changed();
    await tester.pump();
    expect(find.text('Headset ready'), findsOneWidget);
    await tester.tap(find.text('Audio').last);
    await tester.pumpAndSettle();
    await tester.tap(find.text('Settings').last);
    await tester.pumpAndSettle();
    expect(find.text('Headset ready'), findsOneWidget);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });
  testWidgets('navigation and disabled controls stay readable on black', (
    tester,
  ) async {
    await tester.pumpWidget(HeadsetApp(controller: controller));
    final context = tester.element(find.byType(Scaffold));
    final theme = Theme.of(context);
    double contrast(Color foreground, Color background) {
      final a = foreground.computeLuminance();
      final b = background.computeLuminance();
      return (a > b ? (a + .05) / (b + .05) : (b + .05) / (a + .05));
    }

    final style = theme.filledButtonTheme.style!;
    expect(
      contrast(
        style.foregroundColor!.resolve({})!,
        style.backgroundColor!.resolve({})!,
      ),
      greaterThanOrEqualTo(4.5),
    );
    expect(
      contrast(
        style.foregroundColor!.resolve({WidgetState.disabled})!,
        Colors.black,
      ),
      greaterThanOrEqualTo(4.5),
    );
    final nav = theme.navigationBarTheme;
    expect(
      contrast(
        nav.iconTheme!.resolve({WidgetState.selected})!.color!,
        nav.indicatorColor!,
      ),
      greaterThanOrEqualTo(3),
    );
    expect(
      contrast(nav.iconTheme!.resolve({})!.color!, Colors.black),
      greaterThanOrEqualTo(3),
    );
    await tester.pumpWidget(const SizedBox());
  });
  test(
    'failed background saves report an error without an unhandled future',
    () async {
      final file = File('${data.path}/not-a-directory');
      await file.writeAsString('test');
      final failed = HeadsetController(dataPath: file.path);
      await failed.saveInBackground();
      expect(failed.error, contains('Could not save settings'));
      failed.dispose();
    },
  );
  test('late events after disposal do not notify a disposed controller', () {
    final disposed = HeadsetController(dataPath: data.path);
    disposed.dispose();
    expect(
      () => disposed.consume(
        '@{"version":1,"event":"disconnected","value":"","status":0}',
      ),
      returnsNormally,
    );
  });
}
