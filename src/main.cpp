#include <Arduino.h>

// On ESP32 DevKit boards, EN is the reset button.
// Pressing EN restarts the board and runs setup() again.

const int ledPin = 2; // On ESP32 DevKit, onboard LED is usually GPIO 2

void setup() {
  Serial.begin(9600);
  pinMode(ledPin, OUTPUT);
  Serial.println("ESP32 reset by EN button. Starting LED blink...");
}

void loop() {
  digitalWrite(ledPin, HIGH);
  delay(500);
  digitalWrite(ledPin, LOW);
  delay(500);
}