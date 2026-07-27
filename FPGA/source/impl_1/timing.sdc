# ============================================================================
# timing.sdc  --  clk_passthru
#
# 38.4 MHz board oscillator -> FPGA_CLKIN, forwarded 1:1 to FPGA_CLKOUT.
# The design is purely combinational, so this file only documents the input
# frequency and declares the forwarded clock at the output port.
# ============================================================================

create_clock -name FPGA_CLKIN -period 26.041666667 [get_ports FPGA_CLKIN]

create_generated_clock -name FPGA_CLKOUT \
    -source [get_ports FPGA_CLKIN] \
    -divide_by 1 \
    [get_ports FPGA_CLKOUT]
