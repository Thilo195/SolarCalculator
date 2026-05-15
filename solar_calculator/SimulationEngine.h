#pragma once
#include "Models.h"

class SimulationEngine {
public:
    SimulationEngine(const SimulationConfig& config);
    SimulationResult run(double test_wp, double test_battery_wh, bool full_data);

private:
    SimulationConfig config;
    int num_intervals = 1440;
};