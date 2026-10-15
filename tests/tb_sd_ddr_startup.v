`timescale 1ns/1ps
// Exercise the actual generated FIFO, including its two-clock reset release.
module tb_sd_ddr_startup;
reg clk=0,rst=0,active=0;always #5 clk=~clk;
wire ready,en;wire [2:0] cmd;wire [24:0] addr;wire [127:0] data;
integer sent=0,received=0,trial,bad_addresses=0;
wire re=active && ready && sent<240;
mc_to_user_interface dut(.I_clk(clk),.I_rst_n(rst),
 .I_ddr_user_wr_en(1'b0),.I_ddr_user_rd_en(re),.I_ddr_user_addr(sent*8),
 .I_ddr_user_wr_data(128'd0),.O_ddr_user_ready(ready),
 .O_ddr_user_rd_valid(),.O_ddr_user_rd_data(),.O_mc_app_en(en),
 .O_mc_app_addr(addr),.O_mc_app_cmd(cmd),.I_mc_app_rdy(1'b1),
 .O_mc_app_wdf_wren(),.O_mc_app_wdf_data(data),
 .O_mc_app_wdf_end(),.O_mc_app_wdf_mask(),.I_mc_app_wdf_rdy(1'b1),
 .I_mc_app_rd_data(128'd0),.I_mc_app_rd_data_end(1'b0),.I_mc_app_rd_data_valid(1'b0));
always @(posedge clk) begin
 if(!rst) begin active<=0;sent<=0;received<=0;bad_addresses<=0;end
 else begin
  active<=1; // video_out sets its burst active on the first released clock
  if(re) sent<=sent+1;
  if(en) begin
   if(cmd!=1) $fatal(1,"unexpected command");
   if(addr!=received*8) bad_addresses<=bad_addresses+1;
   received<=received+1;
  end
 end
end
initial begin
 for(trial=0;trial<3;trial=trial+1) begin
  @(negedge clk);rst=0;repeat(4) @(negedge clk);rst=1;
  repeat(600) @(negedge clk);
  if(sent!=240 || received!=240) $fatal(1,"RESET RELEASE LOST READ: sent=%d accepted=%d permanently_pending=%d",sent,received,sent-received);
  if(bad_addresses) $fatal(1,"read addresses reordered or duplicated");
 end
 $display("PASS actual vendor command FIFO: no startup read loss across three resets");$finish;
end
endmodule
