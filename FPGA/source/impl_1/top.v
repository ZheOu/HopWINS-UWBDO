// ============================================================================
//  top.v  --  HopWINS-UWBDO  iCE40UP5K-SG48I
//
//  38.4 MHz arrives on CLK_IN from the board clock buffer and leaves again on
//  CLK. PPS (1 Hz) and SYNC (one-shot) are derived from the same clock.
//
//  All three outputs feed one measurement setup, so their absolute delay does
//  not matter but their alignment with each other does. Every one of them
//  therefore leaves the die through an I/O cell output register clocked by the
//  same net:
//
//    CLK       IOL_B, form selected by CLK_OUT_DDR, see the parameter
//    PPS/SYNC  IOL_B DDROUT="NO", DO0=data      -> registered output
//
//  Structure buys the alignment here, not constraints. A plain
//  "assign CLK = CLK_IN" leaves through an input buffer, fabric routing and an
//  output buffer, skipping the clock network and the register Tco entirely, so
//  it arrives roughly 5 ns ahead of two registered outputs and no SDC can pull
//  it back. With all three in I/O registers on one clock net, only the
//  mismatch between I/O cells is left, which is hundreds of picoseconds.
//
//  IOL_B is the Radiant iCE40 I/O logic primitive. SB_IO does not exist in the
//  Radiant libraries -- it belongs to iCEcube2 -- and LSE rejects it. Behaviour
//  per cae_library/simulation/verilog/iCE40UP/IOLOGIC.v:
//
//    always @(posedge OUTCLK) if (CE) paddo_r <= DO0;
//    always @(negedge OUTCLK) if (DDROUT == "YES") if (CE) paddo_r <= DO1;
//
//  so DO0 is captured on the rising edge, DO1 only on the falling edge and
//  only when DDROUT is "YES", CE must be held High, and the path to the pad is
//  always registered. IOL_B drives the fabric side of a separate OB pad buffer,
//  whose O port is the pad itself.
// ============================================================================

