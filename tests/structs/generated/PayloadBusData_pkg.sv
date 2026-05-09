package PayloadBusData_pkg;
import PayloadItem_pkg::*;

typedef union packed {
    PayloadItem[2-1:0] values;
    logic[2-1:0][8-1:0] bytes;
} PayloadBusData;


endpackage
