#include <Adafruit_MAX31855.h>
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define MAXDO   5
#define MAXCS   6
#define MAXCLK  4
#define MOSFET  7

Adafruit_MAX31855 thermocouple(MAXCLK, MAXCS, MAXDO);

#define SERVICE_UUID            "827a86ab-7c11-4f3a-a089-19f79e5fe328"
#define MODE_CHAR_UUID          "33687255-a53b-463c-9890-62cca7d8c1dd"
#define TARGET_CHAR_UUID        "469af82c-c82d-45c5-999e-1621fd38f3c4"
#define MAXPOWER_CHAR_UUID      "78db770f-6c5e-48f7-b548-ba9beae07359"
#define KP_CHAR_UUID            "71a9e483-c434-4d10-a696-e8a056f5929e"
#define KI_CHAR_UUID            "197d01bd-05fd-4462-9e2a-d3caa81fa0fe"
#define PWM_CHAR_UUID           "cc6b0b0e-1f5a-4a41-a9f2-9f9c2a3b2caa"

// ⚡ New UUIDs
#define TEMP_CHAR_UUID          "a1f5b5c2-8d6c-4e0a-92c8-2240c9d457fa"
#define MOSFET_CHAR_UUID        "b4bce661-12c7-4ec7-8cc7-d0cfaf11734f"

// Adjustable variables
bool isHotMode = true;
double targetTempC = 37.0;
double toleranceC = 0.1;
double maxPumpPower = 0.6;
double Kp = 0.3;
double Ki = 0.02;
bool manualMosfetOn = false;   // ⚡ manual control flag

double integral = 0;
unsigned long lastTime = 0;

// BLE Characteristics
BLECharacteristic *modeChar, *targetChar, *maxPowerChar, *kpChar, *kiChar;
BLECharacteristic *tempChar, *mosfetChar; // ⚡ new
BLECharacteristic *pwmChar;

void updateCharacteristic(BLECharacteristic *ch, double val) {
  String s = String(val, 3);
  ch->setValue(s.c_str());
}

// BLE callback handler
class ControlCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    String value = characteristic->getValue();
    
    if (characteristic == modeChar) {
      isHotMode = (value == "hot");
    } else if (characteristic == targetChar) {
      targetTempC = atof(value.c_str());
    } else if (characteristic == maxPowerChar) {
      maxPumpPower = atof(value.c_str());
    } else if (characteristic == kpChar) {
      Kp = atof(value.c_str());
    } else if (characteristic == kiChar) {
      Ki = atof(value.c_str());
    } 
    // ⚡ Manual MOSFET toggle
    else if (characteristic == mosfetChar) {
      String val = characteristic->getValue();
      if (val == "manual_on") {
        manualMosfetOn = true;
      } else if (val == "manual_off") {
        manualMosfetOn = false;
      } else {
        double pwmManual = val.toFloat();
        analogWrite(MOSFET, (int)(pwmManual * 255));
      }
    }
  }
};

void setupBLE() {
  BLEDevice::init("PumpController");
  BLEServer *server = BLEDevice::createServer();
  BLEService *service = server->createService(SERVICE_UUID);

  modeChar = service->createCharacteristic(MODE_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
  targetChar = service->createCharacteristic(TARGET_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
  maxPowerChar = service->createCharacteristic(MAXPOWER_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
  kpChar = service->createCharacteristic(KP_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
  kiChar = service->createCharacteristic(KI_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
  pwmChar = service->createCharacteristic(
    PWM_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pwmChar->addDescriptor(new BLE2902());


  // ⚡ New: temperature + MOSFET
  tempChar = service->createCharacteristic(TEMP_CHAR_UUID,
  BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  tempChar->addDescriptor(new BLE2902());

  mosfetChar = service->createCharacteristic(MOSFET_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);

  // Add callbacks
  ControlCallback *cb = new ControlCallback();
  modeChar->setCallbacks(cb);
  targetChar->setCallbacks(cb);
  maxPowerChar->setCallbacks(cb);
  kpChar->setCallbacks(cb);
  kiChar->setCallbacks(cb);
  mosfetChar->setCallbacks(cb);

  service->start();
  BLEAdvertising *advertising = server->getAdvertising();
  advertising->start();
  Serial.println("BLE started — ready for connection.");
}

double computePumpPower(double tempC, double dt) {
  double error = (isHotMode ? (targetTempC - tempC) : (tempC - targetTempC));
  integral += error * dt;
  double control = Kp * error + Ki * integral;
  control = constrain(control, 0.0, maxPumpPower);
  return control;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Debuggable PI Pump Controller");
  pinMode(MOSFET, OUTPUT);

  if (!thermocouple.begin()) {
    Serial.println("ERROR: MAX31855 not found!");
    while (1);
  }

  setupBLE();
  lastTime = millis();
}

void loop() {
  double tempC = thermocouple.readCelsius();
  if (isnan(tempC)) {
    Serial.println("Thermocouple error!");
    delay(1000);
    return;
  }

  unsigned long now = millis();
  double dt = (now - lastTime) / 1000.0;
  lastTime = now;

  double pumpPower = 0;

  if (!manualMosfetOn) {
    pumpPower = computePumpPower(tempC, dt);
    analogWrite(MOSFET, (int)(pumpPower * 255));
  }

  // Send live data
  updateCharacteristic(tempChar, tempC);
  tempChar->notify();

  updateCharacteristic(pwmChar, pumpPower);
  pwmChar->notify();

  Serial.printf("Temp: %.2f °C | PWM: %.2f\n", tempC, pumpPower);
  delay(500);
}
