#include "Verilog.h"
#include "Mapping.h"
#include "Evaluate.h"
#include <cstdio>

using namespace cpphdl::graph;

static void manyStreamingRegions() {
    Graph original;original.clockContract=ClockContract::RisingEdgeStep;
    for(unsigned i=0;i<1536;++i) {
        StreamPipeline p;p.scope="region_"+std::to_string(i);
        p.logic="saved transition "+std::to_string(i);p.stages=1;p.latency=1+i%19;p.generation=i;
        p.pins["result_out"]=constant(i,32);p.registers.push_back(constant(i+1,32));
        original.pipelines.push_back(std::move(p));
    }
    std::ostringstream serialized;original.save(serialized);
    Graph loaded;loaded.load(serialized.str().c_str());
    std::ostringstream roundTrip;loaded.save(roundTrip);
    if(roundTrip.str()!=serialized.str())throw std::runtime_error("large stream metadata round-trip changed records");
    // A count without matching records must still fail, without preallocating
    // a vector of the claimed size. Exercise the stream overload directly too.
    const auto prefix=serialized.str().substr(0,serialized.str().find("streams_v1\n"));
    std::istringstream truncated(prefix+"streams_v1\n18446744073709551615\n");
    bool rejected=false;
    try {Graph bad;bad.load(truncated);}catch(const std::exception&){rejected=true;}
    if(!rejected)throw std::runtime_error("truncated stream metadata accepted");
}

static void unusedPipelineArgument() {
    Graph g; g.clockContract=ClockContract::RisingEdgeStep;
    auto input=g.wire(8,"input","input"), select=g.wire(1,"select","input");
    auto argument=g.mux(select,input,constant(17,8));
    g.ports={{"input",input,true},{"select",select,true},{"result",input,false}};
    StreamPipeline pipeline; pipeline.scope="unused_argument";
    pipeline.pins["operation_in"]=argument;
    g.pipelines.push_back(pipeline);
    // Preserving scheduling provenance must not retain dead arithmetic in RTL.
    auto check=[](Graph graph) {
        for (auto n:graph.dependencyOrder())
            if (graph.nodes[n].op!="input")
                throw std::runtime_error("unused pipeline metadata made hardware live");
        if (graph.pipelines.size()!=1 || graph.pipelines[0].pins.at("operation_in").size()!=8)
            throw std::runtime_error("mapper lost pipeline argument metadata");
        graph.ports.push_back({"argument",graph.pipelines[0].pins.at("operation_in"),false});
        for (unsigned s=0;s<2;++s) {
            Evaluate sim(graph); sim.input("input",91); sim.input("select",s); sim.eval();
            if (sim.output("argument")!=(s?91:17))
                throw std::runtime_error("mapper changed unused pipeline argument");
        }
    };
    check(cpphdl::synth::mapGates(g));
    check(cpphdl::synth::expandGates(g,{Graph::owner(argument[0])}));
}

