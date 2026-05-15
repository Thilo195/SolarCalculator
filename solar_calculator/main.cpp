#ifdef _WIN32
    #define _WIN32_WINNT 0x0A00
#endif

#include "httplib.h"
#include "json.hpp"
#include "Simulator.h"
#include <iostream>
#include <fstream>

int main() {
    httplib::Server svr;

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        std::ifstream ifs("index.html");
        if (ifs.good()) {
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            res.set_content(content, "text/html");
        } else {
            res.status = 404;
            res.set_content("HTML-Datei nicht gefunden!", "text/plain");
        }
    });

    svr.Post("/calculate", [](const httplib::Request& req, httplib::Response& res) {
        auto data = nlohmann::json::parse(req.body);
        nlohmann::json response = run_simulation(data);
        res.set_content(response.dump(), "application/json");
    });

    std::cout << "Server: http://localhost:8080";
    svr.listen("0.0.0.0", 8080);
    return 0;
}