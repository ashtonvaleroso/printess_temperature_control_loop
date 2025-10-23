#include <Adafruit_MAX31855.h>

// === Pin Definitions ===
#define MAXDO   5   // MISO
#define MAXCS   6   // Chip Select
#define MAXCLK  4   // Clock
#define MOSFET  7   // Pump PWM output

// === Thermocouple Object ===
Adafruit_MAX31855 thermocouple(MAXCLK, MAXCS, MAXDO);

// === Control Settings ===
bool isHotMode = true;        // true = heating loop, false = cooling loop
double targetTempC = 37.0;    // target internal temperature (°C)
double toleranceC = 1.0;      // ±1°C control band

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Precise Pump Temperature Control (±1°C)");

  if (!thermocouple.begin()) {
    Serial.println("ERROR: MAX31855 not found. Check wiring!");
    while (1);
  }

  pinMode(MOSFET, OUTPUT);
}

void loop() {
  double tempC = thermocouple.readCelsius();

  if (isnan(tempC)) {
    Serial.println("Error reading temperature — check thermocouple connection!");
    return;
  }

  Serial.print("Temperature: ");
  Serial.print(tempC);
  Serial.println(" °C");

  double pumpPower = computePumpPower(tempC);
  int pwmValue = (int)(pumpPower * 255);
  pwmValue = constrain(pwmValue, 0, 255);

  analogWrite(MOSFET, pwmValue);

  Serial.print("Pump Power: ");
  Serial.println(pumpPower, 2);

  delay(1000);
}

// === Compute Pump Power ===
// Hot mode: pump runs more if temp < target − tolerance
// Cold mode: pump runs more if temp > target + tolerance
double computePumpPower(double tempC) {
  double power = 0.0;

  if (isHotMode) {
    if (tempC < targetTempC - toleranceC) {
      power = 1.0;  // full speed
    } else if (tempC > targetTempC + toleranceC) {
      power = 0.0;  // off
    } else {
      // within ±1°C: proportional adjustment to smooth response
      power = (targetTempC - tempC + toleranceC) / (2 * toleranceC);
    }
  } else {
    if (tempC > targetTempC + toleranceC) {
      power = 1.0;
    } else if (tempC < targetTempC - toleranceC) {
      power = 0.0;
    } else {
      power = (tempC - targetTempC + toleranceC) / (2 * toleranceC);
    }
  }

  return constrain(power, 0.0, 1.0);
}
