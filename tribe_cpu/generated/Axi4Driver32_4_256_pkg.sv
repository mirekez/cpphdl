package Axi4Driver32_4_256_pkg;
import Axi4WriteAddress32_4_pkg::*;
import Axi4WriteData256_pkg::*;
import Axi4WriteResponseReady_pkg::*;
import Axi4ReadAddress32_4_pkg::*;
import Axi4ReadDataReady_pkg::*;

typedef struct packed {
    Axi4ReadDataReady r;
    Axi4ReadAddress32_4 ar;
    Axi4WriteResponseReady b;
    Axi4WriteData256 w;
    Axi4WriteAddress32_4 aw;
} Axi4Driver32_4_256;


endpackage
