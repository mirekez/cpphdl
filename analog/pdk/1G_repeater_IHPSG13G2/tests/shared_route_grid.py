"""Check planning tracks are a strict subset and originals restore exactly."""
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[1]
script = r'''
read_lef $::env(TEST_TECH)
set db [ord::get_db]
set chip [odb::dbChip_create $db [$db getTech]]
set block [odb::dbBlock_create $chip track_test]
$block setMaxRoutingLayer 7
foreach {name pitch} {Metal2 480 Metal3 420 Metal4 480} {
    set grid [odb::dbTrackGrid_create $block [[$db getTech] findLayer $name]]
    $grid addGridPatternX 240 101 $pitch
    $grid addGridPatternY 240 101 $pitch
}
source $::env(TEST_RECIPE)
set original [$block getGCellTileSize]
shared_coarse_grid
if {[$block getGCellTileSize] != 4*$original} {error "wrong planning grid"}
foreach name {Metal2 Metal4} {
    set grid [$block findTrackGrid [[$db getTech] findLayer $name]]
    foreach axis {X Y} {
        lassign [$grid getGridPattern$axis 0] start count pitch
        if {$start != 240 || $count != 26 || $pitch != 1920} {error "not a subset of legal tracks"}
    }
}
shared_restore_tracks
if {[$block getGCellTileSize] != $original} {error "routing grid not restored"}
foreach {name pitch} {Metal2 480 Metal3 420 Metal4 480} {
    set grid [$block findTrackGrid [[$db getTech] findLayer $name]]
    foreach axis {X Y} {
        if {[$grid getGridPattern$axis 0] != [list 240 101 $pitch]} {error "physical tracks changed"}
    }
}
puts "Planning subset and exact physical-track restoration passed"
exit
'''
if __name__ == '__main__':
    import os
    with tempfile.TemporaryDirectory() as work:
        path = Path(work) / 'tracks.tcl'
        path.write_text(script)
        env = dict(os.environ, TEST_TECH=str(ROOT / 'pdk/IHPSG13G2/ihp-sg13g2/libs.ref/sg13g2_stdcell/lef/sg13g2_tech.lef'),
                   TEST_RECIPE=str(HERE / 'shared_route_grid.tcl'))
        subprocess.run([str(ROOT / 'out/repeater-toolchain/bin/openroad'), '-exit', str(path)],
                       env=env, check=True, timeout=20)
