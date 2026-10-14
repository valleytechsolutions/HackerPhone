// Minimal recovery diagnostic: no board GPIO, radio, SD or modem access.
#ifdef MP24_STARTUP_PROBE
#include <Arduino.h>
#include <esp_system.h>
void setup() {
  Serial.begin(115200);
}
void loop() {
  Serial.printf("HP_PROBE alive uptime=%lu reset=%u heap=%u psram=%u\n",
                (unsigned long)millis(), unsigned(esp_reset_reason()),
                ESP.getFreeHeap(), ESP.getPsramSize());
  delay(500);
}
#endif
