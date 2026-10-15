# Recheck the completed route against the mailbox CDC contract. No placement,
# netlist or routing is modified; retain the original report for audit.
set root [file normalize [file join [file dirname [info script]] ..]]
set out [file join $root build hdmi_stats_jtagfix_20261014]
cd [file join $root td_project]
import_device ph1_35p.db -package PH1P35MDG324 -speed 3
import_db [file join $out optimized_route.db]
read_sdc [file join $root td_project picture_mailbox.sdc]
cd $out
update_timing -mode final
report_timing_status -file corrected_timing_status.txt
report_timing_summary -file corrected_final_timing.rpt
report_timing_path -through [get_nets {u_picture_isp/held*}] -ep_num 64 -path_num 64 -file mailbox_data_timing.rpt
report_timing_exception -ep_num 64 -path_num 64 -file mailbox_exceptions.rpt
report_qor -step route -file corrected_route.qor
export_db corrected_route.db
bitgen -bit camera_to_dsi_display_hdmi_stats_jtagfix.bit
puts HDMI_JTAGFIX_CDC_RECHECK_COMPLETE
exit
