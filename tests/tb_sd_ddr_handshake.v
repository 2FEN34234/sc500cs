`timescale 1ns/1ps
// Actual bridge + actual board DDR wrapper, with independently stalled PAXI
// read-address, write-address and write-data channels.
module tb_sd_ddr_handshake;
reg clk=0,rst=0,we=0,re=0;always #5 clk=~clk;
wire ready,rv,en,ar,wr,wren,wend;wire [2:0] cmd;
wire [24:0] addr;wire [127:0] wd,rd;wire [31:0] mask;
mc_to_user_interface bridge(.I_clk(clk),.I_rst_n(rst),
 .I_ddr_user_wr_en(we),.I_ddr_user_rd_en(re),.I_ddr_user_addr(25'd80),
 .I_ddr_user_wr_data(128'h1234),.O_ddr_user_ready(ready),
 .O_ddr_user_rd_valid(),.O_ddr_user_rd_data(),.O_mc_app_en(en),
 .O_mc_app_addr(addr),.O_mc_app_cmd(cmd),.I_mc_app_rdy(ar),
 .O_mc_app_wdf_wren(wren),.O_mc_app_wdf_data(wd),
 .O_mc_app_wdf_end(wend),.O_mc_app_wdf_mask(mask),.I_mc_app_wdf_rdy(wr),
 .I_mc_app_rd_data(rd),.I_mc_app_rd_data_end(1'b0),.I_mc_app_rd_data_valid(rv));
ph1p35_324_ddr_wrapper wrap(.I_sys_clk(clk),.I_sys_rst_n(rst),
 .I_mc_app_addr(addr),.I_mc_app_cmd(cmd),.I_mc_app_en(en),
 .I_mc_app_wdf_data(wd),.I_mc_app_wdf_end(wend),.I_mc_app_wdf_mask(mask[15:0]),
 .I_mc_app_wdf_wren(wren),.O_mc_app_rd_data(rd),.O_mc_app_rd_data_valid(rv),
 .O_mc_app_rdy(ar),.O_mc_app_wdf_rdy(wr));
integer accepted_reads=0,accepted_writes=0;
always @(posedge clk) if(rst && en) begin
 if(cmd[0]) begin
   if(!wrap.u_ddr2.allow_ar) $fatal(1,"READ lost: accepted using write-channel ready");
   accepted_reads=accepted_reads+1;
 end else begin
   if(!wrap.u_ddr2.allow_aw || !wrap.u_ddr2.allow_w)
     $fatal(1,"WRITE lost: address/data not both ready");
   accepted_writes=accepted_writes+1;
 end
end
task enqueue;input isread;
begin @(negedge clk);while(!ready) @(negedge clk);re=isread;we=!isread;
 @(negedge clk);re=0;we=0;end endtask
initial begin
 repeat(3) @(negedge clk);rst=1;
 wrap.u_ddr2.allow_aw=1;wrap.u_ddr2.allow_w=1;wrap.u_ddr2.allow_ar=0;
 enqueue(1);repeat(10) @(negedge clk);
 if(accepted_reads!=0) $fatal(1,"read popped during stall");
 wrap.u_ddr2.allow_ar=1;wrap.u_ddr2.allow_aw=0;
 repeat(10) @(negedge clk);
 if(accepted_reads!=1) $fatal(1,"read depends on write readiness");
 wrap.u_ddr2.allow_aw=1;wrap.u_ddr2.allow_w=0;
 enqueue(0);repeat(10) @(negedge clk);
 if(accepted_writes!=0) $fatal(1,"write popped without data ready");
 wrap.u_ddr2.allow_w=1;repeat(10) @(negedge clk);
 if(accepted_writes!=1) $fatal(1,"write failed to resume");
 $display("PASS independent DDR read/write/data backpressure, queued commands preserved");$finish;
end
initial begin #10000;$fatal(1,"timeout");end
endmodule

module w155_d512_fifo(input clk,rst,we,re,input [154:0] di,
 output afull,full_flag,valid,empty_flag,aempty,output [9:0] wrusedw,rdusedw,
 output [154:0] dout);
reg [154:0] mem[0:511];reg [9:0] wp=0,rp=0;
assign wrusedw=wp-rp;assign rdusedw=wp-rp;
assign afull=(wp-rp)>=500;assign full_flag=(wp-rp)==512;
assign empty_flag=wp==rp;assign valid=!empty_flag;assign dout=mem[rp[8:0]];
always @(posedge clk) if(rst) begin wp<=0;rp<=0;end else begin
 if(we) begin mem[wp[8:0]]<=di;wp<=wp+1'b1;end
 if(re && !empty_flag) rp<=rp+1'b1;
end
endmodule

module ddr2(input ref_clk,sys_rst_n,apb_pclk,apb_prst_n,
 output dhi_clk,cfg_clk,ddrphy_rst_n,ddrphy_rdy,
 input [24:0] paxi_awaddr,paxi_araddr,
 input paxi_awvalid,paxi_wvalid,paxi_wlast,paxi_arvalid,paxi_rready,
 input [127:0] paxi_wdata,input [15:0] paxi_wstrb,
 output paxi_awready,paxi_arready,paxi_wready,paxi_rlast,paxi_rvalid,
 output [127:0] paxi_rdata,output ddr_ck_p,ddr_ck_n,ddr_cke,ddr_odt,
 ddr_cs_n,ddr_ras_n,ddr_cas_n,ddr_we_n,output [1:0] ddr_ba,
 output [12:0] ddr_addr,inout [1:0] ddr_dqs_p,ddr_dqs_n,ddr_dm,
 inout [15:0] ddr_dq);
reg allow_ar=0,allow_aw=0,allow_w=0;
assign dhi_clk=ref_clk;
assign paxi_arready=allow_ar;assign paxi_awready=allow_aw;assign paxi_wready=allow_w;
assign paxi_rvalid=0;assign paxi_rlast=0;assign paxi_rdata=0;
endmodule
