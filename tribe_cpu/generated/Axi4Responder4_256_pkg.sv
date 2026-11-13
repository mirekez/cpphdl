package Axi4Responder4_256_pkg;
import Axi4WriteAddressReady_pkg::*;
import Axi4WriteDataReady_pkg::*;
import Axi4WriteResponse4_pkg::*;
import Axi4ReadAddressReady_pkg::*;
import Axi4ReadData4_256_pkg::*;

typedef struct packed {
    Axi4ReadData4_256 r;
    Axi4ReadAddressReady ar;
    Axi4WriteResponse4 b;
    Axi4WriteDataReady w;
    Axi4WriteAddressReady aw;
} Axi4Responder4_256;


endpackage
