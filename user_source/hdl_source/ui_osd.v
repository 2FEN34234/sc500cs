// Small resource-friendly menu overlay. It keeps the original video pixel
// stream intact outside the menu rectangle and uses a 5x7 bitmap font.
module ui_osd #(
    parameter integer H_ACTIVE = 1920,
    parameter integer V_ACTIVE = 1080
)(
    input  wire        I_clk,
    input  wire        I_rst_n,
    input  wire        I_vsync,
    input  wire        I_hsync,
    input  wire        I_de,
    input  wire [23:0] I_data,
    input  wire        I_menu_enable,
    input  wire        I_edit_enable,
    input  wire        I_info_enable,
    input  wire [2:0]  I_menu_index,
    input  wire [15:0] I_exposure,
    input  wire [15:0] I_gain,
    input  wire [15:0] I_servo_pan_us,
    input  wire [15:0] I_servo_tilt_us,
    output reg         O_vsync,
    output reg         O_hsync,
    output reg         O_de,
    output reg  [23:0] O_data
);
    reg [11:0] x;
    reg [11:0] y;
    reg de_d;
    reg vs_d;
    reg [23:0] pixel;
    reg overlay;
    reg [7:0] ch;
    reg [2:0] row;
    reg [2:0] col;
    reg [2:0] line_no;
    reg [3:0] char_no;
    reg [11:0] text_x;
    reg [11:0] text_y;
    reg text_active;
    reg [4:0] glyph;
    reg glyph_on;
    reg [7:0] exposure_bar_width;
    reg [7:0] gain_bar_width;
    reg [7:0] pan_bar_width;
    reg [7:0] tilt_bar_width;
    localparam [11:0] MENU_X0 = 12'd24;
    localparam [11:0] MENU_Y0 = 12'd680;

    function [7:0] text_char;
        input [2:0] line;
        input [3:0] index;
        begin
            text_char = 8'h20;
            case(line)
                3'd0: case(index) 0:text_char="M"; 1:text_char="E"; 2:text_char="N"; 3:text_char="U"; endcase
                3'd1: case(index) 0:text_char="E"; 1:text_char="X"; 2:text_char="P"; 3:text_char="O"; 4:text_char="S"; 5:text_char="U"; 6:text_char="R"; 7:text_char="E"; endcase
                3'd2: case(index) 0:text_char="G"; 1:text_char="A"; 2:text_char="I"; 3:text_char="N"; endcase
                3'd3: case(index) 0:text_char="P"; 1:text_char="A"; 2:text_char="N"; endcase
                3'd4: case(index) 0:text_char="T"; 1:text_char="I"; 2:text_char="L"; 3:text_char="T"; endcase
                3'd5: case(index) 0:text_char="M"; 1:text_char="O"; 2:text_char="D"; 3:text_char="E"; endcase
                3'd6: case(index) 0:text_char="E"; 1:text_char="F"; 2:text_char="F"; 3:text_char="E"; 4:text_char="C"; 5:text_char="T"; endcase
                3'd7: case(index) 0:text_char="L"; 1:text_char="O"; 2:text_char="G"; 3:text_char="O"; endcase
                default: text_char = 8'h20;
            endcase
        end
    endfunction

    // 5x7 font; each dot occupies 4x4 output pixels (20x28 per glyph).
    function [4:0] glyph_bits;
        input [7:0] c;
        input [2:0] r;
        begin
            glyph_bits = 5'b00000;
            case(c)
                "A": case(r)
                    3'd0: glyph_bits = 5'b01110;
                    3'd1: glyph_bits = 5'b10001;
                    3'd2: glyph_bits = 5'b10001;
                    3'd3: glyph_bits = 5'b11111;
                    3'd4: glyph_bits = 5'b10001;
                    3'd5: glyph_bits = 5'b10001;
                    3'd6: glyph_bits = 5'b10001;
                endcase
                "C": case(r)
                    3'd0: glyph_bits = 5'b01110;
                    3'd1: glyph_bits = 5'b10001;
                    3'd2: glyph_bits = 5'b10000;
                    3'd3: glyph_bits = 5'b10000;
                    3'd4: glyph_bits = 5'b10000;
                    3'd5: glyph_bits = 5'b10001;
                    3'd6: glyph_bits = 5'b01110;
                endcase
                "D": case(r)
                    3'd0: glyph_bits = 5'b11110;
                    3'd1: glyph_bits = 5'b10001;
                    3'd2: glyph_bits = 5'b10001;
                    3'd3: glyph_bits = 5'b10001;
                    3'd4: glyph_bits = 5'b10001;
                    3'd5: glyph_bits = 5'b10001;
                    3'd6: glyph_bits = 5'b11110;
                endcase
                "E": case(r)
                    3'd0: glyph_bits = 5'b11111;
                    3'd1: glyph_bits = 5'b10000;
                    3'd2: glyph_bits = 5'b10000;
                    3'd3: glyph_bits = 5'b11110;
                    3'd4: glyph_bits = 5'b10000;
                    3'd5: glyph_bits = 5'b10000;
                    3'd6: glyph_bits = 5'b11111;
                endcase
                "F": case(r)
                    3'd0: glyph_bits = 5'b11111;
                    3'd1: glyph_bits = 5'b10000;
                    3'd2: glyph_bits = 5'b10000;
                    3'd3: glyph_bits = 5'b11110;
                    3'd4: glyph_bits = 5'b10000;
                    3'd5: glyph_bits = 5'b10000;
                    3'd6: glyph_bits = 5'b10000;
                endcase
                "G": case(r)
                    3'd0: glyph_bits = 5'b01110;
                    3'd1: glyph_bits = 5'b10001;
                    3'd2: glyph_bits = 5'b10000;
                    3'd3: glyph_bits = 5'b10111;
                    3'd4: glyph_bits = 5'b10001;
                    3'd5: glyph_bits = 5'b10001;
                    3'd6: glyph_bits = 5'b01110;
                endcase
                "I": case(r)
                    3'd0: glyph_bits = 5'b11111;
                    3'd1: glyph_bits = 5'b00100;
                    3'd2: glyph_bits = 5'b00100;
                    3'd3: glyph_bits = 5'b00100;
                    3'd4: glyph_bits = 5'b00100;
                    3'd5: glyph_bits = 5'b00100;
                    3'd6: glyph_bits = 5'b11111;
                endcase
                "L": case(r)
                    3'd0: glyph_bits = 5'b10000;
                    3'd1: glyph_bits = 5'b10000;
                    3'd2: glyph_bits = 5'b10000;
                    3'd3: glyph_bits = 5'b10000;
                    3'd4: glyph_bits = 5'b10000;
                    3'd5: glyph_bits = 5'b10000;
                    3'd6: glyph_bits = 5'b11111;
                endcase
                "M": case(r)
                    3'd0: glyph_bits = 5'b10001;
                    3'd1: glyph_bits = 5'b11011;
                    3'd2: glyph_bits = 5'b10101;
                    3'd3: glyph_bits = 5'b10101;
                    3'd4: glyph_bits = 5'b10001;
                    3'd5: glyph_bits = 5'b10001;
                    3'd6: glyph_bits = 5'b10001;
                endcase
                "N": case(r)
                    3'd0: glyph_bits = 5'b10001;
                    3'd1: glyph_bits = 5'b10001;
                    3'd2: glyph_bits = 5'b11001;
                    3'd3: glyph_bits = 5'b10101;
                    3'd4: glyph_bits = 5'b10011;
                    3'd5: glyph_bits = 5'b10001;
                    3'd6: glyph_bits = 5'b10001;
                endcase
                "O": case(r)
                    3'd0: glyph_bits = 5'b01110;
                    3'd1: glyph_bits = 5'b10001;
                    3'd2: glyph_bits = 5'b10001;
                    3'd3: glyph_bits = 5'b10001;
                    3'd4: glyph_bits = 5'b10001;
                    3'd5: glyph_bits = 5'b10001;
                    3'd6: glyph_bits = 5'b01110;
                endcase
                "P": case(r)
                    3'd0: glyph_bits = 5'b11110;
                    3'd1: glyph_bits = 5'b10001;
                    3'd2: glyph_bits = 5'b10001;
                    3'd3: glyph_bits = 5'b11110;
                    3'd4: glyph_bits = 5'b10000;
                    3'd5: glyph_bits = 5'b10000;
                    3'd6: glyph_bits = 5'b10000;
                endcase
                "R": case(r)
                    3'd0: glyph_bits = 5'b11110;
                    3'd1: glyph_bits = 5'b10001;
                    3'd2: glyph_bits = 5'b10001;
                    3'd3: glyph_bits = 5'b11110;
                    3'd4: glyph_bits = 5'b10100;
                    3'd5: glyph_bits = 5'b10010;
                    3'd6: glyph_bits = 5'b10001;
                endcase
                "S": case(r)
                    3'd0: glyph_bits = 5'b01111;
                    3'd1: glyph_bits = 5'b10000;
                    3'd2: glyph_bits = 5'b10000;
                    3'd3: glyph_bits = 5'b01110;
                    3'd4: glyph_bits = 5'b00001;
                    3'd5: glyph_bits = 5'b00001;
                    3'd6: glyph_bits = 5'b11110;
                endcase
                "T": case(r)
                    3'd0: glyph_bits = 5'b11111;
                    3'd1: glyph_bits = 5'b00100;
                    3'd2: glyph_bits = 5'b00100;
                    3'd3: glyph_bits = 5'b00100;
                    3'd4: glyph_bits = 5'b00100;
                    3'd5: glyph_bits = 5'b00100;
                    3'd6: glyph_bits = 5'b00100;
                endcase
                "U": case(r)
                    3'd0: glyph_bits = 5'b10001;
                    3'd1: glyph_bits = 5'b10001;
                    3'd2: glyph_bits = 5'b10001;
                    3'd3: glyph_bits = 5'b10001;
                    3'd4: glyph_bits = 5'b10001;
                    3'd5: glyph_bits = 5'b10001;
                    3'd6: glyph_bits = 5'b01110;
                endcase
                "X": case(r)
                    3'd0: glyph_bits = 5'b10001;
                    3'd1: glyph_bits = 5'b10001;
                    3'd2: glyph_bits = 5'b01010;
                    3'd3: glyph_bits = 5'b00100;
                    3'd4: glyph_bits = 5'b01010;
                    3'd5: glyph_bits = 5'b10001;
                    3'd6: glyph_bits = 5'b10001;
                endcase
                default: glyph_bits = 5'b00000;
            endcase
        end
    endfunction

    always @* begin
        pixel = I_data;
        overlay = 1'b0;
        ch = 8'h20;
        glyph_on = 1'b0;
        glyph = 5'b00000;
        line_no = 3'd0;
        char_no = 4'd0;
        row = 3'd0;
        col = 3'd0;
        text_x = 12'd0;
        text_y = 12'd0;
        text_active = 1'b0;
        // Panel: lower-left, x=24..492, y=680..1004. The lower-left MENU
        // button remains visible even when this panel is hidden.
        if(I_menu_enable && (x >= MENU_X0) && (x < MENU_X0 + 12'd468) &&
           (y >= MENU_Y0) && (y < MENU_Y0 + 12'd324)) begin
            overlay = 1'b1;
            pixel = 24'h101820;
            if((x < MENU_X0 + 12'd4) || (x >= MENU_X0 + 12'd464) ||
               (y < MENU_Y0 + 12'd4) || (y >= MENU_Y0 + 12'd320))
                pixel = 24'h3090d0;
            else if((y >= MENU_Y0 + 12'd48) && (y < MENU_Y0 + 12'd84) && (I_menu_index == 3'd0))
                pixel = I_edit_enable ? 24'hd08020 : 24'h205080;
            else if((y >= MENU_Y0 + 12'd84) && (y < MENU_Y0 + 12'd120) && (I_menu_index == 3'd1))
                pixel = I_edit_enable ? 24'hd08020 : 24'h205080;
            else if((y >= MENU_Y0 + 12'd120) && (y < MENU_Y0 + 12'd156) && (I_menu_index == 3'd2))
                pixel = I_edit_enable ? 24'hd08020 : 24'h205080;
            else if((y >= MENU_Y0 + 12'd156) && (y < MENU_Y0 + 12'd192) && (I_menu_index == 3'd3))
                pixel = I_edit_enable ? 24'hd08020 : 24'h205080;
            else if((y >= MENU_Y0 + 12'd192) && (y < MENU_Y0 + 12'd228) && (I_menu_index == 3'd4))
                pixel = 24'h205080;
            else if((y >= MENU_Y0 + 12'd228) && (y < MENU_Y0 + 12'd264) && (I_menu_index == 3'd5))
                pixel = 24'h205080;
            else if((y >= MENU_Y0 + 12'd264) && (y < MENU_Y0 + 12'd300) && (I_menu_index == 3'd6))
                pixel = 24'h205080;

            // Eight 32-pixel character cells: 20-pixel glyph + 12-pixel gap.
            // Bound x before taking bit slices to prevent character index wrap.
            // All scaling uses bit slices; no divider is needed in this path.
            if((x >= MENU_X0 + 12'd20) && (x < MENU_X0 + 12'd276)) begin
                text_x = x - (MENU_X0 + 12'd20);
                char_no = {1'b0, text_x[7:5]};
                col = text_x[4:2];
                if((y >= MENU_Y0 + 12'd16) && (y < MENU_Y0 + 12'd44)) begin
                    text_active = 1'b1; line_no = 3'd0; text_y = y - (MENU_Y0 + 12'd16);
                end else if((y >= MENU_Y0 + 12'd52) && (y < MENU_Y0 + 12'd80)) begin
                    text_active = 1'b1; line_no = 3'd1; text_y = y - (MENU_Y0 + 12'd52);
                end else if((y >= MENU_Y0 + 12'd88) && (y < MENU_Y0 + 12'd116)) begin
                    text_active = 1'b1; line_no = 3'd2; text_y = y - (MENU_Y0 + 12'd88);
                end else if((y >= MENU_Y0 + 12'd124) && (y < MENU_Y0 + 12'd152)) begin
                    text_active = 1'b1; line_no = 3'd3; text_y = y - (MENU_Y0 + 12'd124);
                end else if((y >= MENU_Y0 + 12'd160) && (y < MENU_Y0 + 12'd188)) begin
                    text_active = 1'b1; line_no = 3'd4; text_y = y - (MENU_Y0 + 12'd160);
                end else if((y >= MENU_Y0 + 12'd196) && (y < MENU_Y0 + 12'd224)) begin
                    text_active = 1'b1; line_no = 3'd5; text_y = y - (MENU_Y0 + 12'd196);
                end else if((y >= MENU_Y0 + 12'd232) && (y < MENU_Y0 + 12'd260)) begin
                    text_active = 1'b1; line_no = 3'd6; text_y = y - (MENU_Y0 + 12'd232);
                end else if((y >= MENU_Y0 + 12'd268) && (y < MENU_Y0 + 12'd296)) begin
                    text_active = 1'b1; line_no = 3'd7; text_y = y - (MENU_Y0 + 12'd268);
                end
                row = text_y[4:2];
                ch = text_char(line_no, char_no);
                glyph = glyph_bits(ch, row);
                glyph_on = text_active && (col < 3'd5) &&
                           ((glyph & (5'b10000 >> col)) != 0);
            end
            if(glyph_on) pixel = 24'hffffff;
            // Four short value bars fit beside the labels. The servo bars
            // cover the conservative 1000..2000 us range used by the menu.
            if((x >= MENU_X0 + 12'd306) && (x < MENU_X0 + 12'd434)) begin
                if((y >= MENU_Y0 + 12'd60) && (y < MENU_Y0 + 12'd72))
                    pixel = (x < MENU_X0 + 12'd306 + exposure_bar_width) ? 24'h40d080 : 24'h304050;
                else if((y >= MENU_Y0 + 12'd96) && (y < MENU_Y0 + 12'd108))
                    pixel = (x < MENU_X0 + 12'd306 + gain_bar_width) ? 24'hd0c040 : 24'h504820;
                else if((y >= MENU_Y0 + 12'd132) && (y < MENU_Y0 + 12'd144))
                    pixel = (x < MENU_X0 + 12'd306 + pan_bar_width) ? 24'h40d0d0 : 24'h304050;
                else if((y >= MENU_Y0 + 12'd168) && (y < MENU_Y0 + 12'd180))
                    pixel = (x < MENU_X0 + 12'd306 + tilt_bar_width) ? 24'hd080d0 : 24'h504050;
            end
        end
        if(I_info_enable && I_menu_enable && (x >= MENU_X0 + 12'd306) &&
           (x < MENU_X0 + 12'd434) && (y >= MENU_Y0 + 12'd204) &&
           (y < MENU_Y0 + 12'd216))
            pixel = 24'h206060;

        // Always-visible touch target for showing/hiding the menu.
        if((x >= 12'd24) && (x < 12'd220) &&
           (y >= 12'd1016) && (y < 12'd1076)) begin
            pixel = 24'h183050;
            if((x < 12'd28) || (x >= 12'd216) || (y < 12'd1020) || (y >= 12'd1072))
                pixel = 24'h3090d0;
            if((x >= 12'd44) && (x < 12'd172)) begin
                text_x = x - 12'd44;
                char_no = {1'b0, text_x[7:5]};
                col = text_x[4:2];
                row = (y - 12'd1024) >> 2;
                ch = text_char(3'd0, char_no);
                glyph = glyph_bits(ch, row);
                if((col < 3'd5) && ((glyph & (5'b10000 >> col)) != 0))
                    pixel = 24'hffffff;
            end
        end
    end

    always @(posedge I_clk or negedge I_rst_n) begin
        if(!I_rst_n) begin
            x <= 0; y <= 0; de_d <= 0; vs_d <= 0;
            exposure_bar_width <= 8'd48;
            gain_bar_width <= 8'd64;
            pan_bar_width <= 8'd62;
            tilt_bar_width <= 8'd62;
            O_vsync <= 0; O_hsync <= 0; O_de <= 0; O_data <= 0;
        end else begin
            O_vsync <= I_vsync;
            O_hsync <= I_hsync;
            O_de <= I_de;
            O_data <= I_de ? pixel : 24'd0;
            vs_d <= I_vsync;
            de_d <= I_de;
            if(I_vsync && !vs_d) begin
                x <= 0; y <= 0;
                // The bars update once per frame.  Shift/add approximations
                // avoid putting a multiplier/divider in the pixel data path.
                exposure_bar_width <= (I_exposure >= 16'd5990) ? 8'd127 :
                                      ((I_exposure >> 6) + (I_exposure >> 8));
                if(I_gain <= 16'd16)
                    gain_bar_width <= 8'd0;
                else if(I_gain >= 16'd143)
                    gain_bar_width <= 8'd127;
                else
                    gain_bar_width <= I_gain - 16'd16;
                pan_bar_width <= (I_servo_pan_us <= 16'd1000) ? 8'd0 :
                                 (I_servo_pan_us >= 16'd2000) ? 8'd125 :
                                 (I_servo_pan_us - 16'd1000) >> 3;
                tilt_bar_width <= (I_servo_tilt_us <= 16'd1000) ? 8'd0 :
                                  (I_servo_tilt_us >= 16'd2000) ? 8'd125 :
                                  (I_servo_tilt_us - 16'd1000) >> 3;
            end else if(I_de) begin
                x <= x + 1'b1;
            end else if(de_d) begin
                x <= 0;
                y <= y + 1'b1;
            end
        end
    end
endmodule
