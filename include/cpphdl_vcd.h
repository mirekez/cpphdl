#include <fstream>
#include <vector>
#include <string>
#include <ctime>
#include <cstdint>

struct VcdSignal
{
    std::string name;
    unsigned    width;   // number of bits
    const void* ptr;     // pointer to integer OR raw byte buffer
};

struct VcdFile
{
    std::ofstream file;
    std::vector<VcdSignal> signals;

    inline void create(const std::string& name)
    {
        file.close();
        file.open(name);
        std::time_t now = std::time(nullptr);

        file << "$date\n  " << std::ctime(&now) << "$end\n";
        file << "$version\n  tiny-struct-vcd\n$end\n";
        file << "$timescale 1ns $end\n";

        file << "$scope module logic $end\n";

        char id = '!';
        for (const auto& s : signals) {
            file << "$var wire "
               << s.width << " "
               << id++ << " "
               << s.name << " $end\n";
        }

        file << "$upscope $end\n";
        file << "$enddefinitions $end\n";
        file << "#0\n";
    }

    void sample(unsigned time_ns)
    {
        file << "#" << time_ns << "\n";

        char id = '!';

        for (const auto& s : signals) {
            const unsigned bytes = (s.width + 7) / 8;
            const uint8_t* data  = static_cast<const uint8_t*>(s.ptr);

            if (s.width == 1) {
                file << ((*data & 1) ? '1' : '0') << id << "\n";
            }
            else {
                file << "b";

                for (int bit = s.width - 1; bit >= 0; --bit)
                {
                    unsigned byte_index = bit / 8;
                    unsigned bit_index  = bit % 8;
                    file << ((data[byte_index] >> bit_index) & 1);
                }

                file << " " << id << "\n";
            }

            ++id;
        }
    }
};
