#pragma once
#include <analog.hpp>
#include <fstream>
#include <iomanip>

using namespace analog;

// These dimensions are explicit process choices. The compact model's beta
// identifies a reviewed role; it is not a formula for deriving MOS geometry.
static void write_process_devices(const simulation& sim, const electrical& rail,
                                  const std::filesystem::path& destination) {
    auto node = [&](std::size_t id) {
        const auto root = sim.root(id);
        return root == 0 ? std::string("0") : "n" + std::to_string(root);
    };
    std::ofstream output(destination / "process_devices.csv");
    output << "id,kind,a,b,gate,body,value,initial,width_um,length_um\n" << std::setprecision(17);
    for (unsigned id = 0; id < sim.elements.size(); ++id) {
        const auto& e = sim.elements[id];
        if (e.kind == law::sim_voltage_source)
            continue;
        if (e.kind == law::resistor || e.kind == law::capacitor) {
            output << id << ',' << (e.kind == law::resistor ? 'R' : 'C') << ',' << node(e.a) << ','
                   << node(e.b) << ",0,0," << (e.kind == law::resistor ? e.r : e.c) << ','
                   << e.initial << ",0,0\n";
        } else if (e.kind == law::nmos || e.kind == law::pmos) {
            const bool n = e.kind == law::nmos;
            double width = 0;
            if (std::abs(e.threshold - .7) < 1e-12) {
                if (n && std::abs(e.beta - .003) < 1e-12)
                    width = 10;
                else if (!n && std::abs(e.beta - .003) < 1e-12)
                    width = 30;
                else if (n && std::abs(e.beta - .03) < 1e-12)
                    width = 100;
                else if (n && std::abs(e.beta - .06) < 1e-12)
                    width = 200;
                else if (n && std::abs(e.beta - .4) < 1e-12)
                    width = 1600;
                else if (n && std::abs(e.beta - .2) < 1e-12)
                    width = 800;
                else if (!n && std::abs(e.beta - .03) < 1e-12)
                    width = 300;
                else if (!n && std::abs(e.beta - .06) < 1e-12)
                    width = 600;
                else if (!n && std::abs(e.beta - .02) < 1e-12)
                    width = 200;
            }
            if (width == 0)
                throw std::runtime_error("unbound receive gain MOS role");
            output << id << ',' << (n ? 'N' : 'P') << ',' << node(e.a) << ',' << node(e.b) << ','
                   << node(e.gate) << ',' << (n ? "0" : node(rail.id())) << ",0,0," << width << ",0.45\n";
        } else {
            throw std::runtime_error("unsupported receive gain process law");
        }
    }
    if (!output)
        throw std::runtime_error("cannot write receive gain process devices");
}
