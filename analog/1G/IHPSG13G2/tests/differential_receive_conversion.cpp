#include "differential_conversion_fixture.hpp"

int main(int argc, char** argv) {
    try {
        if (argc != 7)
            throw std::runtime_error("expected destination run|export slices gain_code storage_pF bursts");
        const std::filesystem::path destination = argv[1];
        const std::string mode = argv[2];
        const unsigned count = std::stoul(argv[3]), gain = std::stoul(argv[4]), bursts = std::stoul(argv[6]);
        const double storage = std::stod(argv[5]) * 1e-12;
        if ((mode != "run" && mode != "export") || (count != 1 && count != 4 && count != 13) ||
            (gain != 4 && gain != 8) || std::abs(storage - 1.5e-12) > 1e-24 || (bursts != 1 && bursts != 2) || (bursts == 2 && count != 13))
            throw std::runtime_error("invalid compact differential conversion experiment");
        if (count == 1)
            return differential_conversion<1>(destination, mode == "run", gain, storage, bursts);
        if (count == 4)
            return differential_conversion<4>(destination, mode == "run", gain, storage, bursts);
        return differential_conversion<13>(destination, mode == "run", gain, storage, bursts);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
