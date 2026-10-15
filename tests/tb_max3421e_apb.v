`timescale 1ns/1ps
module tb_max3421e_apb;
    reg clk = 0;
    always #5 clk = ~clk;
    reg rst = 1;
    reg int_n = 1;
    reg [19:0] addr = 0;
    reg sel = 0, enable = 0, wr = 0;
    reg [31:0] wdata = 0;
    reg [3:0] strobe = 4'hf;
    wire [31:0] rdata;
    wire ready, error, res_n, spi_enable;
    wire menu, edit, info, lens_cmd, lens_retry, lens_read;
    integer retry_pulses=0, read_pulses=0, cmd_pulses=0;
    always @(posedge clk) begin
        if(lens_cmd) cmd_pulses=cmd_pulses+1;
        if(lens_retry) retry_pulses=retry_pulses+1;
        if(lens_read) read_pulses=read_pulses+1;
    end
    wire [2:0] index;
    wire [15:0] exposure, gain;
    reg [31:0] rtc_result=0;
    wire [31:0] rtc_t0,rtc_t1;
    wire rtc_set;
    wire [1:0] wifi_ctrl;
    reg [1:0] wifi_pads=2'b11;
    integer rtc_pulses=0;
    always @(posedge clk) if(rtc_set) rtc_pulses=rtc_pulses+1;

    mcu_ui_apb_regs dut (
        .I_clk(clk), .I_rst(rst), .I_button(4'hf), .I_usb_int_n(int_n),
        .O_usb_res_n(res_n), .O_usb_spi_enable(spi_enable),
        .I_paddr(addr), .I_psel(sel), .I_penable(enable), .I_pwrite(wr),
        .I_pwdata(wdata), .I_pstrobe(strobe), .O_prdata(rdata),
        .O_pready(ready), .O_pslverr(error), .O_menu_enable(menu),
        .O_edit_enable(edit), .O_info_enable(info), .O_menu_index(index),
        .O_exposure(exposure), .O_gain(gain),
        .I_rtc_set_status(rtc_result),.O_rtc_set_time0(rtc_t0),
        .O_rtc_set_time1(rtc_t1),.O_rtc_set(rtc_set),
        .O_wifi_ctrl(wifi_ctrl), .I_wifi_pads(wifi_pads),
        .I_lens_busy(1'b0), .I_lens_init_done(1'b0), .I_lens_error(1'b1),
        .I_focus_metric(32'd1234),
        .I_lens_diag(32'h00130110), .I_lens_tx_count(32'd1),
        .I_lens_adc_raw(32'h00015ba6), .I_lens_adc_count(32'd2),
        .I_lens_bus_state(32'h0000000f),
        .O_lens_cmd(lens_cmd), .O_lens_retry(lens_retry), .O_lens_read_adc(lens_read)
    );

    task write_reg(input [19:0] a, input [31:0] value, input [3:0] lanes);
        begin
            @(negedge clk); sel = 1; wr = 1; addr = a; wdata = value; strobe = lanes;
            @(negedge clk); enable = 1;
            @(negedge clk); sel = 0; enable = 0; wr = 0;
        end
    endtask
    task read_check(input [19:0] a, input [31:0] expected);
        begin
            @(negedge clk); sel = 1; addr = a;
            @(negedge clk); enable = 1; #1;
            if (rdata !== expected || !ready || error)
                $fatal(1, "APB %h: got %h expected %h", a, rdata, expected);
            @(negedge clk); sel = 0; enable = 0;
        end
    endtask

    initial begin
        repeat (5) @(negedge clk);
        if (res_n !== 0 || spi_enable !== 0) $fatal(1, "Unsafe reset state");
        rst = 0;
        if(wifi_ctrl!==0) $fatal(1,"ESP reset/boot strap not released");
        read_check('h10,3990);
        write_reg('h10,5990,4'hf);read_check('h10,3990);
        write_reg('h10,3991,4'hf);read_check('h10,3990);
        write_reg('h10,0,4'hf);read_check('h10,3);
        write_reg('h10,2000,4'hf);read_check('h10,2000);
        write_reg('h10,3990,4'hf);
        read_check('h98,32'h41465231);
        read_check('h94,32'h01e00370);
        write_reg('h94,32'hffffffff,4'hf);read_check('h94,32'h83c006e0);
        write_reg('h94,0,4'h7);read_check('h94,32'h83c006e0);
        write_reg('h94,32'h80000000,4'hf);read_check('h94,32'h80000000);
        read_check('h90,32'h57494631);
        read_check('h88,0);read_check('h8c,3);
        write_reg('h88,3,4'he);read_check('h88,0);
        write_reg('h88,1,4'h1);read_check('h88,1);
        if(wifi_ctrl!==1) $fatal(1,"ESP reset control");
        write_reg('h88,3,4'h1);read_check('h88,3);
        wifi_pads=0;read_check('h8c,0);
        write_reg('h88,0,4'h1);wifi_pads=3;
        if(wifi_ctrl!==0) $fatal(1,"ESP pins not released");
        read_check(0, 32'h55494d55);
        read_check('h74, 32'h50494331);
        read_check('h70, 32'h1050);
        write_reg('h70, 32'hffff, 4'h1);
        read_check('h70, 32'h1050);
        write_reg('h70, 32'hffff, 4'h3);
        read_check('h70, 32'hf9ff);
        write_reg('h70, 32'h1050, 4'hf);
        write_reg('h08, 9, 4'hf);
        read_check('h08, 9);
        write_reg('h08,17,4'h1);read_check('h08,17);
        if(!dut.O_mode_page || dut.O_picture_page) $fatal(1,"mode page control");
        write_reg('h08,9,4'h1);
        read_check('h58,32'h102);
        write_reg('h58,0,4'h1);read_check('h58,2);
        write_reg('h58,1,4'h1);read_check('h58,2);
        write_reg('h58,7,4'h1);read_check('h58,5);
        write_reg('h58,32'h102,4'h1);
        read_check('h30, 4);
        read_check('h34, 1234);
        read_check('h38, 32'h4c494333);
        read_check('h3c, 32'h00130110);
        read_check('h40, 1);
        read_check('h44, 32'h00015ba6);
        read_check('h48, 2);
        read_check('h4c, 15);
        write_reg('h2c, 7, 4'he);
        repeat(2) @(negedge clk);
        if(cmd_pulses || retry_pulses || read_pulses) $fatal(1,"Command ignored strobe");
        write_reg('h2c, 1, 4'h1);
        write_reg('h2c, 2, 4'h1);
        write_reg('h2c, 4, 4'h1);
        repeat(2) @(negedge clk);
        if(cmd_pulses!=1 || retry_pulses!=1 || read_pulses!=1)
            $fatal(1,"Command pulse count");
        read_check('h1c, 32'h4d415831);
        read_check('h20, 0);
        read_check('h24, 1);
        // APB setup phase must not change the physical reset output.
        @(negedge clk); sel = 1; wr = 1; addr = 'h20; wdata = 3;
        repeat (3) @(negedge clk);
        if (res_n !== 0 || spi_enable !== 0) $fatal(1, "Write in APB setup phase");
        sel = 0; wr = 0;
        write_reg('h20, 3, 4'hf);
        read_check('h20, 3);
        read_check('h24, 7);
        write_reg('h20, 0, 4'he);
        read_check('h20, 3);
        // One clock is insufficient to propagate the asynchronous interrupt.
        @(negedge clk); int_n = 0;
        @(posedge clk); #1;
        if (dut.usb_int_sync !== 1) $fatal(1, "Missing INT synchronizer");
        @(posedge clk); #1;
        if (dut.usb_int_sync !== 0) $fatal(1, "INT synchronization failed");
        read_check('h24, 6);
        int_n = 1;
        read_check('h24, 7);
        write_reg('h08, 7, 4'hf);
        write_reg('h0c, 2, 4'hf);
        write_reg('h10, 2400, 4'hf);
        write_reg('h14, 100, 4'hf);
        read_check('h08, 7);
        read_check('h0c, 2);
        read_check('h10, 2400);
        read_check('h14, 100);
        read_check('h20, 3);
        write_reg('h20, 0, 4'hf);
        read_check('h20, 0);
        read_check('h08, 7);
        read_check('h84,32'h52544332);
        write_reg('h78,32'h00123456,4'hf);write_reg('h7c,32'h26101302,4'hf);
        write_reg('h78,0,4'h1);read_check('h78,32'h00123456);
        write_reg('h80,32'h53455431,4'h1);
        write_reg('h80,0,4'hf);
        if(rtc_pulses!=0) $fatal(1,"RTC command accepted partial/bad key");
        write_reg('h80,32'h53455431,4'hf);repeat(2) @(negedge clk);
        if(rtc_pulses!=1 || rtc_t1!=32'h26101302) $fatal(1,"RTC command staging/pulse");
        rtc_result=1;write_reg('h80,32'h53455431,4'hf);repeat(2) @(negedge clk);
        if(rtc_pulses!=1) $fatal(1,"RTC busy command not rejected");
        read_check('h80,1);
        rst = 1; #1;
        if(wifi_ctrl!==0) $fatal(1,"Reset left ESP in download mode");
        if (res_n !== 0 || spi_enable !== 0) $fatal(1, "Reset did not isolate USB");
        $display("PASS: USB capability, APB phases/strobes, reset, INT CDC, UI registers");
        $finish;
    end
endmodule
