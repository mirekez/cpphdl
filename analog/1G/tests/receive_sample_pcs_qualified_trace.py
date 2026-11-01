from receive_timing_confidence_model import ConfidenceModel
#!/usr/bin/env python3
"""Independent readiness, startup and frame-latency audit for fixed ADC samples."""
import gzip
from pathlib import Path


def audit(path, fixture_directory):
    expected_counts, source_words = [], []
    for role in (0, 1):
        tokens = list(map(int, (fixture_directory / f'role{role}.txt').read_text().split()))
        master, symbols, frames = tokens[:3]
        if master != role or frames != 2:
            raise ValueError('malformed source manifest')
        offset, source_index, words = 3 + symbols, 20000, {}
        for _ in range(frames):
            size = tokens[offset]
            offset += 1
            for byte in tokens[offset:offset + size]:
                words[source_index] = 256 | byte
                source_index += 1
            offset += size
            source_index += 24
        source_words.append(words)
        for ppm in (-1500, 0, 1500):
            expected_counts.extend([int((symbols - 40) * 2 * (1 + ppm * 1e-6)) + 1] * 3)
    counts, cases = [0] * 18, []
    current, qualified, idles, flushed, latest, tag_previous, unwrapped = -1, False, 0, 0, None, 65520, 0
    opener = gzip.open if path.suffix == '.gz' else open
    with opener(path, 'rt') as trace:
        for line in trace:
            values = list(map(int, line.rstrip().split(',')))
            if len(values) != 22 or any(x < 0 for x in values):
                raise ValueError('malformed complete-clock state')
            case, cycle, word, tag, valid, pma, reset, output, ov, fault, locked, sv, st, sf, quality, clipped, actual_idles, actual_flush, confident, timing_error, timing_period, timing_valid = values
            if case >= 18 or case < current or case > current + 1 or cycle != counts[case]:
                raise ValueError('missing or reordered clock')
            if case != current:
                confidence = ConfidenceModel()
                current, qualified, idles, flushed, latest, tag_previous, unwrapped = case, False, 0, 0, None, 65520, 0
                cases.append(dict(case=case, idle_errors=0, clipping_events=0, first_ready_clock=None,
                                  readiness_revocations=0, frame_bytes=0, first_frame_clock=None,
                                  last_idle_error_clock=None))
            result = cases[-1]
            if reset != (cycle == 0) or valid != (cycle != 0) or pma != valid:
                raise ValueError('startup still depends on elapsed training rather than ADC availability')
            if cycle and tag != ((65520 + cycle - 1) & 65535):
                raise ValueError('source sample tag changed')
            if fault or output >= 1024 or word >= 2**32 or sf >= 2**24 or st >= 65536:
                raise ValueError('transport fault or malformed signal range')
            if any(x not in (0, 1) for x in (ov, locked, sv, quality, clipped, confident, timing_valid)):
                raise ValueError('malformed Boolean signal')
            if confident != (confidence.ready and not fault and not clipped):
                raise ValueError("timing confidence differs from complete-window oracle")
            confidence.step(timing_error if timing_error < 8192 else timing_error - 16384, timing_period, timing_valid, fault or clipped, reset)
            bad = not pma or not confident or not locked or fault or clipped or (ov and output & 512)
            if quality != (qualified and not bad) or actual_idles != idles or actual_flush != flushed:
                raise ValueError('readiness differs from independent accepted-symbol model')
            if quality and result['first_ready_clock'] is None:
                result['first_ready_clock'] = cycle
            result['clipping_events'] += clipped
            if ov and locked and output & 512 and not output & 256:
                result['idle_errors'] += 1
                result['last_idle_error_clock'] = cycle
                if result['first_frame_clock'] is not None:
                    raise ValueError('receive error after source frames began')
            if ov and locked and output & 256:
                if not quality or output & 512 or latest is None:
                    raise ValueError('frame error or data accepted without hardware readiness')
                if output != source_words[case // 9].get(latest - 42, 0):
                    raise ValueError('frame byte or fixed PCS latency differs from source truth')
                result['frame_bytes'] += 1
                if result['first_frame_clock'] is None:
                    result['first_frame_clock'] = cycle
            if bad or reset:
                result['readiness_revocations'] += qualified
                qualified, idles, flushed = False, 0, 0
            elif ov and flushed < 42:
                flushed += 1
            elif ov and not qualified:
                idles = idles + 1 if output == 0 else 0
                qualified = idles == 64
            if sv:
                unwrapped += (st - tag_previous) & 65535
                tag_previous = st
                period = 1 + (-1500, 0, 1500)[(case % 9) // 3] * 1e-6
                delay = (0, .105, .9)[case % 3]
                coordinate = (20 + (unwrapped + sf / 2**24) * .5 + delay) / period - .65
                latest = round(coordinate)
            counts[case] += 1
    if counts != expected_counts or any(c['frame_bytes'] != 1598 or c['first_ready_clock'] is None for c in cases):
        raise ValueError('incomplete startup/readiness/frame coverage')
    return dict(status='pass', clocks=sum(counts), cases=cases,
                pipeline_flush_accepted_words=42, consecutive_clean_idle_words=64)
