#include <Adafruit_MAX31855.h>

// === Pin Definitions ===
#define MAXDO   5   // MISO
#define MAXCS   6   // Chip Select
#define MAXCLK  4   // Clock
#define MOSFET  7   // Pump PWM output

// === Thermocouple Object ===
Adafruit_MAX31855 thermocouple(MAXCLK, MAXCS, MAXDO);

// === Control Settings ===
bool isHotMode = true;          // true = heating (hot fluid), false = cooling (cold fluid)
double targetTempC = 37.0;      // desired syringe temperature (°C)
double toleranceC = 0.1;        // acceptable deviation (±0.1°C)

// === PI Controller Parameters ===
double Kp = 0.5;                // proportional gain (tune experimentally)
double Ki = 0.02;               // integral gain (tune experimentally)
double integralTerm = 0.0;
unsigned long lastTime = 0;

// === Output limits ===
double minPower = 0.0;          // pump off
double maxPower = 1.0;          // full power

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("PI Pump Temperature Control (±0.1°C)");

  if (!thermocouple.begin()) {
    Serial.println("ERROR: MAX31855 not found. Check wiring!");
    while (1);
  }

  pinMode(MOSFET, OUTPUT);
  lastTime = millis();
}

void loop() {
  double tempC = thermocouple.readCelsius();

  if (isnan(tempC)) {
    Serial.println("Error reading temperature — check thermocouple connection!");
    delay(1000);
    return;
  }

  unsigned long now = millis();
  double dt = (now - lastTime) / 1000.0;  // seconds
  lastTime = now;

  // === Compute Error ===
  double error = 0.0;
  if (isHotMode) {
    // Heating: error is positive when too cold
    error = targetTempC - tempC;
  } else {
    // Cooling: error is positive when too hot
    error = tempC - targetTempC;
  }

  // === Integral Term with Anti-Windup ===
  if (abs(error) < 2.0) {  // only integrate when close to target
    integralTerm += error * dt;
    integralTerm = constrain(integralTerm, -20.0, 20.0);  // anti-windup clamp
  }

  // === PI Output ===
  double pumpPower = Kp * error + Ki * integralTerm;

  // Limit and normalize output
  pumpPower = constrain(pumpPower, minPower, maxPower);

  int pwmValue = (int)(pumpPower * 255);
  analogWrite(MOSFET, pwmValue);

  // === Print Debug Info ===
  Serial.print("Temp: ");
  Serial.print(tempC, 3);
  Serial.print(" °C | Target: ");
  Serial.print(targetTempC, 1);
  Serial.print(" | Error: ");
  Serial.print(error, 3);
  Serial.print(" | Power: ");
  Serial.println(pumpPower, 3);

  delay(500);
}
