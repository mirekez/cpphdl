for (unsigned k = 0; k < Cfg.NrCachedRegionRules; ++k) {
    if (range_check(Cfg.CachedRegionAddrBase[k], Cfg.CachedRegionLength[k], address)) return 1;
}
return 0;
