#include <Ticker.h>

Ticker pwmTicker;

const int pwmPin = D2;  // change if needed
volatile int dutyPercent = 85;  // starts at slowest (85%)
const int pwmFreq = 63;  // Hz
volatile bool pinState = false;

int speedPercent = 0; // UI scale: 0=slowest, 100=fastest

void IRAM_ATTR pwmTick() {
  static uint32_t highMs, lowMs;

  if (pinState) {
    digitalWrite(pwmPin, LOW);
    pinState = false;
    pwmTicker.once_ms(lowMs, pwmTick);
  } else {
    digitalWrite(pwmPin, HIGH);
    pinState = true;
    pwmTicker.once_ms(highMs, pwmTick);
  }

  // compute high/low times based on dutyPercent
  int periodMs = 1000 / pwmFreq;
  highMs = (periodMs * dutyPercent) / 100;
  lowMs  = periodMs - highMs;
}

void setDutyCycle(int duty) {
  duty = constrain(duty, 50, 85);  // hardware safe range
  dutyPercent = duty;
}

void setSpeedPercent(int uiVal) {
  uiVal = constrain(uiVal, 0, 100);

  // Map UI 0–100 to duty 85–50
  int duty = map(uiVal, 0, 100, 85, 50);

  setDutyCycle(duty);
}

void setup() {
  pinMode(pwmPin, OUTPUT);
  digitalWrite(pwmPin, LOW);
  Serial.begin(115200);
  delay(1000);
  Serial.println("Enter speed (0-100):");

  setSpeedPercent(0); // start at slowest
  pwmTick();          // kick off ticker
}

void loop() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    int val = input.toInt();
    if (val >= 0 && val <= 100) {
      speedPercent = val;
      setSpeedPercent(speedPercent);
      Serial.print("Speed set to: ");
      Serial.println(speedPercent);
    } else {
      Serial.println("Enter 0-100");
    }
  }
}
