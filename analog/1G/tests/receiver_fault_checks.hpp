#pragma once

// The caller provides the same native/RTL receiver DUT type.
inline int receiver_fault_checks(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3)
            throw std::runtime_error("expected receiver fault fixtures [trace.csv]");
        std::ifstream vectors(argv[1]);
        if (!vectors)
            throw std::runtime_error("missing receiver fault fixtures");
        std::ofstream trace;
        if (argc == 3) {
            trace.open(argv[2]);
            if (!trace)
                throw std::runtime_error("cannot open receiver fault trace");
        }
        DUT dut;
        unsigned samples = 0;
        bool master = true, pma = true;
#ifndef VERILATOR
        dut.samples_in = _ASSIGN(samples);
        dut.remote_master_in = _ASSIGN(master);
        dut.pma_ok_in = _ASSIGN(pma);
        dut._assign();
#endif
        std::string line;
        unsigned cycle = 0, previous_window = 0, windows = 0;
        bool saw_error = false;
        auto finish_window = [&] {
            if (previous_window) {
                if (!saw_error)
                    throw std::runtime_error("receiver silently accepted an injected fault");
                ++windows;
            }
            saw_error = false;
        };
        while (std::getline(vectors, line)) {
            if (line.empty() || line[0] == '#')
                continue;
            std::array<unsigned long, 8> row{};
            std::istringstream input(line);
            for (auto& value : row)
                if (!(input >> value))
                    throw std::runtime_error("malformed receiver fault fixture");
            bool reset = row[0];
            master = row[1];
            pma = row[2];
            samples = row[3];
#ifdef VERILATOR
            dut.clk = 0;
            dut.reset = reset;
            dut.remote_master_in = master;
            dut.pma_ok_in = pma;
            dut.samples_in = samples;
            dut.eval();
            dut.clk = 1;
            dut.eval();
            unsigned word = dut.receive_out;
            bool locked = dut.locked_out;
#else
            ++_system_clock;
            dut._work(reset);
            dut._strobe();
            unsigned word = unsigned(dut.receive_out());
            bool locked = dut.locked_out();
#endif
            if (trace.is_open())
                trace << word << ',' << locked << '\n';
            if (row[7] != previous_window) {
                finish_window();
                previous_window = row[7];
            }
            if (word & 512)
                saw_error = true;
            if ((word & row[5]) != (row[4] & row[5]) || (row[6] && !locked))
                throw std::runtime_error("receiver fault cycle " + std::to_string(cycle) + " window " +
                                         std::to_string(row[7]) + ": expected " + std::to_string(row[4]) + " mask " +
                                         std::to_string(row[5]) + ", got " + std::to_string(word) + " lock " +
                                         std::to_string(locked));
            ++cycle;
        }
        finish_window();
        if (windows != 8 || cycle < 4000)
            throw std::runtime_error("receiver fault coverage truncated");
        std::cout << "Both roles reject malformed SSD/ESD, revoke stale-seed/PMA lock and recover valid frames; "
                  << cycle << " observations\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
