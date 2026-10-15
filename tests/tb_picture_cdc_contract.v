`timescale 1ns/1ps
module tb_picture_cdc_contract;
    reg src=0,dst=0,rst=0,frame=0;
    always #10 src=~src;
    always #4.444 dst=~dst;
    reg [15:0] requested=16'h1050;
    wire [15:0] active;
    picture_control_cdc dut(src,dst,rst,requested,frame,active);
    realtime changed=0, captured=0;
    integer captures=0;
    reg [15:0] previous=16'h1050;
    reg busy_before;
    always @(posedge src) begin
        busy_before=(dut.ack2!=dut.req);
        #0.001;
        if(rst && dut.held!=previous) begin
            if(busy_before) $fatal(1,"bundle changed before acknowledgement");
            if(captured!=0 && $realtime-captured<20.0)
                $fatal(1,"insufficient destination hold margin");
            changed=$realtime;
        end
        previous=dut.held;
    end
    always @(posedge dst) if(rst && dut.req3!=dut.ack) begin
        // With three request stages, capture is at least three destination
        // periods after source change; an 8ns data-flight bound is conservative.
        if($realtime-changed<26.66) $fatal(1,"insufficient bundle settling time");
        captured=$realtime;captures=captures+1;
    end
    integer i;
    initial begin
        #37;rst=1;
        for(i=0;i<500;i=i+1) begin
            #13;requested=$random;
            frame=(i%9==0);
        end
        frame=1;
        #200;
        if(active!==requested || captures<20) $fatal(1,"coalesced value/capture lost");
        $display("PASS CDC bundle held until ack; >=3 destination cycles settling; %0d captures",captures);
        $finish;
    end
endmodule
