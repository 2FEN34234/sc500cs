`timescale 1ns/1ps
module tb_picture_control;
    reg src=0,dst=0,rst=0,fs=0;
    always #7 src=~src;
    always #5 dst=~dst;
    reg [15:0] requested=16'h1050;
    wire [15:0] active;
    picture_control_cdc cdc(src,dst,rst,requested,fs,active);
    reg valid=0,last=0,dfs=0;
    reg [95:0] data=0;
    reg [1:0] strength=0;
    wire ov,ol,of;
    wire [95:0] od;
    picture_denoise #(.WORDS(2)) dn(dst,rst,dfs,valid,last,data,strength,of,ov,ol,od);
    task frame;
        begin @(negedge dst); fs=1; @(negedge dst); fs=0; end
    endtask
    task beat;
        input [7:0] value;
        input endline;
        begin
            @(negedge dst); valid=1; data={12{value}}; last=endline;
            @(negedge dst); valid=0; last=0;
            @(negedge dst);
            if(!ov || ol!==endline) $fatal(1,"denoise valid/last mismatch");
        end
    endtask
    initial begin
        #31; rst=1;
        requested=16'hffff;
        #200; if(active!==16'h1050) $fatal(1,"mid-frame update");
        frame; if(active!==16'hffff) $fatal(1,"frame apply failed");
        requested=16'h0101; #14; requested=16'h2222; #14; requested=16'h4060;
        #300; if(active!==16'hffff) $fatal(1,"changed without frame");
        frame; if(active!==16'h4060) $fatal(1,"latest mailbox value lost");
        beat(8'd100,0); if(od!=={12{8'd100}}) $fatal(1,"off is not exact");
        beat(8'd100,1);
        strength=3;
        beat(8'd120,0); if(od[95:88]!==8'd110) $fatal(1,"vertical denoise not applied %d",od[95:88]);
        beat(8'd240,1); if(od[95:88]!==8'd240) $fatal(1,"strong edge blurred");
        @(negedge dst); dfs=1; @(negedge dst); dfs=0;
        beat(8'd20,0); if(od[95:88]!==8'd20) $fatal(1,"previous frame leaked");
        $display("PASS picture mailbox, frame boundary, bypass, denoise, edge and first-line protection");
        $finish;
    end
    initial begin #10000; $fatal(1,"timeout"); end
endmodule
