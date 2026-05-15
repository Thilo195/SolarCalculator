#include "SimulationEngine.h"
#include <cmath>
#include <algorithm>

SimulationEngine::SimulationEngine(const SimulationConfig& cfg) : config(cfg) {}

SimulationResult SimulationEngine::run(double test_wp, double test_battery_wh, bool full_data) {
    SimulationResult res;
    res.wp = test_wp;
    
    std::vector<double> solar_watts(num_intervals, 0.0);
    res.consumption_watts.resize(num_intervals, 0.0);
    res.export_watts.resize(num_intervals, 0.0);
    res.battery_level_wh.resize(num_intervals, 0.0);
    res.ev_level_wh.resize(num_intervals, 0.0);
    
    if (config.custom_solar_curve.size() == 24) {
        for(int i = 0; i < num_intervals; ++i) {
            int hour = i / 60;
            solar_watts[i] = (config.custom_solar_curve[hour] / 1000.0) * test_wp * 0.85;
        }
    } else {
        double pi = 3.14159265358979323846;
        double yield_multiplier = (config.heatpump.season == "winter") ? 0.375 : 1.0;
        double peak_power = test_wp * (pi / 6.0) * yield_multiplier;
        int sun_start = (config.heatpump.season == "winter") ? 480 : 360;
        int sun_end = (config.heatpump.season == "winter") ? 960 : 1080;
        for(int i = sun_start; i < sun_end; ++i) {
            solar_watts[i] = peak_power * std::sin(pi * (i - sun_start) / (double)(sun_end - sun_start));
        }
    }
    
    if (full_data) res.solar_watts = solar_watts;

    double house_battery_wh = 0.0;
    double current_ev_wh = config.ev.capacity_wh;
    double no_pv_ev_wh = config.ev.capacity_wh;

    double total_cons_day2_wh = 0.0;
    double grid_import_day2_wh = 0.0;
    double grid_export_day2_wh = 0.0;
    
    double cost_without_pv_day = 0.0;
    double cost_with_pv_day = 0.0;
    double revenue_export_day = 0.0;

    double hp_power_w = config.heatpump.get_power_w();

    for (int day = 0; day < 2; ++day) {
        for(int i = 0; i < num_intervals; ++i) {
            int current_hour = i / 60;
            double current_kwh_price = config.use_dynamic_price ? config.dynamic_prices[current_hour] : config.fixed_price;

            double base_power = config.base_device_power[i] + hp_power_w;
            double current_consumption_w = base_power;
            double no_pv_consumption_w = base_power;

            bool ev_is_home = false;
            int minutes_to_departure = 0;

            if (config.ev.active) {
                if (config.ev.start_min < config.ev.end_min) {
                    ev_is_home = (i >= config.ev.start_min && i < config.ev.end_min);
                    if (ev_is_home) minutes_to_departure = config.ev.end_min - i;
                } else {
                    ev_is_home = (i >= config.ev.start_min || i < config.ev.end_min);
                    if (ev_is_home) {
                        if (i >= config.ev.start_min) minutes_to_departure = (1440 - i) + config.ev.end_min;
                        else minutes_to_departure = config.ev.end_min - i;
                    }
                }
                
                if (i == config.ev.end_min) {
                    current_ev_wh = std::max(0.0, current_ev_wh - config.ev.daily_wh);
                    no_pv_ev_wh = std::max(0.0, no_pv_ev_wh - config.ev.daily_wh);
                }
            }

            if (config.ev.active && ev_is_home && (config.ev.capacity_wh - no_pv_ev_wh) > 0) {
                double no_pv_space = config.ev.capacity_wh - no_pv_ev_wh;
                double no_pv_charge_w = std::min(11000.0, no_pv_space * 60.0);
                no_pv_ev_wh += (no_pv_charge_w / 60.0);
                no_pv_consumption_w += no_pv_charge_w;
            }

            if (day == 1) cost_without_pv_day += (no_pv_consumption_w / 60000.0) * current_kwh_price;

            double ev_charging_w = 0.0;
            double space_left_wh = config.ev.capacity_wh - current_ev_wh;
            bool is_forced_charge = false;

            if (config.ev.active && ev_is_home && space_left_wh > 0) {
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
                if (config.ev.active && ev_is_home && !is_forced_charge && space_left_wh > 0) {
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
                    revenue_export_day += (exported_power_w / 60000.0) * config.feed_in_tariff;
                    res.export_watts[i] = exported_power_w;
                }
            } 
            else { // deficit
                double deficit_wh = -net_power / 60.0;
                
                double battery_used = std::min(deficit_wh, house_battery_wh);
                house_battery_wh -= battery_used;
                deficit_wh -= battery_used;

                if (deficit_wh > 0 && config.ev.active && config.ev.v2h && ev_is_home && !is_forced_charge) { 
                    double v2h_reserve = config.ev.daily_wh + 5000.0; 
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
                res.consumption_watts[i] = current_consumption_w;
                total_cons_day2_wh += (current_consumption_w / 60.0);
                res.battery_level_wh[i] = house_battery_wh;
                res.ev_level_wh[i] = current_ev_wh;
            }
        }
    }

    res.total_consumption_wh = total_cons_day2_wh;
    res.grid_import_wh = grid_import_day2_wh;
    res.grid_export_wh = grid_export_day2_wh;
    
    res.autarky_pct = 100.0;
    if (total_cons_day2_wh > 0) res.autarky_pct = std::max(0.0, 100.0 * (1.0 - (grid_import_day2_wh / total_cons_day2_wh)));

    res.economics.hardware_cost = (test_wp * config.cost_per_wp) + ((test_battery_wh / 1000.0) * config.cost_per_kwh_bat);
    double yearly_savings = (cost_without_pv_day - (cost_with_pv_day - revenue_export_day)) * 365.0;
    res.economics.roi_years = (yearly_savings > 0) ? (res.economics.hardware_cost / yearly_savings) : 999.0;
    
    res.economics.cost_without_pv = cost_without_pv_day;
    res.economics.cost_with_pv = cost_with_pv_day;
    res.economics.revenue = revenue_export_day;

    return res;
}