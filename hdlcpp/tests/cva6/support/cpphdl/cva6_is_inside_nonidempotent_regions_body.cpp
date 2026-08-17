for (unsigned k = 0; k < Cfg.NrNonIdempotentRules; ++k) {
    if (range_check(Cfg.NonIdempotentAddrBase[k], Cfg.NonIdempotentLength[k], address)) return 1;
}
return 0;
