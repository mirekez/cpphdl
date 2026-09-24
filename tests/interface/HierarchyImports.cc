#include "cpphdl.h"
using namespace cpphdl;

struct ImportDetachedPacket { uint32_t data; } __PACKED;
enum class ImportKind : uint8_t { Read = 1, Write = 2 };
struct ImportMeta { ImportKind kind; uint8_t tag; } __PACKED;
struct ImportRequest { ImportMeta meta; uint16_t data; } __PACKED;
template<unsigned WIDTH>
struct ImportResponse { logic<WIDTH> data; uint8_t tag; } __PACKED;

template<typename Packet>
class ImportPlainLeaf : public Module {
public:
    _PORT(Packet) packet_in;
    _PORT(Packet) packet_out;
    void _assign() { packet_out = _ASSIGN(packet_in()); }
};

class ImportUnusedLeaf : public Module {
public:
    _PORT(ImportDetachedPacket) packet_in;
    _PORT(ImportDetachedPacket) packet_out;
};

// No struct appears in this parent's ports, variables or assignments. The
// unassigned child ports still generate struct-typed interconnect declarations.
class ImportUnboundParent : public Module {
public:
    ImportUnusedLeaf unused;
    ImportUnusedLeaf unused_array[2];
    _PORT(logic<32>) data_in;
    _PORT(logic<32>) data_out;
    void _assign() { data_out = _ASSIGN(data_in()); }
};

template<unsigned WIDTH>
struct ImportLink : public Interface {
    _PORT(ImportRequest) request_in;
    _PORT(ImportResponse<WIDTH>) response_out;
};

class ImportSource : public Module {
public:
    _PORT(logic<32>) seed_in;
    _PORT(logic<32>) received_out;
    ImportLink<16> link_out;
    ImportRequest request_comb;
    ImportRequest& request_comb_func() {
        request_comb.meta.kind = ImportKind::Write;
        request_comb.meta.tag = uint8_t(uint32_t(seed_in()) >> 16);
        request_comb.data = uint16_t(seed_in());
        return request_comb;
    }
    void _assign() {
        link_out.request_in = _ASSIGN_COMB(request_comb_func());
        received_out = _ASSIGN(uint32_t(link_out.response_out().data) |
                              (uint32_t(link_out.response_out().tag) << 16));
    }
};

class ImportSink : public Module {
public:
    ImportLink<16> link_in;
    ImportResponse<16> response_comb;
    ImportResponse<16>& response_comb_func() {
        response_comb.data = uint16_t(link_in.request_in().data + uint8_t(link_in.request_in().meta.kind));
        response_comb.tag = link_in.request_in().meta.tag ^ 0x5a;
        return response_comb;
    }
    void _assign() { link_in.response_out = _ASSIGN_COMB(response_comb_func()); }
};

// These proxies expose interfaces and connect across one hierarchy level.
class ImportSourceProxy : public Module {
public:
    ImportSource child;
    ImportLink<16> link_out;
    _PORT(logic<32>) seed_in;
    _PORT(logic<32>) received_out;
    void _assign() {
        child.seed_in = _ASSIGN(seed_in());
        received_out = _ASSIGN(child.received_out());
        assignIf(child, *this, child.link_out, link_out);
    }
};
class ImportSinkProxy : public Module {
public:
    ImportSink child;
    ImportLink<16> link_in;
    void _assign() { assignIf(*this, child, link_in, child.link_in); }
};

// Interface interconnect types are used only indirectly in this module.
class ImportInterfaceParent : public Module {
public:
    ImportSourceProxy source;
    ImportSinkProxy sink;
    _PORT(logic<32>) seed_in;
    _PORT(logic<32>) result_out;
    void _assign() {
        source.seed_in = _ASSIGN(seed_in());
        result_out = _ASSIGN(source.received_out());
        assignIf(source, sink, source.link_out, sink.link_in);
    }
};

class ImportAssignedParent : public Module {
public:
    ImportSource source;
    ImportPlainLeaf<ImportRequest> relay;
    ImportSink sink;
    _PORT(logic<32>) seed_in;
    _PORT(logic<32>) result_out;
    void _assign() {
        source.seed_in = _ASSIGN(seed_in());
        relay.packet_in = _ASSIGN(source.link_out.request_in());
        result_out = _ASSIGN(uint32_t(relay.packet_out().data) |
                             (uint32_t(relay.packet_out().meta.tag) << 16));
        assignIf(source, sink, source.link_out, sink.link_in);
        relay._assign();
    }
};

// A structured interface without child instances also needs its packages.
class ImportInterfaceOnly : public Module {
public:
    ImportLink<8> link_in;
    ImportResponse<8> response_comb;
    void _assign() { link_in.response_out = _ASSIGN_COMB(response_comb_func()); }
    ImportResponse<8>& response_comb_func() {
        response_comb.data = uint8_t(link_in.request_in().data);
        response_comb.tag = link_in.request_in().meta.tag;
        return response_comb;
    }
};

struct ImportArrayWord { uint16_t value; } __PACKED;
struct ImportArrayLink : public Interface {
    _PORT(array<2, ImportArrayWord, true>) words_in;
    _PORT(array<2, ImportArrayWord, true>) words_out;
};
class ImportArrayInterfaceOnly : public Module {
public:
    ImportArrayLink link_in;
    void _assign() { link_in.words_out = _ASSIGN(link_in.words_in()); }
};

class HierarchyImports : public Module {
public:
    ImportUnboundParent unbound;
    ImportInterfaceParent hierarchy;
    ImportInterfaceParent lanes[2];
    ImportAssignedParent assigned;
    _PORT(logic<32>) seed_in;
    _PORT(logic<32>) pass_out;
    _PORT(logic<32>) response_out;
    _PORT(logic<32>) lane_sum_out;
    _PORT(logic<32>) assigned_out;
    void _assign() {
        unbound.data_in = _ASSIGN(seed_in());
        hierarchy.seed_in = _ASSIGN(seed_in());
        assigned.seed_in = _ASSIGN(seed_in());
        lanes[0].seed_in = _ASSIGN(seed_in() + 1);
        lanes[1].seed_in = _ASSIGN(seed_in() + 0x10003);
        pass_out = _ASSIGN(unbound.data_out());
        response_out = _ASSIGN(hierarchy.result_out());
        lane_sum_out = _ASSIGN(lanes[0].result_out() + lanes[1].result_out());
        assigned_out = _ASSIGN(assigned.result_out());
        unbound._assign();
        hierarchy._assign();
        assigned._assign();
        lanes[0]._assign();
        lanes[1]._assign();
    }
};
