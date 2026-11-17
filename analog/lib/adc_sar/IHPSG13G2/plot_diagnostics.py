#!/usr/bin/env python3
"""Render measured sampling and decision failures from accepted Analog++ traces."""
import argparse
import csv
import hashlib
import json
from pathlib import Path

import matplotlib.pyplot as plt


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def window(path, start_ns, end_ns):
    with path.open(newline='') as stream:
        rows = [r for r in csv.DictReader(stream)
                if len(r) >= 12 and start_ns <= float(r['time_s'])*1e9 <= end_ns]
    if not rows:
        raise ValueError(f'no complete accepted points in {path}')
    return rows


def arrays(rows, name):
    return [float(r[name]) for r in rows]


def edge(rows, output):
    t = [x*1e9 for x in arrays(rows, 'time_s')]
    fig, ax = plt.subplots(figsize=(9, 4.5))
    ax.plot(t, arrays(rows, 'IN'), color='#b7552c', label='Upstream IN, 50 Ω source')
    ax.plot(t, arrays(rows, 'held'), color='#185d9d', label='Held sampling node')
    ax.axvline(28.025, color='#555', ls='--', lw=1, label='Hold measurement begins')
    ax.axhline(.85, color='#888', ls=':', lw=1)
    mid = min(rows, key=lambda r: float(r['held']))
    ax.annotate(f"Held trough {float(mid['held']):.4f} V",
                (float(mid['time_s'])*1e9, float(mid['held'])),
                xytext=(28.095, .815), arrowprops={'arrowstyle': '->'}, color='#185d9d')
    ax.set(xlim=(27.95, 28.25), ylim=(.74, .88), xlabel='Time (ns)', ylabel='Voltage (V)',
           title='Measured one-slice sampling edge: input kickback and hold dip')
    ax.grid(alpha=.2); ax.legend(loc='lower left', fontsize=8)
    fig.tight_layout(); fig.savefig(output, dpi=180); plt.close(fig)


def decision(rows, output):
    t = [x*1e9 for x in arrays(rows, 'time_s')]
    fig, (hv, core) = plt.subplots(2, 1, figsize=(9, 6), sharex=True)
    for name, color in [('positive', '#185d9d'), ('negative', '#b7552c'),
                        ('latch_q', '#6d468e')]:
        hv.plot(t, arrays(rows, name), label=name, color=color)
    hv.set(ylabel='3.3 V domain (V)', ylim=(-.15, 3.6))
    hv.legend(loc='center left', fontsize=8); hv.grid(alpha=.2)
    core.plot(t, arrays(rows, 'interface_drive'), color='#b7552c', label='Interface intermediate')
    core.plot(t, arrays(rows, 'DECISION'), color='#185d9d', label='Core decision')
    core.axhline(.3*1.195, color='#185d9d', ls=':', lw=1, label='Valid-low ceiling')
    core.axhline(.7*1.195, color='#888', ls=':', lw=1, label='Valid-high floor')
    for ax in (hv, core):ax.axvline(90.975, color='#aa182c', ls='--', lw=1)
    at = min(rows, key=lambda r: abs(float(r['time_s'])*1e9-90.975))
    core.annotate(f"Read point: {float(at['DECISION']):.3f} V, invalid",
                  (90.975, float(at['DECISION'])), xytext=(89.4, .8),
                  arrowprops={'arrowstyle': '->'}, color='#aa182c')
    core.set(xlim=(89, 91), ylim=(-.1, 1.4), xlabel='Time (ns)', ylabel='1.2 V domain (V)')
    core.grid(alpha=.2); core.legend(loc='upper right', fontsize=8)
    fig.suptitle('SS / 125 C, 1.15 V: comparator resolves, core decision arrives late\n'
                 'Extracted-RC diagnostic with unplaced fourfold precharge sizing')
    fig.tight_layout(); fig.savefig(output, dpi=180); plt.close(fig)


