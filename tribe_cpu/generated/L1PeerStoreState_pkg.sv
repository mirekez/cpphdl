package L1PeerStoreState_pkg;

typedef struct packed {
    logic[32-1:0] addr;
    logic[7-1:0] _align1;
    logic valid;
} L1PeerStoreState;


endpackage
