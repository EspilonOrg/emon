#include <Arduino.h>

static int s_counter = 0;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("=============================");
    Serial.println("  espilon-monitor hello demo ");
    Serial.println("=============================");
    Serial.printf("Chip  : %s rev%d  %d cores @ %dMHz\n",
        ESP.getChipModel(), ESP.getChipRevision(),
        ESP.getChipCores(), getCpuFrequencyMhz());
    Serial.printf("Flash : %u MB\n", ESP.getFlashChipSize() / (1024 * 1024));
    Serial.printf("Heap  : %u bytes free\n", ESP.getFreeHeap());
    Serial.println("-----------------------------");
    Serial.println("Boot OK - starting loop");
    Serial.println();
}

void loop() {
    Serial.printf("[%04d] Hello from ESP32!\n", s_counter++);
    delay(800);
}
