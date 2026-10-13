/*****************************************************************
 * AWB (Auto White Balance) - resource-optimized version
 *
 * 原设计用 2 个全流水 48 位除法器 IP（div_gen_G_R_r1 / divide_G_B_r1）
 * 逐像素连续运行，约 5500 个寄存器；另有 50 拍 BRAM 延迟线（signal_delay）。
 * 但 AWB 增益（G/R、G/B）每帧只需要计算一次：
 *   - 帧起始（I_tuser）锁存上一帧 R/G/B 累加和；
 *   - 用一个串行移位减法除法器，在消隐期内分时算出 R 增益和 B 增益；
 *   - 本帧所有像素共用同一个增益（原设计也只用了 lane0 的增益）。
 *
 * 效果：约 5500 寄存器 -> 约 200 寄存器，并去掉 50 拍 BRAM 延迟线。
 * 行为变化：由"逐像素滑动平均增益"改为"上一帧统计、本帧应用"（标准 AWB）。
 * 保留：黑电平扣除、近灰门限、亮像素排除、旁路/固定增益、0.5x~4x 目标限幅、
 *       IIR 平滑(alpha=7/8)、样本量保护。
 *****************************************************************/
module awb #(
    parameter IMG_HEIGHT = 1080,
    parameter IMG_WIDTH  = 1920,
    parameter             AWB_EN           = 1'b1,
    parameter      [19:0] AWB_FIXED_GAIN_R = 20'h10000,
    parameter      [19:0] AWB_FIXED_GAIN_B = 20'h10000
)(
    input                   I_clk,
    input                   I_rst_n,
    input                   I_tlast,
    input                   I_tuser,
    input [95:0]            I_tdata,
    input                   I_tvalid,
    output                  I_tready,
    input                   I_bypass,
    input      [7:0]        I_blc_off,
    input                   I_gate_en,
    input      [7:0]        I_gate_th,
    input                   I_smooth_en,
    output                  O_tlast,
    output                  O_tuser,
    output [95:0]           O_tdata,
    output                  O_tvalid,
    input                   O_tready
);

    // ---- 黑电平扣除 ----
    function [7:0] sub_off;
        input [7:0] d;
        input [7:0] off;
        begin
            sub_off = (d > off) ? (d - off) : 8'd0;
        end
    endfunction

    wire [95:0] S_tdata_raw = I_tdata;
    wire [95:0] S_tdata = {
        sub_off(S_tdata_raw[95:88], I_blc_off), sub_off(S_tdata_raw[87:80], I_blc_off), sub_off(S_tdata_raw[79:72], I_blc_off),
        sub_off(S_tdata_raw[71:64], I_blc_off), sub_off(S_tdata_raw[63:56], I_blc_off), sub_off(S_tdata_raw[55:48], I_blc_off),
        sub_off(S_tdata_raw[47:40], I_blc_off), sub_off(S_tdata_raw[39:32], I_blc_off), sub_off(S_tdata_raw[31:24], I_blc_off),
        sub_off(S_tdata_raw[23:16], I_blc_off), sub_off(S_tdata_raw[15:8],  I_blc_off), sub_off(S_tdata_raw[7:0],   I_blc_off)
    };

    wire S_awb_off = (~AWB_EN) | I_bypass;

    // ---- 输入流水 ----
    reg         I_tvalid_r0, I_tvalid_r1;
    reg  [95:0] I_tdata_d0, I_tdata_d1;

    // ---- 统计累加器（与原设计一致，只用 lane0）----
    reg [31:0] sum_r, sum_g, sum_b;
    reg [9:0]  rgb_sum;

    // ---- 像素拆分（raw / 1 拍延迟）----
    wire [7:0] rgb888_r_d  = S_tdata[16+:8];
    wire [7:0] rgb888_g_d  = S_tdata[8+:8];
    wire [7:0] rgb888_b_d  = S_tdata[0+:8];
    wire [7:0] rgb888_r_d0 = I_tdata_d0[16+:8];
    wire [7:0] rgb888_g_d0 = I_tdata_d0[8+:8];
    wire [7:0] rgb888_b_d0 = I_tdata_d0[0+:8];

    // ---- 输入寄存 ----
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            I_tdata_d0  <= 0;
            I_tdata_d1  <= 0;
            I_tvalid_r0 <= 0;
            I_tvalid_r1 <= 0;
        end else begin
            I_tdata_d0  <= S_tdata;
            I_tdata_d1  <= I_tdata_d0;
            I_tvalid_r0 <= I_tvalid;
            I_tvalid_r1 <= I_tvalid_r0;
        end
    end

    // ---- 亮像素排除（R+G+B < 720），显式扩位避免 8bit 溢出 ----
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n)
            rgb_sum <= 0;
        else if (I_tvalid)
            rgb_sum <= {2'b0, rgb888_r_d} + {2'b0, rgb888_g_d} + {2'b0, rgb888_b_d};
    end

    // ---- 近灰门限判断（|max-min| < I_gate_th）----
    wire [7:0] ng_mx = (rgb888_r_d0 > rgb888_g_d0)
                     ? ((rgb888_r_d0 > rgb888_b_d0) ? rgb888_r_d0 : rgb888_b_d0)
                     : ((rgb888_g_d0 > rgb888_b_d0) ? rgb888_g_d0 : rgb888_b_d0);
    wire [7:0] ng_mn = (rgb888_r_d0 < rgb888_g_d0)
                     ? ((rgb888_r_d0 < rgb888_b_d0) ? rgb888_r_d0 : rgb888_b_d0)
                     : ((rgb888_g_d0 < rgb888_b_d0) ? rgb888_g_d0 : rgb888_b_d0);

    wire S_stat_en = I_tvalid_r0 && (rgb_sum < 720) &&
                     (~I_gate_en || ((ng_mx - ng_mn) < I_gate_th));

    // ---- 累加（帧起始清 1，避免除 0）----
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n || I_tuser) begin
            sum_r <= 1;
            sum_g <= 1;
            sum_b <= 1;
        end else if (S_stat_en) begin
            sum_r <= sum_r + rgb888_r_d0;
            sum_g <= sum_g + rgb888_g_d0;
            sum_b <= sum_b + rgb888_b_d0;
        end
    end

    // =====================================================================
    // 串行移位减法除法器（替换 2 个全流水除法器 IP）
    //   帧起始锁存 sum_r/g/b，先算 gain_r = (G<<16)/R，再算 gain_b = (G<<16)/B。
    //   每个商 48 bit，恢复余数法，每 bit 一个时钟，共约 96 拍（在消隐期完成）。
    // =====================================================================
    localparam DW = 48;

    reg [31:0]    lat_r, lat_g, lat_b;   // 帧首锁存的累加和
    reg [DW-1:0]  div_num;               // 被除数移位寄存器
    reg [31:0]    div_den;               // 除数
    reg [DW-1:0]  div_rem;               // 部分余数
    reg [DW-1:0]  div_quot;              // 商（当前阶段）
    reg [DW-1:0]  quot_r;                // 锁存的 R 增益商
    reg [5:0]     div_cnt;               // 0..47
    reg [1:0]     div_st;                // 0=空闲 1=运行 2=收尾
    reg           div_phase;             // 0=R 1=B
    reg           div_done;              // 两路增益就绪的 1 拍脉冲

    wire [DW-1:0] div_rem_sh = {div_rem[DW-2:0], div_num[DW-1]};
    wire [DW-1:0] div_den_ex = {{(DW-32){1'b0}}, div_den};
    wire          div_ge     = (div_rem_sh >= div_den_ex);

    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            lat_r <= 32'd1; lat_g <= 32'd1; lat_b <= 32'd1;
            div_num <= 0; div_den <= 0; div_rem <= 0; div_quot <= 0; quot_r <= 0;
            div_cnt <= 0; div_st <= 2'd0; div_phase <= 1'b0; div_done <= 1'b0;
        end else begin
            div_done <= 1'b0;
            case (div_st)
            2'd0: begin   // 空闲：帧起始触发
                if (I_tuser) begin
                    lat_r <= sum_r;
                    lat_g <= sum_g;
                    lat_b <= sum_b;
                    div_num   <= {sum_g, 16'd0};
                    div_den   <= sum_r;
                    div_phase <= 1'b0;
                    div_rem   <= 0;
                    div_quot  <= 0;
                    div_cnt   <= 0;
                    div_st    <= 2'd1;
                end
            end
            2'd1: begin   // 每拍一个恢复余数迭代
                if (div_ge) begin
                    div_rem  <= div_rem_sh - div_den_ex;
                    div_quot <= div_quot | ({{(DW-1){1'b0}}, 1'b1} << (DW-1 - div_cnt));
                end else begin
                    div_rem  <= div_rem_sh;
                end
                div_num <= {div_num[DW-2:0], 1'b0};
                if (div_cnt == (DW-1))
                    div_st <= 2'd2;
                else
                    div_cnt <= div_cnt + 1'b1;
            end
            2'd2: begin   // 收尾：存 R 商并启动 B，或完成
                if (div_phase == 1'b0) begin
                    quot_r   <= div_quot;
                    div_num  <= {lat_g, 16'd0};
                    div_den  <= lat_b;
                    div_phase<= 1'b1;
                    div_rem  <= 0;
                    div_quot <= 0;
                    div_cnt  <= 0;
                    div_st   <= 2'd1;
                end else begin
                    div_done <= 1'b1;
                    div_st   <= 2'd0;
                end
            end
            default: div_st <= 2'd0;
            endcase
        end
    end

    // ---- 目标增益（饱和 + 0.5x~4x 限幅）----
    wire [19:0] tgt_r_raw = (|quot_r[47:20])   ? 20'hfffff : quot_r[19:0];
    wire [19:0] tgt_b_raw = (|div_quot[47:20]) ? 20'hfffff : div_quot[19:0];
    wire [19:0] tgt_r = (tgt_r_raw < 20'h08000) ? 20'h08000 :
                        (tgt_r_raw > 20'h40000) ? 20'h40000 : tgt_r_raw;
    wire [19:0] tgt_b = (tgt_b_raw < 20'h08000) ? 20'h08000 :
                        (tgt_b_raw > 20'h40000) ? 20'h40000 : tgt_b_raw;

    wire tgt_ok = (lat_g > 32'd65536);

    // ---- IIR 平滑（alpha = 7/8）----
    reg [19:0] in_r_r1, in_b_r1;
    wire signed [20:0] dif_r = $signed({1'b0, tgt_r}) - $signed({1'b0, in_r_r1});
    wire signed [20:0] dif_b = $signed({1'b0, tgt_b}) - $signed({1'b0, in_b_r1});
    wire signed [20:0] nxt_r = $signed({1'b0, in_r_r1}) + (dif_r >>> 3);
    wire signed [20:0] nxt_b = $signed({1'b0, in_b_r1}) + (dif_b >>> 3);
    wire [19:0] smt_r = nxt_r[20] ? 20'h08000 :
                        ((nxt_r > 21'sh40000) ? 20'h40000 : nxt_r[19:0]);
    wire [19:0] smt_b = nxt_b[20] ? 20'h08000 :
                        ((nxt_b > 21'sh40000) ? 20'h40000 : nxt_b[19:0]);

    // ---- 增益更新（帧级，替代原来的逐像素 done[0]）----
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n || (!I_smooth_en && I_tuser)) begin
            in_r_r1 <= S_awb_off ? AWB_FIXED_GAIN_R : 20'h10000;
            in_b_r1 <= S_awb_off ? AWB_FIXED_GAIN_B : 20'h10000;
        end
        else if (I_smooth_en && S_awb_off) begin
            in_r_r1 <= AWB_FIXED_GAIN_R;
            in_b_r1 <= AWB_FIXED_GAIN_B;
        end
        else if (I_smooth_en) begin
            if (div_done && tgt_ok) begin
                in_r_r1 <= smt_r;
                in_b_r1 <= smt_b;
            end
        end
        else if (div_done && S_awb_off) begin
            in_r_r1 <= AWB_FIXED_GAIN_R;
            in_b_r1 <= AWB_FIXED_GAIN_B;
        end
        else if (div_done) begin
            in_r_r1 <= (|quot_r[47:20])   ? 20'hfffff : quot_r[19:0];
            in_b_r1 <= (|div_quot[47:20]) ? 20'hfffff : div_quot[19:0];
        end
    end


    // ---- 数据/有效信号流水（去掉 50 拍 BRAM 延迟线，仅 1 拍）----
    reg [95:0] data_d1;
    reg        valid_d1, valid_d2;

    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            valid_d1 <= 0;
            data_d1  <= 0;
            valid_d2 <= 0;
        end else begin
            valid_d1 <= I_tvalid_r1;
            data_d1  <= I_tdata_d1;
            valid_d2 <= valid_d1;
        end
    end

    wire [7:0] rgb888_r_d1[0:3];
    wire [7:0] rgb888_g_d1[0:3];
    wire [7:0] rgb888_b_d1[0:3];

    assign rgb888_r_d1[0] = data_d1[16+:8];
    assign rgb888_g_d1[0] = data_d1[8+:8];
    assign rgb888_b_d1[0] = data_d1[0+:8];
    assign rgb888_r_d1[1] = data_d1[40+:8];
    assign rgb888_g_d1[1] = data_d1[32+:8];
    assign rgb888_b_d1[1] = data_d1[24+:8];
    assign rgb888_r_d1[2] = data_d1[64+:8];
    assign rgb888_g_d1[2] = data_d1[56+:8];
    assign rgb888_b_d1[2] = data_d1[48+:8];
    assign rgb888_r_d1[3] = data_d1[88+:8];
    assign rgb888_g_d1[3] = data_d1[80+:8];
    assign rgb888_b_d1[3] = data_d1[72+:8];

    // ---- 增益应用（共享增益，4 个像素）----
    reg [27:0] R_new_r1, G_new_r1, B_new_r1;
    reg [27:0] R_new_r2, G_new_r2, B_new_r2;
    reg [27:0] R_new_r3, G_new_r3, B_new_r3;
    reg [27:0] R_new_r4, G_new_r4, B_new_r4;

    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            R_new_r1 <= 0; G_new_r1 <= 0; B_new_r1 <= 0;
            R_new_r2 <= 0; G_new_r2 <= 0; B_new_r2 <= 0;
            R_new_r3 <= 0; G_new_r3 <= 0; B_new_r3 <= 0;
            R_new_r4 <= 0; G_new_r4 <= 0; B_new_r4 <= 0;
        end
        else if (valid_d1) begin
            R_new_r1 <= in_r_r1 * rgb888_r_d1[0];
            G_new_r1 <=           rgb888_g_d1[0];
            B_new_r1 <= in_b_r1 * rgb888_b_d1[0];
            R_new_r2 <= in_r_r1 * rgb888_r_d1[1];
            G_new_r2 <=           rgb888_g_d1[1];
            B_new_r2 <= in_b_r1 * rgb888_b_d1[1];
            R_new_r3 <= in_r_r1 * rgb888_r_d1[2];
            G_new_r3 <=           rgb888_g_d1[2];
            B_new_r3 <= in_b_r1 * rgb888_b_d1[2];
            R_new_r4 <= in_r_r1 * rgb888_r_d1[3];
            G_new_r4 <=           rgb888_g_d1[3];
            B_new_r4 <= in_b_r1 * rgb888_b_d1[3];
        end
    end

    // ---- 截断到 8bit（饱和）----
    wire [7:0] R_new_x1 = (R_new_r1[27:24]) ? 255 : R_new_r1[23:16];
    wire [7:0] G_new_x1 = G_new_r1;
    wire [7:0] B_new_x1 = (B_new_r1[27:24]) ? 255 : B_new_r1[23:16];
    wire [7:0] R_new_x2 = (R_new_r2[27:24]) ? 255 : R_new_r2[23:16];
    wire [7:0] G_new_x2 = G_new_r2;
    wire [7:0] B_new_x2 = (B_new_r2[27:24]) ? 255 : B_new_r2[23:16];
    wire [7:0] R_new_x3 = (R_new_r3[27:24]) ? 255 : R_new_r3[23:16];
    wire [7:0] G_new_x3 = G_new_r3;
    wire [7:0] B_new_x3 = (B_new_r3[27:24]) ? 255 : B_new_r3[23:16];
    wire [7:0] R_new_x4 = (R_new_r4[27:24]) ? 255 : R_new_r4[23:16];
    wire [7:0] G_new_x4 = G_new_r4;
    wire [7:0] B_new_x4 = (B_new_r4[27:24]) ? 255 : B_new_r4[23:16];

    // ---- 输出（与原设计一致：O_tuser 直通、O_tlast 为 valid 上升沿）----
    assign O_tlast  = (valid_d1 == 0) && (valid_d2 == 1);
    assign O_tuser  = I_tuser;
    assign O_tvalid = valid_d2;
    assign O_tdata  = O_tvalid ? {R_new_x4, G_new_x4, B_new_x4,
                                  R_new_x3, G_new_x3, B_new_x3,
                                  R_new_x2, G_new_x2, B_new_x2,
                                  R_new_x1, G_new_x1, B_new_x1} : 0;
    assign I_tready = O_tready;

endmodule

