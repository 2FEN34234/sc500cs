`timescale 1ns/1ps
module tb_touch_focus;
 reg clk=0;always #5 clk=~clk;
 reg rst=0,vs=0,de=0;reg [23:0] data=0;
 reg [31:0] roi=0;wire [31:0] metric;wire toggle;
 focus_metric dut(.I_clk(clk),.I_rst_n(rst),.I_vsync(vs),.I_de(de),
  .I_data(data),.I_roi(roi),.O_metric(metric),.O_frame_toggle(toggle));
 task frame_start;
 begin @(negedge clk);vs=1;de=0;@(negedge clk);vs=0;end endtask
 task frame(input integer rx,input integer ry,input integer outside_pattern,input integer inside_pattern);
 integer x,y;
 begin
   for(y=0;y<1080;y=y+1) begin
     for(x=0;x<1920;x=x+1) begin
       @(negedge clk);de=1;
       if(x>=rx && x<rx+160 && y>=ry && y<ry+120)
         data=inside_pattern && (x%2) ? 24'hffffff:0;
       else data=outside_pattern && ((x+y)%2) ? 24'hffffff:0;
     end
     @(negedge clk);de=0;
   end
 end endtask
 initial begin
   repeat(4) @(negedge clk);rst=1;
   // Center ROI: outside checkerboard must contribute nothing, including
   // transitions from outside into the ROI and between adjacent scanlines.
   roi=32'h81e00370;frame_start();
   frame(880,480,1,0);frame_start();
   if(metric!==0) $fatal(1,"Outside image contaminated metric: %d",metric);
   // Changing the requested ROI mid-frame must wait until the next frame.
   roi=32'h83c006e0;
   frame(880,480,0,1);frame_start();
   if(metric!==159*120*253) $fatal(1,"Centered ROI or row boundary wrong: %d",metric);
   frame(1760,960,1,1);frame_start();
   if(metric!==159*120*253) $fatal(1,"Bottom-right ROI wrong: %d",metric);
   $display("PASS focus metric: local contrast only, frame-atomic ROI, exact 160x120 bounds, no row-crossing contribution");
   $finish;
 end
endmodule