def fast_hold(rows, output):
    held = [(float(r['time_s'])*1e9, float(r['held'])) for r in rows
            if 178.025 <= float(r['time_s'])*1e9 <= 210.975]
    low = min(held, key=lambda x: x[1])
    high = max(held, key=lambda x: x[1])
    pp_mv = (high[1] - low[1])*1e3
    fig, ax = plt.subplots(figsize=(9, 4.5))
    ax.plot([float(r['time_s'])*1e9 for r in rows], arrays(rows, 'held'),
            color='#185d9d', label='Extracted held node')
    ax.axhspan(low[1], low[1]+.020, color='#5aa66a', alpha=.15,
               label='20 mV allowed span from measured minimum')
    ax.scatter([low[0], high[0]], [low[1], high[1]], color='#aa182c', zorder=3)
    ax.annotate(f'Min {low[1]:.4f} V', low, xytext=(180, 1.737),
                arrowprops={'arrowstyle': '->'}, color='#aa182c')
    ax.annotate(f'Max {high[1]:.4f} V', high, xytext=(195, 1.766),
                arrowprops={'arrowstyle': '->'}, color='#aa182c')
    ax.text(198, 1.751, f'Measured span {pp_mv:.3f} mV\nLimit 20 mV: FAIL',
            color='#aa182c', fontsize=10)
    ax.set(xlim=(177.9, 211), ylim=(1.725, 1.77), xlabel='Time (ns)',
           ylabel='Held voltage (V)',
           title='FF / -40 C, 1.75 V: one-slice hold variation after placement')
    ax.grid(alpha=.2); ax.legend(loc='lower right', fontsize=8)
    fig.tight_layout(); fig.savefig(output, dpi=180); plt.close(fig)


def clock_skew(rows, output):
    t = [x*1e9 for x in arrays(rows, 'time_s')]
    fig, (clock, signal) = plt.subplots(2, 1, figsize=(9, 6), sharex=True)
    clock.plot(t, arrays(rows, 'switch_n_gate'), color='#b7552c', label='NMOS gate: TRACK')
    clock.plot(t, arrays(rows, 'switch_p_gate'), color='#6d468e', label='PMOS gate: local complement')
    clock.axvline(28.05, color='#aa182c', ls='--', lw=1)
    clock.text(28.055, 2.6, 'Input trough while both gates still transitioning',
               color='#aa182c', fontsize=8)
    clock.set(ylabel='Switch gate (V)', ylim=(-.5, 3.5));clock.grid(alpha=.2)
    clock.legend(loc='upper right', fontsize=8)
    signal.plot(t, arrays(rows, 'IN'), color='#b7552c', label='Upstream IN')
    signal.plot(t, arrays(rows, 'held'), color='#185d9d', label='Held node')
    signal.axvline(28.05, color='#aa182c', ls='--', lw=1)
    signal.set(xlim=(27.95, 28.3), ylim=(.74, .89), xlabel='Time (ns)',
               ylabel='Voltage (V)');signal.grid(alpha=.2)
    signal.legend(loc='lower right', fontsize=8)
    fig.suptitle('Measured placement: complement clock arrives after TRACK falls')
    fig.tight_layout();fig.savefig(output, dpi=180);plt.close(fig)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--edge-trace', type=Path, required=True)
    p.add_argument('--decision-trace', type=Path, required=True)
    p.add_argument('--fast-hold-trace', type=Path)
    p.add_argument('--clock-trace', type=Path)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args(); a.output.mkdir(parents=True, exist_ok=True)
    edge(window(a.edge_trace, 27.95, 28.25), a.output/'sampling_edge_dip.png')
    decision(window(a.decision_trace, 89, 91), a.output/'slow_hot_decision_delay.png')
    if a.fast_hold_trace:
        fast_hold(window(a.fast_hold_trace, 177.9, 211),
                  a.output/'fast_cold_hold_failure.png')
    if a.clock_trace:
        clock_skew(window(a.clock_trace, 27.95, 28.3),
                   a.output/'sampling_clock_skew.png')
    inputs = {str(x.resolve()): digest(x) for x in
              (a.edge_trace, a.decision_trace, a.fast_hold_trace,
               a.clock_trace, Path(__file__)) if x}
    outputs = {x.name: digest(x) for x in a.output.glob('*.png')}
    (a.output/'plot_receipt.json').write_text(json.dumps({'inputs_sha256': inputs,
                                                          'outputs_sha256': outputs}, indent=2)+'\n')


if __name__ == '__main__':
    main()
