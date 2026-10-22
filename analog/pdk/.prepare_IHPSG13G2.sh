#!/usr/bin/env bash
# Prepare the public IHP SG13G2 130 nm BiCMOS PDK and its ngspice models.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
destination="$script_dir/IHPSG13G2"
repository="https://github.com/IHP-GmbH/IHP-Open-PDK.git"
# Pinned from upstream dev, following the IHP installation guide, 2026-10-17.
revision="89d42ee6d2fd9b50b86a1aada6fee6673c3e6008"
python_bin="${PYTHON:-python3}"
openvaf_bin="${OPENVAF_EXECUTABLE:-}"
ngspice_bin="${NGSPICE_EXECUTABLE:-}"
skip_python=false
skip_models=false
temporary_clone=""

usage() {
    cat <<'EOF'
Usage: .prepare_IHPSG13G2.sh [options]

Clone the pinned public PDK with all submodules, install local Python dependencies,
compile all six OpenVAF models, and test an IHP MOS inverter in ngspice.

  --destination PATH  Installation root (default: script directory/IHPSG13G2)
  --revision SHA      Full upstream commit ID (existing installations are not reset)
  --python PATH       Python with venv/pip support (default: PYTHON or python3)
  --openvaf PATH      OpenVAF executable (also OPENVAF_EXECUTABLE)
  --ngspice PATH      OSDI-enabled ngspice executable (also NGSPICE_EXECUTABLE)
  --skip-python       Skip the local Python environment and its verification
  --skip-models       Skip model compilation and ngspice verification
  -h, --help          Show this help

OpenVAF/ngspice are reused from PATH or analog/out/veriloga-toolchain when present.
The KLayout application and other EDA applications must be installed separately;
the Python KLayout bindings and PDK technology files are configured here.
No sudo, shell startup edits, automatic pulls, or global Python installs.
EOF
}

die() { printf 'IHP setup: %s\n' "$*" >&2; exit 1; }
cleanup() { if [[ -n "$temporary_clone" ]]; then rm -rf -- "$temporary_clone"; fi; }
trap cleanup EXIT

while (($#)); do
    case "$1" in
        --destination|--revision|--python|--openvaf|--ngspice)
            (($# >= 2)) || die "Missing value for $1"
            case "$1" in
                --destination) destination="$2" ;;
                --revision) revision="$2" ;;
                --python) python_bin="$2" ;;
                --openvaf) openvaf_bin="$2" ;;
                --ngspice) ngspice_bin="$2" ;;
            esac
            shift 2 ;;
        --skip-python) skip_python=true; shift ;;
        --skip-models) skip_models=true; shift ;;
        -h|--help) usage; exit 0 ;;
        *) die "Unknown option: $1 (see --help)" ;;
    esac
done

command -v git >/dev/null || die "Git is required"
command -v "$python_bin" >/dev/null || die "Python is required: $python_bin"
python_bin="$(command -v "$python_bin")"
"$python_bin" -c 'import sys; assert sys.version_info >= (3, 10), "Python 3.10+ required"'
[[ "$revision" =~ ^[0-9a-fA-F]{40}$ ]] || die "--revision requires a full 40-character commit ID"
revision="${revision,,}"
destination="$("$python_bin" -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$destination")"
[[ "$destination" != *$'\n'* && "$destination" != *'"'* ]] || die "Unsupported quote/newline in installation path"

resolve_tool() {
    local selected="$1" name="$2" fallback="$3"
    if [[ -n "$selected" ]]; then
        command -v "$selected" || die "Executable not found: $selected"
    elif command -v "$name" >/dev/null; then
        command -v "$name"
    elif [[ -x "$fallback" ]]; then
        printf '%s\n' "$fallback"
    else
        die "$name is required; supply its path or use --skip-models for a data-only setup"
    fi
}

