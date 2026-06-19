// Monitoring des entrées ADC GPIO 2, 3, 4 sur ESP32-C3
// Affichage des valeurs brutes (0..4095) et de la tension estimée (mV) sur le port série.

#include <Arduino.h>

const uint8_t ADC_PINS[] = {2, 3, 4};
const uint8_t NUM_PINS = sizeof(ADC_PINS) / sizeof(ADC_PINS[0]);

const uint32_t SERIAL_BAUD = 115200;
const uint32_t SAMPLE_PERIOD_MS = 500;

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(100);

  // Résolution 12 bits (0..4095) — valeur par défaut sur ESP32-C3
  analogReadResolution(12);

  // Plage d'entrée ~0..3.3 V (atténuation 11 dB)
  for (uint8_t i = 0; i < NUM_PINS; i++) {
    analogSetPinAttenuation(ADC_PINS[i], ADC_11db);
  }

  Serial.println();
  Serial.println(F("ESP32-C3 ADC Monitor"));
  Serial.println(F("GPIO2 | GPIO3 | GPIO4   (raw / mV)"));
}

void loop() {
  for (uint8_t i = 0; i < NUM_PINS; i++) {
    uint8_t pin = ADC_PINS[i];
    int raw = analogRead(pin);
    uint32_t mv = analogReadMilliVolts(pin);

    Serial.print(F("GPIO"));
    Serial.print(pin);
    Serial.print(F("="));
    Serial.print(raw);
    Serial.print(F(" ("));
    Serial.print(mv);
    Serial.print(F(" mV)"));
    if (i < NUM_PINS - 1) Serial.print(F("  |  "));
  }
  Serial.println();

  delay(SAMPLE_PERIOD_MS);
}
