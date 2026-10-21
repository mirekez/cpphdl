`default_nettype none

import Predef_pkg::*;


module PCSSymbolMapper (
    input wire clk
,   input wire reset
,   input wire[6-1:0] point_in
,   input wire[3-1:0] subset_in
,   input wire[4-1:0] condition_in
,   input wire[4-1:0] negate_in
,   output wire[12-1:0] symbols_out
,   output wire invalid_out
);


    // regs and combs
    reg[12-1:0] symbols_reg;
    reg invalid_reg;

    // members

    // tmp variables
    logic[12-1:0] symbols_reg_tmp;
    logic invalid_reg_tmp;


    function logic[31:0] normal (input logic[31:0] index);
        case (index)
        32'h0: begin
            return 32'h492;
        end
        32'h1: begin
            return 32'h692;
        end
        32'h2: begin
            return 32'h6D2;
        end
        32'h3: begin
            return 32'h4D2;
        end
        32'h4: begin
            return 32'h4DA;
        end
        32'h5: begin
            return 32'h6DA;
        end
        32'h6: begin
            return 32'h69A;
        end
        32'h7: begin
            return 32'h49A;
        end
        32'h8: begin
            return 32'h490;
        end
        32'h9: begin
            return 32'h690;
        end
        32'hA: begin
            return 32'h6D0;
        end
        32'hB: begin
            return 32'h4D0;
        end
        32'hC: begin
            return 32'h4D8;
        end
        32'hD: begin
            return 32'h6D8;
        end
        32'hE: begin
            return 32'h698;
        end
        32'hF: begin
            return 32'h498;
        end
        32'h10: begin
            return 32'h482;
        end
        32'h11: begin
            return 32'h682;
        end
        32'h12: begin
            return 32'h6C2;
        end
        32'h13: begin
            return 32'h4C2;
        end
        32'h14: begin
            return 32'h4CA;
        end
        32'h15: begin
            return 32'h6CA;
        end
        32'h16: begin
            return 32'h68A;
        end
        32'h17: begin
            return 32'h48A;
        end
        32'h18: begin
            return 32'h480;
        end
        32'h19: begin
            return 32'h680;
        end
        32'h1A: begin
            return 32'h6C0;
        end
        32'h1B: begin
            return 32'h4C0;
        end
        32'h1C: begin
            return 32'h4C8;
        end
        32'h1D: begin
            return 32'h6C8;
        end
        32'h1E: begin
            return 32'h688;
        end
        32'h1F: begin
            return 32'h488;
        end
        32'h20: begin
            return 32'h412;
        end
        32'h21: begin
            return 32'h612;
        end
        32'h22: begin
            return 32'h652;
        end
        32'h23: begin
            return 32'h452;
        end
        32'h24: begin
            return 32'h45A;
        end
        32'h25: begin
            return 32'h65A;
        end
        32'h26: begin
            return 32'h61A;
        end
        32'h27: begin
            return 32'h41A;
        end
        32'h28: begin
            return 32'h410;
        end
        32'h29: begin
            return 32'h610;
        end
        32'h2A: begin
            return 32'h650;
        end
        32'h2B: begin
            return 32'h450;
        end
        32'h2C: begin
            return 32'h458;
        end
        32'h2D: begin
            return 32'h658;
        end
        32'h2E: begin
            return 32'h618;
        end
        32'h2F: begin
            return 32'h418;
        end
        32'h30: begin
            return 32'h402;
        end
        32'h31: begin
            return 32'h602;
        end
        32'h32: begin
            return 32'h642;
        end
        32'h33: begin
            return 32'h442;
        end
        32'h34: begin
            return 32'h44A;
        end
        32'h35: begin
            return 32'h64A;
        end
        32'h36: begin
            return 32'h60A;
        end
        32'h37: begin
            return 32'h40A;
        end
        32'h38: begin
            return 32'h400;
        end
        32'h39: begin
            return 32'h600;
        end
        32'h3A: begin
            return 32'h640;
        end
        32'h3B: begin
            return 32'h440;
        end
        32'h3C: begin
            return 32'h448;
        end
        32'h3D: begin
            return 32'h648;
        end
        32'h3E: begin
            return 32'h608;
        end
        32'h3F: begin
            return 32'h408;
        end
        32'h40: begin
            return 32'h92;
        end
        32'h41: begin
            return 32'h292;
        end
        32'h42: begin
            return 32'h2D2;
        end
        32'h43: begin
            return 32'hD2;
        end
        32'h44: begin
            return 32'hDA;
        end
        32'h45: begin
            return 32'h2DA;
        end
        32'h46: begin
            return 32'h29A;
        end
        32'h47: begin
            return 32'h9A;
        end
        32'h48: begin
            return 32'h90;
        end
        32'h49: begin
            return 32'h290;
        end
        32'h4A: begin
            return 32'h2D0;
        end
        32'h4B: begin
            return 32'hD0;
        end
        32'h4C: begin
            return 32'hD8;
        end
        32'h4D: begin
            return 32'h2D8;
        end
        32'h4E: begin
            return 32'h298;
        end
        32'h4F: begin
            return 32'h98;
        end
        32'h50: begin
            return 32'h82;
        end
        32'h51: begin
            return 32'h282;
        end
        32'h52: begin
            return 32'h2C2;
        end
        32'h53: begin
            return 32'hC2;
        end
        32'h54: begin
            return 32'hCA;
        end
        32'h55: begin
            return 32'h2CA;
        end
        32'h56: begin
            return 32'h28A;
        end
        32'h57: begin
            return 32'h8A;
        end
        32'h58: begin
            return 32'h80;
        end
        32'h59: begin
            return 32'h280;
        end
        32'h5A: begin
            return 32'h2C0;
        end
        32'h5B: begin
            return 32'hC0;
        end
        32'h5C: begin
            return 32'hC8;
        end
        32'h5D: begin
            return 32'h2C8;
        end
        32'h5E: begin
            return 32'h288;
        end
        32'h5F: begin
            return 32'h88;
        end
        32'h60: begin
            return 32'h12;
        end
        32'h61: begin
            return 32'h212;
        end
        32'h62: begin
            return 32'h252;
        end
        32'h63: begin
            return 32'h52;
        end
        32'h64: begin
            return 32'h5A;
        end
        32'h65: begin
            return 32'h25A;
        end
        32'h66: begin
            return 32'h21A;
        end
        32'h67: begin
            return 32'h1A;
        end
        32'h68: begin
            return 32'h10;
        end
        32'h69: begin
            return 32'h210;
        end
        32'h6A: begin
            return 32'h250;
        end
        32'h6B: begin
            return 32'h50;
        end
        32'h6C: begin
            return 32'h58;
        end
        32'h6D: begin
            return 32'h258;
        end
        32'h6E: begin
            return 32'h218;
        end
        32'h6F: begin
            return 32'h18;
        end
        32'h70: begin
            return 32'h2;
        end
        32'h71: begin
            return 32'h202;
        end
        32'h72: begin
            return 32'h242;
        end
        32'h73: begin
            return 32'h42;
        end
        32'h74: begin
            return 32'h4A;
        end
        32'h75: begin
            return 32'h24A;
        end
        32'h76: begin
            return 32'h20A;
        end
        32'h77: begin
            return 32'hA;
        end
        32'h78: begin
            return 32'h0;
        end
        32'h79: begin
            return 32'h200;
        end
        32'h7A: begin
            return 32'h240;
        end
        32'h7B: begin
            return 32'h40;
        end
        32'h7C: begin
            return 32'h48;
        end
        32'h7D: begin
            return 32'h248;
        end
        32'h7E: begin
            return 32'h208;
        end
        32'h7F: begin
            return 32'h8;
        end
        32'h80: begin
            return 32'h6DB;
        end
        32'h81: begin
            return 32'h4DB;
        end
        32'h82: begin
            return 32'h49B;
        end
        32'h83: begin
            return 32'h69B;
        end
        32'h84: begin
            return 32'h693;
        end
        32'h85: begin
            return 32'h493;
        end
        32'h86: begin
            return 32'h4D3;
        end
        32'h87: begin
            return 32'h6D3;
        end
        32'h88: begin
            return 32'h6D9;
        end
        32'h89: begin
            return 32'h4D9;
        end
        32'h8A: begin
            return 32'h499;
        end
        32'h8B: begin
            return 32'h699;
        end
        32'h8C: begin
            return 32'h691;
        end
        32'h8D: begin
            return 32'h491;
        end
        32'h8E: begin
            return 32'h4D1;
        end
        32'h8F: begin
            return 32'h6D1;
        end
        32'h90: begin
            return 32'h6CB;
        end
        32'h91: begin
            return 32'h4CB;
        end
        32'h92: begin
            return 32'h48B;
        end
        32'h93: begin
            return 32'h68B;
        end
        32'h94: begin
            return 32'h683;
        end
        32'h95: begin
            return 32'h483;
        end
        32'h96: begin
            return 32'h4C3;
        end
        32'h97: begin
            return 32'h6C3;
        end
        32'h98: begin
            return 32'h6C9;
        end
        32'h99: begin
            return 32'h4C9;
        end
        32'h9A: begin
            return 32'h489;
        end
        32'h9B: begin
            return 32'h689;
        end
        32'h9C: begin
            return 32'h681;
        end
        32'h9D: begin
            return 32'h481;
        end
        32'h9E: begin
            return 32'h4C1;
        end
        32'h9F: begin
            return 32'h6C1;
        end
        32'hA0: begin
            return 32'h65B;
        end
        32'hA1: begin
            return 32'h45B;
        end
        32'hA2: begin
            return 32'h41B;
        end
        32'hA3: begin
            return 32'h61B;
        end
        32'hA4: begin
            return 32'h613;
        end
        32'hA5: begin
            return 32'h413;
        end
        32'hA6: begin
            return 32'h453;
        end
        32'hA7: begin
            return 32'h653;
        end
        32'hA8: begin
            return 32'h659;
        end
        32'hA9: begin
            return 32'h459;
        end
        32'hAA: begin
            return 32'h419;
        end
        32'hAB: begin
            return 32'h619;
        end
        32'hAC: begin
            return 32'h611;
        end
        32'hAD: begin
            return 32'h411;
        end
        32'hAE: begin
            return 32'h451;
        end
        32'hAF: begin
            return 32'h651;
        end
        32'hB0: begin
            return 32'h64B;
        end
        32'hB1: begin
            return 32'h44B;
        end
        32'hB2: begin
            return 32'h40B;
        end
        32'hB3: begin
            return 32'h60B;
        end
        32'hB4: begin
            return 32'h603;
        end
        32'hB5: begin
            return 32'h403;
        end
        32'hB6: begin
            return 32'h443;
        end
        32'hB7: begin
            return 32'h643;
        end
        32'hB8: begin
            return 32'h649;
        end
        32'hB9: begin
            return 32'h449;
        end
        32'hBA: begin
            return 32'h409;
        end
        32'hBB: begin
            return 32'h609;
        end
        32'hBC: begin
            return 32'h601;
        end
        32'hBD: begin
            return 32'h401;
        end
        32'hBE: begin
            return 32'h441;
        end
        32'hBF: begin
            return 32'h641;
        end
        32'hC0: begin
            return 32'h2DB;
        end
        32'hC1: begin
            return 32'hDB;
        end
        32'hC2: begin
            return 32'h9B;
        end
        32'hC3: begin
            return 32'h29B;
        end
        32'hC4: begin
            return 32'h293;
        end
        32'hC5: begin
            return 32'h93;
        end
        32'hC6: begin
            return 32'hD3;
        end
        32'hC7: begin
            return 32'h2D3;
        end
        32'hC8: begin
            return 32'h2D9;
        end
        32'hC9: begin
            return 32'hD9;
        end
        32'hCA: begin
            return 32'h99;
        end
        32'hCB: begin
            return 32'h299;
        end
        32'hCC: begin
            return 32'h291;
        end
        32'hCD: begin
            return 32'h91;
        end
        32'hCE: begin
            return 32'hD1;
        end
        32'hCF: begin
            return 32'h2D1;
        end
        32'hD0: begin
            return 32'h2CB;
        end
        32'hD1: begin
            return 32'hCB;
        end
        32'hD2: begin
            return 32'h8B;
        end
        32'hD3: begin
            return 32'h28B;
        end
        32'hD4: begin
            return 32'h283;
        end
        32'hD5: begin
            return 32'h83;
        end
        32'hD6: begin
            return 32'hC3;
        end
        32'hD7: begin
            return 32'h2C3;
        end
        32'hD8: begin
            return 32'h2C9;
        end
        32'hD9: begin
            return 32'hC9;
        end
        32'hDA: begin
            return 32'h89;
        end
        32'hDB: begin
            return 32'h289;
        end
        32'hDC: begin
            return 32'h281;
        end
        32'hDD: begin
            return 32'h81;
        end
        32'hDE: begin
            return 32'hC1;
        end
        32'hDF: begin
            return 32'h2C1;
        end
        32'hE0: begin
            return 32'h25B;
        end
        32'hE1: begin
            return 32'h5B;
        end
        32'hE2: begin
            return 32'h1B;
        end
        32'hE3: begin
            return 32'h21B;
        end
        32'hE4: begin
            return 32'h213;
        end
        32'hE5: begin
            return 32'h13;
        end
        32'hE6: begin
            return 32'h53;
        end
        32'hE7: begin
            return 32'h253;
        end
        32'hE8: begin
            return 32'h259;
        end
        32'hE9: begin
            return 32'h59;
        end
        32'hEA: begin
            return 32'h19;
        end
        32'hEB: begin
            return 32'h219;
        end
        32'hEC: begin
            return 32'h211;
        end
        32'hED: begin
            return 32'h11;
        end
        32'hEE: begin
            return 32'h51;
        end
        32'hEF: begin
            return 32'h251;
        end
        32'hF0: begin
            return 32'h24B;
        end
        32'hF1: begin
            return 32'h4B;
        end
        32'hF2: begin
            return 32'hB;
        end
        32'hF3: begin
            return 32'h20B;
        end
        32'hF4: begin
            return 32'h203;
        end
        32'hF5: begin
            return 32'h3;
        end
        32'hF6: begin
            return 32'h43;
        end
        32'hF7: begin
            return 32'h243;
        end
        32'hF8: begin
            return 32'h249;
        end
        32'hF9: begin
            return 32'h49;
        end
        32'hFA: begin
            return 32'h9;
        end
        32'hFB: begin
            return 32'h209;
        end
        32'hFC: begin
            return 32'h201;
        end
        32'hFD: begin
            return 32'h1;
        end
        32'hFE: begin
            return 32'h41;
        end
        32'hFF: begin
            return 32'h241;
        end
        32'h100: begin
            return 32'h494;
        end
        32'h101: begin
            return 32'h694;
        end
        32'h102: begin
            return 32'h6D4;
        end
        32'h103: begin
            return 32'h4D4;
        end
        32'h104: begin
            return 32'h4DC;
        end
        32'h105: begin
            return 32'h6DC;
        end
        32'h106: begin
            return 32'h69C;
        end
        32'h107: begin
            return 32'h49C;
        end
        32'h108: begin
            return 32'h484;
        end
        32'h109: begin
            return 32'h684;
        end
        32'h10A: begin
            return 32'h6C4;
        end
        32'h10B: begin
            return 32'h4C4;
        end
        32'h10C: begin
            return 32'h4CC;
        end
        32'h10D: begin
            return 32'h6CC;
        end
        32'h10E: begin
            return 32'h68C;
        end
        32'h10F: begin
            return 32'h48C;
        end
        32'h110: begin
            return 32'h414;
        end
        32'h111: begin
            return 32'h614;
        end
        32'h112: begin
            return 32'h654;
        end
        32'h113: begin
            return 32'h454;
        end
        32'h114: begin
            return 32'h45C;
        end
        32'h115: begin
            return 32'h65C;
        end
        32'h116: begin
            return 32'h61C;
        end
        32'h117: begin
            return 32'h41C;
        end
        32'h118: begin
            return 32'h404;
        end
        32'h119: begin
            return 32'h604;
        end
        32'h11A: begin
            return 32'h644;
        end
        32'h11B: begin
            return 32'h444;
        end
        32'h11C: begin
            return 32'h44C;
        end
        32'h11D: begin
            return 32'h64C;
        end
        32'h11E: begin
            return 32'h60C;
        end
        32'h11F: begin
            return 32'h40C;
        end
        32'h120: begin
            return 32'h94;
        end
        32'h121: begin
            return 32'h294;
        end
        32'h122: begin
            return 32'h2D4;
        end
        32'h123: begin
            return 32'hD4;
        end
        32'h124: begin
            return 32'hDC;
        end
        32'h125: begin
            return 32'h2DC;
        end
        32'h126: begin
            return 32'h29C;
        end
        32'h127: begin
            return 32'h9C;
        end
        32'h128: begin
            return 32'h84;
        end
        32'h129: begin
            return 32'h284;
        end
        32'h12A: begin
            return 32'h2C4;
        end
        32'h12B: begin
            return 32'hC4;
        end
        32'h12C: begin
            return 32'hCC;
        end
        32'h12D: begin
            return 32'h2CC;
        end
        32'h12E: begin
            return 32'h28C;
        end
        32'h12F: begin
            return 32'h8C;
        end
        32'h130: begin
            return 32'h14;
        end
        32'h131: begin
            return 32'h214;
        end
        32'h132: begin
            return 32'h254;
        end
        32'h133: begin
            return 32'h54;
        end
        32'h134: begin
            return 32'h5C;
        end
        32'h135: begin
            return 32'h25C;
        end
        32'h136: begin
            return 32'h21C;
        end
        32'h137: begin
            return 32'h1C;
        end
        32'h138: begin
            return 32'h4;
        end
        32'h139: begin
            return 32'h204;
        end
        32'h13A: begin
            return 32'h244;
        end
        32'h13B: begin
            return 32'h44;
        end
        32'h13C: begin
            return 32'h4C;
        end
        32'h13D: begin
            return 32'h24C;
        end
        32'h13E: begin
            return 32'h20C;
        end
        32'h13F: begin
            return 32'hC;
        end
        32'h140: begin
            return 32'h512;
        end
        32'h141: begin
            return 32'h712;
        end
        32'h142: begin
            return 32'h51B;
        end
        32'h143: begin
            return 32'h71B;
        end
        32'h144: begin
            return 32'h713;
        end
        32'h145: begin
            return 32'h513;
        end
        32'h146: begin
            return 32'h71A;
        end
        32'h147: begin
            return 32'h51A;
        end
        32'h148: begin
            return 32'h510;
        end
        32'h149: begin
            return 32'h710;
        end
        32'h14A: begin
            return 32'h519;
        end
        32'h14B: begin
            return 32'h719;
        end
        32'h14C: begin
            return 32'h711;
        end
        32'h14D: begin
            return 32'h511;
        end
        32'h14E: begin
            return 32'h718;
        end
        32'h14F: begin
            return 32'h518;
        end
        32'h150: begin
            return 32'h502;
        end
        32'h151: begin
            return 32'h702;
        end
        32'h152: begin
            return 32'h50B;
        end
        32'h153: begin
            return 32'h70B;
        end
        32'h154: begin
            return 32'h703;
        end
        32'h155: begin
            return 32'h503;
        end
        32'h156: begin
            return 32'h70A;
        end
        32'h157: begin
            return 32'h50A;
        end
        32'h158: begin
            return 32'h500;
        end
        32'h159: begin
            return 32'h700;
        end
        32'h15A: begin
            return 32'h509;
        end
        32'h15B: begin
            return 32'h709;
        end
        32'h15C: begin
            return 32'h701;
        end
        32'h15D: begin
            return 32'h501;
        end
        32'h15E: begin
            return 32'h708;
        end
        32'h15F: begin
            return 32'h508;
        end
        32'h160: begin
            return 32'h112;
        end
        32'h161: begin
            return 32'h312;
        end
        32'h162: begin
            return 32'h11B;
        end
        32'h163: begin
            return 32'h31B;
        end
        32'h164: begin
            return 32'h313;
        end
        32'h165: begin
            return 32'h113;
        end
        32'h166: begin
            return 32'h31A;
        end
        32'h167: begin
            return 32'h11A;
        end
        32'h168: begin
            return 32'h110;
        end
        32'h169: begin
            return 32'h310;
        end
        32'h16A: begin
            return 32'h119;
        end
        32'h16B: begin
            return 32'h319;
        end
        32'h16C: begin
            return 32'h311;
        end
        32'h16D: begin
            return 32'h111;
        end
        32'h16E: begin
            return 32'h318;
        end
        32'h16F: begin
            return 32'h118;
        end
        32'h170: begin
            return 32'h102;
        end
        32'h171: begin
            return 32'h302;
        end
        32'h172: begin
            return 32'h10B;
        end
        32'h173: begin
            return 32'h30B;
        end
        32'h174: begin
            return 32'h303;
        end
        32'h175: begin
            return 32'h103;
        end
        32'h176: begin
            return 32'h30A;
        end
        32'h177: begin
            return 32'h10A;
        end
        32'h178: begin
            return 32'h100;
        end
        32'h179: begin
            return 32'h300;
        end
        32'h17A: begin
            return 32'h109;
        end
        32'h17B: begin
            return 32'h309;
        end
        32'h17C: begin
            return 32'h301;
        end
        32'h17D: begin
            return 32'h101;
        end
        32'h17E: begin
            return 32'h308;
        end
        32'h17F: begin
            return 32'h108;
        end
        32'h180: begin
            return 32'h4A2;
        end
        32'h181: begin
            return 32'h6A2;
        end
        32'h182: begin
            return 32'h6E2;
        end
        32'h183: begin
            return 32'h4E2;
        end
        32'h184: begin
            return 32'h6A3;
        end
        32'h185: begin
            return 32'h4A3;
        end
        32'h186: begin
            return 32'h4E3;
        end
        32'h187: begin
            return 32'h6E3;
        end
        32'h188: begin
            return 32'h4A0;
        end
        32'h189: begin
            return 32'h6A0;
        end
        32'h18A: begin
            return 32'h6E0;
        end
        32'h18B: begin
            return 32'h4E0;
        end
        32'h18C: begin
            return 32'h6A1;
        end
        32'h18D: begin
            return 32'h4A1;
        end
        32'h18E: begin
            return 32'h4E1;
        end
        32'h18F: begin
            return 32'h6E1;
        end
        32'h190: begin
            return 32'h422;
        end
        32'h191: begin
            return 32'h622;
        end
        32'h192: begin
            return 32'h662;
        end
        32'h193: begin
            return 32'h462;
        end
        32'h194: begin
            return 32'h623;
        end
        32'h195: begin
            return 32'h423;
        end
        32'h196: begin
            return 32'h463;
        end
        32'h197: begin
            return 32'h663;
        end
        32'h198: begin
            return 32'h420;
        end
        32'h199: begin
            return 32'h620;
        end
        32'h19A: begin
            return 32'h660;
        end
        32'h19B: begin
            return 32'h460;
        end
        32'h19C: begin
            return 32'h621;
        end
        32'h19D: begin
            return 32'h421;
        end
        32'h19E: begin
            return 32'h461;
        end
        32'h19F: begin
            return 32'h661;
        end
        32'h1A0: begin
            return 32'hA2;
        end
        32'h1A1: begin
            return 32'h2A2;
        end
        32'h1A2: begin
            return 32'h2E2;
        end
        32'h1A3: begin
            return 32'hE2;
        end
        32'h1A4: begin
            return 32'h2A3;
        end
        32'h1A5: begin
            return 32'hA3;
        end
        32'h1A6: begin
            return 32'hE3;
        end
        32'h1A7: begin
            return 32'h2E3;
        end
        32'h1A8: begin
            return 32'hA0;
        end
        32'h1A9: begin
            return 32'h2A0;
        end
        32'h1AA: begin
            return 32'h2E0;
        end
        32'h1AB: begin
            return 32'hE0;
        end
        32'h1AC: begin
            return 32'h2A1;
        end
        32'h1AD: begin
            return 32'hA1;
        end
        32'h1AE: begin
            return 32'hE1;
        end
        32'h1AF: begin
            return 32'h2E1;
        end
        32'h1B0: begin
            return 32'h22;
        end
        32'h1B1: begin
            return 32'h222;
        end
        32'h1B2: begin
            return 32'h262;
        end
        32'h1B3: begin
            return 32'h62;
        end
        32'h1B4: begin
            return 32'h223;
        end
        32'h1B5: begin
            return 32'h23;
        end
        32'h1B6: begin
            return 32'h63;
        end
        32'h1B7: begin
            return 32'h263;
        end
        32'h1B8: begin
            return 32'h20;
        end
        32'h1B9: begin
            return 32'h220;
        end
        32'h1BA: begin
            return 32'h260;
        end
        32'h1BB: begin
            return 32'h60;
        end
        32'h1BC: begin
            return 32'h221;
        end
        32'h1BD: begin
            return 32'h21;
        end
        32'h1BE: begin
            return 32'h61;
        end
        32'h1BF: begin
            return 32'h261;
        end
        32'h1C0: begin
            return 32'h892;
        end
        32'h1C1: begin
            return 32'h8DB;
        end
        32'h1C2: begin
            return 32'h89B;
        end
        32'h1C3: begin
            return 32'h8D2;
        end
        32'h1C4: begin
            return 32'h8DA;
        end
        32'h1C5: begin
            return 32'h893;
        end
        32'h1C6: begin
            return 32'h8D3;
        end
        32'h1C7: begin
            return 32'h89A;
        end
        32'h1C8: begin
            return 32'h890;
        end
        32'h1C9: begin
            return 32'h8D9;
        end
        32'h1CA: begin
            return 32'h899;
        end
        32'h1CB: begin
            return 32'h8D0;
        end
        32'h1CC: begin
            return 32'h8D8;
        end
        32'h1CD: begin
            return 32'h891;
        end
        32'h1CE: begin
            return 32'h8D1;
        end
        32'h1CF: begin
            return 32'h898;
        end
        32'h1D0: begin
            return 32'h882;
        end
        32'h1D1: begin
            return 32'h8CB;
        end
        32'h1D2: begin
            return 32'h88B;
        end
        32'h1D3: begin
            return 32'h8C2;
        end
        32'h1D4: begin
            return 32'h8CA;
        end
        32'h1D5: begin
            return 32'h883;
        end
        32'h1D6: begin
            return 32'h8C3;
        end
        32'h1D7: begin
            return 32'h88A;
        end
        32'h1D8: begin
            return 32'h880;
        end
        32'h1D9: begin
            return 32'h8C9;
        end
        32'h1DA: begin
            return 32'h889;
        end
        32'h1DB: begin
            return 32'h8C0;
        end
        32'h1DC: begin
            return 32'h8C8;
        end
        32'h1DD: begin
            return 32'h881;
        end
        32'h1DE: begin
            return 32'h8C1;
        end
        32'h1DF: begin
            return 32'h888;
        end
        32'h1E0: begin
            return 32'h812;
        end
        32'h1E1: begin
            return 32'h85B;
        end
        32'h1E2: begin
            return 32'h81B;
        end
        32'h1E3: begin
            return 32'h852;
        end
        32'h1E4: begin
            return 32'h85A;
        end
        32'h1E5: begin
            return 32'h813;
        end
        32'h1E6: begin
            return 32'h853;
        end
        32'h1E7: begin
            return 32'h81A;
        end
        32'h1E8: begin
            return 32'h810;
        end
        32'h1E9: begin
            return 32'h859;
        end
        32'h1EA: begin
            return 32'h819;
        end
        32'h1EB: begin
            return 32'h850;
        end
        32'h1EC: begin
            return 32'h858;
        end
        32'h1ED: begin
            return 32'h811;
        end
        32'h1EE: begin
            return 32'h851;
        end
        32'h1EF: begin
            return 32'h818;
        end
        32'h1F0: begin
            return 32'h802;
        end
        32'h1F1: begin
            return 32'h84B;
        end
        32'h1F2: begin
            return 32'h80B;
        end
        32'h1F3: begin
            return 32'h842;
        end
        32'h1F4: begin
            return 32'h84A;
        end
        32'h1F5: begin
            return 32'h803;
        end
        32'h1F6: begin
            return 32'h843;
        end
        32'h1F7: begin
            return 32'h80A;
        end
        32'h1F8: begin
            return 32'h800;
        end
        32'h1F9: begin
            return 32'h849;
        end
        32'h1FA: begin
            return 32'h809;
        end
        32'h1FB: begin
            return 32'h840;
        end
        32'h1FC: begin
            return 32'h848;
        end
        32'h1FD: begin
            return 32'h801;
        end
        32'h1FE: begin
            return 32'h841;
        end
        32'h1FF: begin
            return 32'h808;
        end
        default: begin
            return 32'h0;
        end
        endcase
    endfunction

    function logic[31:0] special (
        input logic[31:0] condition
,       input logic[31:0] subset
    );
        case ((condition*32'h8) + subset)
        32'h8: begin
            return 32'h522;
        end
        32'hA: begin
            return 32'h91B;
        end
        32'hC: begin
            return 32'h8DC;
        end
        32'hE: begin
            return 32'h71C;
        end
        32'h9: begin
            return 32'h6A4;
        end
        32'hB: begin
            return 32'h8E2;
        end
        32'hD: begin
            return 32'h523;
        end
        32'hF: begin
            return 32'h51C;
        end
        32'h10: begin
            return 32'h120;
        end
        32'h12: begin
            return 32'h909;
        end
        32'h14: begin
            return 32'h84C;
        end
        32'h16: begin
            return 32'h30C;
        end
        32'h11: begin
            return 32'h224;
        end
        32'h13: begin
            return 32'h860;
        end
        32'h15: begin
            return 32'h121;
        end
        32'h17: begin
            return 32'h10C;
        end
        32'h18: begin
            return 32'h894;
        end
        32'h1A: begin
            return 32'h6E4;
        end
        32'h1C: begin
            return 32'h723;
        end
        32'h1E: begin
            return 32'h8E3;
        end
        32'h19: begin
            return 32'h714;
        end
        32'h1B: begin
            return 32'h8D4;
        end
        32'h1D: begin
            return 32'h913;
        end
        32'h1F: begin
            return 32'h89C;
        end
        32'h20: begin
            return 32'h804;
        end
        32'h22: begin
            return 32'h264;
        end
        32'h24: begin
            return 32'h321;
        end
        32'h26: begin
            return 32'h861;
        end
        32'h21: begin
            return 32'h304;
        end
        32'h23: begin
            return 32'h844;
        end
        32'h25: begin
            return 32'h901;
        end
        32'h27: begin
            return 32'h80C;
        end
        32'h28: begin
            return 32'h924;
        end
        32'h30: begin
            return 32'h124;
        end
        32'h38: begin
            return 32'h924;
        end
        32'h40: begin
            return 32'h124;
        end
        32'h48: begin
            return 32'h824;
        end
        32'h50: begin
            return 32'h904;
        end
        32'h58: begin
            return 32'h920;
        end
        default: begin
            return 32'hFFF;
        end
        endcase
    endfunction

    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic invalid;
        logic[31:0] mapped;
        logic[31:0] result;
        logic[31:0] level;
        mapped=32'hFFF;
        if (condition_in == 64'h0) begin
            mapped=normal((unsigned'(32'(point_in))*32'h8) + unsigned'(32'(subset_in)));
        end
        else begin
            if (((condition_in == 64'hC) && (subset_in == 64'h0)) && (point_in < 64'h10)) begin
                mapped=normal(unsigned'(32'(point_in))*32'h8);
            end
            else begin
                if (condition_in == 64'hD) begin
                    mapped=32'h492;
                end
                else begin
                    mapped=special(unsigned'(32'(condition_in)), unsigned'(32'(subset_in)));
                end
            end
        end
        invalid=mapped == 32'hFFF;
        result=32'h0;
        for (logic[31:0] lane = 32'h0;lane < 32'h4;lane=lane+1) begin
            level=((mapped >>> ((32'h3*lane)))) & 32'h7;
            if (((((negate_in >>> lane)) & 64'h1) != '0)) begin
                level=32'h4 - level;
            end
            result=result | ((((level & 32'h7)) <<< ((32'h3*lane))));
        end
        symbols_reg_tmp = unsigned'(32'((invalid) ? (32'h492) : (result)));
        invalid_reg_tmp = invalid;
        if (reset) begin
            symbols_reg_tmp = 64'h492;
            invalid_reg_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        symbols_reg_tmp = symbols_reg;
        invalid_reg_tmp = invalid_reg;

            _work(reset);

        symbols_reg <= symbols_reg_tmp;
        invalid_reg <= invalid_reg_tmp;
    end

    assign symbols_out = symbols_reg;

    assign invalid_out = invalid_reg;


endmodule
