// ============================================================================
//  top.v  --  iCE40UP5K-SG48I  bring-up / bitstream-flow test design
//
//  Purpose:
//      Minimal "assign out = in" pass-through used to validate the whole
//      tool + programming chain:
//          Radiant synthesis -> .bin bitstream -> MCU internal flash ->
//          STM32U585 SPI slave-configuration of the iCE40 -> live output.
//
//  Function:
//      FPGA_CLKOUT (pin 21, IOB_23b)  =  FPGA_CLKIN (pin 20, IOB_25b_G3)
//
//      FPGA_CLKIN  comes from BUFF1 output CLK0 (38.4 MHz board oscillator).
//      FPGA_CLKOUT goes to the U.FL / IPEX test connector, so a scope on that
//      connector is a direct pass/fail indicator: 38.4 MHz present == the
//      bitstream was loaded and the FPGA is configured;
//      silent == configuration failed.
//
//  Note on the config-SPI bank (pins 14/15/16/17):
//      FPGA_SPIMISO / FPGA_SPICLK / FPGA_SPICS / FPGA_SPIMOSI are the
//      dedicated configuration pins.  They are deliberately NOT used by this
//      design so they stay free for the MCU to (re)configure the device.
//
//  Device : iCE40UP5K-SG48I
//  Banks  : all used pins are in Bank 2 (VCCIO_2 = FPGA_1V8 = 1.8 V)
// ============================================================================

`default_nettype none

module top (
    input  wire FPGA_CLKIN,     // pin 20, IOB_25b_G3  <- CLK0 from BUFF1, 38.4 MHz
    output wire FPGA_CLKOUT     // pin 21, IOB_23b     -> IPEX / U.FL test connector
);

    // out = in
    assign FPGA_CLKOUT = FPGA_CLKIN;

endmodule

`default_nettype wire
