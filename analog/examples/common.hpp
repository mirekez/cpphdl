#pragma once
#include <analog.hpp>
#include <basic/basic.hpp>
#include <VerilogA/experiment.hpp>
#include <iostream>

using namespace analog;
using namespace analog::literals;

// Build the class hierarchy once; the engine then evolves the entire network.
template <class Build>
int example_main(int argc, char** argv, const char* default_out, duration stop, duration step, Build build) {
    auto out = std::filesystem::path(argc > 1 ? argv[1] : default_out);
    try {
        simulation sim;
        build(sim);
        if (argc > 2 && std::string_view(argv[argc - 1]) == "--verilog-a") {
            verilog_a::write_experiment(sim, out, {stop, step, step});
            return 0;
        }
        sim.run(stop, step);
        sim.write(out);
        std::cout << sim.status() << '\n';
        return sim.status() == "sampled_pass" ? 0 : 1;
    } catch (const diagnostic& error) {
        std::filesystem::create_directories(out);
        std::ofstream file(out / "report.json");
        file << "{\"status\":\"error\",\"code\":" << json_string(error.code)
             << ",\"message\":" << json_string(error.what()) << "}\n";
        std::cerr << error.what() << '\n';
        return 2;
    }
}
