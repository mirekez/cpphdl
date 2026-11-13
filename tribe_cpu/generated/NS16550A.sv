`default_nettype none

import Predef_pkg::*;


module NS16550A #(
    parameter ADDR_WIDTH = 'h20
,   parameter ID_WIDTH = 'h4
,   parameter DATA_WIDTH = 'h100
 )
 (
    input wire clk
,   input wire l2_clock
,   input wire reset
,   input wire axi_in__awvalid_in
,   output wire axi_in__awready_out
,   input wire[ADDR_WIDTH-1:0] axi_in__awaddr_in
,   input wire[ID_WIDTH-1:0] axi_in__awid_in
,   input wire axi_in__wvalid_in
,   output wire axi_in__wready_out
,   input wire[DATA_WIDTH-1:0] axi_in__wdata_in
,   input wire[unsigned'(64'(DATA_WIDTH))/64'h8-1:0] axi_in__wstrb_in
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
    localparam  REG_RBR_THR_DLL = 'h0;
    localparam  REG_IER_DLM = 'h1;
    localparam  REG_IIR_FCR = 'h2;
    localparam  REG_LCR = 'h3;
    localparam  REG_MCR = 'h4;
    localparam  REG_LSR = 'h5;
    localparam  REG_MSR = 'h6;
    localparam  REG_SCR = 'h7;


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
    reg[8-1:0] fcr_reg;
    reg[8-1:0] scr_reg;
    reg[8-1:0] dll_reg;
    reg[8-1:0] dlm_reg;
    reg uart_valid_reg;
    reg[8-1:0] uart_data_reg;
    reg rx_valid_reg;
    reg[8-1:0] rx_data_reg;
    reg[8-1:0] rx_fifo_reg[15];
    reg[8-1:0] rx_fifo_head_reg;
    reg[8-1:0] rx_fifo_tail_reg;
    reg[8-1:0] rx_fifo_count_reg;
    reg rbr_duplicate_valid_reg;
    logic dlab_comb;
    logic irq_comb;
    logic[DATA_WIDTH-1:0] read_data_comb;

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
    logic[8-1:0] fcr_reg_tmp;
    logic[8-1:0] scr_reg_tmp;
    logic[8-1:0] dll_reg_tmp;
    logic[8-1:0] dlm_reg_tmp;
    logic uart_valid_reg_tmp;
    logic[8-1:0] uart_data_reg_tmp;
    logic rx_valid_reg_tmp;
    logic[8-1:0] rx_data_reg_tmp;
    logic[8-1:0] rx_fifo_reg_tmp[15];
    logic[8-1:0] rx_fifo_head_reg_tmp;
    logic[8-1:0] rx_fifo_tail_reg_tmp;
    logic[8-1:0] rx_fifo_count_reg_tmp;
    logic rbr_duplicate_valid_reg_tmp;


    always_comb begin : irq_comb_func  // irq_comb_func
        irq_comb=((((mcr_reg & 8'(64'h8))) != 64'h0)) && ((((rx_valid_reg) != '0) && ((((ier_reg & 8'(64'h1))) != 64'h0))));
    end

    always_comb begin : dlab_comb_func  // dlab_comb_func
        dlab_comb=((lcr_reg & 8'(64'h80))) != 64'h0;
    end

    always_comb begin : read_data_comb_func  // read_data_comb_func
        logic[31:0] addr;
        logic[31:0] lane;
        logic[7:0] data;
        read_data_comb = 'h0;
        addr=unsigned'(32'(read_addr_reg)) & 'h7;
        lane=64'(unsigned'(32'(read_addr_reg))) % ((DATA_WIDTH/64'h8));
        data=8'h0;
        if (addr == unsigned'(32'(REG_RBR_THR_DLL))) begin
            data=(dlab_comb) ? (signed'(32'(unsigned'(8'(dll_reg))))) : (((((rx_valid_reg) != '0)) ? (signed'(32'(unsigned'(8'(rx_data_reg))))) : ('h0)));
        end
        if (addr == unsigned'(32'(REG_IER_DLM))) begin
            data=(dlab_comb) ? (unsigned'(8'(dlm_reg))) : (unsigned'(8'(ier_reg)));
        end
        if (addr == unsigned'(32'(REG_IIR_FCR))) begin
            if (((rx_valid_reg) != '0) && ((((ier_reg & 8'(64'h1))) != 64'h0))) begin
                data=8'hC4;
            end
            else begin
                data=8'hC1;
            end
        end
        if (addr == unsigned'(32'(REG_LCR))) begin
            data=lcr_reg;
        end
        if (addr == unsigned'(32'(REG_MCR))) begin
            data=mcr_reg;
        end
        if (addr == unsigned'(32'(REG_LSR))) begin
            data=('h60 | ((((rx_valid_reg) != '0)) ? ('h1) : ('h0)));
        end
        if (addr == unsigned'(32'(REG_MSR))) begin
            data=8'h0;
        end
        if (addr == unsigned'(32'(REG_SCR))) begin
            data=scr_reg;
        end
        read_data_comb[lane*32'h8 +:8] = 64'(data);
    end

    generate  // _assign
        assign axi_in__awready_out = ((!((write_addr_valid_reg) != '0) && !((write_resp_valid_reg) != '0)) != '0);
        assign axi_in__wready_out = ((((write_addr_valid_reg) != '0) && !((write_resp_valid_reg) != '0)) != '0);
        assign axi_in__bvalid_out = write_resp_valid_reg;
        assign axi_in__bid_out = write_id_reg;
        assign axi_in__arready_out = ((!((read_valid_reg) != '0)) != '0);
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
        logic rx_incoming;
        logic rx_available;
        logic rbr_read;
        logic direct_incoming_consumed;
        logic[7:0] rx_data;
        logic[31:0] fifo_head;
        logic[31:0] fifo_tail;
        logic[31:0] fifo_count;
        logic[31:0] qi;
        logic[DATA_WIDTH-1:0] read_data;
        uart_valid_reg_tmp = 0;
        for (qi=32'h0;qi < 32'hF;qi=qi+1) begin
            rx_fifo_reg_tmp[qi] = rx_fifo_reg[qi];
        end
        rx_fifo_head_reg_tmp = rx_fifo_head_reg;
        rx_fifo_tail_reg_tmp = rx_fifo_tail_reg;
        rx_fifo_count_reg_tmp = rx_fifo_count_reg;
        rx_incoming=uart_rx_valid_in && ((rx_fifo_count_reg < 8'(64'hF)));
        rx_available=((rx_valid_reg) != '0) || rx_incoming;
        rx_data=(((rx_valid_reg) != '0)) ? (unsigned'(8'(rx_data_reg))) : (unsigned'(8'(uart_rx_data_in)));
        fifo_head=unsigned'(8'(rx_fifo_head_reg));
        fifo_tail=unsigned'(8'(rx_fifo_tail_reg));
        fifo_count=unsigned'(8'(rx_fifo_count_reg));
        rbr_read=0;
        direct_incoming_consumed=0;
        if (rx_incoming) begin
            if (!((rx_valid_reg) != '0)) begin
                rx_data_reg_tmp = uart_rx_data_in;
                rx_valid_reg_tmp = 1;
            end
            else begin
                if (fifo_count < 32'hF) begin
                    rx_fifo_reg_tmp[fifo_tail] = uart_rx_data_in;
                    rx_fifo_tail_reg_tmp = (((fifo_tail + 32'h1)) % 32'hF);
                    rx_fifo_count_reg_tmp = (fifo_count + 32'h1);
                end
            end
            rbr_duplicate_valid_reg_tmp = 0;
        end
        if (axi_in__arvalid_in && axi_in__arready_out) begin
            addr=unsigned'(32'(axi_in__araddr_in)) & 'h7;
            lane=64'(unsigned'(32'(axi_in__araddr_in))) % ((DATA_WIDTH/64'h8));
            data=8'h0;
            if (addr == unsigned'(32'(REG_RBR_THR_DLL))) begin
                if (dlab_comb) begin
                    data=dll_reg;
                end
                else begin
                    if (((rbr_duplicate_valid_reg) != '0) && !rx_available) begin
                        data=8'h0;
                    end
                    else begin
                        data=(rx_available) ? (signed'(32'(rx_data))) : ('h0);
                    end
                end
                rbr_read=1;
            end
            if (addr == unsigned'(32'(REG_IER_DLM))) begin
                data=(dlab_comb) ? (unsigned'(8'(dlm_reg))) : (unsigned'(8'(ier_reg)));
            end
            if (addr == unsigned'(32'(REG_IIR_FCR))) begin
                if (rx_available && ((((ier_reg & 8'(64'h1))) != 64'h0))) begin
                    data=8'hC4;
                end
                else begin
                    data=8'hC1;
                end
            end
            if (addr == unsigned'(32'(REG_LCR))) begin
                data=lcr_reg;
            end
            if (addr == unsigned'(32'(REG_MCR))) begin
                data=mcr_reg;
            end
            if (addr == unsigned'(32'(REG_LSR))) begin
                data=('h60 | ((rx_available) ? ('h1) : ('h0)));
            end
            if (addr == unsigned'(32'(REG_MSR))) begin
                data=8'h0;
            end
            if (addr == unsigned'(32'(REG_SCR))) begin
                data=scr_reg;
            end
            read_data = 'h0;
            read_data[lane*32'h8 +:8] = 64'(data);
            read_data_reg_tmp = read_data;
            read_addr_reg_tmp = axi_in__araddr_in;
            read_id_reg_tmp = axi_in__arid_in;
            read_valid_reg_tmp = 1;
            if ((addr == unsigned'(32'(REG_RBR_THR_DLL))) && !dlab_comb) begin
                if (rx_available) begin
                    if (((rx_valid_reg) != '0)) begin
                        if (fifo_count != 32'h0) begin
                            rx_data_reg_tmp = rx_fifo_reg[fifo_head];
                            rx_valid_reg_tmp = 1;
                            rx_fifo_head_reg_tmp = (((fifo_head + 32'h1)) % 32'hF);
                            rx_fifo_count_reg_tmp = ((fifo_count - 32'h1) + unsigned'(32'(((rx_incoming) ? ('h1) : ('h0)))));
                        end
                        else begin
                            if (rx_incoming) begin
                                rx_data_reg_tmp = uart_rx_data_in;
                                rx_valid_reg_tmp = 1;
                                rx_fifo_tail_reg_tmp = rx_fifo_tail_reg;
                                rx_fifo_count_reg_tmp = 8'h0;
                            end
                            else begin
                                rx_valid_reg_tmp = 0;
                            end
                        end
                    end
                    else begin
                        direct_incoming_consumed=1;
                        rx_valid_reg_tmp = 0;
                    end
                    rbr_duplicate_valid_reg_tmp = 1;
                end
            end
        end
        if (rbr_read && direct_incoming_consumed) begin
            rx_valid_reg_tmp = 0;
        end
        if (((read_valid_reg) != '0) && axi_in__rready_in) begin
            read_valid_reg_tmp = 0;
        end
        if (axi_in__awvalid_in && axi_in__awready_out) begin
            write_addr_reg_tmp = axi_in__awaddr_in;
            write_id_reg_tmp = axi_in__awid_in;
            write_addr_valid_reg_tmp = 1;
        end
        if (axi_in__wvalid_in && axi_in__wready_out) begin
            addr=unsigned'(32'(write_addr_reg)) & 'h7;
            lane=64'(unsigned'(32'(write_addr_reg))) % ((DATA_WIDTH/64'h8));
            data=unsigned'(32'(axi_in__wdata_in[lane*32'h8 +:8]));
            write_addr_valid_reg_tmp = 0;
            write_resp_valid_reg_tmp = 1;
            if (addr == unsigned'(32'(REG_RBR_THR_DLL))) begin
                if (dlab_comb) begin
                    dll_reg_tmp = data;
                end
                else begin
                    uart_data_reg_tmp = data;
                    uart_valid_reg_tmp = 1;
                end
            end
            if (addr == unsigned'(32'(REG_IER_DLM))) begin
                if (dlab_comb) begin
                    dlm_reg_tmp = data;
                end
                else begin
                    ier_reg_tmp = data;
                end
            end
            if (addr == unsigned'(32'(REG_LCR))) begin
                lcr_reg_tmp = data;
            end
            if (addr == unsigned'(32'(REG_MCR))) begin
                mcr_reg_tmp = data;
            end
            if (addr == unsigned'(32'(REG_IIR_FCR))) begin
                fcr_reg_tmp = data;
                if (signed'(32'(((signed'(32'(data)) & 'h2)))) != 32'sh0) begin
                    rx_valid_reg_tmp = 0;
                    rx_fifo_head_reg_tmp = 8'h0;
                    rx_fifo_tail_reg_tmp = 8'h0;
                    rx_fifo_count_reg_tmp = 8'h0;
                    rbr_duplicate_valid_reg_tmp = 0;
                end
            end
            if (addr == unsigned'(32'(REG_SCR))) begin
                scr_reg_tmp = data;
            end
        end
        if (((write_resp_valid_reg) != '0) && axi_in__bready_in) begin
            write_resp_valid_reg_tmp = 0;
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
            fcr_reg_tmp = '0;
            scr_reg_tmp = '0;
            dll_reg_tmp = '0;
            dlm_reg_tmp = '0;
            uart_valid_reg_tmp = '0;
            uart_data_reg_tmp = '0;
            rx_valid_reg_tmp = '0;
            rx_data_reg_tmp = '0;
            rx_fifo_reg_tmp['h0] = 8'h0;
            rx_fifo_reg_tmp['h1] = 8'h0;
            rx_fifo_reg_tmp['h2] = 8'h0;
            rx_fifo_reg_tmp['h3] = 8'h0;
            rx_fifo_reg_tmp['h4] = 8'h0;
            rx_fifo_reg_tmp['h5] = 8'h0;
            rx_fifo_reg_tmp['h6] = 8'h0;
            rx_fifo_reg_tmp['h7] = 8'h0;
            rx_fifo_reg_tmp['h8] = 8'h0;
            rx_fifo_reg_tmp['h9] = 8'h0;
            rx_fifo_reg_tmp['hA] = 8'h0;
            rx_fifo_reg_tmp['hB] = 8'h0;
            rx_fifo_reg_tmp['hC] = 8'h0;
            rx_fifo_reg_tmp['hD] = 8'h0;
            rx_fifo_reg_tmp['hE] = 8'h0;
            rx_fifo_head_reg_tmp = '0;
            rx_fifo_tail_reg_tmp = '0;
            rx_fifo_count_reg_tmp = '0;
            rbr_duplicate_valid_reg_tmp = '0;
        end
    end
    endtask

    task _work_l2_clock (input logic reset);
    begin: _work_l2_clock
    end
    endtask

    always_ff @(posedge clk) begin
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
        fcr_reg_tmp = fcr_reg;
        scr_reg_tmp = scr_reg;
        dll_reg_tmp = dll_reg;
        dlm_reg_tmp = dlm_reg;
        uart_valid_reg_tmp = uart_valid_reg;
        uart_data_reg_tmp = uart_data_reg;
        rx_valid_reg_tmp = rx_valid_reg;
        rx_data_reg_tmp = rx_data_reg;
        rx_fifo_reg_tmp = rx_fifo_reg;
        rx_fifo_head_reg_tmp = rx_fifo_head_reg;
        rx_fifo_tail_reg_tmp = rx_fifo_tail_reg;
        rx_fifo_count_reg_tmp = rx_fifo_count_reg;
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
        fcr_reg <= fcr_reg_tmp;
        scr_reg <= scr_reg_tmp;
        dll_reg <= dll_reg_tmp;
        dlm_reg <= dlm_reg_tmp;
        uart_valid_reg <= uart_valid_reg_tmp;
        uart_data_reg <= uart_data_reg_tmp;
        rx_valid_reg <= rx_valid_reg_tmp;
        rx_data_reg <= rx_data_reg_tmp;
        rx_fifo_reg <= rx_fifo_reg_tmp;
        rx_fifo_head_reg <= rx_fifo_head_reg_tmp;
        rx_fifo_tail_reg <= rx_fifo_tail_reg_tmp;
        rx_fifo_count_reg <= rx_fifo_count_reg_tmp;
        rbr_duplicate_valid_reg <= rbr_duplicate_valid_reg_tmp;
    end

    always_ff @(posedge l2_clock) begin

            _work_l2_clock(reset);

    end

    assign uart_valid_out = uart_valid_reg;

    assign uart_data_out = uart_data_reg;

    assign uart_rx_ready_out = ((rx_fifo_count_reg < 8'(64'hF)) != '0);

    assign irq_out = irq_comb;


endmodule
