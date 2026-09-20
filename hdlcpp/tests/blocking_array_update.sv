module blocking_array_update (
    input logic valid_i,
    input logic [31:0] data_i,
    output logic front_valid_o,
    output logic [31:0] front_data_o,
    output logic independent_ready_o
);
    typedef struct packed {
        logic valid;
        logic [31:0] data;
    } entry_t;
    entry_t [1:0] next;
    always_comb begin
        next = '0;
        if (!next[1].valid) begin
            next[1].valid = valid_i;
            next[1].data = data_i;
        end
        if (!next[0].valid) begin
            next[0] = next[1];
            next[1].valid = 0;
        end
    end
    assign front_valid_o = next[0].valid;
    assign front_data_o = next[0].data;

    entry_t [1:0] channels;
    logic ready;
    always_comb begin
        channels = '0;
        channels[0].data = '0;
        channels[0].data = {31'b0, ready};
        channels[0].valid = valid_i;
    end
    assign ready = !channels[0].valid;
    assign independent_ready_o = channels[0].data[0];
endmodule
