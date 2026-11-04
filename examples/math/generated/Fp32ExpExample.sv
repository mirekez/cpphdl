`default_nettype none

import Predef_pkg::*;
import cpphdl_examples_math_Fp32ExpMethods_pkg::*;


module Fp32ExpExample (
    input wire clk
,   input wire reset
,   input wire[31:0] data_in
,   output wire[31:0] result_out
);


    // regs and combs
    logic[31:0] result_comb;

    // members

    // tmp variables


    function logic[31:0] cpphdl_examples_math_Fp32ExpMethods___exp2_table (input logic[31:0] index);
        if (index < 'h80) begin
            if (index < 'h40) begin
                if (index < 'h20) begin
                    if (index < 'h10) begin
                        if (index < 'h8) begin
                            if (index < 'h4) begin
                                if (index < 'h2) begin
                                    if (index < 'h1) begin
                                        return 'h40000000;
                                    end
                                    else begin
                                        return 'h402C6BE9;
                                    end
                                end
                                else begin
                                    if (index < 'h3) begin
                                        return 'h4058F6A8;
                                    end
                                    else begin
                                        return 'h4085A051;
                                    end
                                end
                            end
                            else begin
                                if (index < 'h6) begin
                                    if (index < 'h5) begin
                                        return 'h40B268FA;
                                    end
                                    else begin
                                        return 'h40DF50B8;
                                    end
                                end
                                else begin
                                    if (index < 'h7) begin
                                        return 'h410C57A2;
                                    end
                                    else begin
                                        return 'h41397DCC;
                                    end
                                end
                            end
                        end
                        else begin
                            if (index < 'hC) begin
                                if (index < 'hA) begin
                                    if (index < 'h9) begin
                                        return 'h4166C34C;
                                    end
                                    else begin
                                        return 'h41942839;
                                    end
                                end
                                else begin
                                    if (index < 'hB) begin
                                        return 'h41C1ACA7;
                                    end
                                    else begin
                                        return 'h41EF50AE;
                                    end
                                end
                            end
                            else begin
                                if (index < 'hE) begin
                                    if (index < 'hD) begin
                                        return 'h421D1462;
                                    end
                                    else begin
                                        return 'h424AF7DA;
                                    end
                                end
                                else begin
                                    if (index < 'hF) begin
                                        return 'h4278FB2B;
                                    end
                                    else begin
                                        return 'h42A71E6C;
                                    end
                                end
                            end
                        end
                    end
                    else begin
                        if (index < 'h18) begin
                            if (index < 'h14) begin
                                if (index < 'h12) begin
                                    if (index < 'h11) begin
                                        return 'h42D561B4;
                                    end
                                    else begin
                                        return 'h4303C518;
                                    end
                                end
                                else begin
                                    if (index < 'h13) begin
                                        return 'h433248AE;
                                    end
                                    else begin
                                        return 'h4360EC8D;
                                    end
                                end
                            end
                            else begin
                                if (index < 'h16) begin
                                    if (index < 'h15) begin
                                        return 'h438FB0CB;
                                    end
                                    else begin
                                        return 'h43BE957F;
                                    end
                                end
                                else begin
                                    if (index < 'h17) begin
                                        return 'h43ED9AC0;
                                    end
                                    else begin
                                        return 'h441CC0A3;
                                    end
                                end
                            end
                        end
                        else begin
                            if (index < 'h1C) begin
                                if (index < 'h1A) begin
                                    if (index < 'h19) begin
                                        return 'h444C0740;
                                    end
                                    else begin
                                        return 'h447B6EAD;
                                    end
                                end
                                else begin
                                    if (index < 'h1B) begin
                                        return 'h44AAF702;
                                    end
                                    else begin
                                        return 'h44DAA054;
                                    end
                                end
                            end
                            else begin
                                if (index < 'h1E) begin
                                    if (index < 'h1D) begin
                                        return 'h450A6ABB;
                                    end
                                    else begin
                                        return 'h453A564D;
                                    end
                                end
                                else begin
                                    if (index < 'h1F) begin
                                        return 'h456A6323;
                                    end
                                    else begin
                                        return 'h459A9152;
                                    end
                                end
                            end
                        end
                    end
                end
                else begin
                    if (index < 'h30) begin
                        if (index < 'h28) begin
                            if (index < 'h24) begin
                                if (index < 'h22) begin
                                    if (index < 'h21) begin
                                        return 'h45CAE0F2;
                                    end
                                    else begin
                                        return 'h45FB521A;
                                    end
                                end
                                else begin
                                    if (index < 'h23) begin
                                        return 'h462BE4E2;
                                    end
                                    else begin
                                        return 'h465C9961;
                                    end
                                end
                            end
                            else begin
                                if (index < 'h26) begin
                                    if (index < 'h25) begin
                                        return 'h468D6FAE;
                                    end
                                    else begin
                                        return 'h46BE67E0;
                                    end
                                end
                                else begin
                                    if (index < 'h27) begin
                                        return 'h46EF8210;
                                    end
                                    else begin
                                        return 'h4720BE55;
                                    end
                                end
                            end
                        end
                        else begin
                            if (index < 'h2C) begin
                                if (index < 'h2A) begin
                                    if (index < 'h29) begin
                                        return 'h47521CC6;
                                    end
                                    else begin
                                        return 'h47839D7B;
                                    end
                                end
                                else begin
                                    if (index < 'h2B) begin
                                        return 'h47B5408C;
                                    end
                                    else begin
                                        return 'h47E70611;
                                    end
                                end
                            end
                            else begin
                                if (index < 'h2E) begin
                                    if (index < 'h2D) begin
                                        return 'h4818EE22;
                                    end
                                    else begin
                                        return 'h484AF8D6;
                                    end
                                end
                                else begin
                                    if (index < 'h2F) begin
                                        return 'h487D2646;
                                    end
                                    else begin
                                        return 'h48AF768A;
                                    end
                                end
                            end
                        end
                    end
                    else begin
                        if (index < 'h38) begin
                            if (index < 'h34) begin
                                if (index < 'h32) begin
                                    if (index < 'h31) begin
                                        return 'h48E1E9BA;
                                    end
                                    else begin
                                        return 'h49147FEE;
                                    end
                                end
                                else begin
                                    if (index < 'h33) begin
                                        return 'h4947393F;
                                    end
                                    else begin
                                        return 'h497A15C4;
                                    end
                                end
                            end
                            else begin
                                if (index < 'h36) begin
                                    if (index < 'h35) begin
                                        return 'h49AD1598;
                                    end
                                    else begin
                                        return 'h49E038D0;
                                    end
                                end
                                else begin
                                    if (index < 'h37) begin
                                        return 'h4A137F88;
                                    end
                                    else begin
                                        return 'h4A46E9D6;
                                    end
                                end
                            end
                        end
                        else begin
                            if (index < 'h3C) begin
                                if (index < 'h3A) begin
                                    if (index < 'h39) begin
                                        return 'h4A7A77D4;
                                    end
                                    else begin
                                        return 'h4AAE299B;
                                    end
                                end
                                else begin
                                    if (index < 'h3B) begin
                                        return 'h4AE1FF43;
                                    end
                                    else begin
                                        return 'h4B15F8E6;
                                    end
                                end
                            end
                            else begin
                                if (index < 'h3E) begin
                                    if (index < 'h3D) begin
                                        return 'h4B4A169C;
                                    end
                                    else begin
                                        return 'h4B7E587E;
                                    end
                                end
                                else begin
                                    if (index < 'h3F) begin
                                        return 'h4BB2BEA5;
                                    end
                                    else begin
                                        return 'h4BE7492B;
                                    end
                                end
                            end
                        end
                    end
                end
            end
            else begin
                if (index < 'h60) begin
                    if (index < 'h50) begin
                        if (index < 'h48) begin
                            if (index < 'h44) begin
                                if (index < 'h42) begin
                                    if (index < 'h41) begin
                                        return 'h4C1BF829;
                                    end
                                    else begin
                                        return 'h4C50CBB8;
                                    end
                                end
                                else begin
                                    if (index < 'h43) begin
                                        return 'h4C85C3F1;
                                    end
                                    else begin
                                        return 'h4CBAE0EF;
                                    end
                                end
                            end
                            else begin
                                if (index < 'h46) begin
                                    if (index < 'h45) begin
                                        return 'h4CF022CA;
                                    end
                                    else begin
                                        return 'h4D25899C;
                                    end
                                end
                                else begin
                                    if (index < 'h47) begin
                                        return 'h4D5B157E;
                                    end
                                    else begin
                                        return 'h4D90C68B;
                                    end
                                end
                            end
                        end
                        else begin
                            if (index < 'h4C) begin
                                if (index < 'h4A) begin
                                    if (index < 'h49) begin
                                        return 'h4DC69CDD;
                                    end
                                    else begin
                                        return 'h4DFC988C;
                                    end
                                end
                                else begin
                                    if (index < 'h4B) begin
                                        return 'h4E32B9B4;
                                    end
                                    else begin
                                        return 'h4E69006E;
                                    end
                                end
                            end
                            else begin
                                if (index < 'h4E) begin
                                    if (index < 'h4D) begin
                                        return 'h4E9F6CD4;
                                    end
                                    else begin
                                        return 'h4ED5FF00;
                                    end
                                end
                                else begin
                                    if (index < 'h4F) begin
                                        return 'h4F0CB70C;
                                    end
                                    else begin
                                        return 'h4F439514;
                                    end
                                end
                            end
                        end
                    end
                    else begin
                        if (index < 'h58) begin
                            if (index < 'h54) begin
                                if (index < 'h52) begin
                                    if (index < 'h51) begin
                                        return 'h4F7A9930;
                                    end
                                    else begin
                                        return 'h4FB1C37C;
                                    end
                                end
                                else begin
                                    if (index < 'h53) begin
                                        return 'h4FE91413;
                                    end
                                    else begin
                                        return 'h50208B0E;
                                    end
                                end
                            end
                            else begin
                                if (index < 'h56) begin
                                    if (index < 'h55) begin
                                        return 'h50582888;
                                    end
                                    else begin
                                        return 'h508FEC9C;
                                    end
                                end
                                else begin
                                    if (index < 'h57) begin
                                        return 'h50C7D765;
                                    end
                                    else begin
                                        return 'h50FFE8FE;
                                    end
                                end
                            end
                        end
                        else begin
                            if (index < 'h5C) begin
                                if (index < 'h5A) begin
                                    if (index < 'h59) begin
                                        return 'h51382182;
                                    end
                                    else begin
                                        return 'h5170810B;
                                    end
                                end
                                else begin
                                    if (index < 'h5B) begin
                                        return 'h51A907B4;
                                    end
                                    else begin
                                        return 'h51E1B59A;
                                    end
                                end
                            end
                            else begin
                                if (index < 'h5E) begin
                                    if (index < 'h5D) begin
                                        return 'h521A8AD7;
                                    end
                                    else begin
                                        return 'h52538786;
                                    end
                                end
                                else begin
                                    if (index < 'h5F) begin
                                        return 'h528CABC3;
                                    end
                                    else begin
                                        return 'h52C5F7AA;
                                    end
                                end
                            end
                        end
                    end
                end
                else begin
                    if (index < 'h70) begin
                        if (index < 'h68) begin
                            if (index < 'h64) begin
                                if (index < 'h62) begin
                                    if (index < 'h61) begin
                                        return 'h52FF6B55;
                                    end
                                    else begin
                                        return 'h533906E0;
                                    end
                                end
                                else begin
                                    if (index < 'h63) begin
                                        return 'h5372CA68;
                                    end
                                    else begin
                                        return 'h53ACB607;
                                    end
                                end
                            end
                            else begin
                                if (index < 'h66) begin
                                    if (index < 'h65) begin
                                        return 'h53E6C9DA;
                                    end
                                    else begin
                                        return 'h542105FD;
                                    end
                                end
                                else begin
                                    if (index < 'h67) begin
                                        return 'h545B6A8B;
                                    end
                                    else begin
                                        return 'h5495F7A1;
                                    end
                                end
                            end
                        end
                        else begin
                            if (index < 'h6C) begin
                                if (index < 'h6A) begin
                                    if (index < 'h69) begin
                                        return 'h54D0AD5A;
                                    end
                                    else begin
                                        return 'h550B8BD4;
                                    end
                                end
                                else begin
                                    if (index < 'h6B) begin
                                        return 'h55469329;
                                    end
                                    else begin
                                        return 'h5581C378;
                                    end
                                end
                            end
                            else begin
                                if (index < 'h6E) begin
                                    if (index < 'h6D) begin
                                        return 'h55BD1CDB;
                                    end
                                    else begin
                                        return 'h55F89F70;
                                    end
                                end
                                else begin
                                    if (index < 'h6F) begin
                                        return 'h56344B52;
                                    end
                                    else begin
                                        return 'h567020A0;
                                    end
                                end
                            end
                        end
                    end
                    else begin
                        if (index < 'h78) begin
                            if (index < 'h74) begin
                                if (index < 'h72) begin
                                    if (index < 'h71) begin
                                        return 'h56AC1F75;
                                    end
                                    else begin
                                        return 'h56E847EF;
                                    end
                                end
                                else begin
                                    if (index < 'h73) begin
                                        return 'h57249A29;
                                    end
                                    else begin
                                        return 'h57611642;
                                    end
                                end
                            end
                            else begin
                                if (index < 'h76) begin
                                    if (index < 'h75) begin
                                        return 'h579DBC57;
                                    end
                                    else begin
                                        return 'h57DA8C83;
                                    end
                                end
                                else begin
                                    if (index < 'h77) begin
                                        return 'h581786E6;
                                    end
                                    else begin
                                        return 'h5854AB9B;
                                    end
                                end
                            end
                        end
                        else begin
                            if (index < 'h7C) begin
                                if (index < 'h7A) begin
                                    if (index < 'h79) begin
                                        return 'h5891FAC1;
                                    end
                                    else begin
                                        return 'h58CF7474;
                                    end
                                end
                                else begin
                                    if (index < 'h7B) begin
                                        return 'h590D18D3;
                                    end
                                    else begin
                                        return 'h594AE7FB;
                                    end
                                end
                            end
                            else begin
                                if (index < 'h7E) begin
                                    if (index < 'h7D) begin
                                        return 'h5988E209;
                                    end
                                    else begin
                                        return 'h59C7071C;
                                    end
                                end
                                else begin
                                    if (index < 'h7F) begin
                                        return 'h5A055751;
                                    end
                                    else begin
                                        return 'h5A43D2C6;
                                    end
                                end
                            end
                        end
                    end
                end
            end
        end
        else begin
            if (index < 'hC0) begin
                if (index < 'hA0) begin
                    if (index < 'h90) begin
                        if (index < 'h88) begin
                            if (index < 'h84) begin
                                if (index < 'h82) begin
                                    if (index < 'h81) begin
                                        return 'h5A82799A;
                                    end
                                    else begin
                                        return 'h5AC14BEA;
                                    end
                                end
                                else begin
                                    if (index < 'h83) begin
                                        return 'h5B0049D4;
                                    end
                                    else begin
                                        return 'h5B3F7377;
                                    end
                                end
                            end
                            else begin
                                if (index < 'h86) begin
                                    if (index < 'h85) begin
                                        return 'h5B7EC8F2;
                                    end
                                    else begin
                                        return 'h5BBE4A61;
                                    end
                                end
                                else begin
                                    if (index < 'h87) begin
                                        return 'h5BFDF7E5;
                                    end
                                    else begin
                                        return 'h5C3DD19C;
                                    end
                                end
                            end
                        end
                        else begin
                            if (index < 'h8C) begin
                                if (index < 'h8A) begin
                                    if (index < 'h89) begin
                                        return 'h5C7DD7A4;
                                    end
                                    else begin
                                        return 'h5CBE0A1C;
                                    end
                                end
                                else begin
                                    if (index < 'h8B) begin
                                        return 'h5CFE6923;
                                    end
                                    else begin
                                        return 'h5D3EF4D7;
                                    end
                                end
                            end
                            else begin
                                if (index < 'h8E) begin
                                    if (index < 'h8D) begin
                                        return 'h5D7FAD59;
                                    end
                                    else begin
                                        return 'h5DC092C7;
                                    end
                                end
                                else begin
                                    if (index < 'h8F) begin
                                        return 'h5E01A53F;
                                    end
                                    else begin
                                        return 'h5E42E4E3;
                                    end
                                end
                            end
                        end
                    end
                    else begin
                        if (index < 'h98) begin
                            if (index < 'h94) begin
                                if (index < 'h92) begin
                                    if (index < 'h91) begin
                                        return 'h5E8451D0;
                                    end
                                    else begin
                                        return 'h5EC5EC26;
                                    end
                                end
                                else begin
                                    if (index < 'h93) begin
                                        return 'h5F07B405;
                                    end
                                    else begin
                                        return 'h5F49A98C;
                                    end
                                end
                            end
                            else begin
                                if (index < 'h96) begin
                                    if (index < 'h95) begin
                                        return 'h5F8BCCDB;
                                    end
                                    else begin
                                        return 'h5FCE1E12;
                                    end
                                end
                                else begin
                                    if (index < 'h97) begin
                                        return 'h60109D51;
                                    end
                                    else begin
                                        return 'h60534AB7;
                                    end
                                end
                            end
                        end
                        else begin
                            if (index < 'h9C) begin
                                if (index < 'h9A) begin
                                    if (index < 'h99) begin
                                        return 'h60962665;
                                    end
                                    else begin
                                        return 'h60D9307B;
                                    end
                                end
                                else begin
                                    if (index < 'h9B) begin
                                        return 'h611C6919;
                                    end
                                    else begin
                                        return 'h615FD05E;
                                    end
                                end
                            end
                            else begin
                                if (index < 'h9E) begin
                                    if (index < 'h9D) begin
                                        return 'h61A3666D;
                                    end
                                    else begin
                                        return 'h61E72B65;
                                    end
                                end
                                else begin
                                    if (index < 'h9F) begin
                                        return 'h622B1F66;
                                    end
                                    else begin
                                        return 'h626F4292;
                                    end
                                end
                            end
                        end
                    end
                end
                else begin
                    if (index < 'hB0) begin
                        if (index < 'hA8) begin
                            if (index < 'hA4) begin
                                if (index < 'hA2) begin
                                    if (index < 'hA1) begin
                                        return 'h62B39509;
                                    end
                                    else begin
                                        return 'h62F816EB;
                                    end
                                end
                                else begin
                                    if (index < 'hA3) begin
                                        return 'h633CC85B;
                                    end
                                    else begin
                                        return 'h6381A978;
                                    end
                                end
                            end
                            else begin
                                if (index < 'hA6) begin
                                    if (index < 'hA5) begin
                                        return 'h63C6BA64;
                                    end
                                    else begin
                                        return 'h640BFB41;
                                    end
                                end
                                else begin
                                    if (index < 'hA7) begin
                                        return 'h64516C2E;
                                    end
                                    else begin
                                        return 'h64970D4F;
                                    end
                                end
                            end
                        end
                        else begin
                            if (index < 'hAC) begin
                                if (index < 'hAA) begin
                                    if (index < 'hA9) begin
                                        return 'h64DCDEC3;
                                    end
                                    else begin
                                        return 'h6522E0AD;
                                    end
                                end
                                else begin
                                    if (index < 'hAB) begin
                                        return 'h6569132F;
                                    end
                                    else begin
                                        return 'h65AF766A;
                                    end
                                end
                            end
                            else begin
                                if (index < 'hAE) begin
                                    if (index < 'hAD) begin
                                        return 'h65F60A7F;
                                    end
                                    else begin
                                        return 'h663CCF92;
                                    end
                                end
                                else begin
                                    if (index < 'hAF) begin
                                        return 'h6683C5C3;
                                    end
                                    else begin
                                        return 'h66CAED35;
                                    end
                                end
                            end
                        end
                    end
                    else begin
                        if (index < 'hB8) begin
                            if (index < 'hB4) begin
                                if (index < 'hB2) begin
                                    if (index < 'hB1) begin
                                        return 'h6712460B;
                                    end
                                    else begin
                                        return 'h6759D065;
                                    end
                                end
                                else begin
                                    if (index < 'hB3) begin
                                        return 'h67A18C68;
                                    end
                                    else begin
                                        return 'h67E97A34;
                                    end
                                end
                            end
                            else begin
                                if (index < 'hB6) begin
                                    if (index < 'hB5) begin
                                        return 'h683199ED;
                                    end
                                    else begin
                                        return 'h6879EBB6;
                                    end
                                end
                                else begin
                                    if (index < 'hB7) begin
                                        return 'h68C26FB1;
                                    end
                                    else begin
                                        return 'h690B2601;
                                    end
                                end
                            end
                        end
                        else begin
                            if (index < 'hBC) begin
                                if (index < 'hBA) begin
                                    if (index < 'hB9) begin
                                        return 'h69540EC9;
                                    end
                                    else begin
                                        return 'h699D2A2C;
                                    end
                                end
                                else begin
                                    if (index < 'hBB) begin
                                        return 'h69E6784D;
                                    end
                                    else begin
                                        return 'h6A2FF94F;
                                    end
                                end
                            end
                            else begin
                                if (index < 'hBE) begin
                                    if (index < 'hBD) begin
                                        return 'h6A79AD56;
                                    end
                                    else begin
                                        return 'h6AC39485;
                                    end
                                end
                                else begin
                                    if (index < 'hBF) begin
                                        return 'h6B0DAEFF;
                                    end
                                    else begin
                                        return 'h6B57FCE9;
                                    end
                                end
                            end
                        end
                    end
                end
            end
            else begin
                if (index < 'hE0) begin
                    if (index < 'hD0) begin
                        if (index < 'hC8) begin
                            if (index < 'hC4) begin
                                if (index < 'hC2) begin
                                    if (index < 'hC1) begin
                                        return 'h6BA27E65;
                                    end
                                    else begin
                                        return 'h6BED3399;
                                    end
                                end
                                else begin
                                    if (index < 'hC3) begin
                                        return 'h6C381CA6;
                                    end
                                    else begin
                                        return 'h6C8339B2;
                                    end
                                end
                            end
                            else begin
                                if (index < 'hC6) begin
                                    if (index < 'hC5) begin
                                        return 'h6CCE8AE1;
                                    end
                                    else begin
                                        return 'h6D1A1057;
                                    end
                                end
                                else begin
                                    if (index < 'hC7) begin
                                        return 'h6D65CA38;
                                    end
                                    else begin
                                        return 'h6DB1B8A8;
                                    end
                                end
                            end
                        end
                        else begin
                            if (index < 'hCC) begin
                                if (index < 'hCA) begin
                                    if (index < 'hC9) begin
                                        return 'h6DFDDBCC;
                                    end
                                    else begin
                                        return 'h6E4A33C9;
                                    end
                                end
                                else begin
                                    if (index < 'hCB) begin
                                        return 'h6E96C0C3;
                                    end
                                    else begin
                                        return 'h6EE382DE;
                                    end
                                end
                            end
                            else begin
                                if (index < 'hCE) begin
                                    if (index < 'hCD) begin
                                        return 'h6F307A41;
                                    end
                                    else begin
                                        return 'h6F7DA710;
                                    end
                                end
                                else begin
                                    if (index < 'hCF) begin
                                        return 'h6FCB096F;
                                    end
                                    else begin
                                        return 'h7018A185;
                                    end
                                end
                            end
                        end
                    end
                    else begin
                        if (index < 'hD8) begin
                            if (index < 'hD4) begin
                                if (index < 'hD2) begin
                                    if (index < 'hD1) begin
                                        return 'h70666F76;
                                    end
                                    else begin
                                        return 'h70B47368;
                                    end
                                end
                                else begin
                                    if (index < 'hD3) begin
                                        return 'h7102AD80;
                                    end
                                    else begin
                                        return 'h71511DE4;
                                    end
                                end
                            end
                            else begin
                                if (index < 'hD6) begin
                                    if (index < 'hD5) begin
                                        return 'h719FC4B9;
                                    end
                                    else begin
                                        return 'h71EEA226;
                                    end
                                end
                                else begin
                                    if (index < 'hD7) begin
                                        return 'h723DB650;
                                    end
                                    else begin
                                        return 'h728D015D;
                                    end
                                end
                            end
                        end
                        else begin
                            if (index < 'hDC) begin
                                if (index < 'hDA) begin
                                    if (index < 'hD9) begin
                                        return 'h72DC8374;
                                    end
                                    else begin
                                        return 'h732C3CBA;
                                    end
                                end
                                else begin
                                    if (index < 'hDB) begin
                                        return 'h737C2D55;
                                    end
                                    else begin
                                        return 'h73CC556D;
                                    end
                                end
                            end
                            else begin
                                if (index < 'hDE) begin
                                    if (index < 'hDD) begin
                                        return 'h741CB528;
                                    end
                                    else begin
                                        return 'h746D4CAC;
                                    end
                                end
                                else begin
                                    if (index < 'hDF) begin
                                        return 'h74BE1C20;
                                    end
                                    else begin
                                        return 'h750F23AB;
                                    end
                                end
                            end
                        end
                    end
                end
                else begin
                    if (index < 'hF0) begin
                        if (index < 'hE8) begin
                            if (index < 'hE4) begin
                                if (index < 'hE2) begin
                                    if (index < 'hE1) begin
                                        return 'h75606374;
                                    end
                                    else begin
                                        return 'h75B1DBA2;
                                    end
                                end
                                else begin
                                    if (index < 'hE3) begin
                                        return 'h76038C5B;
                                    end
                                    else begin
                                        return 'h765575C8;
                                    end
                                end
                            end
                            else begin
                                if (index < 'hE6) begin
                                    if (index < 'hE5) begin
                                        return 'h76A7980F;
                                    end
                                    else begin
                                        return 'h76F9F359;
                                    end
                                end
                                else begin
                                    if (index < 'hE7) begin
                                        return 'h774C87CC;
                                    end
                                    else begin
                                        return 'h779F5590;
                                    end
                                end
                            end
                        end
                        else begin
                            if (index < 'hEC) begin
                                if (index < 'hEA) begin
                                    if (index < 'hE9) begin
                                        return 'h77F25CCE;
                                    end
                                    else begin
                                        return 'h78459DAC;
                                    end
                                end
                                else begin
                                    if (index < 'hEB) begin
                                        return 'h78991854;
                                    end
                                    else begin
                                        return 'h78ECCCEC;
                                    end
                                end
                            end
                            else begin
                                if (index < 'hEE) begin
                                    if (index < 'hED) begin
                                        return 'h7940BB9E;
                                    end
                                    else begin
                                        return 'h7994E492;
                                    end
                                end
                                else begin
                                    if (index < 'hEF) begin
                                        return 'h79E947EF;
                                    end
                                    else begin
                                        return 'h7A3DE5DF;
                                    end
                                end
                            end
                        end
                    end
                    else begin
                        if (index < 'hF8) begin
                            if (index < 'hF4) begin
                                if (index < 'hF2) begin
                                    if (index < 'hF1) begin
                                        return 'h7A92BE8B;
                                    end
                                    else begin
                                        return 'h7AE7D21A;
                                    end
                                end
                                else begin
                                    if (index < 'hF3) begin
                                        return 'h7B3D20B6;
                                    end
                                    else begin
                                        return 'h7B92AA88;
                                    end
                                end
                            end
                            else begin
                                if (index < 'hF6) begin
                                    if (index < 'hF5) begin
                                        return 'h7BE86FBA;
                                    end
                                    else begin
                                        return 'h7C3E7073;
                                    end
                                end
                                else begin
                                    if (index < 'hF7) begin
                                        return 'h7C94ACDE;
                                    end
                                    else begin
                                        return 'h7CEB2523;
                                    end
                                end
                            end
                        end
                        else begin
                            if (index < 'hFC) begin
                                if (index < 'hFA) begin
                                    if (index < 'hF9) begin
                                        return 'h7D41D96E;
                                    end
                                    else begin
                                        return 'h7D98C9E6;
                                    end
                                end
                                else begin
                                    if (index < 'hFB) begin
                                        return 'h7DEFF6B6;
                                    end
                                    else begin
                                        return 'h7E476009;
                                    end
                                end
                            end
                            else begin
                                if (index < 'hFE) begin
                                    if (index < 'hFD) begin
                                        return 'h7E9F0606;
                                    end
                                    else begin
                                        return 'h7EF6E8DA;
                                    end
                                end
                                else begin
                                    if (index < 'hFF) begin
                                        return 'h7F4F08AE;
                                    end
                                    else begin
                                        return 'h7FA765AD;
                                    end
                                end
                            end
                        end
                    end
                end
            end
        end
    endfunction

    function logic[31:0] cpphdl_examples_math_Fp32ExpMethods___fp32_exp_table (input logic[31:0] a);
        logic signed[63:0] y;
        logic negative;
        logic[31:0] magnitude;
        logic[31:0] exponent;
        logic[31:0] mantissa;
        logic[31:0] shift;
        logic[31:0] fixed;
        logic[31:0] logarithm;
        logic[31:0] fraction;
        logic[31:0] residual;
        logic[31:0] t;
        logic[31:0] polynomial;
        logic[31:0] significand;
        logic[31:0] retained;
        logic[31:0] tail;
        logic signed[31:0] power;
        logic signed[31:0] output_exponent;
        magnitude=a & 'h7FFFFFFF;
        exponent=magnitude >>> 32'sh17;
        negative=((a >>> 32'sh1F)) != 32'h0;
        if (magnitude > 'h7F800000) begin
            return 'h7FC00000;
        end
        if (magnitude > 'h42B40000) begin
            return (negative) ? ('h0) : ('h7F800000);
        end
        if (exponent == 32'h0) begin
            return 'h3F800000;
        end
        mantissa=((magnitude & 'h7FFFFF)) | 'h800000;
        shift=((exponent>=32'h7E) ? (exponent - 32'h7E) : (32'h7E - exponent));
        fixed=(exponent>=32'h7E) ? (mantissa <<< ((shift & 32'h1F))) : ((shift>=32'h20) ? ('h0) : (mantissa >>> ((shift & 32'h1F))));
        logarithm=(((64'(fixed)*64'h5C551D95) + ((64'h1 <<< 'h1D)))) >>> 32'sh1E;
        y=(negative) ? (-signed'(64'(logarithm))) : (signed'(64'(logarithm)));
        power=y >>> 32'sh18;
        fraction=unsigned'(32'(y)) & 'hFFFFFF;
        residual=fraction & 'hFFFF;
        t=((64'(residual)*64'h2C5C85FE)) >>> 32'sh18;
        polynomial=('h40000000 + t) + 32'((((128'(64'(t))*128'(64'(t)))) >>> 32'sh1F));
        significand=((64'((128'(unsigned'(64'(cpphdl_examples_math_Fp32ExpMethods___exp2_table((fraction >>> 32'sh10)))))*128'(64'(polynomial)))) + ((64'h1 <<< 'h1D)))) >>> 32'sh1E;
        retained=significand >>> 32'sh7;
        tail=significand & 'h7F;
        if ((tail > 32'h40) || (((tail == 32'h40) && ((((retained & 32'h1))) != '0)))) begin
            retained=retained+1;
        end
        if (retained>='h1000000) begin
            retained>>='h1;
            power=power+1;
        end
        output_exponent='h7F + power;
        if (output_exponent<=32'sh0) begin
            return 32'h0;
        end
        if (output_exponent>=32'shFF) begin
            return 'h7F800000;
        end
        return ((unsigned'(32'(output_exponent)) <<< 'h17)) | ((retained & 'h7FFFFF));
    endfunction

    always_comb begin : result_comb_func  // result_comb_func
        result_comb=cpphdl_examples_math_Fp32ExpMethods___fp32_exp_table(data_in);
    end

    generate  // _assign
        assign result_out = result_comb;
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

            _work(reset);

    end


endmodule