static void predicateCanonicalization() {
    Graph g;g.clockContract=ClockContract::RisingEdgeStep;
    auto a=g.wire(1,"a","input"), b=g.wire(1,"b","input");
    auto equal=g.binary("eq",{a[0],1,0},{a[0],1,0},1);
    auto identity=g.binary("eq",{a[0],0,1},{1,0,1},1);
    auto inversion=g.binary("eq",{a[0],0,1},{0,0,1},1);
    auto contradiction=g.binary("eq",{a[0],0,1},{a[0],1,1},1);
    auto twoBits=g.binary("eq",{a[0],b[0]},{1,0},1);
    auto twoVariables=g.binary("eq",a,b,1);
    auto unequalWidths=g.binary("eq",a,{a[0],0},1);
    auto doubleNot=g.binary("xor",g.binary("xor",{a[0],b[0],1},{1,1,1},3),{1,1,1},3);
    auto alias=g.wire(3,"alias");
    g.connect(alias,g.binary("xor",{a[0],b[0],1},{1,1,1},3));
    auto aliasDoubleNot=g.binary("xor",alias,{1,1,1},3);
    if(g.resolved(equal)!=Value{1} || g.resolved(identity)!=a ||
       g.resolved(contradiction)!=Value{0} ||
       g.resolved(doubleNot)!=Value{a[0],b[0],1} ||
       g.resolved(aliasDoubleNot)!=Value{a[0],b[0],1})
        throw std::runtime_error("predicate canonicalization missed a wiring identity");
    if(g.nodes[Graph::owner(twoBits[0])].op!="eq" ||
       g.nodes[Graph::owner(twoVariables[0])].op!="eq" ||
       g.nodes[Graph::owner(unequalWidths[0])].op!="eq")
        throw std::runtime_error("predicate canonicalization widened beyond its safe case");
    g.ports={{"a",a,true},{"b",b,true},{"equal",equal,false},{"identity",identity,false},
             {"inversion",inversion,false},{"contradiction",contradiction,false},
             {"two_bits",twoBits,false},{"two_variables",twoVariables,false},
             {"double_not",doubleNot,false},{"alias_double_not",aliasDoubleNot,false}};
    for(unsigned av=0;av<2;++av)for(unsigned bv=0;bv<2;++bv) {
        Evaluate sim(g);sim.input("a",av);sim.input("b",bv);sim.eval();
        if(sim.output("equal")!=1 || sim.output("identity")!=av ||
           sim.output("inversion")!=(!av) || sim.output("contradiction")!=0 ||
           sim.output("two_bits")!=(av&&!bv) || sim.output("two_variables")!=(av==bv) ||
           sim.output("double_not")!=(unsigned(4)|bv<<1|av) ||
           sim.output("alias_double_not")!=(unsigned(4)|bv<<1|av))
            throw std::runtime_error("predicate canonicalization truth table changed");
    }
}

static void forwardWireInversion() {
    Graph g;g.clockContract=ClockContract::RisingEdgeStep;
    auto x=g.wire(1,"x","input"),f=g.wire(1,"f"),w=g.wire(1,"w");
    auto a=g.unary("not",f),b=g.unary("not",x),c=g.unary("not",w);
    g.connect(w,b);g.connect(f,c);g.optimize();
    if(g.resolved(a)!=g.resolved(b) || g.resolved(c)!=g.resolved(x))
        throw std::runtime_error("forward-wire inversion created a reverse alias cycle");
    g.ports={{"x",x,true},{"a",a,false},{"b",b,false},{"c",c,false}};
    for(auto graph:{g,cpphdl::synth::mapGates(g)})for(unsigned value=0;value<2;++value) {
        Evaluate sim(graph);sim.input("x",value);sim.eval();
        if(sim.output("a")!=!value || sim.output("b")!=!value || sim.output("c")!=value)
            throw std::runtime_error("forward-wire inversion truth table changed");
    }
}

