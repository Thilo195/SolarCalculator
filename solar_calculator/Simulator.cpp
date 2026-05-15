#include "Simulator.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>

using json = nlohmann::json;

json run_simulation(const nlohmann::json& data) {
    double user_wp = data.value("solar_wp", 5000.0);
    double user_battery_wh = data.value("battery_wh", 5000.0);
    
    double cost_per_wp = data.value("cost_wp", 0.60);
    double cost_per_kwh_bat = data.value("cost_bat", 400.0);
    double feed_in_tariff = data.value("feed_in_tariff", 0.08);
    double fixed_price = data.value("fixed_price", 0.35);
    bool use_dynamic_price = data.value("use_dynamic", false);
    
    std::vector<double> dynamic_prices = data.value("dynamic_prices", std::vector<double>(24, fixed_price));
    std::vector<double> custom_solar_curve = data.value("custom_solar_curve", std::vector<double>());

    auto devices = data["devices"];
    bool hp_active = data["heatpump"]["active"];
    std::string season = data["heatpump"]["season"];
    double hp_thermal_kwh = data["heatpump"].value("thermal_kwh", 0.0);
    
    bool ev_active = data["ev"]["active"];
    double ev_capacity_wh = data["ev"].value("capacity_kwh", 0.0) * 1000.0;
    double ev_daily_wh = data["ev"].value("daily_kwh", 0.0) * 1000.0;
    bool ev_v2h = data["ev"].value("v2h", false);
    int ev_start_min = data["ev"].value("start_min", 1020);
    int ev_end_min = data["ev"].value("end_min", 420);

    int num_intervals = 1440;
    std::vector<double> base_device_power(num_intervals, 0.0);
    
    for (const auto& dev : devices) {
        double power = dev.value("power", 0.0);
        int start_min = dev.value("start_min", 0);
        int end_min = dev.value("end_min", 0);
        for (int i = start_min; i < end_min && i < num_intervals; ++i) {
            base_device_power[i] += power;
        }
    }

    double hp_power_w = 0.0;
    if (hp_active) {
        double cop = (season == "winter") ? 3.0 : 4.0;
        hp_power_w = ((hp_thermal_kwh * 1000.0) / cop) / 24.0;
    }

    auto simulate_setup = [&](double test_wp, double test_battery_wh, bool full_data) -> json {
        std::vector<double> solar_watts(num_intervals, 0.0);
        std::vector<double> final_consumption(num_intervals, 0.0);
        std::vector<double> grid_export_curve(num_intervals, 0.0);
        std::vector<double> battery_level_curve(num_intervals, 0.0);
        std::vector<double> ev_level_curve(num_intervals, 0.0);
        
        if (custom_solar_curve.size() == 24) {
            for(int i = 0; i < num_intervals; ++i) {
                int hour = i / 60;
                solar_watts[i] = (custom_solar_curve[hour] / 1000.0) * test_wp * 0.85;
            }
        } else {
            double pi = 3.14159265358979323846;
            double yield_multiplier = (season == "winter") ? 0.375 : 1.0;
            double peak_power = test_wp * (pi / 6.0) * yield_multiplier;
            int sun_start = (season == "winter") ? 480 : 360;
            int sun_end = (season == "winter") ? 960 : 1080;
            for(int i = sun_start; i < sun_end; ++i) {
                solar_watts[i] = peak_power * std::sin(pi * (i - sun_start) / (double)(sun_end - sun_start));
            }
        }

        double house_battery_wh = 0.0;
        double current_ev_wh = ev_capacity_wh;
        double no_pv_ev_wh = ev_capacity_wh;

        double total_cons_day2_wh = 0.0;
        double grid_import_day2_wh = 0.0;
        double grid_export_day2_wh = 0.0;
        
        double cost_without_pv_day = 0.0;
        double cost_with_pv_day = 0.0;
        double revenue_export_day = 0.0;

        for (int day = 0; day < 2; ++day) {
            for(int i = 0; i < num_intervals; ++i) {
                int current_hour = i / 60;
                double current_kwh_price = use_dynamic_price ? dynamic_prices[current_hour] : fixed_price;

                double base_power = base_device_power[i] + hp_power_w;
                double current_consumption_w = base_power;
                double no_pv_consumption_w = base_power;

                bool ev_is_home = false;
                int minutes_to_departure = 0;

                if (ev_active) {
                    if (ev_start_min < ev_end_min) {
                        ev_is_home = (i >= ev_start_min && i < ev_end_min);
                        if (ev_is_home) minutes_to_departure = ev_end_min - i;
                    } else {
                        ev_is_home = (i >= ev_start_min || i < ev_end_min);
                        if (ev_is_home) {
                            if (i >= ev_start_min) minutes_to_departure = (1440 - i) + ev_end_min;
                            else minutes_to_departure = ev_end_min - i;
                        }
                    }
                    
                    if (i == ev_end_min) {
                        current_ev_wh = std::max(0.0, current_ev_wh - ev_daily_wh);
                        no_pv_ev_wh = std::max(0.0, no_pv_ev_wh - ev_daily_wh);
                    }
                }

                if (ev_active && ev_is_home && (ev_capacity_wh - no_pv_ev_wh) > 0) {
                    double no_pv_space = ev_capacity_wh - no_pv_ev_wh;
                    double no_pv_charge_w = std::min(11000.0, no_pv_space * 60.0);
                    no_pv_ev_wh += (no_pv_charge_w / 60.0);
                    no_pv_consumption_w += no_pv_charge_w;
                }

                if (day == 1) {
                    cost_without_pv_day += (no_pv_consumption_w / 60000.0) * current_kwh_price;
                }

                double ev_charging_w = 0.0;
                double space_left_wh = ev_capacity_wh - current_ev_wh;
                bool is_forced_charge = false;

                if (ev_active && ev_is_home && space_left_wh > 0) {
                    double minutes_needed_at_11kw = space_left_wh / (11000.0 / 60.0);
                    if (minutes_to_departure <= minutes_needed_at_11kw + 10) {
                        is_forced_charge = true;
                        ev_charging_w = std::min(11000.0, space_left_wh * 60.0);
                        current_ev_wh += (ev_charging_w / 60.0);
                        space_left_wh -= (ev_charging_w / 60.0);
                    }
                }

                current_consumption_w += ev_charging_w;
                
                double net_power = solar_watts[i] - current_consumption_w;
                double exported_power_w = 0.0;
                
                if (net_power >= 0) { // surplus
                    if (ev_active && ev_is_home && !is_forced_charge && space_left_wh > 0) {
                        double smart_charge_w = std::min({net_power, 11000.0, space_left_wh * 60.0});
                        current_ev_wh += (smart_charge_w / 60.0);
                        net_power -= smart_charge_w;
                        current_consumption_w += smart_charge_w; 
                    }
                    
                    double battery_space = test_battery_wh - house_battery_wh;
                    double charge_w = std::min(net_power, battery_space * 60.0);
                    house_battery_wh += (charge_w / 60.0);
                    net_power -= charge_w;
                    
                    exported_power_w = net_power;
                    
                    if (day == 1) {
                        grid_export_day2_wh += (exported_power_w / 60.0);
                        revenue_export_day += (exported_power_w / 60000.0) * feed_in_tariff;
                        grid_export_curve[i] = exported_power_w;
                    }
                } 
                else { // deficit
                    double deficit_wh = -net_power / 60.0;
                    
                    double battery_used = std::min(deficit_wh, house_battery_wh);
                    house_battery_wh -= battery_used;
                    deficit_wh -= battery_used;

                    if (deficit_wh > 0 && ev_active && ev_v2h && ev_is_home && !is_forced_charge) { 
                        double v2h_reserve = ev_daily_wh + 5000.0; 
                        if (current_ev_wh > v2h_reserve) {
                            double v2h_use = std::min({deficit_wh, current_ev_wh - v2h_reserve, 11000.0 / 60.0});
                            current_ev_wh -= v2h_use;
                            deficit_wh -= v2h_use; 
                        }
                    }
                    
                    if (day == 1) {
                        grid_import_day2_wh += deficit_wh;
                        cost_with_pv_day += (deficit_wh / 1000.0) * current_kwh_price;
                    }
                }

                if (day == 1) {
                    final_consumption[i] = current_consumption_w;
                    total_cons_day2_wh += (current_consumption_w / 60.0);
                    battery_level_curve[i] = house_battery_wh;
                    ev_level_curve[i] = current_ev_wh;
                }
            }
        }

        double autarky_pct = 100.0;
        if (total_cons_day2_wh > 0) autarky_pct = std::max(0.0, 100.0 * (1.0 - (grid_import_day2_wh / total_cons_day2_wh)));

        double hardware_cost = (test_wp * cost_per_wp) + ((test_battery_wh / 1000.0) * cost_per_kwh_bat);
        double yearly_savings = (cost_without_pv_day - (cost_with_pv_day - revenue_export_day)) * 365.0;
        double roi_years = (yearly_savings > 0) ? (hardware_cost / yearly_savings) : 999.0;

        if (full_data) {
            return json{
                {"autarky", autarky_pct},
                {"total_consumption_wh", total_cons_day2_wh},
                {"grid_import_wh", grid_import_day2_wh},
                {"grid_export_wh", grid_export_day2_wh},
                {"consumption_watts", final_consumption},
                {"solar_watts", solar_watts},
                {"export_watts", grid_export_curve},
                {"battery_level_wh", battery_level_curve},
                {"ev_level_wh", ev_level_curve},
                {"economics", {
                    {"cost_without_pv", cost_without_pv_day},
                    {"cost_with_pv", cost_with_pv_day},
                    {"revenue", revenue_export_day},
                    {"hardware_cost", hardware_cost},
                    {"roi_years", roi_years}
                }}
            };
        } else {
            return json{{"wp", test_wp}, {"autarky", autarky_pct}, {"roi", roi_years}};
        }
    };

    json result = simulate_setup(user_wp, user_battery_wh, true);

    json matrix = json::array();
    std::vector<double> test_batteries = {0, 5000, 10000}; 
    
    for (double tb : test_batteries) {
        json series;
        series["battery_kwh"] = tb / 1000.0;
        json points = json::array();
        for (double twp = 1000; twp <= 12000; twp += 1000) {
            points.push_back(simulate_setup(twp, tb, false));
        }
        series["data"] = points;
        matrix.push_back(series);
    }

    result["investment_matrix"] = matrix;
    return result;
}