import 'package:flutter/material.dart';
import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';
import 'package:permission_handler/permission_handler.dart';

final flutterReactiveBle = FlutterReactiveBle();

void main() {
  runApp(const PumpControlApp());
}

class PumpControlApp extends StatelessWidget {
  const PumpControlApp({super.key});

  @override
  Widget build(BuildContext context) {
    return const MaterialApp(
      debugShowCheckedModeBanner: false,
      home: DeviceSelectionScreen(),
    );
  }
}

// =========================
// DEVICE SELECTION SCREEN
// =========================
class DeviceSelectionScreen extends StatefulWidget {
  const DeviceSelectionScreen({super.key});

  @override
  State<DeviceSelectionScreen> createState() => _DeviceSelectionScreenState();
}

class _DeviceSelectionScreenState extends State<DeviceSelectionScreen> {
  final List<DiscoveredDevice> _devices = [];
  late Stream<DiscoveredDevice> _scanStream;
  bool _scanning = false;

  Future<bool> requestPermissions() async {
    // Android 12+ permissions
    if (await Permission.bluetoothScan.request().isGranted &&
        await Permission.bluetoothConnect.request().isGranted) {
      // Android <= 30
      if (await Permission.location.request().isGranted) {
        return true;
      }
    }
    return false;
  }

  void _startScan() async {
    bool granted = await requestPermissions();
    if (!granted) {
      print("Permissions denied");
      return;
    }

    setState(() {
      _devices.clear();
      _scanning = true;
    });

    _scanStream = flutterReactiveBle.scanForDevices(withServices: []);
    _scanStream.listen((device) {
      if (device.name.isNotEmpty && !_devices.any((d) => d.id == device.id)) {
        setState(() {
          _devices.add(device);
        });
      }
    });
  }

  void _stopScan() {
    flutterReactiveBle.deinitialize();
    setState(() => _scanning = false);
  }

  @override
  void dispose() {
    flutterReactiveBle.deinitialize();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text("Select Pump Controller")),
      body: SafeArea(
        child: Column(
          children: [
            ElevatedButton(
              onPressed: _scanning ? _stopScan : _startScan,
              child: Text(_scanning ? "Stop Scanning" : "Scan for Devices"),
            ),
            Expanded(
              child: ListView.builder(
                itemCount: _devices.length,
                itemBuilder: (context, index) {
                  final device = _devices[index];
                  return ListTile(
                    title: Text(device.name.isEmpty ? "Unknown" : device.name),
                    subtitle: Text(device.id),
                    onTap: () {
                      Navigator.push(
                        context,
                        MaterialPageRoute(
                          builder: (_) => PumpControlPage(device: device),
                        ),
                      );
                    },
                  );
                },
              ),
            ),
          ],
        ),
      ),
    );
  }
}

// =========================
// PUMP CONTROL SCREEN
// =========================
class PumpControlPage extends StatefulWidget {
  final DiscoveredDevice device;
  const PumpControlPage({super.key, required this.device});

  @override
  State<PumpControlPage> createState() => _PumpControlPageState();
}

class _PumpControlPageState extends State<PumpControlPage> {
  final targetController = TextEditingController(text: "37.0");
  final kpController = TextEditingController(text: "0.3");
  final kiController = TextEditingController(text: "0.02");
  final maxPowerController = TextEditingController(text: "0.6");
  bool isHotMode = true;

  late QualifiedCharacteristic modeChar,
      targetChar,
      kpChar,
      kiChar,
      maxPowerChar,
      tempChar,
      mosfetChar,
      pwmChar; // <-- New PWM feedback

  bool _connected = false;
  double currentTemp = 0.0;
  double currentPwm = 0.0;
  bool manualMode = false; // manual slider control
  double manualPwmValue = 0.0;

  @override
  void initState() {
    super.initState();
    _connectToPump();
  }

