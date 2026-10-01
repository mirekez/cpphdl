package native_packed_layout;
    typedef struct packed { logic [2:0] tag; logic [64:0] data; } First;
    typedef struct packed { logic [2:0] tag; logic [64:0] data; } Second;
    typedef struct packed { logic [64:0] data; logic [2:0] tag; } Reordered;
    typedef struct packed { First entry; logic [4:0] lane; } OuterFirst;
    typedef struct packed { Second entry; logic [4:0] lane; } OuterSecond;
    typedef union packed { logic [67:0] raw; First entry; } Overlay;
    typedef struct packed { int unsigned word; byte unsigned lane; logic flag; } Words;
endpackage
