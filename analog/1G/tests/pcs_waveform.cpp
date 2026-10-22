#include "../pcs_receiver.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

long _system_clock = -1;

int main(int argc, char** argv) {
    if (argc != 6)
        return 2;
    try {
        std::ifstream soft(argv[1]), gmii(argv[2]);
        std::ofstream trace(argv[3]), result(argv[4]);
        if (!soft || !gmii || !trace || !result)
            throw std::runtime_error("missing waveform/GMII inputs or unwritable PCS outputs");
        unsigned training = std::stoul(argv[5]);
        PCSReceiver receiver[2];
        unsigned samples[2]{};
        bool pma = false;
        for (unsigned i = 0; i < 2; ++i) {
            receiver[i].samples_in = _ASSIGN_I(samples[i]);
            receiver[i].remote_master_in = _ASSIGN_I(i == 1);
            receiver[i].pma_ok_in = _ASSIGN(pma);
            receiver[i]._assign();
        }
        std::string line, original;
        std::getline(soft, line);
        std::getline(gmii, original);
        std::array<std::deque<unsigned>, 2> expected;
        for (auto& queue : expected)
            queue.assign(39, 0); // Include the metric and trellis decision pipelines.
        std::array<std::vector<unsigned>, 2> frames;
        unsigned count = 0, checked = 0, completed = 0, octets = 0;
        trace << "symbol,rx_a,rx_b\n";
        while (std::getline(soft, line)) {
            if (!std::getline(gmii, original))
                throw std::runtime_error("missing GMII stimulus rows");
            std::replace(line.begin(), line.end(), ',', ' ');
            std::replace(original.begin(), original.end(), ',', ' ');
            std::istringstream values(line), source(original);
            unsigned index, input_index, a, b;
            if (!(values >> index) || index != count || !(source >> input_index >> a >> b) || input_index != count)
                throw std::runtime_error("nonsequential waveform/stimulus input");
            for (unsigned side = 0; side < 2; ++side) {
                samples[side] = 0;
                for (unsigned lane = 0; lane < 4; ++lane) {
                    double value;
                    if (!(values >> value) || !std::isfinite(value))
                        throw std::runtime_error("incomplete/nonfinite equalized ADC samples");
                    unsigned code = unsigned(std::clamp(std::lround(128 + value * 32), 0L, 255L));
                    samples[side] |= code << (8 * lane);
                }
            }
            double extra;
            if (values >> extra || source >> extra)
                throw std::runtime_error("unexpected waveform/stimulus columns");
            pma = count >= training;
            ++_system_clock;
            for (auto& rx : receiver)
                rx._work(count == 0);
            for (auto& rx : receiver)
                rx._strobe();
            for (unsigned side = 0; side < 2; ++side) {
                unsigned word = unsigned(receiver[side].receive_out());
                unsigned want = expected[side].front();
                expected[side].pop_front();
                expected[side].push_back(side == 0 ? b : a);
                if (count >= training + 160) {
                    if (word != want || !receiver[side].locked_out())
                        throw std::runtime_error("electrical-waveform PCS mismatch at symbol " + std::to_string(count) +
                                                 " side " + std::to_string(side) + ": expected " +
                                                 std::to_string(want) + ", got " + std::to_string(word));
                    ++checked;
                    if (side == 0)
                        trace << count;
                    trace << ',' << word;
                    if (word & 256) {
                        frames[side].push_back(word & 255);
                        ++octets;
                    } else if (!frames[side].empty()) {
                        auto& frame = frames[side];
                        if (frame.size() != 72)
                            throw std::runtime_error("waveform PCS frame length mismatch");
                        for (unsigned byte = 0; byte < 8; ++byte)
                            if (frame[byte] != (byte == 7 ? 0xD5 : 0x55))
                                throw std::runtime_error("waveform PCS preamble/SFD mismatch");
                        unsigned crc = 0xFFFFFFFF, fcs = 0;
                        for (unsigned byte = 8; byte < 68; ++byte) {
                            crc ^= frame[byte];
                            for (unsigned bit = 0; bit < 8; ++bit)
                                crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
                        }
                        for (unsigned byte = 0; byte < 4; ++byte)
                            fcs |= frame[68 + byte] << (8 * byte);
                        if (~crc != fcs)
                            throw std::runtime_error("waveform PCS output FCS mismatch");
                        frame.clear();
                        ++completed;
                    }
                }
            }
            if (count >= training + 160)
                trace << '\n';
            ++count;
        }
        if (std::getline(gmii, original) || completed != 2 || octets != 144 || checked < 350)
            throw std::runtime_error("waveform PCS test did not cover both complete frames");
        result << "{\"status\":\"pass\",\"frames\":" << completed << ",\"gmii_octets\":" << octets
               << ",\"checked_output_words\":" << checked << ",\"symbol_errors\":0}\n";
        std::cout << "C++RTL PCS recovered two complete FCS-checked frames from independent electrical samples\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
