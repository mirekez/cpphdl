module CppGraphNets(input logic enable_i,
                    input logic [2:0] select_i,
                    output logic [6:0] tree_o);
  logic [6:0] tree;
  assign tree[0] = enable_i;
  for (genvar left = 0; left < 3; left++) begin
    assign tree[2*left+1] = tree[left] & ~select_i[left];
  end
  for (genvar right = 0; right < 3; right++) begin
    assign tree[2*right+2] = tree[right] & select_i[right];
  end
  assign tree_o = tree;
endmodule
