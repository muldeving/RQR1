#include <WiFi.h>
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  delay(500);
  Serial.print("MAC STA: ");
  Serial.println(WiFi.macAddress());
}
void loop() { delay(2000); Serial.println(WiFi.macAddress()); }
