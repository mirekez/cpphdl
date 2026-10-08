#include <map>
#include <vector>
#ifndef _LIBCPP_VERSION
#error "HLS examples require libc++, not libstdc++"
#endif

int main()
{
    std::vector<int> values{7, 11};
    std::map<int, int> entries{{values[0], values[1]}};
    return entries.at(7) != 11;
}
