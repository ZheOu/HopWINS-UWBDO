if {[catch {

# define run engine funtion
source [file join {D:/ice40/ice40k} scripts tcl flow run_engine.tcl]
# define global variables
global para
set para(gui_mode) "1"
set para(prj_dir) "C:/Users/zhang/OneDrive/Desktop/UWBDO/HopWINS-UWBDO/FPGA"
if {![file exists {C:/Users/zhang/OneDrive/Desktop/UWBDO/HopWINS-UWBDO/FPGA/impl_1}]} {
  file mkdir {C:/Users/zhang/OneDrive/Desktop/UWBDO/HopWINS-UWBDO/FPGA/impl_1}
}
cd {C:/Users/zhang/OneDrive/Desktop/UWBDO/HopWINS-UWBDO/FPGA/impl_1}
# synthesize IPs
# synthesize VMs
# propgate constraints
file delete -force -- HopWINS_UWBDO_FPGA_impl_1_cpe.ldc
::radiant::runengine::run_engine_newmsg cpe -syn lse -f "HopWINS_UWBDO_FPGA_impl_1.cprj" -a "iCE40UP"  -o HopWINS_UWBDO_FPGA_impl_1_cpe.ldc
# synthesize top design
file delete -force -- HopWINS_UWBDO_FPGA_impl_1.vm HopWINS_UWBDO_FPGA_impl_1.ldc
::radiant::runengine::run_engine_newmsg synthesis -f "C:/Users/zhang/OneDrive/Desktop/UWBDO/HopWINS-UWBDO/FPGA/impl_1/HopWINS_UWBDO_FPGA_impl_1_lattice.synproj" -logfile "HopWINS_UWBDO_FPGA_impl_1_lattice.srp"
::radiant::runengine::run_postsyn [list -a iCE40UP -p iCE40UP5K -t SG48 -sp High-Performance_1.2V -oc Industrial -top -w -o HopWINS_UWBDO_FPGA_impl_1_syn.udb HopWINS_UWBDO_FPGA_impl_1.vm] [list HopWINS_UWBDO_FPGA_impl_1.ldc]

} out]} {
   ::radiant::runengine::runtime_log $out
   exit 1
}
