module ExternalPointerChecks;
  reg clk=0, reset=1;
  always #5 clk=~clk;
  reg command_valid_in=0, response_ready_in=0;
  reg [31:0] operation_in=0,index_in=0,value_in=0;
  wire command_ready_out,response_valid_out;
  wire [63:0] result_out;
  wire [31:0] fault_out;
  wire memory_out__valid_out,memory_out__write_out,memory_out__ready_out;
  wire [31:0] memory_out__addr_out;
  wire [7:0] memory_out__size_out;
  wire [63:0] memory_out__data_out;
  reg memory_out__ready_in=0,memory_out__valid_in=0,memory_out__error_in=0;
  reg [63:0] memory_out__data_in=0;
  `MEMORY_DUT dut(.*);
  reg [7:0] bytes[64];
  reg busy=0,write_pending=0;
  reg [31:0] address;
  reg [7:0] count;
  reg [63:0] data;
  int cycles=0,delay_left=0,reads=0,writes=0;
  reg held=0;
  reg [104:0] held_request;
  // Deterministic variable latency and request backpressure, including long waits.
  always @(negedge clk) begin
    memory_out__ready_in = !reset && !busy && cycles%7 >= 3;
  end
  always @(posedge clk) begin
    cycles <= cycles+1;
    if (cycles>10000) $fatal(1,"external pointer watchdog");
    if (reset) begin busy<=0; memory_out__valid_in<=0; held<=0; end
    else begin
      if (held && (!memory_out__valid_out || held_request !== {memory_out__write_out,memory_out__addr_out,memory_out__size_out,memory_out__data_out}))
        $fatal(1,"request changed under backpressure");
      held <= memory_out__valid_out && !memory_out__ready_in;
      held_request <= {memory_out__write_out,memory_out__addr_out,memory_out__size_out,memory_out__data_out};
      if(memory_out__valid_out && memory_out__ready_in) begin
        if(busy) $fatal(1,"duplicate request");
        busy<=1; address<=memory_out__addr_out; count<=memory_out__size_out;
        data<=memory_out__data_out; write_pending<=memory_out__write_out;
        delay_left<=1+cycles%13;
        if(memory_out__write_out) writes<=writes+1; else reads<=reads+1;
      end
      if(busy && !memory_out__valid_in) begin
        if(delay_left!=0) delay_left<=delay_left-1;
        else begin
          memory_out__valid_in<=1;
          memory_out__error_in<=address<4096 || address+count>4160;
          memory_out__data_in<=0;
          if(address>=4096 && address+count<=4160) begin
            for(int b=0;b<8;b=b+1) if(b<count) begin
              if(write_pending) bytes[address-4096+b]<=data[8*b+:8];
              else memory_out__data_in[8*b+:8]<=bytes[address-4096+b];
            end
          end
        end
      end
      if(memory_out__valid_in && memory_out__ready_out) begin busy<=0; memory_out__valid_in<=0; end
    end
  end
  task automatic restart;
    @(negedge clk); reset=1; command_valid_in=0; response_ready_in=0;
    repeat(3) @(negedge clk);
    reset=0;
    while(!command_ready_out) @(negedge clk);
  endtask
  task automatic command(input int op,idx,val,input reg [63:0] expected,input int fault_expected=0);
    @(negedge clk); operation_in=op; index_in=idx; value_in=val; command_valid_in=1;
    do @(posedge clk); while(!command_ready_out);
    @(negedge clk); command_valid_in=0; operation_in=99; index_in=99; value_in=99;
    while(!response_valid_out) @(negedge clk);
    repeat(8) begin
      if(fault_out!=fault_expected || (!fault_expected && result_out!=expected))
        $fatal(1,"op %0d got %h fault %d expected %h fault %d",op,result_out,fault_out,expected,fault_expected);
      if(!response_valid_out) $fatal(1,"response lost under backpressure");
      @(negedge clk);
    end
    response_ready_in=1; @(negedge clk); response_ready_in=0;
  endtask
  initial begin
    for(int i=0;i<64;i=i+1) bytes[i]=0;
    bytes[0]=10; bytes[8]=20; bytes[16]=30; bytes[24]=40;
    restart();
    command(0,2,7,37); command(2,4,0,100);
    command(1,4,11,22); command(4,3,2,42);
    if(reads!=8 || writes!=2) $fatal(1,"unexpected transaction count %d/%d",reads,writes);
    command(5,33,171,171); command(6,18,65534,64'hfffffffffffffffe);
    command(3,4097,0,0,3); // Misalignment must not reach the controller.
    if(reads!=10 || writes!=4) $fatal(1,"issued misaligned access or missed narrow transfer");
    restart(); command(3,8192,0,0,5); // Controller error.
    restart();
    @(negedge clk); operation_in=0; index_in=0; value_in=0; command_valid_in=1;
    @(negedge clk); command_valid_in=0;
    wait(busy); restart(); // Controller and requester reset together.
    repeat(30) begin @(negedge clk); if(response_valid_out) $fatal(1,"stale response after reset"); end
    command(0,0,1,11);
    $display("PASS: external pointer delayed reads/writes, aliases, loops, stalls, faults, reset");
    $finish;
  end
endmodule
