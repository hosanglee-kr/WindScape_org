
#include <Arduino.h>


#include "v001/SC10_WindScape_001.h"


WindScapeSimulator g_SC10_simulator;

void setup() {

    Serial.begin(115200);

    g_SC10_simulator.init();
}

void loop() {
    g_SC10_simulator.run();
}
