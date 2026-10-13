// Small ASCII panel and geometry for the red-target test gear.
// All overlays use the existing mixer pipeline; BCD conversion is serial.
module red_tracking_osd #(
    parameter integer IMG_WIDTH=1920, IMG_HEIGHT=1080,
    parameter integer DEAD_X=48, DEAD_Y=36
)(
    input wire I_clk,I_rst_n,I_enable,I_de,I_new_result,I_valid,
    input wire [11:0] I_x,I_y,I_x0,I_y0,I_x1,I_y1,I_cx,I_cy,
    input wire [1:0] I_state,I_dir_x,I_dir_y,I_level_x,I_level_y,
    input wire signed [12:0] I_error_x,I_error_y,
    input wire [6:0] I_area_percent,
    output reg O_hit,
    output reg [23:0] O_color
);
    reg [9:0] shift_x,shift_y,shift_area;
    reg [11:0] work_x,work_y,work_area,bcd_x,bcd_y,bcd_area;
    reg [3:0] convert_count;
    reg convert_busy;
    reg [11:0] adjust_x,adjust_y,adjust_area;
    integer d;
    always @(*) begin
        adjust_x=work_x;adjust_y=work_y;adjust_area=work_area;
        for(d=0;d<3;d=d+1) begin
            if(work_x[d*4 +: 4]>=5) adjust_x[d*4 +: 4]=work_x[d*4 +: 4]+4'd3;
            if(work_y[d*4 +: 4]>=5) adjust_y[d*4 +: 4]=work_y[d*4 +: 4]+4'd3;
            if(work_area[d*4 +: 4]>=5) adjust_area[d*4 +: 4]=work_area[d*4 +: 4]+4'd3;
        end
    end
    always @(posedge I_clk or negedge I_rst_n) begin
        if(!I_rst_n) begin
            shift_x<=0;shift_y<=0;shift_area<=0;work_x<=0;work_y<=0;work_area<=0;
            bcd_x<=0;bcd_y<=0;bcd_area<=0;convert_count<=0;convert_busy<=0;
        end else if(!I_enable) begin
            bcd_x<=0;bcd_y<=0;bcd_area<=0;convert_busy<=0;
        end else if(I_new_result) begin
            shift_x<=I_error_x[12] ? -I_error_x : I_error_x;
            shift_y<=I_error_y[12] ? -I_error_y : I_error_y;
            shift_area<={3'd0,I_area_percent};
            work_x<=0;work_y<=0;work_area<=0;convert_count<=0;convert_busy<=1;
        end else if(convert_busy) begin
            work_x<={adjust_x[10:0],shift_x[9]};
            work_y<={adjust_y[10:0],shift_y[9]};
            work_area<={adjust_area[10:0],shift_area[9]};
            shift_x<=shift_x<<1;shift_y<=shift_y<<1;shift_area<=shift_area<<1;
            if(convert_count==9) begin
                bcd_x<={adjust_x[10:0],shift_x[9]};bcd_y<={adjust_y[10:0],shift_y[9]};
                bcd_area<={adjust_area[10:0],shift_area[9]};convert_busy<=0;
            end else convert_count<=convert_count+1'b1;
        end
    end
    function [34:0] glyph;
        input [7:0] c;
        begin
            case(c)
                "0":glyph=35'b01110_10001_10011_10101_11001_10001_01110;
                "1":glyph=35'b00100_01100_00100_00100_00100_00100_01110;
                "2":glyph=35'b01110_10001_00001_00010_00100_01000_11111;
                "3":glyph=35'b11110_00001_00001_01110_00001_00001_11110;
                "4":glyph=35'b00010_00110_01010_10010_11111_00010_00010;
                "5":glyph=35'b11111_10000_10000_11110_00001_00001_11110;
                "6":glyph=35'b01110_10000_10000_11110_10001_10001_01110;
                "7":glyph=35'b11111_00001_00010_00100_01000_01000_01000;
                "8":glyph=35'b01110_10001_10001_01110_10001_10001_01110;
                "9":glyph=35'b01110_10001_10001_01111_00001_00001_01110;
                "A":glyph=35'b01110_10001_10001_11111_10001_10001_10001;
                "C":glyph=35'b01111_10000_10000_10000_10000_10000_01111;
                "D":glyph=35'b11110_10001_10001_10001_10001_10001_11110;
                "E":glyph=35'b11111_10000_10000_11110_10000_10000_11111;
                "H":glyph=35'b10001_10001_10001_11111_10001_10001_10001;
                "K":glyph=35'b10001_10010_10100_11000_10100_10010_10001;
                "L":glyph=35'b10000_10000_10000_10000_10000_10000_11111;
                "O":glyph=35'b01110_10001_10001_10001_10001_10001_01110;
                "R":glyph=35'b11110_10001_10001_11110_10100_10010_10001;
                "S":glyph=35'b01111_10000_10000_01110_00001_00001_11110;
                "T":glyph=35'b11111_00100_00100_00100_00100_00100_00100;
                "U":glyph=35'b10001_10001_10001_10001_10001_10001_01110;
                "X":glyph=35'b10001_10001_01010_00100_01010_10001_10001;
                "Y":glyph=35'b10001_10001_01010_00100_00100_00100_00100;
                "%":glyph=35'b11001_11010_00100_01000_10110_00110_00000;
                "+":glyph=35'b00000_00100_00100_11111_00100_00100_00000;
                "-":glyph=35'b00000_00000_00000_11111_00000_00000_00000;
                default:glyph=0;
            endcase
        end
    endfunction
    reg [11:0] lx,ly;
    reg [3:0] ch;
    reg [2:0] text_row,col,row;
    reg [7:0] c;
    reg [34:0] bits;
    reg [5:0] bit_index;
    wire [23:0] status_colour = I_state==1 ? 24'h00ff80 : (I_state==2 ? 24'hff6000 : 24'hffff00);
    localparam integer CX=IMG_WIDTH/2,CY=IMG_HEIGHT/2;
    always @(*) begin
        O_hit=0;O_color=0;lx=I_x-12'd24;ly=I_y-12'd24;
        ch=lx[8:5];text_row=ly[7:5];col=lx[4:2];row=ly[4:2];c=" ";bits=0;bit_index=0;
        if(I_enable && I_de) begin
            // Fixed reference cross and outer (start-moving) dead zone.
            if((I_x>=CX-12 && I_x<=CX+12 && I_y>=CY-1 && I_y<=CY+1) ||
               (I_y>=CY-12 && I_y<=CY+12 && I_x>=CX-1 && I_x<=CX+1)) begin O_hit=1;O_color=24'hffffff;end
            if(I_x>=CX-DEAD_X && I_x<=CX+DEAD_X && I_y>=CY-DEAD_Y && I_y<=CY+DEAD_Y &&
               (I_x==CX-DEAD_X || I_x==CX+DEAD_X || I_y==CY-DEAD_Y || I_y==CY+DEAD_Y)) begin O_hit=1;O_color=24'h00bfff;end
            if(I_valid) begin
                if(I_x>=I_x0 && I_x<=I_x1 && I_y>=I_y0 && I_y<=I_y1 &&
                   (I_x-I_x0<2 || I_x1-I_x<2 || I_y-I_y0<2 || I_y1-I_y<2)) begin O_hit=1;O_color=24'h00ff80;end
                if((I_x+8>=I_cx && I_x<=I_cx+8 && I_y+1>=I_cy && I_y<=I_cy+1) ||
                   (I_y+8>=I_cy && I_y<=I_cy+8 && I_x+1>=I_cx && I_x<=I_cx+1)) begin O_hit=1;O_color=24'hffff00;end
            end
            // 13 columns x 5 rows, each character cell 32x32, no division.
            if(I_x>=24 && I_x<440 && I_y>=24 && I_y<184) begin
                O_hit=1;O_color=24'h101010;
                case(text_row)
                    0:case(ch)
                        0:c="R";1:c="E";2:c="D";4:c="T";5:c="R";6:c="A";7:c="C";8:c="K";
                        10:c="1";11:c="5";12:c="%";default:c=" ";endcase
                    1:begin
                        if(I_state==1) case(ch) 0:c="L";1:c="O";2:c="C";3:c="K";default:c=" ";endcase
                        else if(I_state==2) case(ch) 0:c="L";1:c="O";2:c="S";3:c="T";default:c=" ";endcase
                        else case(ch) 0:c="S";1:c="E";2:c="A";3:c="R";4:c="C";5:c="H";default:c=" ";endcase
                    end
                    2:case(ch)
                        0:c="X";2:c=I_error_x[12] ? "-" : "+";
                        3:c="0"+bcd_x[11:8];4:c="0"+bcd_x[7:4];5:c="0"+bcd_x[3:0];
                        7:c=(I_dir_x==1) ? "L" : ((I_dir_x==2) ? "R" : "-");
                        9:c="L";10:c="0"+{6'd0,I_level_x};default:c=" ";endcase
                    3:case(ch)
                        0:c="Y";2:c=I_error_y[12] ? "-" : "+";
                        3:c="0"+bcd_y[11:8];4:c="0"+bcd_y[7:4];5:c="0"+bcd_y[3:0];
                        7:c=(I_dir_y==1) ? "U" : ((I_dir_y==2) ? "D" : "-");
                        9:c="L";10:c="0"+{6'd0,I_level_y};default:c=" ";endcase
                    4:case(ch)
                        0:c="A";1:c="R";2:c="E";3:c="A";
                        5:c="0"+bcd_area[11:8];6:c="0"+bcd_area[7:4];7:c="0"+bcd_area[3:0];8:c="%";default:c=" ";endcase
                endcase
                bits=glyph(c);bit_index=row*6'd5+col;
                if(col<5 && row<7 && bits[34-bit_index]) O_color=status_colour;
            end
        end
    end
endmodule
