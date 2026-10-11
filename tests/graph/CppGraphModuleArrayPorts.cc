#include "cpphdl.h"

class GraphBundle : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<32>) request, response;
};

class GraphBundleChild : public cpphdl::Module {
public:
    _PORT(cpphdl::array<2, GraphBundle>) bundles;
    _PORT(cpphdl::logic<32>) request_in;
    _PORT(cpphdl::logic<32>) response_out = _ASSIGN(bundles()[0].response() ^ bundles()[1].response());
    void _assign() {
        bundles()[0].request = _ASSIGN(request_in());
        bundles()[1].request = _ASSIGN(~request_in());
    }
};

class GraphModuleArrayPorts : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<32>) request_in;
    _PORT(cpphdl::logic<32>) response_out = _ASSIGN(child.response_out());
    cpphdl::array<2, GraphBundle> bundles;
    GraphBundleChild child;
    void _assign() {
        child.bundles = _ASSIGN_REG(bundles);
        child.request_in = _ASSIGN(request_in());
        bundles[0].response = _ASSIGN(bundles[0].request() + cpphdl::logic<32>(7));
        bundles[1].response = _ASSIGN(bundles[1].request() ^ cpphdl::logic<32>(0x12345678));
        child._assign();
    }
    void _work(bool) {}
    void _strobe() {}
};

extern GraphModuleArrayPorts cpphdl_top;

#ifdef CPP_GRAPH_MODULE_ARRAY_PORTS_RUN
#include "model.h"
#include <cstdio>
int main() {
    cpphdl_native::Model model;
    for (unsigned sample = 0; sample < 20000; ++sample) {
        const uint32_t request = sample * 2654435761u;
        model.request[0] = request;
        model.step();
        if (model.response[0] != ((request + 7) ^ (~request ^ 0x12345678u))) return 1;
    }
    std::puts("ordinary C++ graph: 20000 module-array port bindings preserve hierarchy and bidirectional wires");
}
#endif
