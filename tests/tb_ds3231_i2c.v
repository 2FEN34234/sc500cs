`timescale 1ns/1ps
// Independent slave reacts only to pad edges, never to master state.
module rtc_test_slave(inout wire scl, sda);
    reg drive_low=0, stretch_low=0, stuck_scl=0, stuck_sda=0;
    reg absent=0, active=0, reading=0, read_finished=0;
    reg [7:0] shift=0, pointer=0, value=0;
    integer bits=0, byte_in_segment=0, total_bytes=0;
    integer nack_at=0, starts=0, stops=0, master_nacks=0, observed_count=0;
    integer read_segments=0;
    reg [7:0] observed[0:4095];
    reg [7:0] regs[0:15];
    reg rollover=0;
    reg stretch_enable=0, stretched=0;
    assign scl=(stretch_low || stuck_scl) ? 1'b0 : 1'bz;
    assign sda=(drive_low || stuck_sda) ? 1'b0 : 1'bz;
    // Fault injection is an interrupted transfer, not a new slave command.
    always @(negedge sda) if(scl===1'b1 && !stuck_sda && !absent) begin
        if(!active) total_bytes=0;
        active=1; bits=0; byte_in_segment=0; reading=0; read_finished=0;
        starts=starts+1;
    end
    always @(posedge sda) if(scl===1'b1 && active) begin active=0; stops=stops+1; end
    always @(posedge scl) if(active && !absent && !stuck_sda) begin
        if(reading) begin
            if(bits<8) bits=bits+1;
            else if(!read_finished) begin
                if(sda!==1'b1) $fatal(1,"Master must NACK its final read byte");
                master_nacks=master_nacks+1; read_finished=1;
            end
        end else if(bits<8) begin
            shift={shift[6:0],sda}; bits=bits+1;
            if(bits==8) begin
                observed[observed_count]=shift; observed_count=observed_count+1;
                if(byte_in_segment==0 && shift!=8'hd0 && shift!=8'hd1)
                    $fatal(1,"Incorrect address byte %h",shift);
                if(byte_in_segment==1) pointer=shift;
            end
        end else begin
            bits=0; total_bytes=total_bytes+1;
            if(byte_in_segment==0 && shift==8'hd1 && total_bytes!=nack_at) begin
                reading=1; read_segments=read_segments+1;
                if(pointer>15) $fatal(1,"Invalid RTC register %h",pointer);
                if(rollover && pointer==1) begin
                    regs[0]=0; regs[1]=0; regs[2]=0;
                    regs[3]=2; regs[4]=8'h13; rollover=0;
                end
                value=regs[pointer];
            end
            byte_in_segment=byte_in_segment+1;
        end
    end
    always @(negedge scl) begin
        if(!active || absent || stuck_sda) drive_low=0;
        else if(reading) drive_low=(bits<8) ? !value[7-bits] : 0;
        else drive_low=(bits==8) && (total_bytes+1!=nack_at);
    end
    always @(negedge scl) if(stretch_enable && !stretched && active) begin
        stretched=1; stretch_low=1; #50000; stretch_low=0;
    end
endmodule


module tb_ds3231_i2c;
    reg clk=0; always #10 clk=~clk;
    reg rst_n=0;
    tri1 scl,sda;
    wire [7:0] sec,min,hr,day,date,month,year;
    wire busy,valid,error,osf;
    wire [3:0] fault;
    wire [31:0] count;
    ds3231_i2c_ctrl #(.CLK_DIV(49),.POLL_CYCLES(1000),.GAP_CYCLES(10)) dut(
        .I_clk(clk),.I_rst_n(rst_n),.IO_scl(scl),.IO_sda(sda),
        .O_seconds(sec),.O_minutes(min),.O_hours(hr),.O_day(day),
        .O_date(date),.O_month(month),.O_year(year),.O_busy(busy),
        .O_valid(valid),.O_error(error),.O_fault(fault),.O_osf(osf),.O_read_count(count));
    rtc_test_slave slave(scl,sda);
    reg [55:0] prior=0;
    reg [31:0] prior_count=0;
    // Visible APB fields may only change when a complete snapshot commits.
    always @(negedge clk) if(rst_n) begin
        if({year,month,date,day,hr,min,sec}!==prior && count==prior_count)
            $fatal(1,"Partial snapshot exposed");
        prior={year,month,date,day,hr,min,sec}; prior_count=count;
    end
    task next_snapshot;
        integer old_count,i;
        begin : waiting
            old_count=count;
            for(i=0;i<800000;i=i+1) begin
                @(posedge clk); #1;
                if(count!=old_count) disable waiting;
            end
            $fatal(1,"RTC refresh timeout");
        end
    endtask
    task wait_error;
        integer i;
        begin : waiting
            for(i=0;i<800000;i=i+1) begin
                @(posedge clk); #1;
                if(error && !busy) disable waiting;
            end
            $fatal(1,"Fault not reported");
        end
    endtask
    integer i,old_count;
    reg [55:0] saved;
    initial begin
        for(i=0;i<16;i=i+1) slave.regs[i]=0;
        slave.regs[0]=8'h59; slave.regs[1]=8'h59; slave.regs[2]=8'h23;
        slave.regs[3]=1; slave.regs[4]=8'h12; slave.regs[5]=8'h10; slave.regs[6]=8'h26;
        repeat(10) @(negedge clk); rst_n=1;
        next_snapshot();
        if(!valid || osf || error || {year,month,date,day,hr,min,sec}!==56'h26101201235959)
            $fatal(1,"Initial BCD snapshot wrong");
        if(slave.starts!=18 || slave.stops!=9 || slave.master_nacks!=9)
            $fatal(1,"Repeated START / final NACK missing");
        $display("PASS: RTC D0/D1 addresses, 7 BCD bytes, repeated START/NACK, atomic publish");
        slave.rollover=1; next_snapshot();
        if({year,month,date,day,hr,min,sec}!==56'h26101302000000)
            $fatal(1,"Midnight rollover mixed dates");
        $display("PASS: rollover re-reads complete date/time");
        slave.regs[15]=8'h80; next_snapshot();
        if(valid || !osf || error) $fatal(1,"OSF must mark time untrusted");
        slave.regs[15]=0; slave.regs[2]=8'h72; next_snapshot();
        if(!valid || osf || hr!=8'h72) $fatal(1,"12-hour bits lost");
        $display("PASS: OSF validity and raw 12-hour encoding");
        saved={year,month,date,day,hr,min,sec}; old_count=count;
        slave.absent=1; wait_error();
        if(valid || fault!=1 || count!=old_count || saved!={year,month,date,day,hr,min,sec})
            $fatal(1,"NACK must retain last data but invalidate it");
        slave.absent=0; next_snapshot();
        if(!valid || error) $fatal(1,"Reconnect failed");
        $display("PASS: absent RTC NACK and automatic reconnect");
        slave.stuck_scl=1; wait_error();
        if(valid || fault!=2) $fatal(1,"SCL timeout diagnostic");
        slave.stuck_scl=0; slave.active=0; next_snapshot();
        if(!valid || error) $fatal(1,"Stuck bus recovery failed");
        $display("PASS: bounded stuck-SCL timeout and recovery");
        $finish;
    end
    initial begin #100000000; $fatal(1,"Global timeout"); end
endmodule
