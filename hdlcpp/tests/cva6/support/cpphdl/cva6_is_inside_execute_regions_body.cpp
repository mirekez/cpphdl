if (Cfg.NrExecuteRegionRules == 0) return 1;
for (unsigned k = 0; k < Cfg.NrExecuteRegionRules; ++k) {
    if (range_check(Cfg.ExecuteRegionAddrBase[k], Cfg.ExecuteRegionLength[k], address)) return 1;
}
return 0;
