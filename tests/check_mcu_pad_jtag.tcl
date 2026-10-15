# Board integration contract: the generated PIB inputs are tied low, so the
# hard MCU must select PAD JTAG. Do not silently accept a regenerated hierarchy.
proc check_mcu_pad_jtag {root} {
    set f [open [file join $root user_source hdl_source design_top_wrapper.v] r]
    set top [read $f]; close $f
    set f [open [file join $root td_project al_ip RISCV_F6F1D1P0_0 RISCV_F6F1D1P0_0.v] r]
    set wrapper [read $f]; close $f
    if {![regexp {defparam\s+u_mcu\.MCU_521bd43d073e_Inst\.MCU_JTAG_SEL\s*=\s*"0"\s*;} $top]} {
        error "Board PAD JTAG override missing: do not build a bit with CPU debug disconnected"
    }
    if {![regexp {\)\s*MCU_521bd43d073e_Inst\s*\(} $wrapper]} {
        error "Generated MCU instance changed: review the board PAD JTAG override before building"
    }
    set f [open [file join $root td_project al_ip RISCV_F6F1D1P0_0 RTL MCU_521bd43d073e.v] r]
    set mcu [read $f]; close $f
    if {![regexp {\.MCU_JTAG_SEL\("0"\)} $wrapper] ||
        ![regexp -nocase {\.MCU_RESET_VECTOR\('h20e00000\)} $wrapper] ||
        ![regexp {\.dbg_stop_on_reset\(1'b0\)} $mcu]} {
        error "MCU regeneration changed the working RTC boot/debug baseline: require PAD=0, vector=20e00000, dbg_stop_on_reset=0"
    }
    puts "MCU_PAD_JTAG_PREFLIGHT_OK: PAD=0 vector=20e00000 dbg_stop_on_reset=0; hardware scan still required"
}
