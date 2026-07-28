# ============================================================================
# timing.sdc  --  HopWINS-UWBDO  iCE40UP5K-SG48I
#
# CLK, PPS and SYNC all leave through I/O cell output registers on the CLK_IN
# net, so what matters is not their absolute delay but how closely they track
# each other. All three are held to the same physical clock-to-out window:
#
#     9.500 ns .. 11.500 ns
#
# measured 9.813 .. 11.206 ns at LVCMOS18 with DRIVE=4, so about 0.3 ns of
# margin on each side. The width of this window is the skew budget.
#
# The window has to be re-centred whenever the I/O standard or drive strength
# changes: moving from LVCMOS33/8 mA to LVCMOS18/4 mA slowed every output by a
# uniform 1.56 to 1.66 ns, which left the previous 8.000..10.000 window behind
# and failed PPS and SYNC by 1.206 ns each while CLK still passed.
# ============================================================================

# 38.4 MHz -> 26.041666667 ns, half period 13.020833 ns
create_clock -name CLK_IN -period 26.041666667 [get_ports CLK_IN]

# CLK is intentionally not declared with create_generated_clock. Declaring an
# output port as a clock makes the engine treat it as a clock source, and clock
# sources are left out of the I/O Timing Analysis clock-to-out table, which is
# exactly why CLK used to be missing next to PPS and SYNC.

# ----------------------------------------------------------------------------
# All three outputs, single-edge and analysed against the full period. This is
# the constraint set for top.v with CLK_OUT_DDR = 0, where CLK is a regular
# registered output like the other two and therefore belongs in the same group
# with the same numbers.
#
#     allowed clock-to-out <= period - (-max) = 26.042 - 14.542 = 11.500 ns
#     allowed clock-to-out >= (-min)                           = 10.500 ns
#
# All three measure 11.206 ns (max, R) and 10.996 ns (min, R), identical to the
# last decimal, and all three pass with 0.293 ns of slack. The global clock net
# reaches every I/O cell at the same 6.349 ns despite the cells sitting apart on
# the die, so there is no spatial skew to budget for and this window can stay
# narrow: 1 ns wide, with roughly 0.5 ns below and 0.3 ns above the measurement.
# ----------------------------------------------------------------------------
set_output_delay -clock CLK_IN -min 10.500 [get_ports {CLK PPS SYNC}]
set_output_delay -clock CLK_IN -max 14.542 [get_ports {CLK PPS SYNC}]

# ----------------------------------------------------------------------------
# For CLK_OUT_DDR = 1 instead, CLK must be pulled out of the group above and
# constrained separately, because a DDR port drives on both edges and is
# analysed against the HALF period, 13.021 ns, not the full 26.042 ns. Reusing
# -max 14.542 there asks for a clock-to-out of 13.021 - 14.542, a negative
# number, which is unsatisfiable and lands as a large negative slack on that one
# port while the other two look nearly fine.
#
# Swap the three lines above for these four:
#
#   set_output_delay -clock CLK_IN -min  9.500 [get_ports {PPS SYNC}]
#   set_output_delay -clock CLK_IN -max 14.542 [get_ports {PPS SYNC}]
#   set_output_delay -clock CLK_IN -min  9.500 [get_ports CLK]
#   set_output_delay -clock CLK_IN -max  1.521 [get_ports CLK]
#
# both expressing the same physical 9.500..11.500 ns window, the CLK -max being
# computed from 13.021 rather than 26.042.
# ----------------------------------------------------------------------------

# ----------------------------------------------------------------------------
# FPGA_EN is asynchronous and resynchronised in the RTL, so it carries no
# timing requirement. The input delay keeps it out of the "IO ports are not
# constrained" warning; the false path keeps PAR from working on it.
# ----------------------------------------------------------------------------
set_input_delay -clock CLK_IN -min 0.000 [get_ports FPGA_EN]
set_input_delay -clock CLK_IN -max 5.000 [get_ports FPGA_EN]
set_false_path -from [get_ports FPGA_EN]
