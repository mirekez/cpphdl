# OpenROAD 08f67ee5ec derives GCell size from 15 times the median M2/M3/M4
# track pitch (odb/src/db/dbBlock.cpp:getGCellTileSize). Plan conservatively
# on a subset of legal M2/M4 tracks, then restore every physical track before
# detailed routing. No foundry spacing rule or routing capacity is relaxed.
proc shared_coarse_grid {} {
    global shared_saved_tracks
    set block [ord::get_db_block]
    set tech [[ord::get_db] getTech]
    puts "Original global-routing tile: [$block getGCellTileSize] DBU"
    foreach name {Metal2 Metal4} {
        set layer [$tech findLayer $name]
        set grid [$block findTrackGrid $layer]
        foreach axis {X Y} {
            set shared_saved_tracks($name,$axis) {}
            set count [$grid getNumGridPatterns$axis]
            for {set i 0} {$i < $count} {incr i} {
                lappend shared_saved_tracks($name,$axis) [$grid getGridPattern$axis $i]
            }
        }
        odb::dbTrackGrid_destroy $grid
        set grid [odb::dbTrackGrid_create $block $layer]
        foreach axis {X Y} {
            foreach pattern $shared_saved_tracks($name,$axis) {
                lassign $pattern origin count pitch
                $grid addGridPattern$axis $origin [expr {($count+3)/4}] [expr {$pitch*4}]
            }
        }
    }
    puts "Conservative planning tile: [$block getGCellTileSize] DBU"
}
proc shared_restore_tracks {} {
    global shared_saved_tracks
    set block [ord::get_db_block]
    set tech [[ord::get_db] getTech]
    foreach name {Metal2 Metal4} {
        set layer [$tech findLayer $name]
        odb::dbTrackGrid_destroy [$block findTrackGrid $layer]
        set grid [odb::dbTrackGrid_create $block $layer]
        foreach axis {X Y} {
            foreach pattern $shared_saved_tracks($name,$axis) {
                $grid addGridPattern$axis {*}$pattern
            }
        }
    }
    puts "Restored original physical routing tracks"
}
