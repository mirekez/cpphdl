#pragma once

// Replay the independent IEEE-coded fixture with arbitrary clock pauses. The
// expected words remain indexed by symbols; invalid-clock inputs are poison.
inline int receiver_enable_checks(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3)
            throw std::runtime_error("expected receiver enable fixtures [trace.csv]");
        std::ifstream vectors(argv[1]);
        std::ofstream trace;
        if (argc == 3)
            trace.open(argv[2]);
        if (!vectors || (argc == 3 && !trace))
            throw std::runtime_error("cannot open receiver enable fixture/trace");
        DUT dut;
        unsigned samples = 0, symbols = 0, pauses = 0, clocks = 0, resets = 0;
        bool master = false, pma = true, enable = false;
#ifndef VERILATOR
        dut.samples_in = _ASSIGN(samples);
        dut.remote_master_in = _ASSIGN(master);
        dut.pma_ok_in = _ASSIGN(pma);
        dut.symbol_enable_in = _ASSIGN(enable);
        dut._assign();
#endif
        auto tick = [&](bool reset) {
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = reset;
            dut.samples_in = samples;
            dut.remote_master_in = master;
            dut.pma_ok_in = pma;
            dut.symbol_enable_in = enable;
            dut.eval();
            dut.clk = 1;
            dut.eval();
            std::array<unsigned, 4> result{unsigned(dut.receive_out), unsigned(dut.locked_out),
                unsigned(dut.remote_receiver_ok_out), unsigned(dut.receive_valid_out)};
#else
            ++_system_clock;
            dut._work(reset);
            dut._strobe();
            std::array<unsigned, 4> result{unsigned(dut.receive_out()), unsigned(dut.locked_out()),
                unsigned(dut.remote_receiver_ok_out()), unsigned(dut.receive_valid_out())};
#endif
            if (result[3] != unsigned(enable && pma && !reset))
                throw std::runtime_error("receiver validity does not match the accepted symbol tick");
            if (trace.is_open())
                trace << clocks << ',' << result[0] << ',' << result[1] << ',' << result[2] << ',' << result[3] << '\n';
            ++clocks;
            return result;
        };
        std::string line;
        while (std::getline(vectors, line)) {
            if (line.empty() || line[0] == '#')
                continue;
            std::array<unsigned long, 7> row{};
            std::istringstream input(line);
            for (auto& value : row)
                if (!(input >> value))
                    throw std::runtime_error("malformed receiver enable fixture");
            bool reset = row[0];
            master = row[1];
            pma = row[2];
            samples = row[3];
            // Reset must work even when no symbol is accepted.
            enable = !reset;
            auto expected = tick(reset);
            if ((expected[0] & row[5]) != row[4] || (row[6] && !expected[1]))
                throw std::runtime_error("paused receiver disagrees with symbol oracle at " + std::to_string(symbols));
            resets += reset;
            ++symbols;
            // Exercise adjacent valid clocks, ordinary 2:1 sampling, and long
            // pauses while survivor paths and delimiter tags are in flight.
            unsigned gap = symbols % 257 == 0 ? 37 : symbols % 5;
            enable = false;
            for (unsigned j = 0; j < gap; ++j) {
                samples = 0xDEADBEEF ^ (symbols * 2654435761U) ^ j;
                master = !master;
                auto held = tick(false);
                if (held[0] != expected[0] || held[1] != expected[1] || held[2] != expected[2])
                    throw std::runtime_error("receiver advanced during a symbol pause at " + std::to_string(symbols));
                ++pauses;
            }
        }
        if (symbols < 50000 || pauses < 100000 || resets < 2)
            throw std::runtime_error("receiver enable coverage truncated");
        // PMA failure must revoke lock even while the symbol pipeline is held.
        pma = false;
        enable = false;
        auto failed = tick(false);
        if (failed[0] != 512 || failed[1] || failed[2] || failed[3])
            throw std::runtime_error("PMA loss failed to invalidate a paused receiver");
        auto cleared = tick(true);
        if (cleared[0] || cleared[1] || cleared[2] || cleared[3])
            throw std::runtime_error("disabled reset failed to flush the receiver");
        std::cout << symbols << " oracle symbols, " << pauses << " poisoned pause clocks, " << resets
                  << " disabled resets; complete PCS pipeline holds and validity is exact\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