  Future<void> _connectToPump() async {
    try {
      await flutterReactiveBle.connectToDevice(id: widget.device.id).first;
      setState(() => _connected = true);

      final serviceUuid = Uuid.parse("827a86ab-7c11-4f3a-a089-19f79e5fe328");

      modeChar = QualifiedCharacteristic(
          serviceId: serviceUuid,
          characteristicId: Uuid.parse("33687255-a53b-463c-9890-62cca7d8c1dd"),
          deviceId: widget.device.id);
      targetChar = QualifiedCharacteristic(
          serviceId: serviceUuid,
          characteristicId: Uuid.parse("469af82c-c82d-45c5-999e-1621fd38f3c4"),
          deviceId: widget.device.id);
      maxPowerChar = QualifiedCharacteristic(
          serviceId: serviceUuid,
          characteristicId: Uuid.parse("78db770f-6c5e-48f7-b548-ba9beae07359"),
          deviceId: widget.device.id);
      kpChar = QualifiedCharacteristic(
          serviceId: serviceUuid,
          characteristicId: Uuid.parse("71a9e483-c434-4d10-a696-e8a056f5929e"),
          deviceId: widget.device.id);
      kiChar = QualifiedCharacteristic(
          serviceId: serviceUuid,
          characteristicId: Uuid.parse("197d01bd-05fd-4462-9e2a-d3caa81fa0fe"),
          deviceId: widget.device.id);

      tempChar = QualifiedCharacteristic(
          serviceId: serviceUuid,
          characteristicId: Uuid.parse("a1f5b5c2-8d6c-4e0a-92c8-2240c9d457fa"),
          deviceId: widget.device.id);
      mosfetChar = QualifiedCharacteristic(
          serviceId: serviceUuid,
          characteristicId: Uuid.parse("b4bce661-12c7-4ec7-8cc7-d0cfaf11734f"),
          deviceId: widget.device.id);
      pwmChar = QualifiedCharacteristic(
          serviceId: serviceUuid,
          characteristicId: Uuid.parse("cc6b0b0e-1f5a-4a41-a9f2-9f9c2a3b2caa"),
          deviceId: widget.device.id);

      // Temperature stream
      flutterReactiveBle.subscribeToCharacteristic(tempChar).listen((data) {
        final tempStr = String.fromCharCodes(data);
        setState(() => currentTemp = double.tryParse(tempStr) ?? 0.0);
      });

      // PWM stream
      flutterReactiveBle.subscribeToCharacteristic(pwmChar).listen((data) {
        final pwmStr = String.fromCharCodes(data);
        setState(() => currentPwm = double.tryParse(pwmStr) ?? 0.0);
      });
    } catch (e) {
      debugPrint("Connection failed: $e");
    }
  }

  Future<void> _sendValue(QualifiedCharacteristic char, String value) async {
    try {
      await flutterReactiveBle.writeCharacteristicWithResponse(char,
          value: value.codeUnits);
    } catch (e) {
      debugPrint("Write failed: $e");
    }
  }

  void _sendManualPwm(double value) {
    _sendValue(mosfetChar, value.toStringAsFixed(2));
  }

  void _toggleManualMode(bool value) {
    setState(() => manualMode = value);
    _sendValue(mosfetChar, value ? "manual_on" : "manual_off");
  }

  void _updateSettings() {
    if (!_connected) return;
    _sendValue(modeChar, isHotMode ? "hot" : "cold");
    _sendValue(targetChar, targetController.text);
    _sendValue(kpChar, kpController.text);
    _sendValue(kiChar, kiController.text);
    _sendValue(maxPowerChar, maxPowerController.text);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: Text("Pump Controller (${widget.device.name})")),
      body: !_connected
          ? const Center(child: CircularProgressIndicator())
          : Padding(
              padding: const EdgeInsets.all(16),
              child: SingleChildScrollView(
                child: Column(
                  children: [
                    Text("Temp: ${currentTemp.toStringAsFixed(2)} °C",
                        style: const TextStyle(fontSize: 20)),
                    Text("PWM: ${(currentPwm * 100).toStringAsFixed(1)}%",
                        style: const TextStyle(
                            fontSize: 18, color: Colors.blueGrey)),
                    const SizedBox(height: 10),
                    SwitchListTile(
                      title: const Text("Manual Pump Control"),
                      value: manualMode,
                      onChanged: _toggleManualMode,
                    ),
                    if (manualMode)
                      Column(
                        children: [
                          Text(
                              "Manual PWM: ${manualPwmValue.toStringAsFixed(2)}"),
                          Slider(
                            value: manualPwmValue,
                            min: 0,
                            max: 1,
                            divisions: 20,
                            label: manualPwmValue.toStringAsFixed(2),
                            onChanged: (v) {
                              setState(() => manualPwmValue = v);
                              _sendManualPwm(v);
                            },
                          ),
                        ],
                      ),
                    SwitchListTile(
                      title: const Text("Hot Mode"),
                      value: isHotMode,
                      onChanged: (v) => setState(() => isHotMode = v),
                    ),
                    _textField("Target Temp (°C)", targetController),
                    _textField("Kp", kpController),
                    _textField("Ki", kiController),
                    _textField("Max Power (0–1)", maxPowerController),
                    const SizedBox(height: 16),
                    ElevatedButton(
                      onPressed: _updateSettings,
                      child: const Text("Send to Pump"),
                    ),
                  ],
                ),
              ),
            ),
    );
  }

  Widget _textField(String label, TextEditingController c) => TextField(
        controller: c,
        decoration: InputDecoration(labelText: label),
        keyboardType: TextInputType.number,
      );
}

class TestScreen extends StatelessWidget {
  final DiscoveredDevice device;
  const TestScreen({super.key, required this.device});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: Text("Test Screen - ${device.name}")),
      body: Center(
        child: Text(
          "Testing device:\n${device.name}\n${device.id}",
          textAlign: TextAlign.center,
          style: const TextStyle(fontSize: 18),
        ),
      ),
    );
  }
}
