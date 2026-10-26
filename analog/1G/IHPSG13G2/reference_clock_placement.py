"""Optimize PLL groups with all three supply rails excluded from signal cost."""
from collections import defaultdict
import math
import random


def routing_cost(order, widths, nets, net_weights, *, row_width_um=None,
                 prefix_width_um=0, row_pitch_um=0, left_anchored_nets=()):
    """Estimate the actual left-spine routes used by analog_row_fold.

    A short connection across a row cut traverses both row buses and their
    connecting spine. Linear HPWL alone incorrectly treats it as a short wire.
    """
    locations, x, row = {}, prefix_width_um, 0
    for index in order:
        width = widths[index]
        if row_width_um is not None and x + width > row_width_um:
            row += 1
            x = 0
        locations[index] = (row, x + width / 2)
        x += width
    total = 0.
    for net, indices in nets.items():
        rows = defaultdict(list)
        for index in indices:
            row, x = locations[index]
            rows[row].append(x)
        if net in left_anchored_nets:
            for points in rows.values():
                points.append(0)
        if len(rows) == 1:
            points = next(iter(rows.values()))
            length = max(points) - min(points)
        else:
            length = sum(max(points) for points in rows.values())
            length += (max(rows) - min(rows)) * row_pitch_um
        total += net_weights.get(net, 1) * length
    return total


def placement_order(parts, dbu, *, power_nets, net_weights, row_width_um=None,
                    prefix_width_um=0, row_pitch_um=0, left_anchored_nets=()):
    widths = [cell.bbox().width() * dbu + 10 for cell, _, _, _ in parts]
    if (not math.isfinite(prefix_width_um) or prefix_width_um < 0 or
            not math.isfinite(row_pitch_um) or row_pitch_um < 0 or
            (row_width_um is not None and (not math.isfinite(row_width_um) or
             row_width_um <= 0 or prefix_width_um > row_width_um or
             any(width > row_width_um for width in widths)))):
        raise ValueError('invalid folded placement dimensions')
    nets = defaultdict(list)
    for index, (_, _, connections, _) in enumerate(parts):
        for net in set(connections.values()) - set(power_nets):
            nets[net].append(index)

    def cost(order):
        return routing_cost(order, widths, nets, net_weights,
            row_width_um=row_width_um, prefix_width_um=prefix_width_um,
            row_pitch_um=row_pitch_um, left_anchored_nets=left_anchored_nets)

    order = list(range(len(parts)))
    initial = score = best_score = cost(order)
    best = list(order)
    rng = random.Random(1302)
    for iteration in range(24000 if len(parts) > 1 else 0):
        a, b = sorted(rng.sample(range(len(order)), 2))
        trial = list(order)
        if iteration % 3:
            trial[a], trial[b] = trial[b], trial[a]
        else:
            trial[a:b + 1] = reversed(trial[a:b + 1])
        proposed = cost(trial)
        temperature = 100 * (1 - iteration / 24000) ** 3 + .01
        if proposed < score or rng.random() < math.exp(min(0, (score - proposed) / temperature)):
            order, score = trial, proposed
            if score < best_score:
                best, best_score = list(order), score
    return best, dict(initial_weighted_span_um=initial, placed_weighted_span_um=best_score,
                      row_width_um=row_width_um, prefix_width_um=prefix_width_um,
                      row_pitch_um=row_pitch_um,
                      component_order=[parts[index][0].name for index in best])
