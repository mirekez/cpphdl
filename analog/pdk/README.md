# IHP SG13G2 setup

The [single-reference 1G repeater project](1G_repeater_IHPSG13G2/README.md)
adds a tested C++RTL-to-GDSII forwarding-core flow and records the physical PHY
work still required for a complete copper repeater.

```bash
~/cpphdl/analog/pdk/.prepare_IHPSG13G2.sh
source ~/cpphdl/analog/pdk/IHPSG13G2/env.sh
```

This installs the **public 130 nm SG13G2 SiGe BiCMOS PDK**, not a 65 nm kit.
The default destination is `IHPSG13G2/` beside the script. The upstream checkout,
submodules, compiled models and generated setup are ignored by the C++HDL Git
repository. No NDA is needed to download this Apache-2.0 PDK.

The script follows the [IHP installation guide](https://ihp-open-pdk-docs.readthedocs.io/en/latest/install/installation.html)
using [IHP-Open-PDK](https://github.com/IHP-GmbH/IHP-Open-PDK), pinned to
`89d42ee6d2fd9b50b86a1aada6fee6673c3e6008` from `dev` on 2026-10-17.
The guide currently recommends `dev` for the ngspice examples. It also requires
recursive submodules; all are initialized at their recorded commits.

## What setup does

1. Fetch the pinned PDK and recursively initialize its submodules.
2. Check that the layout technology, DRC/LVS runners and MOS model entry exist.
3. Create `IHPSG13G2/.cpphdl/venv/` and install upstream Python requirements plus
   `psutil`, required by the PyCell platform detection. This includes KLayout's
   Python bindings. Record the resolved packages in `.cpphdl/python-packages.txt`.
4. Compile all six ngspice OSDI models: `psp103`, `psp103_nqs`, `r3_cmc`, `mosvar`,
   `cap_cmomi`, and `cap_cmomf`. Every compiler exit and output is checked.
5. Generate `env.sh` with `PDK_ROOT`, `PDK`, `PDKPATH`, `SPICE_USERINIT_DIR`,
   `KLAYOUT_PATH`, the local Python environment and simulator/compiler paths.
6. Load the KLayout technology, round-trip a small GDSII fixture, and simulate
   an actual IHP LV CMOS inverter using `cornerMOSlv.lib`, corner `mos_tt`.

The inverter check requires a monotonic DC transfer with outputs near 1.2 V and
0 V at the opposite input rails. Its deck, trace and simulator log are retained
in `.cpphdl/smoke/`. The GDSII fixture only verifies file I/O; it is not a circuit
layout and no DRC-clean claim is made for it.

## Dependencies and options

Required: Linux, Bash 4+, Git, Python 3.10+ with `venv`/`pip`, internet access, OpenVAF and
OSDI-enabled ngspice. Reuse the existing [analog toolchain](../VerilogA/toolchain.md)
or provide paths:

```bash
./.prepare_IHPSG13G2.sh \
    --python /path/to/python3 \
    --openvaf /path/to/openvaf \
    --ngspice /path/to/ngspice
```

`PYTHON`, `OPENVAF_EXECUTABLE`, and `NGSPICE_EXECUTABLE` provide equivalent defaults.
OpenVAF/ngspice are otherwise found on `PATH`, then in the existing
`analog/out/veriloga-toolchain/` installation. Tools must actually run and load
the compiled models; a version string alone is not sufficient.

The script installs PDK data and Python dependencies, not the KLayout GUI/Ruby
application, Xschem, Magic, Xyce or a complete RTL-to-GDS toolchain. Install those
applications separately as needed. KLayout's Python wheel does **not** provide
the Ruby DRC/LVS executable. The script reports whether `klayout` is on `PATH`.
For GUI PyCells, install dependencies for the Python version embedded in your
KLayout application; the local simulation environment alone does not configure
another interpreter's packages.

Run `--help` for all options. `--skip-models` allows setup without OpenVAF/ngspice;
`--skip-python` omits the Python environment. Such runs explicitly report partial
setup. No skipped stage is treated as verified.

Repeated runs preserve the selected revision and reuse the Python environment
and compiled models. Models rebuild when the PDK revision or compiler binary
changes, or a model output is missing. The inverter smoke test runs again.
The script refuses another origin, a different revision or tracked modifications
instead of resetting them. For an intentional revision change, use a fresh
`--destination` and an explicit full `--revision` commit ID. Generated files stay
inside the installation; shell startup files and global Python packages are
not modified.

## Use the configured PDK

After sourcing `env.sh`, the upstream model initialization is selected through
`SPICE_USERINIT_DIR`. Do not use ngspice `-n` for IHP models: that would skip its
OSDI initialization. The existing C++HDL generic solver tests may still use their
own independent simulator setup.

For physical verification, install a compatible KLayout application (this pinned
release records 0.30.5 in `versions.txt`, which the DRC runner reads for its
minimum version), source the environment, and use the
[IHP DRC runner](https://ihp-open-pdk-docs.readthedocs.io/en/latest/verification/drc/04_04_usage.html):

```bash
python "$PDKPATH/libs.tech/klayout/tech/drc/run_drc.py" \
    --path=/absolute/path/design.gds \
    --topcell=TOP \
    --run_dir=/absolute/path/drc-results \
    --antenna
```

Review the selected rule options and result database. DRC, LVS, extracted
simulation and electrical compliance remain separate checks. Installing the PDK
does not yet map C++HDL classes to IHP devices or make existing 10G models
foundry-verified.

## Script regression

```bash
python3 ~/cpphdl/analog/pdk/tests/test_prepare.py
# After configuring the analog CMake build:
ctest --test-dir ~/cpphdl/analog/build -R '^analog\.pdk\.' --output-on-failure
```

This offline regression uses synthetic files and a local Git fixture. It checks
cloning, repeat runs, environment quoting, incomplete releases, revision/dirty
checkout protection and failed-fetch cleanup. Model loading and circuit behavior
are verified separately by the real setup smoke test.

Validated on 2026-10-17 on Ubuntu 22.04 with Python 3.14, the existing OpenVAF
OSDI 0.3 toolchain, ngspice 47 and KLayout Python 0.30.5. All six models compiled;
the 13-point inverter sweep, GDSII round-trip, repeat installation and seven
offline regression cases passed. This setup validation preceded installation of
the KLayout application. Subsequent actual core and padframe DRC/LVS results are
recorded in the [repeater project](1G_repeater_IHPSG13G2/chip.md).
