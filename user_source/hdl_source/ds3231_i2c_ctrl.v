// DS3231 real-time-clock reader on a dedicated open-drain I2C bus.
//
// The device is read at its standard 7-bit address 0x68.  The controller
// fetches seconds, minutes, hours, day, date, month and year (registers
// 0x00..0x06) once per second.  Values remain in the DS3231's BCD format so
// the MCU can display them without losing the original register contents.
module ds3231_i2c_ctrl #(
    parameter integer CLK_DIV = 16'd499,       // 50 MHz -> approximately 100 kHz
    parameter integer POLL_CYCLES = 50000000,  // one refresh per second
    parameter integer GAP_CYCLES = 1000
)(
    input  wire       I_clk,
    input  wire       I_rst_n,
    inout  wire       IO_scl,
    inout  wire       IO_sda,
    output reg [7:0]  O_seconds,
    output reg [7:0]  O_minutes,
    output reg [7:0]  O_hours,
    output reg [7:0]  O_day,
    output reg [7:0]  O_date,
    output reg [7:0]  O_month,
    output reg [7:0]  O_year,
    output reg        O_busy,
    output reg        O_valid,
    output reg        O_error,
    output reg [3:0]  O_fault,
    output reg        O_osf,
    output reg [31:0] O_read_count
);
    localparam [2:0] ST_WAIT = 3'd0;
    localparam [2:0] ST_START = 3'd1;
    localparam [2:0] ST_XFER = 3'd2;
    localparam [2:0] ST_GAP = 3'd3;

    reg [2:0] state;
    reg [3:0] index;
    reg [7:0] pending [0:6];
    reg [1:0] rollover_retries;
    reg [31:0] poll_count;
    reg [31:0] gap_count;
    reg start;
    wire done;
    wire [2:0] ack_mask;
    wire [3:0] fault;
    wire [7:0] nack_byte;
    wire [7:0] rx_data;
    wire [31:0] bus_state_unused;

    // The generic master is the same bounded-recovery engine used by VCM;
    // only the slave address and bus pins differ.
    lens_i2c_master #(
        .PERIOD_CYCLES (CLK_DIV + 1),
        .SLAVE_ADDR    (7'h68)
    ) u_ds3231_bus (
        .clk       (I_clk),
        .rst_n     (I_rst_n),
        .start     (start),
        .read_reg  (1'b1),
        .reg_addr  (index == 8 ? 8'h0f : index == 7 ? 8'h00 : {4'd0, index}),
        .reg_value (8'd0),
        .scl       (IO_scl),
        .sda       (IO_sda),
        .done      (done),
        .ack_mask  (ack_mask),
        .fault     (fault),
        .nack_byte (nack_byte),
        .rx_data   (rx_data),
        .bus_state (bus_state_unused)
    );

    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            state       <= ST_WAIT;
            index       <= 3'd0;
            poll_count  <= 32'd1000;
            gap_count   <= 32'd0;
            start       <= 1'b0;
            O_seconds   <= 8'd0;
            O_minutes   <= 8'd0;
            O_hours     <= 8'd0;
            O_day       <= 8'd0;
            O_date      <= 8'd0;
            O_month     <= 8'd0;
            O_year      <= 8'd0;
            O_busy      <= 1'b0;
            O_valid     <= 1'b0;
            O_error     <= 1'b0;
            O_fault     <= 4'd0;
            O_osf       <= 1'b1;
            rollover_retries <= 0;
            O_read_count <= 32'd0;
        end else begin
            start <= 1'b0;
            case (state)
                ST_WAIT: begin
                    O_busy <= 1'b0;
                    if (poll_count == 0) begin
                        index <= 3'd0;
                        rollover_retries <= 0;
                        state <= ST_START;
                        O_busy <= 1'b1;
                    end else begin
                        poll_count <= poll_count - 1'b1;
                    end
                end
                ST_START: begin
                    // A one-cycle request starts one register read.
                    start <= 1'b1;
                    state <= ST_XFER;
                end
                ST_XFER: begin
                    if (done) begin
                        if (fault != 0 || ack_mask != 3'b111) begin
                            O_error <= 1'b1;
                            O_valid <= 1'b0;
                            O_fault <= (fault != 0) ? fault : 4'd1;
                            O_busy <= 1'b0;
                            poll_count <= POLL_CYCLES;
                            state <= ST_WAIT;
                        end else if (index == 7 && rx_data != pending[0]) begin
                            // Every START refreshes the DS3231 shadow buffer.
                            // Repeat the entire scan if a second rolled over.
                            if (rollover_retries == 2) begin
                                O_valid <= 0; O_error <= 1; O_fault <= 4;
                                O_busy <= 0; poll_count <= POLL_CYCLES;
                                state <= ST_WAIT;
                            end else begin
                                rollover_retries <= rollover_retries + 1'b1;
                                index <= 0; gap_count <= GAP_CYCLES;
                                state <= ST_GAP;
                            end
                        end else begin
                            if (index < 7) pending[index] <= rx_data;
                            if (index == 8) begin
                                // Publish one coherent snapshot only after all
                                // transfers succeed. OSF needs explicit setting
                                // of the clock; this reader never clears it.
                                O_seconds <= pending[0]; O_minutes <= pending[1];
                                O_hours <= pending[2]; O_day <= pending[3];
                                O_date <= pending[4]; O_month <= pending[5];
                                O_year <= pending[6]; O_osf <= rx_data[7];
                                O_valid <= !rx_data[7];
                                O_error <= 1'b0;
                                O_fault <= 4'd0;
                                O_read_count <= O_read_count + 1'b1;
                                O_busy <= 1'b0;
                                poll_count <= POLL_CYCLES;
                                state <= ST_WAIT;
                            end else begin
                                index <= index + 1'b1;
                                gap_count <= GAP_CYCLES;
                                state <= ST_GAP;
                            end
                        end
                    end
                end
                ST_GAP: begin
                    if (gap_count == 0)
                        state <= ST_START;
                    else
                        gap_count <= gap_count - 1'b1;
                end
                default: state <= ST_WAIT;
            endcase
        end
    end
endmodule
