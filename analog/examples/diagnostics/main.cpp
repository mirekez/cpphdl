#include "../common.hpp"

int main(int argc, char** argv) {
    auto out = std::filesystem::path(argc > 1 ? argv[1] : "examples/diagnostics/results");
    std::filesystem::create_directories(out);
    std::ofstream diagnostics(out / "diagnostics.json");
    diagnostics << "[\n";
    unsigned checked = 0;
    auto expect = [&](const char* code, auto build) {
        try {
            build();
            throw std::runtime_error(std::string("missing diagnostic ") + code);
        } catch (const diagnostic& error) {
            if (error.code != code)
                throw;
            if (checked++)
                diagnostics << ",\n";
            diagnostics << "  {\"code\":" << json_string(error.code) << ",\"message\":" << json_string(error.what())
                        << '}';
        }
    };
    try {
        expect("E_SINGULAR", [] {
            simulation sim;
            electrical A, B, Vss;
            sim.reference(Vss);
            B = resistor(A, 1_kOhm); // Purpose alone cannot fix a floating common voltage.
            sim.run(1_us, 100_ns);
        });
        expect("E_DRIVER", [] {
            simulation sim;
            bits<1> A;
            sim.drive(A, 0);
            sim.drive(A, 1); // Bit assignments need one writer.
        });
        expect("E_VALUE", [] {
            simulation sim;
            electrical A, Y;
            Y = resistor(A, resistance(-1)); // Invalid selected model parameter.
        });
        expect("E_UNKNOWN_PHYSICAL", [] {
            simulation sim;
            electrical A, Vdd, Vss, Y;
            bits<1> D;
            sim.reference(Vss);
            A = sim_voltage_source(Vss, .6_V);
            Vdd = sim_voltage_source(Vss, 1.2_V);
            D = sim_sense(A, Vss, .35_V, .85_V); // Middle region deliberately has unknown logic.
            Y = sim_driver(D, Vdd, Vss,
                           100_Ohm); // The engine cannot silently turn unknown into a rail choice.
            sim.run(1_us, 100_ns);
        });
        {
            simulation sim;
            electrical Y, Vss;
            sim.reference(Vss);
            Y = sim_voltage_source(Vss, 1_V);
            sim.observe("Y", Y, Vss);
            sim.assume_voltage("supply envelope", Y, Vss, 1.1_V, 1.3_V);
            sim.require_final_voltage("numerically in bound", Y, Vss, 0_V, 1.3_V);
            sim.run(1_us, 100_ns);
            if (sim.status() != "out_of_contract")
                throw std::runtime_error("assumption was not checked");
            sim.write(out / "out_of_contract"); // A numerical comparison passing cannot repair its assumption.
            ++checked;
        }
        diagnostics << "\n]\n";
        std::ofstream report(out / "report.json");
        report << "{\"language\":\"Analog C++ 0.2\",\"status\":\"expected_diagnostics\",\"checks\":" << checked
               << "}\n";
        std::cout << checked << " expected diagnostics verified\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
