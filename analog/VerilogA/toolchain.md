# Simulator toolchain and references

Conversion requires a C++20 compiler only. Independent simulation additionally
requires a Verilog-A compiler and a simulator supporting its compiled interface.
The tested combination is OpenVAF's OSDI 0.3 bugfix branch and ngspice 47. Binaries,
third-party sources and generated results remain outside tracked source files.

## Reproduce the tested Linux toolchain

On this Ubuntu 22.04 host, the original OpenVAF 23.5 download crashes even on a
minimal resistor model. The 2026 OpenVAF Reloaded binary requires a newer glibc.
The following older bugfix-branch binary works; it still reports version 23.5.0,
so its archive identity matters. Other compatible OpenVAF/OpenVAF Reloaded builds
can be selected with `OPENVAF_EXECUTABLE` and are checked by actual compilation.

From the package root, with Git, curl, a C compiler, make, autoconf, automake,
libtool, flex and bison installed:

```bash
VERILOGA_DIR="$PWD/analog/out/veriloga-tools"
mkdir -p "$VERILOGA_DIR/openvaf"
curl -fL https://fides.fe.uni-lj.si/openvaf/download/openvaf-reloaded-osdi_0.3-31-gf47d557-linux_x64.tar.gz \
  -o "$VERILOGA_DIR/openvaf.tar.gz"
(cd "$VERILOGA_DIR" && \
  echo '9bc343d52e00529ddba452b90358cdd14da14ec7437d75dec2dbd621eda54bc8  openvaf.tar.gz' | sha256sum -c -)
tar -xzf "$VERILOGA_DIR/openvaf.tar.gz" -C "$VERILOGA_DIR/openvaf"
"$VERILOGA_DIR/openvaf/openvaf" --version

git clone --depth 1 --branch ngspice-47 https://github.com/imr/ngspice.git "$VERILOGA_DIR/ngspice-src"
git -C "$VERILOGA_DIR/ngspice-src" rev-parse HEAD
# Tested tag commit: a80f6e3e95d51534905b1f23410a951802666656
(cd "$VERILOGA_DIR/ngspice-src" && ./autogen.sh)
mkdir -p "$VERILOGA_DIR/ngspice-build"
(cd "$VERILOGA_DIR/ngspice-build" && \
  ../ngspice-src/configure --prefix="$VERILOGA_DIR/install" \
    --with-x=no --with-readline=no --enable-osdi --enable-predictor CFLAGS=-O2 && \
  make -j 4 && make install)
"$VERILOGA_DIR/install/bin/ngspice" --version

cmake -S analog -B analog/build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DCPPHDL_BUILD_ANALOG_TESTS=ON \
  -DCPPHDL_ANALOG_VERILOGA_TESTS=ON \
  -DOPENVAF_EXECUTABLE="$VERILOGA_DIR/openvaf/openvaf" \
  -DNGSPICE_EXECUTABLE="$VERILOGA_DIR/install/bin/ngspice"
cmake --build analog/build -j 4
ctest --test-dir analog/build --output-on-failure -L veriloga -j 3
```

This installation is local and does not require root privileges. The build/test
system does not download tools automatically. Tests record compiler information
in native reports and external versions in their per-stage logs. An executable
merely existing is insufficient: model compilation and waveform generation must
succeed for a simulation test to pass. Some detailed loop tests take over a minute.

## Primary references and design choices

- [Accellera Verilog-AMS standards](https://www.accellera.org/downloads/standards/v-ams): language reference, electrical disciplines, branch contributions and analog operators. The exporter uses the analog subset with explicit current contributions and `ddt`.
- [OpenVAF usage](https://openvaf.github.io/docs/getting-started/usage/): `.va` to `.osdi`, `pre_osdi` and ngspice `N` instances. This is the executable regression path.
- [OpenVAF language compliance](https://openvaf.github.io/docs/details/verilog-a-standard/): analog subset and event-control limitations. The exporter avoids digital processes and monitored events.
- [OpenVAF Reloaded source and interface versions](https://github.com/OpenVAF/OpenVAF-Reloaded): compiler maintenance and OSDI compatibility. The [binary archive](https://fides.fe.uni-lj.si/openvaf/download/) provides the tested bugfix branch.
- [Ngspice OSDI overview](https://ngspice.sourceforge.io/osdi.html): compiled Verilog-A model loading, independently implemented simulation engine and deprecated ADMS route.
- [Ngspice manual](https://ngspice.sourceforge.io/docs.html): transient/UIC initial conditions, PWL/PULSE sources, solver options and waveform output.
- [Ngspice downloads](https://ngspice.sourceforge.io/download.html): releases, source builds and the linked GitHub source mirror used above.

Verilog-A is a device/circuit equation language, not a simulator. OSDI is the
compiled device interface; SPICE is the host netlist/analysis environment. The
regressions use all three layers and retain the intermediate files.

## Why not substitute BSIM?

[Berkeley BSIM](https://bsim.berkeley.edu/models/bsim4/) is a family of compact transistor models with additional terminal physics,
parameters and dynamic states. Substituting a SPICE BSIM transistor for the current
C++ square-law channel would test two different device models. It would not isolate
numerical solver correctness. These tests therefore export the selected square-law
law explicitly; they do not select a SPICE MOS model level or claim BSIM fidelity.
No BSIM substitution or BSIM import option is implemented. Physical compact-model
validation and the current same-equation solver comparison are separate tasks.
