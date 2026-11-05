# Finish physical top ports after internal analog/RTL crossings are joined.
set work $::env(SHARED_WORK)
set pdk "$::env(SHARED_PDK)/ihp-sg13g2/libs.ref"
read_db "$work/connected.odb"
read_liberty "$pdk/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib"
read_liberty "$pdk/sg13g2_sram/lib/RM_IHPSG13_2P_1024x16_c2_bm_bist_typ_1p20V_25C.lib"
place_pins -hor_layers Metal3 -ver_layers Metal2
write_db "$work/connected.odb"
write_def "$work/connected.def"
