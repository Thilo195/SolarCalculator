#pragma once
#include <string>
#include <vector>

struct HeatpumpConfig {
    bool active;
    std::string season;
    double thermal_kwh;
    double get_power_w() const {
        if (!active) return 0.0;
        double cop = (season == "winter") ? 3.0 : 4.0;
        return ((thermal_kwh * 1000.0) / cop) / 24.0;
    }
};

struct EVConfig {
    bool active;
    double capacity_wh;
    double daily_wh;
    bool v2h;
    int start_min;
    int end_min;
};

struct SimulationConfig {
    double cost_per_wp;
    double cost_per_kwh_bat;
    double feed_in_tariff;
    double fixed_price;
    bool use_dynamic_price;
    
    std::vector<double> dynamic_prices;
    std::vector<double> custom_solar_curve;
    std::vector<double> base_device_power;

    HeatpumpConfig heatpump;
    EVConfig ev;
};

struct Economics {
    double cost_without_pv;
    double cost_with_pv;
    double revenue;
    double hardware_cost;
    double roi_years;
};

struct SimulationResult {
    double wp;
    double autarky_pct;
    double total_consumption_wh;
    double grid_import_wh;
    double grid_export_wh;
    std::vector<double> consumption_watts;
    std::vector<double> solar_watts;
    std::vector<double> export_watts;
    std::vector<double> battery_level_wh;
    std::vector<double> ev_level_wh;
    Economics economics;
};