package ArrayPayload_pkg;
import PayloadBusData_pkg::*;
import PayloadItem_pkg::*;
import PayloadChoice_pkg::*;

typedef struct packed {
    logic[3-1:0] _align0;
    logic[5-1:0] tail;
    PayloadBusData bus_data;
    PayloadChoice[2-1:0] choices;
    logic[1-1:0][16-1:0] halfs;
    logic[5-1:0] _align2;
    logic[3-1:0] mid;
    PayloadItem[4-1:0][5-1:0][3-1:0][2-1:0] item_grid;
    PayloadItem[4-1:0][3-1:0][2-1:0] item_tables;
    PayloadItem[3-1:0][2-1:0] item_rows;
    PayloadItem[3-1:0][2-1:0] item_matrix;
    PayloadItem[2-1:0] items;
    logic[4-1:0][5-1:0][3-1:0][2-1:0][8-1:0] byte_grid;
    logic[4-1:0][3-1:0][2-1:0][8-1:0] byte_tables;
    logic[3-1:0][2-1:0][8-1:0] byte_rows;
    logic[3-1:0][2-1:0][8-1:0] byte_matrix;
    logic[3-1:0][8-1:0] bytes;
    logic[4-1:0] _align1;
    logic[4-1:0] prefix;
} ArrayPayload;


endpackage
