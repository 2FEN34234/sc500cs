`timescale 1ns/1ps
module tb_sd_recorder;
reg a=0,p=0,d=0,rst=0;
always #10 a=~a;always #7 p=~p;always #4 d=~d;
reg [7:0] addr=0;reg sel=0,en=0,write=0;reg [31:0] wd=0;wire [31:0] q;
reg vs=0,de=0;reg [23:0] rgb=0;wire sc,mo,cs;
reg vi=0,vo=0,vr=0,ready=1,rv=0;reg [127:0] rd=0;
wire blockv,we,re,vvalid;wire [24:0] ma;wire [127:0] md;wire [31:0] ui;
sd_recorder #(.WIDTH(24),.HEIGHT(12),.BASE(128)) dut(
 .rst_n(rst),.apb_clk(a),.pixel_clk(p),.ddr_clk(d),.addr(addr),.sel(sel),.enable(en),.write(write),.wdata(wd),.rdata(q),
 .vs(vs),.de(de),.rgb(rgb),.sd_clk(sc),.sd_mosi(mo),.sd_cs(cs),.sd_miso(~mo),
 .vi_busy(vi),.vo_busy(vo),.vo_read(vr),.ready(ready),.rd_valid(rv),.rd_data(rd),
 .block_video(blockv),.wr_en(we),.rd_en(re),.mem_addr(ma),.mem_data(md),.video_rd_valid(vvalid),.ui_status(ui));
reg [127:0] mem[0:7];integer writes=0,reads=0,lat=0,readaddr=0,edges=0;
reg [31:0] expected;integer xx,yy,k,n,frame_no;
always @(posedge sc) edges=edges+1;
always @(posedge d) if(rst) begin
 if(we) begin
   if(vi || vo || vr) $fatal(1,"DDR master collision");
   if(ma<128 || ma>=192) $fatal(1,"capture address outside reserved buffer");
   mem[(ma-128)/8]<=md;writes=writes+1;
 end
 if(re) begin
   if(vi || vo || vr) $fatal(1,"DDR read collision");
   readaddr=(ma-128)/8;lat=5;reads=reads+1;
 end
end
always @(negedge d) begin
 rv=0;
 if(lat>0) begin lat=lat-1;if(lat==0) begin rv=1;rd=mem[readaddr];end end
end
task put;input [7:0] regaddr;input [31:0] data;
 begin @(negedge a);addr=regaddr;wd=data;sel=1;en=0;write=1;
 @(negedge a);en=1;@(negedge a);sel=0;en=0;write=0;end
endtask
task frame;input integer color;
 begin
 @(negedge p);vs=1;repeat(5) @(negedge p);vs=0;repeat(5) @(negedge p);
 for(yy=0;yy<12;yy=yy+1) begin
   for(xx=0;xx<24;xx=xx+1) begin de=1;rgb={color[7:0],yy[7:0],xx[7:0]};@(negedge p);end
   de=0;repeat(4) @(negedge p);
 end
 end
endtask
initial begin
 #65;rst=1;repeat(30) @(negedge a);
 put('h40,2);put('h44,0);put('h48,'ha5);
 addr='h4c;#1;wait(q[0]==0);#1;
 if(q[15:8]!==8'h5a || edges!=8 || cs!=0 || sc!=0) $fatal(1,"SPI mode0 byte or CS failed");
 put('h44,1);
 // Both captures use a new frame boundary; subsequent live frames cannot alter it.
 for(frame_no=1;frame_no<=2;frame_no=frame_no+1) begin
   writes=0;put(4,1);repeat(20) @(negedge a);
   vi=1;fork
     begin repeat(100) @(negedge d);vi=0;end
     frame(frame_no);
   join
   repeat(100) @(negedge a);addr=4;#1;
   if(q[2:0]!==3'b010 || writes!=8) $fatal(1,"capture incomplete %h writes=%d",q,writes);
   frame(99);
   for(k=0;k<8;k=k+1) begin
     put('h20,k);addr='h24;#1;wait(q[0]==0);
     for(n=0;n<4;n=n+1) begin
       addr='h30+4*n;#1;
       xx=((k*4+n)%8)*3;yy=((k*4+n)/8)*3;
       expected={8'd0,frame_no[7:0],yy[7:0],xx[7:0]};
       if(q!==expected) $fatal(1,"sample order %d/%d got=%h expected=%h",k,n,q,expected);
     end
   end
 end
 // A malformed short frame is never reported as a complete photograph.
 put(4,1);repeat(20) @(negedge a);
 @(negedge p);vs=1;repeat(5) @(negedge p);vs=0;
 repeat(5) @(negedge p);vs=1;repeat(10) @(negedge a);addr=4;#1;
 if(!q[2] || q[1]) $fatal(1,"incomplete frame not rejected");
 $display("PASS SPI mode0, held CS, async snapshot, 3x downsample, DDR reads, immutable frame, recapture, malformed frame");$finish;
end
initial begin #1000000;$fatal(1,"timeout");end
endmodule
