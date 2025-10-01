// main.cpp

#include <Arduino.h>


#include "v001/SC10_WindScape_005.h"


WindScapeSimulator g_SC10_simulator;

void setup() {

    Serial.begin(115200);

    g_SC10_simulator.SC10_init();
}

void loop() {
    g_SC10_simulator.SC10_run();

    // WDT 리셋 방지를 위한 안전 장치
    delay(1); 

}
