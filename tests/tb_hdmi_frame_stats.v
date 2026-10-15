`timescale 1ns/1ps
module tb_hdmi_frame_stats;
    reg clk=0; always #5 clk=~clk;
    reg rst=0,vs=0;
    wire [31:0] frames;
    wire [11:0] fps;
    wire valid;
    hdmi_frame_stats #(.PIXEL_CLOCK_HZ(100)) dut(clk,rst,vs,frames,fps,valid);
    integer i,total=0,window=0,ticks=0,last_fps=0;
    reg previous=0;
    function integer decimal;
        input [31:0] bcd;
        integer n,m;
        begin
            decimal=0;m=1;
            for(n=0;n<8;n=n+1) begin
                if(bcd[n*4 +:4]>9) $fatal(1,"Invalid BCD");
                decimal=decimal+bcd[n*4 +:4]*m;m=m*10;
            end
        end
    endfunction
    initial begin
        repeat(2) @(negedge clk);rst=1;
        for(i=0;i<22000;i=i+1) begin
            vs=i<21000 && i%7<3; // wide VS pulses, non-integral window period
            if(vs && !previous) begin total=total+1;window=window+1;end
            previous=vs;ticks=ticks+1;
            if(ticks==100) begin last_fps=window;window=0;ticks=0;end
            @(posedge clk);#1;
            if(decimal(frames)!=total || decimal({20'd0,fps})!=last_fps)
                $fatal(1,"Counter/window mismatch at %d frames=%h fps=%h",i,frames,fps);
            if(valid!==(i>=99)) $fatal(1,"Startup valid");
            @(negedge clk);
        end
        if(fps!=0) $fatal(1,"Idle output must report zero FPS");
        rst=0;@(negedge clk);
        if(frames || fps || valid) $fatal(1,"Reset failed");
        rst=1;dut.frames_bcd=32'h99999998;
        vs=1;@(negedge clk);vs=0;@(negedge clk);vs=1;@(negedge clk);
        if(frames!==32'h99999999) $fatal(1,"Frame saturation");
        $display("PASS HDMI frame starts, wide VS, decimal carry, one-second boundary, idle, reset, saturation");
        $finish;
    end
endmodule
