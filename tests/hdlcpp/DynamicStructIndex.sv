module DynamicStructIndex (
    input logic clk,
    input logic rst_n,
    input logic bank,
    input logic [1:0] index,
    input logic [2:0] write_enable,
    input logic [12:0] tag,
    input logic [31:0] payload,
    input logic valid,
    output logic [45:0] row,
    output logic [31:0] field_payload,
    output logic [12:0] field_tag,
    output logic field_valid,
    output logic [45:0] packed_row,
    output logic [31:0] packed_payload
);
    typedef struct packed {
        logic [12:0] tag;
        logic [31:0] payload;
        logic valid;
    } row_t;
    row_t rows [2][4];
    row_t [1:0][3:0] packed_rows;
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            for (int unsigned outer = 0; outer < 2; outer++)
                for (int unsigned inner = 0; inner < 4; inner++)
                    rows[outer][inner] <= 0;
            packed_rows <= 0;
        end else begin
            if (write_enable[0]) begin
                rows[bank][index].payload <= payload;
                packed_rows[bank][index].payload <= payload;
            end
            if (write_enable[1]) begin
                rows[bank][index].tag <= tag;
                packed_rows[bank][index].tag <= tag;
            end
            if (write_enable[2]) begin
                rows[bank][index].valid <= valid;
                packed_rows[bank][index].valid <= valid;
            end
        end
    end
    assign row = rows[bank][index];
    assign field_payload = rows[bank][index].payload;
    assign field_tag = rows[bank][index].tag;
    assign field_valid = rows[bank][index].valid;
    assign packed_row = packed_rows[bank][index];
    assign packed_payload = packed_rows[bank][index].payload;
endmodule
