#pragma once
#include "../../hls/MemoryMap.h"
#include "../../hls/MaskedSynchronousRam.h"

struct MapAddMethods
{
    cpphdl::hls::MapOutput<1> command(cpphdl::hls::MapInput<2> in, cpphdl::hls::MapInput<2>,
                                      cpphdl::hls::MapInput<2>)
    {
        return {{in.values[0] + in.values[1] + in.parameter}};
    }
};
struct MapPairMethods
{
    cpphdl::hls::MapOutput<2> command(cpphdl::hls::MapInput<1> in, cpphdl::hls::MapInput<1>,
                                      cpphdl::hls::MapInput<1>)
    {
        return {{in.values[0], in.values[0] + 1}};
    }
};
using MapAdd = cpphdl::hls::ClockedMap<MapAddMethods, 2, 1, 4>;
using MapPair = cpphdl::hls::ClockedMap<MapPairMethods, 1, 2, 4>;
using MaskedRam = cpphdl::hls::MaskedSynchronousRam<2, 8>;

class DimensionTop : public cpphdl::Module
{
    MapAdd add;
    MapPair pair;
    MaskedRam ram;

public:
    void _assign()
    {
        add._assign();
        pair._assign();
    }
    void _work(bool reset)
    {
        add._work(reset);
        pair._work(reset);
        ram._work(reset);
    }
    void _strobe()
    {
        add._strobe();
        pair._strobe();
        ram._strobe();
    }
};