if ! "$skip_models"; then
    if [[ -z "$openvaf_bin" ]] && command -v openvaf-r >/dev/null; then
        openvaf_bin="$(command -v openvaf-r)"
    fi
    openvaf_bin="$(resolve_tool "$openvaf_bin" openvaf "$script_dir/../out/veriloga-toolchain/openvaf-compatible/openvaf")"
    ngspice_bin="$(resolve_tool "$ngspice_bin" ngspice "$script_dir/../out/veriloga-toolchain/install/bin/ngspice")"
    openvaf_bin="$("$python_bin" -c 'import os,sys; print(os.path.abspath(sys.argv[1]))' "$openvaf_bin")"
    ngspice_bin="$("$python_bin" -c 'import os,sys; print(os.path.abspath(sys.argv[1]))' "$ngspice_bin")"
    "$openvaf_bin" --version
    "$ngspice_bin" --version
fi

if [[ ! -e "$destination" ]]; then
    mkdir -p -- "$(dirname -- "$destination")"
    temporary_clone="$(mktemp -d "$(dirname -- "$destination")/.IHPSG13G2-clone.XXXXXX")"
    git init -q "$temporary_clone/kit"
    git -C "$temporary_clone/kit" remote add origin "$repository"
    git -C "$temporary_clone/kit" fetch --depth 1 origin "$revision"
    git -C "$temporary_clone/kit" checkout --detach FETCH_HEAD
    mv -- "$temporary_clone/kit" "$destination"
elif [[ ! -d "$destination/.git" ]]; then
    die "Destination already exists and is not a PDK checkout: $destination"
fi

[[ "$(git -C "$destination" config --get remote.origin.url)" == "$repository" ]] || die "Existing checkout has another origin"
[[ "$(git -C "$destination" rev-parse HEAD)" == "$revision" ]] || die "Existing revision differs; choose another destination or its exact --revision"
git -C "$destination" diff --quiet && git -C "$destination" diff --cached --quiet || die "PDK contains tracked changes; preserve them before setup"
git -C "$destination" submodule foreach --quiet --recursive \
    'git diff --quiet && git diff --cached --quiet' || die "A PDK submodule has tracked changes"
git -C "$destination" submodule sync --recursive
git -C "$destination" submodule update --init --recursive --depth 1
if git -C "$destination" submodule status --recursive | grep -Eq '^[-+U]'; then
    die "Submodules do not match the pinned release"
fi

technology="$destination/ihp-sg13g2"
local_dir="$destination/.cpphdl"
mkdir -p "$local_dir"
for required in \
    LICENSE requirements.txt \
    ihp-sg13g2/libs.tech/klayout/tech/sg13g2.lyt \
    ihp-sg13g2/libs.tech/klayout/tech/drc/run_drc.py \
    ihp-sg13g2/libs.tech/klayout/tech/lvs/run_lvs.py \
    ihp-sg13g2/libs.tech/ngspice/.spiceinit \
    ihp-sg13g2/libs.tech/ngspice/models/cornerMOSlv.lib; do
    [[ -s "$destination/$required" ]] || die "Incomplete PDK: missing $required"
done

if ! "$skip_python"; then
    if [[ ! -x "$local_dir/venv/bin/python" ]]; then
        "$python_bin" -m venv "$local_dir/venv" || die "Python venv/pip is unavailable; install python3-venv or select --python"
    fi
    venv_python="$local_dir/venv/bin/python"
    dependencies_signature="$("$python_bin" -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], "rb").read() + b"\npsutil\n").hexdigest())' "$destination/requirements.txt")"
    if [[ ! -f "$local_dir/python.signature" ]] || [[ "$(cat "$local_dir/python.signature")" != "$dependencies_signature" ]]; then
        "$venv_python" -m pip install --no-cache-dir -r "$destination/requirements.txt" psutil
        printf '%s\n' "$dependencies_signature" > "$local_dir/python.signature"
    fi
    "$venv_python" -m pip check
    "$venv_python" -c 'import klayout.db, docopt, pandas, yaml, gdstk, tqdm, termcolor, psutil; print("PDK Python dependencies imported successfully")'
    "$venv_python" -m pip freeze > "$local_dir/python-packages.txt"
fi

