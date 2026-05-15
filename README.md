
//What the Simulator Can Do   

1. Solar Yield Simulation: Calculates power generation based on system size (Wp) using either real-world Open-Meteo API weather data or mathematical seasonal models.

2. Household Consumption: Models energy usage for base appliances, electric vehicles (EV), and heat pumps.

3. Intelligent EV Charging: Simulates smart solar-excess charging, emergency grid charging, and Vehicle-to-Home (V2H) discharging.

4. Battery Storage Analysis: Tracks the state of charge (SoC) for both house batteries and EV batteries to optimize storage sizing.

5. Dynamic Economics: Integrates Awattar API for live exchange electricity prices to calculate daily savings and Return on Investment (ROI).

6. Investment Matrix: Generates a "sweet spot" analysis comparing different solar and battery combinations to find the best value for money.


      
//How to Use It   

Configure Consumers:

Add your daily appliances in the Base Consumers section.

Toggle the Heat Pump or EV settings to match your home setup.

Set Market Prices:

Enter your current grid electricity price and feed-in tariff.

Input the current market hardware costs (Price/Wp for panels and Price/kWh for batteries).

Define Planned Hardware:

Enter the solar capacity and battery size you are considering.

Run Simulation:

Click Fetch Data & Start. The system will pull live data and run the C++ calculation engine.

Analyze Results:

Check the ROI and Self-Sufficiency tiles.

Review the 24h Daily Profile and Battery State of Charge graphs to see if your battery or solar array is sized correctly for your needs.


Prerequisites:
  C++ Compiler:  
  win: MinGW-w64  
  linux: sudo apt install build-essential  
  macOS: xcode-select --install  


Compile:  
win:   
g++ main.cpp Simulator.cpp SimulationEngine.cpp -o solar_calc.exe -std=c++11 -lws2_32   
.\solar_calc.exe     
linux,macOS:   
g++ main.cpp Simulator.cpp SimulationEngine.cpp -o solar_calc -std=c++11 -lpthread   
./solar_calc   
http://localhost:8080
