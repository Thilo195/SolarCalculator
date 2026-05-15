#include "Simulator.h"
#include "SimulationEngine.h"

using json = nlohmann::json;

json run_simulation(const nlohmann::json& data) {
    SimulationConfig config;
    double user_wp = data.value("solar_wp", 5000.0);
    double user_battery_wh = data.value("battery_wh", 5000.0);
    
    config.cost_per_wp = data.value("cost_wp", 0.60);
    config.cost_per_kwh_bat = data.value("cost_bat", 400.0);
    config.feed_in_tariff = data.value("feed_in_tariff", 0.08);
    config.fixed_price = data.value("fixed_price", 0.35);
    config.use_dynamic_price = data.value("use_dynamic", false);
    
    config.dynamic_prices = data.value("dynamic_prices", std::vector<double>(24, config.fixed_price));
    config.custom_solar_curve = data.value("custom_solar_curve", std::vector<double>());

    config.heatpump.active = data["heatpump"]["active"];
    config.heatpump.season = data["heatpump"]["season"];
    config.heatpump.thermal_kwh = data["heatpump"].value("thermal_kwh", 0.0);
    
    config.ev.active = data["ev"]["active"];
    config.ev.capacity_wh = data["ev"].value("capacity_kwh", 0.0) * 1000.0;
    config.ev.daily_wh = data["ev"].value("daily_kwh", 0.0) * 1000.0;
    config.ev.v2h = data["ev"].value("v2h", false);
    config.ev.start_min = data["ev"].value("start_min", 1020);
    config.ev.end_min = data["ev"].value("end_min", 420);
    config.base_device_power.resize(1440, 0.0);


    for (const auto& dev : data["devices"]) {
        double power = dev.value("power", 0.0);
        int start_min = dev.value("start_min", 0);
        int end_min = dev.value("end_min", 0);
        for (int i = start_min; i < end_min && i < 1440; ++i) {
            config.base_device_power[i] += power;
        }
    }

    SimulationEngine engine(config);
    SimulationResult main_res = engine.run(user_wp, user_battery_wh, true);

    json result = {
        {"autarky", main_res.autarky_pct},
        {"total_consumption_wh", main_res.total_consumption_wh},
        {"grid_import_wh", main_res.grid_import_wh},
        {"grid_export_wh", main_res.grid_export_wh},
        {"consumption_watts", main_res.consumption_watts},
        {"solar_watts", main_res.solar_watts},
        {"export_watts", main_res.export_watts},
        {"battery_level_wh", main_res.battery_level_wh},
        {"ev_level_wh", main_res.ev_level_wh},
        {"economics", {
            {"cost_without_pv", main_res.economics.cost_without_pv},
            {"cost_with_pv", main_res.economics.cost_with_pv},
            {"revenue", main_res.economics.revenue},
            {"hardware_cost", main_res.economics.hardware_cost},
            {"roi_years", main_res.economics.roi_years}
        }}
    };

    json matrix = json::array();
    std::vector<double> test_batteries = {0, 5000, 10000}; 
    
    for (double tb : test_batteries) {
        json series;
        series["battery_kwh"] = tb / 1000.0;
        json points = json::array();
        for (double twp = 1000; twp <= 12000; twp += 1000) {
            SimulationResult point_res = engine.run(twp, tb, false);
            points.push_back({
                {"wp", point_res.wp}, 
                {"autarky", point_res.autarky_pct}, 
                {"roi", point_res.economics.roi_years}
            });
        }
        series["data"] = points;
        matrix.push_back(series);
    }

    result["investment_matrix"] = matrix;
    return result;
}