if ! "$skip_models"; then
    # The upstream helper does not stop at each compiler failure; check every model here.
    models=(psp103 psp103_nqs r3_cmc mosvar cap_cmomi cap_cmomf)
    model_sources=(psp103/psp103.va psp103/psp103_nqs.va r3_cmc/r3_cmc.va mosvar/mosvar.va cap_cmomi/cap_cmomi.va cap_cmomf/cap_cmomf.va)
    compiler_signature="$("$python_bin" -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' "$openvaf_bin")"
    model_signature="$revision $compiler_signature"
    rebuild=true
    if [[ -f "$local_dir/models.signature" ]] && [[ "$(cat "$local_dir/models.signature")" == "$model_signature" ]]; then
        rebuild=false
        for model in "${models[@]}"; do
            [[ -s "$technology/libs.tech/ngspice/osdi/$model.osdi" ]] || rebuild=true
        done
    fi
    if "$rebuild"; then
        rm -f "$local_dir/models.signature"
        mkdir -p "$technology/libs.tech/ngspice/osdi" "$local_dir/logs"
        for index in "${!models[@]}"; do
            model="${models[index]}"
            printf 'Compiling %s\n' "$model"
            if ! (cd "$technology/libs.tech/verilog-a" && \
                "$openvaf_bin" -D__NGSPICE__ --target_cpu generic \
                -o "../ngspice/osdi/$model.osdi" "${model_sources[index]}") \
                > "$local_dir/logs/$model.log" 2>&1; then
                cat "$local_dir/logs/$model.log" >&2
                die "Model compilation failed: $model"
            fi
            [[ -s "$technology/libs.tech/ngspice/osdi/$model.osdi" ]] || die "Compiler produced no $model.osdi"
        done
        printf '%s\n' "$model_signature" > "$local_dir/models.signature"
    fi
fi

# Generate a sourceable environment; leave ~/.bashrc, ~/.spiceinit and ~/.klayout alone.
{
    printf '# IHP SG13G2 environment, generated by .prepare_IHPSG13G2.sh\n'
    printf 'export PDK_ROOT=%q\nexport PDK=ihp-sg13g2\n' "$destination"
    printf 'export PDKPATH=%q\n' "$technology"
    printf 'export SPICE_USERINIT_DIR=%q\n' "$technology/libs.tech/ngspice"
    printf 'export KLAYOUT_PATH=%q:%q:"${KLAYOUT_PATH:-$HOME/.klayout}"\n' \
        "$technology/libs.tech/klayout" "$technology/libs.tech/klayout/tech"
    if ! "$skip_python"; then
        printf 'source %q\n' "$local_dir/venv/bin/activate"
    fi
    if ! "$skip_models"; then
        printf 'export OPENVAF_EXECUTABLE=%q\nexport NGSPICE_EXECUTABLE=%q\n' "$openvaf_bin" "$ngspice_bin"
        printf 'export PATH=%q:%q:"$PATH"\n' "$(dirname -- "$openvaf_bin")" "$(dirname -- "$ngspice_bin")"
    fi
} > "$destination/env.sh"

git -C "$destination" rev-parse HEAD > "$local_dir/revision.txt"
git -C "$destination" submodule status --recursive > "$local_dir/submodules.txt"
if ! "$skip_models"; then
    verify_python="$python_bin"
    verify_arguments=()
    if ! "$skip_python"; then verify_python="$venv_python"; fi
    if "$skip_python"; then verify_arguments+=(--skip-layout-python); fi
    "$verify_python" "$script_dir/verify_IHPSG13G2.py" \
        --root "$destination" --ngspice "$ngspice_bin" --output "$local_dir/smoke" \
        "${verify_arguments[@]}"
fi
if command -v klayout >/dev/null; then
    klayout -b -v
else
    printf 'KLayout application is not installed; install it separately for Ruby DRC/LVS decks.\n'
fi
printf 'PDK ready at %s\nRevision: %s\n' "$destination" "$revision"
if "$skip_models" || "$skip_python"; then
    printf 'Partial setup: Python skipped=%s, model compilation/simulation skipped=%s\n' "$skip_python" "$skip_models"
fi
printf 'Activate with: source %q\n' "$destination/env.sh"
