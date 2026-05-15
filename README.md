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
