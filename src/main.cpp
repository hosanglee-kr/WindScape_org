
#include <Arduino.h>

#include "SC10_WindScape_001.h"


WindScapeSimulator simulator;

void setup() {
    simulator.setup();
}

void loop() {
    simulator.loop();
}
