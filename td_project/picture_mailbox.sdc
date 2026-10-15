# Bundled-data CDC, APB -> MIPI: held changes with req and is held until ack2.
# Destination captures held only after req1/req2/req3 (>=3 destination edges).
# Bound data flight to <1 destination period (8.888ns), preserving >2 cycles
# settling margin. There is no same-edge hold relationship between these clocks.
# Do NOT except destination pending->active, req1->req2, or the whole domains.
set_max_delay -datapath_only 8.000 -through [get_nets {u_picture_isp/held*}] \
    -to [get_clocks {MIPI_RX_BYTE_CLK}]
set_false_path -hold -through [get_nets {u_picture_isp/held*}] \
    -to [get_clocks {MIPI_RX_BYTE_CLK}]
