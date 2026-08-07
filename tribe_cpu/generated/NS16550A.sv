`default_nettype none

import Predef_pkg::*;


module NS16550A #(
    parameter ADDR_WIDTH
,   parameter ID_WIDTH
,   parameter DATA_WIDTH
 )
 (
    input wire clk
,   input wire reset
,   input wire axi_in__awvalid_in
,   output wire axi_in__awready_out
,   input wire[ADDR_WIDTH-1:0] axi_in__awaddr_in
,   input wire[ID_WIDTH-1:0] axi_in__awid_in
,   input wire axi_in__wvalid_in
,   output wire axi_in__wready_out
,   input wire[DATA_WIDTH-1:0] axi_in__wdata_in
,   input wire axi_in__wlast_in
,   output wire axi_in__bvalid_out
,   input wire axi_in__bready_in
,   output wire[ID_WIDTH-1:0] axi_in__bid_out
,   input wire axi_in__arvalid_in
,   output wire axi_in__arready_out
,   input wire[ADDR_WIDTH-1:0] axi_in__araddr_in
,   input wire[ID_WIDTH-1:0] axi_in__arid_in
,   output wire axi_in__rvalid_out
,   input wire axi_in__rready_in
,   output wire[DATA_WIDTH-1:0] axi_in__rdata_out
,   output wire axi_in__rlast_out
,   output wire[ID_WIDTH-1:0] axi_in__rid_out
,   output wire uart_valid_out
,   output wire[7:0] uart_data_out
,   input wire uart_rx_valid_in
,   input wire[7:0] uart_rx_data_in
,   output wire uart_rx_ready_out
,   output wire irq_out
);
    parameter  REG_RBR_THR_DLL = 'h0;
    parameter  REG_IER_DLM = 'h1;
    parameter  REG_IIR_FCR = 'h2;
    parameter  REG_LCR = 'h3;
    parameter  REG_MCR = 'h4;
    parameter  REG_LSR = 'h5;
    parameter  REG_MSR = 'h6;
    parameter  REG_SCR = 'h7;


    // regs and combs
    reg[ADDR_WIDTH-1:0] read_addr_reg;
    reg[ID_WIDTH-1:0] read_id_reg;
    reg read_valid_reg;
    reg[DATA_WIDTH-1:0] read_data_reg;
    reg[ADDR_WIDTH-1:0] write_addr_reg;
    reg[ID_WIDTH-1:0] write_id_reg;
    reg write_addr_valid_reg;
    reg write_resp_valid_reg;
    reg[8-1:0] ier_reg;
    reg[8-1:0] lcr_reg;
    reg[8-1:0] mcr_reg;
    reg[8-1:0] scr_reg;
    reg[8-1:0] dll_reg;
    reg[8-1:0] dlm_reg;
    reg uart_valid_reg;
    reg[8-1:0] uart_data_reg;
    reg rx_valid_reg;
    reg[8-1:0] rx_data_reg;
    reg rbr_duplicate_valid_reg;
    logic dlab_comb;
;
    logic irq_comb;
;
    logic[DATA_WIDTH-1:0] read_data_comb;
;

    // members

    // tmp variables
    logic[ADDR_WIDTH-1:0] read_addr_reg_tmp;
    logic[ID_WIDTH-1:0] read_id_reg_tmp;
    logic read_valid_reg_tmp;
    logic[DATA_WIDTH-1:0] read_data_reg_tmp;
    logic[ADDR_WIDTH-1:0] write_addr_reg_tmp;
    logic[ID_WIDTH-1:0] write_id_reg_tmp;
    logic write_addr_valid_reg_tmp;
    logic write_resp_valid_reg_tmp;
    logic[8-1:0] ier_reg_tmp;
    logic[8-1:0] lcr_reg_tmp;
    logic[8-1:0] mcr_reg_tmp;
    logic[8-1:0] scr_reg_tmp;
    logic[8-1:0] dll_reg_tmp;
    logic[8-1:0] dlm_reg_tmp;
    logic uart_valid_reg_tmp;
    logic[8-1:0] uart_data_reg_tmp;
    logic rx_valid_reg_tmp;
    logic[8-1:0] rx_data_reg_tmp;
    logic rbr_duplicate_valid_reg_tmp;


    always_comb begin : dlab_comb_func  // dlab_comb_func
        dlab_comb=((lcr_reg & unsigned'(8'(unsigned'(8'('h80)))))) != 'h0;
    end

    generate  // _assign
        assign axi_in__awready_out = !write_addr_valid_reg && !write_resp_valid_reg;
        assign axi_in__wready_out = write_addr_valid_reg && !write_resp_valid_reg;
        assign axi_in__bvalid_out = write_resp_valid_reg;
        assign axi_in__bid_out = write_id_reg;
        assign axi_in__arready_out = !read_valid_reg;
        assign axi_in__rvalid_out = read_valid_reg;
        assign axi_in__rdata_out = read_data_reg;
        assign axi_in__rlast_out = read_valid_reg;
        assign axi_in__rid_out = read_id_reg;
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[31:0] addr;
        logic[31:0] lane;
        logic[7:0] data;
        logic[64-1:0] read_data;
        uart_valid_reg_tmp = unsigned'(1'(0));
        if (uart_rx_valid_in && !rx_valid_reg) begin
            rx_data_reg_tmp = unsigned'(8'(uart_rx_data_in));
            rx_valid_reg_tmp = unsigned'(1'(1));
            rbr_duplicate_valid_reg_tmp = unsigned'(1'(0));
        end
        if (axi_in__arvalid_in && axi_in__arready_out) begin
            addr=unsigned'(32'(axi_in__araddr_in)) & 'h7;
            lane=unsigned'(32'(axi_in__araddr_in)) % ((DATA_WIDTH/'h8));
            data='h0;
            if (addr == REG_RBR_THR_DLL) begin
                if (dlab_comb) begin
                    data=unsigned'(8'(dll_reg));
                end
                else begin
                    if (rbr_duplicate_valid_reg) begin
                        data='h0;
                    end
                    else begin
                        data=(rx_valid_reg) ? (unsigned'(8'(rx_data_reg))) : ('h0);
                    end
                end
            end
            if (addr == REG_IER_DLM) begin
                data=(dlab_comb) ? (unsigned'(8'(dlm_reg))) : (unsigned'(8'(ier_reg)));
            end
            if (addr == REG_IIR_FCR) begin
                if (rx_valid_reg && ((((ier_reg & unsigned'(8'(unsigned'(8'('h1)))))) != 'h0))) begin
                    data='h4;
                end
                else begin
                    data='h1;
                end
            end
            if (addr == REG_LCR) begin
                data=unsigned'(8'(lcr_reg));
            end
            if (addr == REG_MCR) begin
                data=unsigned'(8'(mcr_reg));
            end
            if (addr == REG_LSR) begin
                data=unsigned'(8'(('h60 | ((rx_valid_reg) ? ('h1) : ('h0)))));
            end
            if (addr == REG_MSR) begin
                data='h0;
            end
            if (addr == REG_SCR) begin
                data=unsigned'(8'(scr_reg));
            end
            read_data = 'h0;
            read_data[lane*'h8 +:8] = data;
            read_data_reg_tmp = read_data;
            read_addr_reg_tmp = axi_in__araddr_in;
            read_id_reg_tmp = axi_in__arid_in;
            read_valid_reg_tmp = unsigned'(1'(1));
            if ((addr == REG_RBR_THR_DLL) && !dlab_comb) begin
                if (!rbr_duplicate_valid_reg) begin
                    rx_valid_reg_tmp = unsigned'(1'(0));
                    rbr_duplicate_valid_reg_tmp = unsigned'(1'(1));
                end
            end
        end
        if (read_valid_reg && axi_in__rready_in) begin
            read_valid_reg_tmp = unsigned'(1'(0));
        end
        if (axi_in__awvalid_in && axi_in__awready_out) begin
            write_addr_reg_tmp = axi_in__awaddr_in;
            write_id_reg_tmp = axi_in__awid_in;
            write_addr_valid_reg_tmp = unsigned'(1'(1));
        end
        if (axi_in__wvalid_in && axi_in__wready_out) begin
            addr=unsigned'(32'(write_addr_reg)) & 'h7;
            lane=unsigned'(32'(write_addr_reg)) % ((DATA_WIDTH/'h8));
            data=unsigned'(8'(unsigned'(32'(axi_in__wdata_in[lane*'h8 +:8]))));
            write_addr_valid_reg_tmp = unsigned'(1'(0));
            write_resp_valid_reg_tmp = unsigned'(1'(1));
            if (addr == REG_RBR_THR_DLL) begin
                if (dlab_comb) begin
                    dll_reg_tmp = unsigned'(8'(data));
                end
                else begin
                    uart_data_reg_tmp = unsigned'(8'(data));
                    uart_valid_reg_tmp = unsigned'(1'(1));
                end
            end
            if (addr == REG_IER_DLM) begin
                if (dlab_comb) begin
                    dlm_reg_tmp = unsigned'(8'(data));
                end
                else begin
                    ier_reg_tmp = unsigned'(8'(data));
                end
            end
            if (addr == REG_LCR) begin
                lcr_reg_tmp = unsigned'(8'(data));
            end
            if (addr == REG_MCR) begin
                mcr_reg_tmp = unsigned'(8'(data));
            end
            if (addr == REG_SCR) begin
                scr_reg_tmp = unsigned'(8'(data));
            end
        end
        if (write_resp_valid_reg && axi_in__bready_in) begin
            write_resp_valid_reg_tmp = unsigned'(1'(0));
        end
        if (reset) begin
            read_addr_reg_tmp = '0;
            read_id_reg_tmp = '0;
            read_valid_reg_tmp = '0;
            read_data_reg_tmp = '0;
            write_addr_reg_tmp = '0;
            write_id_reg_tmp = '0;
            write_addr_valid_reg_tmp = '0;
            write_resp_valid_reg_tmp = '0;
            ier_reg_tmp = '0;
            lcr_reg_tmp = '0;
            mcr_reg_tmp = '0;
            scr_reg_tmp = '0;
            dll_reg_tmp = '0;
            dlm_reg_tmp = '0;
            uart_valid_reg_tmp = '0;
            uart_data_reg_tmp = '0;
            rx_valid_reg_tmp = '0;
            rx_data_reg_tmp = '0;
            rbr_duplicate_valid_reg_tmp = '0;
        end
    end
    endtask

    always_comb begin : irq_comb_func  // irq_comb_func
        irq_comb=((((mcr_reg & unsigned'(8'(unsigned'(8'('h8)))))) != 'h0)) && ((rx_valid_reg && ((((ier_reg & unsigned'(8'(unsigned'(8'('h1)))))) != 'h0))));
    end

    always @(posedge clk) begin
        read_addr_reg_tmp = read_addr_reg;
        read_id_reg_tmp = read_id_reg;
        read_valid_reg_tmp = read_valid_reg;
        read_data_reg_tmp = read_data_reg;
        write_addr_reg_tmp = write_addr_reg;
        write_id_reg_tmp = write_id_reg;
        write_addr_valid_reg_tmp = write_addr_valid_reg;
        write_resp_valid_reg_tmp = write_resp_valid_reg;
        ier_reg_tmp = ier_reg;
        lcr_reg_tmp = lcr_reg;
        mcr_reg_tmp = mcr_reg;
        scr_reg_tmp = scr_reg;
        dll_reg_tmp = dll_reg;
        dlm_reg_tmp = dlm_reg;
        uart_valid_reg_tmp = uart_valid_reg;
        uart_data_reg_tmp = uart_data_reg;
        rx_valid_reg_tmp = rx_valid_reg;
        rx_data_reg_tmp = rx_data_reg;
        rbr_duplicate_valid_reg_tmp = rbr_duplicate_valid_reg;

        _work(reset);

        read_addr_reg <= read_addr_reg_tmp;
        read_id_reg <= read_id_reg_tmp;
        read_valid_reg <= read_valid_reg_tmp;
        read_data_reg <= read_data_reg_tmp;
        write_addr_reg <= write_addr_reg_tmp;
        write_id_reg <= write_id_reg_tmp;
        write_addr_valid_reg <= write_addr_valid_reg_tmp;
        write_resp_valid_reg <= write_resp_valid_reg_tmp;
        ier_reg <= ier_reg_tmp;
        lcr_reg <= lcr_reg_tmp;
        mcr_reg <= mcr_reg_tmp;
        scr_reg <= scr_reg_tmp;
        dll_reg <= dll_reg_tmp;
        dlm_reg <= dlm_reg_tmp;
        uart_valid_reg <= uart_valid_reg_tmp;
        uart_data_reg <= uart_data_reg_tmp;
        rx_valid_reg <= rx_valid_reg_tmp;
        rx_data_reg <= rx_data_reg_tmp;
        rbr_duplicate_valid_reg <= rbr_duplicate_valid_reg_tmp;
    end

    assign uart_valid_out = uart_valid_reg;

    assign uart_data_out = uart_data_reg;

    assign uart_rx_ready_out = !rx_valid_reg;

    assign irq_out = irq_comb;


endmodule
