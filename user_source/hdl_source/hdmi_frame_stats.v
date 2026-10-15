`ifndef HDMI_FRAME_STATS_V
`define HDMI_FRAME_STATS_V
// All counters stay in the HDMI pixel domain. Decimal counters avoid a
// binary-to-decimal divider in the pixel/OSD path. FPS is a one-second count.
module hdmi_frame_stats #(
    parameter integer PIXEL_CLOCK_HZ = 75000000
)(
    input wire clk, rst_n, vsync,
    output reg [31:0] frames_bcd,
    output reg [11:0] fps_bcd,
    output reg fps_valid
);
    localparam integer TIMER_BITS = (PIXEL_CLOCK_HZ > 1) ? $clog2(PIXEL_CLOCK_HZ) : 1;
    reg [TIMER_BITS-1:0] ticks;
    reg vs_d;
    reg [11:0] window_frames;
    wire frame_start = vsync && !vs_d;
    function [31:0] increment_bcd;
        input [31:0] value;
        integer digit;
        reg carry;
        begin
            increment_bcd=value; carry=1;
            for(digit=0;digit<8;digit=digit+1) begin
                if(carry) begin
                    if(value[digit*4 +: 4]==9) increment_bcd[digit*4 +: 4]=0;
                    else begin increment_bcd[digit*4 +: 4]=value[digit*4 +: 4]+1'b1; carry=0; end
                end
            end
        end
    endfunction
    wire [31:0] window_next = increment_bcd({20'd0,window_frames});
    wire [11:0] window_with_frame = frame_start && window_frames!=12'h999 ?
                                    window_next[11:0] : window_frames;
    always @(posedge clk or negedge rst_n) begin
        if(!rst_n) begin
            ticks<=0; vs_d<=0; frames_bcd<=0; window_frames<=0; fps_bcd<=0; fps_valid<=0;
        end else begin
            vs_d<=vsync;
            if(frame_start && frames_bcd!=32'h99999999) frames_bcd<=increment_bcd(frames_bcd);
            if(ticks==PIXEL_CLOCK_HZ-1) begin
                ticks<=0; fps_bcd<=window_with_frame; fps_valid<=1; window_frames<=0;
            end else begin
                ticks<=ticks+1'b1; window_frames<=window_with_frame;
            end
        end
    end
endmodule
`endif