`default_nettype none

module top #(
    parameter integer PPS_PERIOD_CYCLES = 38400000,
    parameter integer PPS_HIGH_CYCLES   = 7680000,
    parameter integer SYNC_PULSE_CYCLES = 1,

    // ------------------------------------------------------------------------
    // How CLK leaves the die. The two options are not equivalent and the choice
    // is a real trade-off, so it is exposed rather than buried.
    //
    //   0  Regular registered output, same cell configuration as PPS and SYNC.
    //      A single-edge I/O register can only change once per rising edge, so
    //      the best it can produce from a 38.4 MHz clock is a 19.2 MHz square
    //      wave. In exchange the port is analysed exactly like PPS and SYNC:
    //      full period, R->R on both report columns, so Clock To Out (MIN) is
    //      below (MAX) as expected, and one set_output_delay covers all three.
    //
    //   1  DDR output, driving on both clock edges, which is the only way to
    //      forward the full 38.4 MHz. The tool then analyses the port against
    //      the HALF period and reports MAX from the falling-edge path and MIN
    //      from the rising-edge path at a different corner, which is why MIN
    //      can come out above MAX.
    //
    // Measured on this board at LVCMOS18/DRIVE=4:
    //
    //   mode 0  all three ports 11.206 ns max, 10.996 ns min, R on both
    //           columns, identical to the last decimal, so zero skew
    //   mode 1  CLK 9.813 ns max on the falling edge, 10.910 ns min on the
    //           rising edge, which is where the MIN-above-MAX confusion comes
    //           from, and 0.086 ns of rising-edge skew against PPS/SYNC
    //
    // Note what mode 0 proves: the global clock net reaches all three I/O cells
    // at exactly the same time, 0.850 + 5.499 = 6.349 ns, even though they sit
    // at different places on the die (IOL_L19B, IOL_L16A, IOL_L17A). There is
    // no spatial clock skew to chase here. The 1.32 ns that mode 1 appears to
    // gain is not position, it is edge: this clock path propagates a falling
    // edge about 1.7 ns faster than a rising one, and mode 1's MAX figure is
    // measured on the falling edge.
    // ------------------------------------------------------------------------
    parameter integer CLK_OUT_DDR = 0
)(
    input  wire CLK_IN,     // site 20, PCLKT1_0, 38.4 MHz from the clock buffer
    input  wire FPGA_EN,    // site 36, MCU link, asserted after configuration

    output wire CLK,        // site 21, forwarded 38.4 MHz
    output wire SYNC,       // site 10, one-shot pulse
    output wire PPS         // site 9,  1 Hz
);

    localparam integer PPS_COUNTER_WIDTH  = clog2(PPS_PERIOD_CYCLES);
    localparam integer SYNC_COUNTER_WIDTH = clog2(SYNC_PULSE_CYCLES + 1);

    reg [PPS_COUNTER_WIDTH-1:0]  pps_counter;
    reg [SYNC_COUNTER_WIDTH-1:0] sync_counter;

    reg pps_reg;
    reg sync_reg;
    reg sync_done;

    wire clk_paddo;
    wire pps_paddo;
    wire sync_paddo;

    // ------------------------------------------------------------------------
    // FPGA_EN is driven by the MCU and has no relationship to CLK_IN, so it is
    // resynchronised before it can gate the counters. Sampling it directly
    // would let a metastable value settle differently across the counter bits
    // and put a glitch on the very edges this design exists to keep clean.
    // ------------------------------------------------------------------------
    reg [1:0] enable_sync;

    always @(posedge CLK_IN) begin
        enable_sync <= {enable_sync[0], FPGA_EN};
    end

    wire enable = enable_sync[1];

    // ------------------------------------------------------------------------
    // Counters. Behaviour unchanged: PPS_HIGH_CYCLES high out of every
    // PPS_PERIOD_CYCLES, and SYNC pulsed once for SYNC_PULSE_CYCLES after the
    // first release of the enable.
    // ------------------------------------------------------------------------
    always @(posedge CLK_IN) begin
        if (!enable) begin
            pps_counter  <= {PPS_COUNTER_WIDTH{1'b0}};
            sync_counter <= {SYNC_COUNTER_WIDTH{1'b0}};

            pps_reg   <= 1'b0;
            sync_reg  <= 1'b0;
            sync_done <= 1'b0;
        end else begin
            if (pps_counter == PPS_PERIOD_CYCLES - 1) begin
                pps_counter <= {PPS_COUNTER_WIDTH{1'b0}};
            end else begin
                pps_counter <= pps_counter + 1'b1;
            end

            if (pps_counter < PPS_HIGH_CYCLES) begin
                pps_reg <= 1'b1;
            end else begin
                pps_reg <= 1'b0;
            end

            if (!sync_done) begin
                if (sync_counter < SYNC_PULSE_CYCLES) begin
                    sync_reg     <= 1'b1;
                    sync_counter <= sync_counter + 1'b1;
                end else begin
                    sync_reg  <= 1'b0;
                    sync_done <= 1'b1;
                end
            end else begin
                sync_reg <= 1'b0;
            end
        end
    end

    // ------------------------------------------------------------------------
    // CLK, in whichever of the two forms CLK_OUT_DDR selects. Both drive the
    // same OB pad buffer, so only the I/O logic cell differs.
    // ------------------------------------------------------------------------
    generate
        if (CLK_OUT_DDR != 0) begin : g_clk_ddr

            // Full-rate forward: High presented on the rising edge, Low on the
            // falling edge, reproducing CLK_IN in phase at 38.4 MHz.
            IOL_B #(
                .LATCHIN("NONE_REG"),
                .DDROUT("YES")
            ) clk_iol (
                .PADDI(1'b0),
                .DO1(1'b0),
                .DO0(1'b1),
                .CE(1'b1),
                .IOLTO(1'b0),
                .HOLD(1'b0),
                .INCLK(1'b0),
                .OUTCLK(CLK_IN),
                .PADDO(clk_paddo),
                .PADDT(),
                .DI1(),
                .DI0()
            );

        end else begin : g_clk_sdr

            // Half-rate output from a regular single-edge register. The toggle
            // is deliberately NOT gated by the enable: this is a forwarded
            // clock, and it has to keep running whatever the MCU does with
            // FPGA_EN. It also self-starts, since toggling is correct from
            // either power-up state.
            reg clk_toggle;

            always @(posedge CLK_IN) begin
                clk_toggle <= ~clk_toggle;
            end

            IOL_B #(
                .LATCHIN("NONE_REG"),
                .DDROUT("NO")
            ) clk_iol (
                .PADDI(1'b0),
                .DO1(1'b0),
                .DO0(clk_toggle),
                .CE(1'b1),
                .IOLTO(1'b0),
                .HOLD(1'b0),
                .INCLK(1'b0),
                .OUTCLK(CLK_IN),
                .PADDO(clk_paddo),
                .PADDT(),
                .DI1(),
                .DI0()
            );

        end
    endgenerate

    OB clk_ob (.I(clk_paddo), .O(CLK));

    // ------------------------------------------------------------------------
    // PPS and SYNC: fabric counter output retimed in the I/O register. Costs
    // one extra clock of latency on each, identical for both, and removes the
    // fabric routing between the last register and the pad, which is where the
    // 0.44 ns spread between PPS and SYNC came from.
    // ------------------------------------------------------------------------
    IOL_B #(
        .LATCHIN("NONE_REG"),
        .DDROUT("NO")
    ) pps_iol (
        .PADDI(1'b0),
        .DO1(1'b0),
        .DO0(pps_reg),
        .CE(1'b1),
        .IOLTO(1'b0),
        .HOLD(1'b0),
        .INCLK(1'b0),
        .OUTCLK(CLK_IN),
        .PADDO(pps_paddo),
        .PADDT(),
        .DI1(),
        .DI0()
    );

    OB pps_ob (.I(pps_paddo), .O(PPS));

    IOL_B #(
        .LATCHIN("NONE_REG"),
        .DDROUT("NO")
    ) sync_iol (
        .PADDI(1'b0),
        .DO1(1'b0),
        .DO0(sync_reg),
        .CE(1'b1),
        .IOLTO(1'b0),
        .HOLD(1'b0),
        .INCLK(1'b0),
        .OUTCLK(CLK_IN),
        .PADDO(sync_paddo),
        .PADDT(),
        .DI1(),
        .DI0()
    );

    OB sync_ob (.I(sync_paddo), .O(SYNC));

    function integer clog2;
        input integer value;
        integer i;
        begin
            clog2 = 1;
            for (i = value - 1; i > 1; i = i >> 1) begin
                clog2 = clog2 + 1;
            end
        end
    endfunction

endmodule

`default_nettype wire
