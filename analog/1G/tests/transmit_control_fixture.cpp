#include "transmit_stimulus.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

long _system_clock = -1;

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected controller trace destination");
        std::ofstream output(argv[1]);
        if (!output)
            throw std::runtime_error("cannot open controller trace");
        output << "index,code,previous,enable,reset,valid,fault,main_p,main_n,history_p,history_n\n";
        const auto words = stimulus();
        for (unsigned index = 0; index < words.size(); ++index) {
            const auto& word = words[index];
            output << index << ',' << word.code << ',' << word.previous << ',' << word.enable << ','
                   << word.reset << ',' << word.valid << ',' << word.fault << ',' << word.main_p << ','
                   << word.main_n << ',' << word.history_p << ',' << word.history_n << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