static Graph simple() {
    Graph graph;
    graph.clockContract = ClockContract::RisingEdgeStep;
    auto input = graph.wire(8, "a", "input");
    graph.ports = {{"a", input, true}, {"result", graph.binary("add", input, constant(1, 8), 8), false}};
    return graph;
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    manyStreamingRegions();
    predicateCanonicalization();
    unusedPipelineArgument();
    forwardWireInversion();
    auto rejected = [&](Graph graph, const std::string& message) {
        try { cpphdl::synth::emitVerilog(graph, argv[1], "Test"); }
        catch (const std::exception& error) {
            if (std::string(error.what()).find(message) != std::string::npos) return true;
            std::fprintf(stderr, "wrong diagnostic: %s\n", error.what());
        }
        return false;
    };
    auto graph = simple();
    cpphdl::synth::emitVerilog(graph, argv[1], "Test");
    Graph legacy;
    legacy.load("0\n0\n0\n0\n");
    if (legacy.clockContract != ClockContract::ExplicitEvents) return 1;
    Graph current;
    current.load("0\n0\n0\n0\n0\n0\n0\nclock_contract 1\n");
    if (current.clockContract != ClockContract::RisingEdgeStep) return 1;
    graph.clockContract = ClockContract::ExplicitEvents;
    if (!rejected(graph, "lifecycle")) return 1;
    graph = simple(); graph.memories.push_back({"ram", 8, 16});
    cpphdl::synth::emitVerilog(graph, argv[1], "Test");
    graph.memoryWrites.push_back({1, constant(0, 4), constant(0, 8), {1}});
    if (!rejected(graph, "invalid graph memory write")) return 1;
    graph = simple();
    auto romAddress=graph.ports[0].bits;
    graph.ports.push_back({"rom",graph.constantArray(constant(0x12345abc,36),romAddress,9),false});
    auto romText=cpphdl::synth::verilogText(graph,"Rom");
    if(romText.find("__cpphdl_memory_0[0] = 9'b010111100;")==std::string::npos ||
       romText.find("__cpphdl_memory_0[3] = 9'b000000010;")==std::string::npos) return 1;
    graph.memoryWrites.push_back({0,constant(0,8),constant(0,9),{1}});
    if(!rejected(graph,"write to graph ROM")) return 1;
    graph = simple(); graph.add("host_random", 32, {}, {}, {1});
    if (!rejected(graph, "host effects")) return 1;
    graph = simple(); graph.ports[0].name = "clk";
    if (!rejected(graph, "conflicting")) return 1;
    graph = simple();
    auto state = graph.wire(8, "state", "state");
    graph.states.push_back({state, constant(0, 8), {0}});
    if (!rejected(graph, "state/event")) return 1;
    graph = simple(); graph.clockContract = ClockContract::NamedEdges;
    graph.clocks = {{"main_clk", 100}, {"secondary_clk", 25}};
    state = graph.wire(8, "state", "state");
    graph.states.push_back({state, constant(0, 8), {1}, 2, false});
    if (!rejected(graph, "invalid state clock")) return 1;
    graph.states[0].clock = 0;
    graph.states.push_back({state, constant(1, 8), {1}, 1, false});
    if (!rejected(graph, "multiple clock/edge")) return 1;
    graph.states.pop_back();
    graph.clocks[1].name = "main_clk";
    if (!rejected(graph, "duplicate clock")) return 1;
    graph.clocks[1].name = "a";
    if (!rejected(graph, "conflicts with data port")) return 1;
    graph.clocks[1].name = "secondary_clk";
    graph.memories.push_back({"ram", 8, 4});
    graph.memoryWrites.push_back({0, constant(0, 2), constant(7, 8), {1}, 0, false});
    graph.memoryWrites.push_back({0, constant(1, 2), constant(9, 8), {1}, 1, false});
    if (!rejected(graph, "multiple clock/edge writers")) return 1;
    graph.memoryWrites.pop_back();
    graph.memoryWrites[0].clock = 2;
    if (!rejected(graph, "invalid memory clock")) return 1;
    graph.memoryWrites[0].clock = 0;
    graph.states[0].reset = graph.ports[0].bits;
    graph.states[0].resetValue = constant(0, 8);
    if (!rejected(graph, "constant full-width")) return 1;
    graph.states[0].reset = graph.wire(1, "reset", "input");
    graph.ports.push_back({"reset", graph.states[0].reset, true});
    graph.states[0].resetValue = state;
    if (!rejected(graph, "constant full-width")) return 1;
    graph.states[0].resetValue = constant(0xa5, 8);
    cpphdl::synth::emitVerilog(graph, argv[1], "Test");
    graph = simple(); graph.ports[1].bits = graph.wire(8, "missing");
    if (!rejected(graph, "undriven")) return 1;
    graph = simple();
    auto hole = graph.wire(8, "loop");
    auto loop = graph.binary("add", hole, constant(1, 8), 8);
    graph.connect(hole, loop); graph.ports[1].bits = loop;
    if (!rejected(graph, "combinational cycle")) return 1;
    std::puts("synthesis rejection checks passed");
}
