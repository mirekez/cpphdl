# Fresh process releases global placer's large optimization buffers first.
set_thread_count 1
set work $::env(SHARED_WORK)
set pdk "$::env(SHARED_PDK)/ihp-sg13g2/libs.ref"
read_db "$work/global.odb"
read_liberty "$pdk/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib"
read_liberty "$pdk/sg13g2_sram/lib/RM_IHPSG13_2P_1024x16_c2_bm_bist_typ_1p20V_25C.lib"
set_placement_padding -global -left 1 -right 1
detailed_placement
check_placement -verbose
global_connect
write_db "$work/placed.odb"
write_def "$work/placed.def"
