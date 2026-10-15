`timescale 1ns/1ps
module tb_rtc_set;
    reg clk=0; always #10 clk=~clk;
    reg rst_n=0,req=0;
    reg [31:0] t0=32'h00235958,t1=32'h26101302;
    tri1 scl,sda;
    wire valid,error,osf,busy;
    wire [31:0] result,count;
    wire [7:0] sec,min,hr,day,date,month,year;
    ds3231_i2c_ctrl #(.CLK_DIV(49),.POLL_CYCLES(1000),.GAP_CYCLES(10)) dut(
        .I_clk(clk),.I_rst_n(rst_n),.I_set(req),.I_set_time0(t0),.I_set_time1(t1),
        .O_set_status(result),.IO_scl(scl),.IO_sda(sda),.O_seconds(sec),
        .O_minutes(min),.O_hours(hr),.O_day(day),.O_date(date),.O_month(month),.O_year(year),
        .O_valid(valid),.O_error(error),.O_osf(osf),.O_busy(busy),.O_read_count(count));
    rtc_test_slave slave(scl,sda);
    task command;
        begin @(negedge clk);req=1;@(negedge clk);req=0;end
    endtask
    task complete;
        integer i;
        begin : waiting
            for(i=0;i<1000000;i=i+1) begin
                @(negedge clk);
                if(result[1]&&!result[0]) disable waiting;
            end
            $fatal(1,"set timed out %h",result);
        end
    endtask
    integer i,old_status;
    initial begin
        for(i=0;i<16;i=i+1) slave.regs[i]=0;
        slave.regs[15]=8'h8b;slave.regs[14]=8'h9c;
        repeat(10) @(negedge clk);rst_n=1;
        // Arrive during an existing read; stage registers can change afterward.
        wait(busy); command();t0=0;t1=0;
        wait(dut.operation==dut.WRITE);command(); // busy command cannot replace it
        complete();
        if(result!=32'h00010002 || !valid || osf || error ||
           {year,month,date,day,hr,min,sec}!=56'h26101302235958)
            $fatal(1,"verified setting failed status=%h",result);
        if(slave.regs[15]!=8'h0b || slave.regs[14]!=8'h1c || slave.status_writes!=1)
            $fatal(1,"OSF/control/alarms not preserved");
        $display("PASS set queued during read, frozen command, verified BCD, EOSC clear, OSF clear and alarm preservation");
        // ACKs alone are insufficient: discarded writes must fail verification.
        slave.ignore_write=1;slave.regs[15]=8'h80;
        t0=32'h00120000;t1=32'h28022902;old_status=slave.status_writes;
        command();complete();
        if(result!=32'h00020056 || valid || slave.status_writes!=old_status)
            $fatal(1,"mismatch cleared OSF or reported success %h",result);
        repeat(50000) @(negedge clk);
        if(valid) $fatal(1,"failed-set time trusted after polling");
        $display("PASS readback mismatch leaves OSF and validity untrusted");
        slave.ignore_write=0;slave.absent=1;
        command();complete();
        if(!result[2]||valid) $fatal(1,"NACK not propagated");
        slave.absent=0;command();complete();
        if(result[2]||!valid||osf) $fatal(1,"retry failed %h",result);
        $display("PASS NACK bounded failure, explicit retry recovery");
        slave.regs[15]=8'h80;slave.ignore_status_clear=1;
        command();complete();
        if(result[7:0]!=8'h66 || valid || !osf) $fatal(1,"OSF clear failure hidden %h",result);
        slave.ignore_status_clear=0;command();complete();
        if(result[2] || !valid || osf) $fatal(1,"OSF clear retry failed");
        $display("PASS status readback detects OSF not cleared and recovers on retry");
        $finish;
    end
    initial begin #150000000;$fatal(1,"global timeout");end
endmodule
