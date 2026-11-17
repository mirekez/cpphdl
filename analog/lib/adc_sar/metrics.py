"""ADC measurement methods; no circuit predictions or invented acceptance budgets.

A spectral result describes the supplied record and its bandwidth/window only.
DNL requires measured transition boundaries, not a sparse voltage/code sweep.
"""
import cmath
import math


def spectrum(values, fundamental, harmonics=5):
    n = len(values)
    if n < 32 or n % 2 or not 0 < fundamental < n // 2:
        raise ValueError('need an even coherent record and an interior fundamental bin')
    if any(not math.isfinite(x) for x in values):
        raise ValueError('nonfinite spectral input')
    mean = sum(values) / n
    bins = [abs(sum((v-mean)*cmath.exp(-2j*math.pi*k*i/n)
                    for i, v in enumerate(values)))**2 / n**2
            for k in range(n//2+1)]
    bins = [p if k in (0, n//2) else 2*p for k, p in enumerate(bins)]
    signal = bins[fundamental]
    if signal <= 1e-30:
        raise ValueError('missing fundamental')
    harmonic_bins = {min((h*fundamental) % n, n-(h*fundamental) % n)
                     for h in range(2, harmonics+1)} - {0, fundamental}
    distortion = sum(bins[k] for k in harmonic_bins)
    other = sum(p for k, p in enumerate(bins) if k not in (0, fundamental))
    noise = sum(p for k, p in enumerate(bins) if k not in harmonic_bins | {0, fundamental})
    spur = max(p for k, p in enumerate(bins) if k not in (0, fundamental))
    ratio = lambda a, b: 10*math.log10(a/b) if b else math.inf
    sndr = ratio(signal, other)
    return dict(snr_db=ratio(signal, noise), sndr_db=sndr,
                enob_at_stimulus=(sndr-1.76)/6.02, sfdr_db=ratio(signal, spur),
                thd_db=ratio(distortion, signal) if distortion else -math.inf)


def static_linearity(transitions):
    """Endpoint-fit INL/DNL from ordered measured boundaries including endpoints."""
    if len(transitions) < 3 or any(not math.isfinite(x) for x in transitions):
        raise ValueError('need finite ordered transition boundaries')
    widths = [b-a for a, b in zip(transitions, transitions[1:])]
    if min(widths) < 0 or sum(widths) <= 0:
        raise ValueError('invalid/nonmonotonic boundaries')
    lsb = sum(widths)/len(widths)
    dnl = [width/lsb-1 for width in widths]
    inl = [(v-transitions[0])/lsb-i for i, v in enumerate(transitions)]
    return dict(dnl_lsb=dnl, inl_lsb=inl, missing_codes=sum(w == 0 for w in widths))


def wilson(passed, total, z=1.959963984540054):
    """Two-sided binomial interval; counts alone do not define process yield."""
    if not isinstance(total, int) or not isinstance(passed, int) or not 0 <= passed <= total or total == 0:
        raise ValueError('invalid trial counts')
    p = passed/total
    denominator = 1+z*z/total
    center = (p+z*z/(2*total))/denominator
    half = z*math.sqrt(p*(1-p)/total+z*z/(4*total*total))/denominator
    return max(0, center-half), min(1, center+half)


def transition_linearity(codes, low, high, nominal_lsb):
    """Linearity of COMPLETE code intervals bounded by measured transitions.

    T[k] is the boundary from k-1 to k. Only bins k_first .. k_last-1
    are complete; the operating-range endpoints are not invented transitions.
    Endpoint-fit metrics remove offset/gain. Nominal-LSB metrics retain them.
    Interval bounds conservatively propagate each measured voltage bracket.
    """
    n = len(codes)
    if n < 3 or len(low) != n or len(high) != n:
        raise ValueError('need at least three matched transition brackets')
    if any(type(k) is not int or k < 0 for k in codes) or any(b != a+1 for a,b in zip(codes,codes[1:])):
        raise ValueError('transition codes must be consecutive integers')
    if not math.isfinite(nominal_lsb) or nominal_lsb <= 0:
        raise ValueError('invalid nominal LSB')
    if any(not math.isfinite(v) for v in [*low,*high]) or any(a > b for a,b in zip(low,high)):
        raise ValueError('invalid transition voltage brackets')
    width_low = [low[i+1]-high[i] for i in range(n-1)]
    width_high = [high[i+1]-low[i] for i in range(n-1)]
    if min(width_low) <= 0:
        raise ValueError('missing/narrow code or overlapping unresolved transition brackets')
    transitions = [(a+b)/2 for a,b in zip(low,high)]
    span = codes[-1]-codes[0]
    fitted = (transitions[-1]-transitions[0])/span
    fit_low,fit_high = (low[-1]-high[0])/span,(high[-1]-low[0])/span
    if fit_low <= 0: raise ValueError('nonpositive fitted LSB')
    width = [b-a for a,b in zip(transitions,transitions[1:])]
    dnl = [w/fitted-1 for w in width]
    inl = [(v-(transitions[0]+i*fitted))/fitted for i,v in enumerate(transitions)]
    inl_bounds = []
    for i in range(n):
        if i in (0,n-1):
            inl_bounds.append([0.,0.]); continue
        fraction=i/span
        ref_low=(1-fraction)*low[0]+fraction*low[-1]
        ref_high=(1-fraction)*high[0]+fraction*high[-1]
        candidates=[v/lsb for v in (low[i]-ref_high,high[i]-ref_low) for lsb in (fit_low,fit_high)]
        inl_bounds.append([min(candidates),max(candidates)])
    return dict(transition_codes=codes,complete_codes=codes[:-1],transition_V=transitions,
                bracket_low_V=low,bracket_high_V=high,maximum_bracket_V=max(b-a for a,b in zip(low,high)),
                nominal_lsb_V=nominal_lsb,fitted_lsb_V=fitted,
                fitted_lsb_interval_V=[fit_low,fit_high],
                fitted_gain_error_fraction=fitted/nominal_lsb-1,
                fitted_offset_V=transitions[0]-codes[0]*fitted,
                width_V=width,width_low_V=width_low,width_high_V=width_high,
                dnl_lsb=dnl,inl_lsb=inl,
                dnl_nominal_lsb=[w/nominal_lsb-1 for w in width],
                transition_error_nominal_lsb=[v/nominal_lsb-k for k,v in zip(codes,transitions)],
                dnl_bounds_lsb=[[a/fit_high-1,b/fit_low-1] for a,b in zip(width_low,width_high)],
                inl_bounds_lsb=inl_bounds,
                maximum_abs_dnl_lsb=max(map(abs,dnl)),maximum_abs_inl_lsb=max(map(abs,inl)),
                missing_complete_codes=0)
