#include "phy.hpp"
#include <VerilogA/export.hpp>
#include <iostream>

using namespace analog;
using namespace analog::ten_g;
using analog::verilog_a::named_port;

template <class T, class Interface>
void emit(const std::filesystem::path& root, const std::filesystem::path& destination, const char* name,
          Interface interface) {
    simulation sim;
    T component;
    component.name(name);
    component.elaborate();
    for (auto& element : sim.elements)
        element.file =
            std::filesystem::relative(std::filesystem::weakly_canonical(element.file), root).generic_string();
    auto file = destination / "generated" / (std::string(name) + ".va");
    std::filesystem::create_directories(file.parent_path());
    verilog_a::detail::write_file(file, verilog_a::render_simulation_model(sim, name, interface(component)));
}
#define P(NAME)                                                                                                        \
    named_port {                                                                                                       \
        #NAME, c.NAME                                                                                                  \
    }
#define EXPORT(TYPE, NAME, ...)                                                                                        \
    emit<TYPE>(root, destination, NAME, [](auto& c) { return std::vector<named_port>{P(Vdd), P(Vss), __VA_ARGS__}; })

int main(int argc, char** argv) {
    if (argc != 3)
        return 2;
    try {
        std::filesystem::path root = std::filesystem::canonical(argv[1]), destination(argv[2]);
        EXPORT(tx_emphasis, "TenGTxEmphasis", P(PRE), P(MAIN), P(POST), P(VCM), P(P), P(N));
        EXPORT(ctle, "TenGCTLE", P(P), P(N), P(VCM), P(OUTP), P(OUTN));
        EXPORT(channel, "TenGChannel", P(P), P(N), P(VCM), P(OUTP), P(OUTN));
        EXPORT(sfi_input, "TenGSFIInput", P(P), P(N), P(VCM), P(OUTP), P(OUTN));
        EXPORT(dfe, "TenGDFE", P(P), P(N), P(VCM), P(CLOCK), P(Q));
        EXPORT(hogge_detector, "TenGHoggeDetector", P(DATA), P(CLOCK), P(ERROR), P(REFERENCE));
        EXPORT(vco_10g, "TenGVCO", P(CONTROL), P(OUT));
        EXPORT(lock_monitor, "TenGLockMonitor", P(ERROR), P(REFERENCE), P(VCM), P(LOCK));
        EXPORT(pll_cdr, "TenGCDR", P(P), P(N), P(VCM), P(CLOCK), P(LOCK));
        EXPORT(tap_history, "TenGTapHistory", P(IN), P(CLOCK), named_port{"PRE", c.history[0].Q},
               named_port{"MAIN", c.history[1].Q}, named_port{"POST", c.history[2].Q});
        EXPORT(serializer, "TenGSerializer", P(CLOCK_HALF), P(CLOCK_QUARTER), P(OUT), named_port{"D0", c.DATA[0]},
               named_port{"D1", c.DATA[1]}, named_port{"D2", c.DATA[2]}, named_port{"D3", c.DATA[3]});
        EXPORT(deserializer, "TenGDeserializer", P(IN), P(CLOCK), named_port{"D0", c.DATA[0]},
               named_port{"D1", c.DATA[1]}, named_port{"D2", c.DATA[2]}, named_port{"D3", c.DATA[3]});
        EXPORT(transmitter, "TenGTransmitter", P(CLOCK), P(CLOCK_HALF), P(CLOCK_QUARTER), P(VCM), P(P), P(N),
               named_port{"D0", c.DATA[0]}, named_port{"D1", c.DATA[1]}, named_port{"D2", c.DATA[2]},
               named_port{"D3", c.DATA[3]});
        EXPORT(receiver, "TenGReceiver", P(P), P(N), P(VCM), named_port{"D0", c.DATA[0]}, named_port{"D1", c.DATA[1]},
               named_port{"D2", c.DATA[2]}, named_port{"D3", c.DATA[3]}, named_port{"CLOCK", c.recovery.CLOCK},
               named_port{"LOCK", c.recovery.LOCK});
        EXPORT(link, "TenGLink", P(CLOCK), P(CLOCK_HALF), P(CLOCK_QUARTER), P(VCM), named_port{"D0", c.DATA[0]},
               named_port{"D1", c.DATA[1]}, named_port{"D2", c.DATA[2]}, named_port{"D3", c.DATA[3]},
               named_port{"R0", c.rx.DATA[0]}, named_port{"R1", c.rx.DATA[1]}, named_port{"R2", c.rx.DATA[2]},
               named_port{"R3", c.rx.DATA[3]}, named_port{"RECOVERED_CLOCK", c.rx.recovery.CLOCK},
               named_port{"LOCK", c.rx.recovery.LOCK});
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
#undef EXPORT
#undef P
