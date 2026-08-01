#!/usr/bin/env python3
import os
import re
import shlex
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "cpphdl"
HDLCPP = Path(os.environ.get("HDLCPP", "/home/me/cpphdl/hdlcpp/build/hdlcpp"))
TARGET = os.environ.get("TARGET", "cv32a6_imafdc_sv32")
RISCV = os.environ.get("RISCV", "/home/me/riscv")
HPDCACHE_DIR = Path(os.environ.get("HPDCACHE_DIR", str(ROOT / "core/cache_subsystem/hpdcache")))
NATIVE_HARNESS = os.environ.get("CPPHDL_CVA6_NATIVE_HARNESS", "0") == "1"


def hdlcpp_jobs() -> int:
    raw = os.environ.get("HDLCPP_JOBS")
    if raw:
        try:
            return max(1, int(raw))
        except ValueError:
            raise SystemExit(f"invalid HDLCPP_JOBS={raw!r}")
    return max(1, min(os.cpu_count() or 1, 8))


def expand_vars(text: str) -> str:
    return (text.replace("${CVA6_REPO_DIR}", str(ROOT))
                .replace("$(CVA6_REPO_DIR)", str(ROOT))
                .replace("${TARGET_CFG}", TARGET)
                .replace("$(TARGET_CFG)", TARGET)
                .replace("${HPDCACHE_DIR}", str(HPDCACHE_DIR))
                .replace("$(HPDCACHE_DIR)", str(HPDCACHE_DIR)))


def parse_flist(path: Path, seen=None):
    if seen is None:
        seen = set()
    path = path.resolve()
    if path in seen:
        return [], []
    seen.add(path)
    files = []
    incdirs = []
    if not path.exists():
        print(f"WARN: missing file list {path}", file=sys.stderr)
        return files, incdirs
    for raw in path.read_text().splitlines():
        line = raw.split("//", 1)[0].strip()
        if not line:
            continue
        line = expand_vars(line)
        if line.startswith("+incdir+"):
            incdirs.append(line[len("+incdir+"):])
            continue
        if line.startswith("-F ") or line.startswith("-f "):
            nested = Path(line.split(None, 1)[1])
            if not nested.is_absolute():
                nested = path.parent / nested
            sub_files, sub_incdirs = parse_flist(nested, seen)
            files.extend(sub_files)
            incdirs.extend(sub_incdirs)
            continue
        if line.endswith((".sv", ".v")):
            p = Path(line)
            if not p.is_absolute():
                p = path.parent / p
            files.append(p.resolve())
    return files, incdirs


def make_verilate_tokens():
    env = os.environ.copy()
    env["RISCV"] = RISCV
    proc = subprocess.run(
        ["make", "-pn", "verilate", f"target={TARGET}"],
        cwd=ROOT,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    text = proc.stdout + "\n" + proc.stderr
    match = re.search(r"^verilate_command := (.*)$", text, re.MULTILINE)
    if not match:
        print(text[-4000:], file=sys.stderr)
        raise SystemExit("failed to extract verilate_command from make -pn")
    return shlex.split(match.group(1))


def unique(seq):
    out = []
    seen = set()
    for item in seq:
        key = str(item)
        if key in seen:
            continue
        seen.add(key)
        out.append(item)
    return out


# Generated support headers are broad Makefile dependencies for optimized units.
# Preserve their timestamps when regeneration produces identical content so an
# optimizer-only rerun does not force recompilation of the entire C++ model.
def write_if_changed(path: Path, text: str) -> None:
    if path.exists() and path.read_text() == text:
        return
    path.write_text(text)


def collect_sources():
    files, incdirs = parse_flist(ROOT / "core/Flist.cva6")
    for tok in make_verilate_tokens():
        tok = expand_vars(tok)
        if tok.startswith("+incdir+"):
            incdirs.append(tok[len("+incdir+"):])
        elif tok.endswith((".sv", ".v")):
            p = Path(tok)
            if not p.is_absolute():
                p = ROOT / p
            files.append(p.resolve())
    for extra in [
        ROOT / "core/cache_subsystem/cva6_hpdcache_subsystem_l15_adapter.sv",
        ROOT / "core/cache_subsystem/hpdcache/rtl/src/utils/hpdcache_to_l15.sv",
        ROOT / "core/cache_subsystem/hpdcache/rtl/src/utils/hpdcache_l15_req_arbiter.sv",
        ROOT / "core/cache_subsystem/hpdcache/rtl/src/utils/hpdcache_l15_resp_demux.sv",
        ROOT / "corev_apu/fpga/src/apb_uart/src/apb_uart.sv",
        ROOT / "vendor/pulp-platform/common_cells/src/onehot_to_bin.sv",
        ROOT / "vendor/pulp-platform/common_cells/src/id_queue.sv",
        ROOT / "vendor/pulp-platform/axi/src/axi_burst_splitter.sv",
        ROOT / "corev_apu/riscv-dbg/debug_rom/debug_rom_one_scratch.sv",
    ]:
        if extra.exists():
            files.append(extra.resolve())
    uart_dir = ROOT / "corev_apu/fpga/src/apb_uart/src"
    if uart_dir.exists():
        for extra in sorted(uart_dir.glob("*.sv")):
            files.append(extra.resolve())
    return unique(files), unique(incdirs)


def rel_source(path: Path) -> Path:
    try:
        return path.resolve().relative_to(ROOT)
    except ValueError:
        return Path("external") / path.name


def generated_header_for_source(src: Path) -> Path:
    rel = rel_source(src)
    return OUT / "generated" / rel.parent / f"{src.stem}.h"


module_type_declaration_re = re.compile(
    r"^ {4}(\S.*\S)\s+"
    r"[A-Za-z_][A-Za-z0-9_]*\s*(?:\[[^;\n]*\])?\s*(?:;|=)",
    re.MULTILINE,
)
identifier_re = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")


def referenced_module_types(text, module_names):
    references = set()
    for match in module_type_declaration_re.finditer(text):
        references.update(module_names.intersection(identifier_re.findall(match.group(1))))
    return references


def topo_refine_include_order(seed_order):
    headers = {src: generated_header_for_source(src) for src in seed_order}
    texts = {}
    class_to_src = {}
    class_re = re.compile(r"\bclass\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*public\s+Module\b")

    for src, header in headers.items():
        if not header.exists():
            texts[src] = ""
            continue
        text = header.read_text(errors="replace")
        texts[src] = text
        for match in class_re.finditer(text):
            class_to_src.setdefault(match.group(1), src)

    deps = {src: set() for src in seed_order}
    class_names = set(class_to_src)
    for src, text in texts.items():
        # Scan C++ type positions once. Plain identifiers may be member variables with
        # the same spelling as another module and must not invent dependency cycles.
        candidates = referenced_module_types(text, class_names)
        for name in candidates:
            dep_src = class_to_src[name]
            if dep_src != src:
                deps[src].add(dep_src)

    seed_index = {src: idx for idx, src in enumerate(seed_order)}
    ordered = []
    state = {}

    def visit(src):
        mark = state.get(src, 0)
        if mark == 2:
            return
        if mark == 1:
            return
        state[src] = 1
        for dep in sorted(deps.get(src, ()), key=lambda item: seed_index.get(item, 0)):
            visit(dep)
        state[src] = 2
        ordered.append(src)

    for src in seed_order:
        visit(src)
    return ordered


def topo_refine_include_lines(seed_lines):
    include_re = re.compile(r'#include\s+"([^"]+)"')
    header_for_line = {}
    texts = {}
    class_to_line = {}
    class_re = re.compile(r"\bclass\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*public\s+Module\b")

    for line in seed_lines:
        match = include_re.match(line)
        if not match:
            continue
        header = OUT / match.group(1)
        header_for_line[line] = header
        if not header.exists():
            texts[line] = ""
            continue
        text = header.read_text(errors="replace")
        texts[line] = text
        for class_match in class_re.finditer(text):
            class_to_line.setdefault(class_match.group(1), line)

    deps = {line: set() for line in seed_lines}
    def references_class(text, name):
        escaped = re.escape(name)
        patterns = [
            rf"(?:^|[^\w])(?:::)?{escaped}\s*<",
            rf"\barray\s*<\s*(?:::)?{escaped}\b",
            rf"\barray\s*<[^;\n]*?(?:::)?{escaped}\s*(?:,|>)",
            rf"(?:^|[;\{{])\s*(?:::)?{escaped}\s+[A-Za-z_][A-Za-z0-9_]*\s*(?:;|\[|=)",
        ]
        return any(re.search(pattern, text, re.MULTILINE) for pattern in patterns)

    token_re = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
    class_names = set(class_to_line)
    for line, text in texts.items():
        candidates = class_names.intersection(token_re.findall(text))
        for name in candidates:
            if not references_class(text, name):
                continue
            dep_line = class_to_line[name]
            if dep_line != line:
                deps[line].add(dep_line)

    seed_index = {line: idx for idx, line in enumerate(seed_lines)}
    ordered = []
    state = {}

    def visit(line):
        mark = state.get(line, 0)
        if mark == 2:
            return
        if mark == 1:
            return
        state[line] = 1
        for dep in sorted(deps.get(line, ()), key=lambda item: seed_index.get(item, 0)):
            visit(dep)
        state[line] = 2
        ordered.append(line)

    for line in seed_lines:
        visit(line)
    return ordered


def include_source_for_cpphdl(src: Path):
    rel = rel_source(src).as_posix()
    if rel == "corev_apu/instr_tracing/rv_tracer-main/rtl/lzc.sv":
        return False
    if rel == "corev_apu/fpga/src/apb_uart/src/reg_uart_wrap.sv":
        return False
    if rel == "corev_apu/fpga/src/axi2apb/src/axi2apb_wrap.sv":
        return False
    if NATIVE_HARNESS:
        if rel.endswith((".vhd", ".vhdl")):
            return False
        if TARGET.startswith("cv32a6_imac") or TARGET.endswith("_imac_sv32"):
            if rel.startswith("core/cvfpu/") and rel != "core/cvfpu/src/fpnew_pkg.sv":
                return False
            if rel.startswith("core/cache_subsystem/std_"):
                return False
            if rel.startswith("core/cache_subsystem/std_nbdcache"):
                return False
        return True
    if rel in {
        "corev_apu/tb/ariane_axi_pkg.sv",
        "corev_apu/src/ariane.sv",
    }:
        return True
    if rel.startswith("corev_apu/"):
        return False
    if rel == "core/cva6_rvfi.sv":
        return False
    if rel.startswith("vendor/pulp-platform/axi_riscv_atomics/"):
        return False
    if rel.startswith("vendor/pulp-platform/axi/src/") and rel != "vendor/pulp-platform/axi/src/axi_pkg.sv":
        return False
    if rel.startswith("vendor/pulp-platform/tech_cells_generic/") and rel != "vendor/pulp-platform/tech_cells_generic/src/rtl/tc_sram.sv":
        return False
    if rel.startswith("vendor/pulp-platform/common_cells/src/deprecated/"):
        return False
    if rel.startswith("vendor/pulp-platform/common_cells/src/rstgen"):
        return False
    if rel.startswith("vendor/pulp-platform/common_cells/src/stream_register"):
        return False
    if rel.startswith("vendor/pulp-platform/common_cells/src/sync_wedge"):
        return False
    if rel.startswith("vendor/pulp-platform/common_cells/src/edge_detect"):
        return False
    if rel.startswith("common/local/util/") and rel not in {
        "common/local/util/instr_tracer.sv",
        "common/local/util/hpdcache_sram_1rw.sv",
        "common/local/util/hpdcache_sram_wbyteenable_1rw.sv",
        "common/local/util/tc_sram_wrapper.sv",
        "common/local/util/tc_sram_wrapper_cache_techno.sv",
        "common/local/util/sram.sv",
        "common/local/util/sram_cache.sv",
    }:
        return False
    if TARGET.startswith("cv32a6_imac") or TARGET.endswith("_imac_sv32"):
        if rel.startswith("core/cvfpu/") and rel != "core/cvfpu/src/fpnew_pkg.sv":
            return False
        if rel.startswith("core/cache_subsystem/std_"):
            return False
        if rel.startswith("core/cache_subsystem/std_nbdcache"):
            return False
    return True


def setup_hdlcpp_env(env, port_types: Path | None = None, module_params: Path | None = None):
    if module_params is None:
        env.setdefault("HDLCPP_MODULE_PARAMS", str(OUT / "cva6_module_params.tsv"))
    else:
        env["HDLCPP_MODULE_PARAMS"] = str(module_params)
    env.setdefault("HDLCPP_MODULE_TRAITS", str(OUT / "cva6_merged_module_traits.tsv"))
    env.setdefault("HDLCPP_PACKAGE_CALLABLES", str(OUT / "cva6_auto_package_callables.tsv"))
    env.setdefault("HDLCPP_SKIP_USING_NAMESPACE_IMPORTS", "cvxif_instr_pkg")
    env.setdefault("HDLCPP_QUALIFY_IMPORTED_TYPE_PACKAGES", "cvxif_instr_pkg")
    env.setdefault("HDLCPP_SKIP_UNKNOWN_INSTANCE_TYPES", "instr_tracer")
    env.setdefault(
        "HDLCPP_NOCACHE_COMB_METHODS",
        "id_stage|instruction_cvxif_i_comb_func,"
        "id_stage|is_illegal_cvxif_i_comb_func,"
        "id_stage|is_compressed_cvxif_i_comb_func,"
        "cva6|fetch_entry_if_id_comb_func,"
        "cva6|fetch_valid_if_id_comb_func",
    )
    env.setdefault(
        "HDLCPP_READONLY_COMB_OUTPUTS",
        "cva6_fifo_v3|data_o,fifo_v3|data_o",
    )
    env.setdefault("HDLCPP_INLINE_COMB_MODULES", "")
    env.setdefault("HDLCPP_SKIP_ASSIGN_MODULES", "")
    env.setdefault("HDLCPP_SKIP_ASSIGN_LINE_PREFIXES", "cva6|instr_tracer_i.pck_in,cva6|instr_tracer_i.rstn_in,cva6|instr_tracer_i.flush_unissued_in,cva6|instr_tracer_i.flush_all_in,cva6|instr_tracer_i.instruction_in,cva6|instr_tracer_i.fetch_valid_in,cva6|instr_tracer_i.fetch_ack_in,cva6|instr_tracer_i.issue_ack_in,cva6|instr_tracer_i.issue_sbe_in,cva6|instr_tracer_i.waddr_in,cva6|instr_tracer_i.wdata_in,cva6|instr_tracer_i.we_gpr_in,cva6|instr_tracer_i.we_fpr_in,cva6|instr_tracer_i.commit_instr_in,cva6|instr_tracer_i.commit_ack_in,cva6|instr_tracer_i.commit_drop_in,cva6|instr_tracer_i.st_valid_in,cva6|instr_tracer_i.st_paddr_in,cva6|instr_tracer_i.ld_valid_in,cva6|instr_tracer_i.ld_kill_in,cva6|instr_tracer_i.ld_paddr_in,cva6|instr_tracer_i.resolve_branch_in,cva6|instr_tracer_i.commit_exception_in,cva6|instr_tracer_i.priv_lvl_in,cva6|instr_tracer_i.debug_mode_in,cva6|instr_tracer_i.hart_id_i_in")
    for key, filename in {
        "HDLCPP_LINE_PATCHES": "cva6_line_patches.tsv",
        "HDLCPP_FUNCTION_WIDTHS": "cva6_function_widths.tsv",
        "HDLCPP_NUMERIC_ARG_INDICES": "cva6_numeric_arg_indices.tsv",
        "HDLCPP_GENERATE_PARAM_VALUES": "cva6_generate_param_values.tsv",
        "HDLCPP_AGGREGATE_DEFAULTS": "cva6_aggregate_defaults.tsv",
        "HDLCPP_QUALIFIED_CALLS": "cva6_qualified_calls.tsv",
        "HDLCPP_TYPE_DECL_OVERRIDES": "cva6_type_decl_overrides.tsv",
        "HDLCPP_TYPE_PARAM_DEFAULTS": "cva6_type_param_defaults.tsv",
        "HDLCPP_TYPE_ALIAS_OVERRIDES": "cva6_type_alias_overrides.tsv",
        "HDLCPP_METHOD_BODY_OVERRIDES": "cva6_method_body_overrides.tsv",
        "HDLCPP_VAR_TYPE_PATCHES": "cva6_var_type_patches.tsv",
        "HDLCPP_INLINE_COMB_BODIES": "cva6_inline_comb_bodies.tsv",
        "HDLCPP_ASSIGN_LINE_PATCHES": "cva6_assign_line_patches.tsv",
        "HDLCPP_ASSIGN_PREFIX_CODE": "cva6_assign_prefix_code.tsv",
        "HDLCPP_ASSIGN_SUFFIX_CODE": "cva6_assign_suffix_code.tsv",
        "HDLCPP_COMB_RETURN_INJECTIONS": "cva6_comb_return_injections.tsv",
        "HDLCPP_PACKAGE_METHOD_OVERRIDES": "cva6_package_method_overrides.tsv",
        "HDLCPP_ENUM_WIDTH_PREFIXES": "cva6_enum_width_prefixes.tsv",
        "HDLCPP_READONLY_COMB_STOP_TOKENS": "cva6_readonly_comb_stop_tokens.tsv",
        "HDLCPP_TYPE_WIDTHS": "cva6_merged_type_widths.tsv",
    }.items():
        env.setdefault(key, str(OUT / filename))
    if port_types is None:
        env.setdefault("HDLCPP_PORT_TYPES", str(OUT / "cva6_port_types.tsv"))
    else:
        env["HDLCPP_PORT_TYPES"] = str(port_types)


def metadata_line_module(line: str, dotted_key: bool) -> str:
    key = line.split("\t", 1)[0]
    return key.split(".", 1)[0] if dotted_key else key


def write_source_module_map(src: Path, work_root: Path, outputs) -> None:
    modules = set()
    for category, path in outputs.items():
        if not path.exists():
            continue
        dotted_key = category in {"ports", "widths", "callables"}
        for line in path.read_text().splitlines():
            if line.strip():
                modules.add(metadata_line_module(line, dotted_key))
    rel = rel_source(src)
    path = work_root / "_source_modules" / rel.parent / f"{rel.name}.txt"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(sorted(modules)) + ("\n" if modules else ""))


def source_filtered_metadata(src: Path, work: Path, work_root: Path) -> tuple[Path, Path]:
    rel = rel_source(src)
    modules_path = work_root / "_source_modules" / rel.parent / f"{rel.name}.txt"
    if not modules_path.exists():
        return OUT / "cva6_merged_port_types.tsv", OUT / "cva6_merged_module_params.tsv"
    own_modules = {line.strip() for line in modules_path.read_text().splitlines() if line.strip()}

    auto_ports_path = OUT / "cva6_auto_port_types.tsv"
    manual_ports_path = OUT / "cva6_port_types.tsv"
    auto_ports = auto_ports_path.read_text().splitlines() if auto_ports_path.exists() else []
    manual_ports = manual_ports_path.read_text() if manual_ports_path.exists() else ""
    filtered_ports = [
        line for line in auto_ports
        if not line.strip() or metadata_line_module(line, True) not in own_modules
    ]
    port_types = work / ".cpphdl_dependency_port_types.tsv"
    port_text = "\n".join(filtered_ports)
    port_types.write_text(
        port_text +
        ("\n" if port_text else "") +
        manual_ports
    )

    auto_params_path = OUT / "cva6_auto_module_params.tsv"
    manual_params_path = OUT / "cva6_module_params.tsv"
    auto_params = auto_params_path.read_text().splitlines() if auto_params_path.exists() else []
    manual_params = manual_params_path.read_text() if manual_params_path.exists() else ""
    filtered_params = [
        line for line in auto_params
        if not line.strip() or metadata_line_module(line, False) not in own_modules
    ]
    module_params = work / ".cpphdl_dependency_module_params.tsv"
    param_text = "\n".join(filtered_params)
    module_params.write_text(
        manual_params +
        ("\n" if manual_params and param_text else "") +
        param_text +
        ("\n" if param_text else "")
    )
    return port_types, module_params


def build_metadata(files, work_root: Path) -> tuple[Path, Path]:
    auto_path = OUT / "cva6_auto_port_types.tsv"
    merged_path = OUT / "cva6_merged_port_types.tsv"
    auto_module_params = OUT / "cva6_auto_module_params.tsv"
    merged_module_params = OUT / "cva6_merged_module_params.tsv"
    auto_type_widths = OUT / "cva6_auto_type_widths.tsv"
    merged_type_widths = OUT / "cva6_merged_type_widths.tsv"
    auto_module_traits = OUT / "cva6_auto_module_traits.tsv"
    merged_module_traits = OUT / "cva6_merged_module_traits.tsv"
    auto_package_callables = OUT / "cva6_auto_package_callables.tsv"
    auto_module_dependencies = OUT / "cva6_auto_module_dependencies.tsv"
    auto_path.write_text("")
    auto_module_params.write_text("")
    auto_type_widths.write_text("")
    auto_module_traits.write_text("")
    auto_package_callables.write_text("")
    auto_module_dependencies.write_text("")
    metadata_tmp = work_root / "_metadata_tsv"
    metadata_tmp.mkdir(parents=True, exist_ok=True)

    def run_one(item):
        idx, src = item
        rel = rel_source(src)
        work = work_root / "_metadata" / rel.parent
        work.mkdir(parents=True, exist_ok=True)
        port_out = metadata_tmp / f"{idx:04d}.ports.tsv"
        params_out = metadata_tmp / f"{idx:04d}.params.tsv"
        widths_out = metadata_tmp / f"{idx:04d}.widths.tsv"
        traits_out = metadata_tmp / f"{idx:04d}.traits.tsv"
        callables_out = metadata_tmp / f"{idx:04d}.callables.tsv"
        dependencies_out = metadata_tmp / f"{idx:04d}.dependencies.tsv"
        env = os.environ.copy()
        setup_hdlcpp_env(env)
        env["HDLCPP_WRITE_PORT_TYPES"] = str(port_out)
        env["HDLCPP_WRITE_MODULE_PARAMS"] = str(params_out)
        env["HDLCPP_WRITE_TYPE_WIDTHS"] = str(widths_out)
        env["HDLCPP_WRITE_MODULE_TRAITS"] = str(traits_out)
        env["HDLCPP_WRITE_PACKAGE_CALLABLES"] = str(callables_out)
        env["HDLCPP_WRITE_MODULE_DEPENDENCIES"] = str(dependencies_out)
        env["HDLCPP_METADATA_ONLY"] = "1"
        proc = subprocess.run(
            [str(HDLCPP), str(src)],
            cwd=work,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        return (idx, rel, proc.returncode, proc.stdout[-1000:], port_out, params_out,
                widths_out, traits_out, callables_out, dependencies_out)

    failures = []
    results = {}
    jobs = hdlcpp_jobs()
    with ThreadPoolExecutor(max_workers=jobs) as executor:
        futures = [executor.submit(run_one, item) for item in enumerate(files)]
        for future in as_completed(futures):
            (idx, rel, code, log, port_out, params_out, widths_out, traits_out,
             callables_out, dependencies_out) = future.result()
            results[idx] = (port_out, params_out, widths_out, traits_out,
                            callables_out, dependencies_out)
            if code != 0:
                failures.append((rel, code, log))
    for idx in range(len(files)):
        (port_out, params_out, widths_out, traits_out,
         callables_out, dependencies_out) = results[idx]
        write_source_module_map(
            files[idx],
            work_root,
            {
                "ports": port_out,
                "params": params_out,
                "widths": widths_out,
                "traits": traits_out,
                "callables": callables_out,
                "dependencies": dependencies_out,
            },
        )
        if port_out.exists():
            with port_out.open() as src_f, auto_path.open("a") as dst_f:
                shutil.copyfileobj(src_f, dst_f)
        if params_out.exists():
            with params_out.open() as src_f, auto_module_params.open("a") as dst_f:
                shutil.copyfileobj(src_f, dst_f)
        if widths_out.exists():
            with widths_out.open() as src_f, auto_type_widths.open("a") as dst_f:
                shutil.copyfileobj(src_f, dst_f)
        if traits_out.exists():
            with traits_out.open() as src_f, auto_module_traits.open("a") as dst_f:
                shutil.copyfileobj(src_f, dst_f)
        if callables_out.exists():
            with callables_out.open() as src_f, auto_package_callables.open("a") as dst_f:
                shutil.copyfileobj(src_f, dst_f)
        if dependencies_out.exists():
            with dependencies_out.open() as src_f, auto_module_dependencies.open("a") as dst_f:
                shutil.copyfileobj(src_f, dst_f)
    manual_path = OUT / "cva6_port_types.tsv"
    manual = manual_path.read_text() if manual_path.exists() else ""
    auto = auto_path.read_text()
    merged_path.write_text(auto + ("\n" if auto and not auto.endswith("\n") else "") + manual)
    manual_module_params_path = OUT / "cva6_module_params.tsv"
    manual_module_params = manual_module_params_path.read_text() if manual_module_params_path.exists() else ""
    auto_module_params_text = auto_module_params.read_text()
    manual_module_names = {
        line.split("\t", 1)[0]
        for line in manual_module_params.splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }
    filtered_auto_module_params = "\n".join(
        line for line in auto_module_params_text.splitlines()
        if not line.strip() or line.split("\t", 1)[0] not in manual_module_names
    )
    merged_module_params.write_text(
        manual_module_params +
        ("\n" if manual_module_params and filtered_auto_module_params else "") +
        filtered_auto_module_params +
        ("\n" if filtered_auto_module_params else "")
    )
    manual_type_widths_path = OUT / "cva6_type_widths.tsv"
    manual_type_widths = manual_type_widths_path.read_text() if manual_type_widths_path.exists() else ""
    auto_type_widths_text = auto_type_widths.read_text()
    merged_type_widths.write_text(
        auto_type_widths_text +
        ("\n" if auto_type_widths_text and not auto_type_widths_text.endswith("\n") else "") +
        manual_type_widths
    )
    manual_module_traits_path = OUT / "cva6_module_traits.tsv"
    manual_module_traits = manual_module_traits_path.read_text() if manual_module_traits_path.exists() else ""
    auto_module_traits_text = auto_module_traits.read_text()
    merged_module_traits.write_text(
        auto_module_traits_text +
        ("\n" if auto_module_traits_text and manual_module_traits and not auto_module_traits_text.endswith("\n") else "") +
        manual_module_traits
    )
    if failures:
        detail = "\n".join(f"{rel}: exit={code}\n{log}" for rel, code, log in failures[:5])
        raise SystemExit(f"port metadata prepass failed:\n{detail}")
    return merged_path, merged_module_params


def order_sources_by_module_dependencies(files, work_root: Path):
    module_owner = {}
    for index, src in enumerate(files):
        rel = rel_source(src)
        modules_path = work_root / "_source_modules" / rel.parent / f"{rel.name}.txt"
        if not modules_path.exists():
            continue
        for module in modules_path.read_text().splitlines():
            if module.strip():
                module_owner[module.strip()] = index

    dependencies = [set() for _ in files]
    dependency_path = OUT / "cva6_auto_module_dependencies.tsv"
    if dependency_path.exists():
        for line in dependency_path.read_text().splitlines():
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            fields = line.split("\t")
            if len(fields) < 2:
                continue
            parent = module_owner.get(fields[0].strip())
            child = module_owner.get(fields[1].strip())
            if parent is not None and child is not None and parent != child:
                dependencies[parent].add(child)

    # Projected aggregate fields propagate from emitted children into their parents.
    # A stable topological order makes that metadata available in one conversion pass.
    # Any invalid/cyclic remainder retains manifest order and is diagnosed downstream.
    dependents = [set() for _ in files]
    indegree = [len(items) for items in dependencies]
    for parent, children in enumerate(dependencies):
        for child in children:
            dependents[child].add(parent)
    ready = [index for index, degree in enumerate(indegree) if degree == 0]
    ordered = []
    while ready:
        index = ready.pop(0)
        ordered.append(index)
        for parent in sorted(dependents[index]):
            indegree[parent] -= 1
            if indegree[parent] == 0:
                insert_at = 0
                while insert_at < len(ready) and ready[insert_at] < parent:
                    insert_at += 1
                ready.insert(insert_at, parent)
    emitted = set(ordered)
    ordered.extend(index for index in range(len(files)) if index not in emitted)
    return [files[index] for index in ordered]


def expand_sources_with_dependents(files, all_files, work_root: Path):
    module_owner = {}
    source_modules = {}
    for src in all_files:
        rel = rel_source(src)
        modules_path = work_root / "_source_modules" / rel.parent / f"{rel.name}.txt"
        modules = {
            line.strip() for line in modules_path.read_text().splitlines() if line.strip()
        } if modules_path.exists() else set()
        source_modules[src] = modules
        for module in modules:
            module_owner[module] = src

    parents_by_child = {}
    dependency_path = OUT / "cva6_auto_module_dependencies.tsv"
    if dependency_path.exists():
        for line in dependency_path.read_text().splitlines():
            fields = line.split("\t")
            if len(fields) >= 2 and fields[0].strip() and fields[1].strip():
                parents_by_child.setdefault(fields[1].strip(), set()).add(fields[0].strip())

    generated_parent_cache = None
    def generated_parents(child):
        nonlocal generated_parent_cache
        if generated_parent_cache is not None:
            return generated_parent_cache.get(child, set())

        # Build the complete stale-header fallback index once. Complete emission can
        # discover many child contracts in one pass, so rescanning every large header
        # separately for each child makes fixed-point propagation quadratic.
        generated_parent_cache = {}
        module_names = set(module_owner)
        for parent_src in all_files:
            header = generated_header_for_source(parent_src)
            if not header.exists():
                continue
            text = header.read_text(errors="replace")
            children = referenced_module_types(text, module_names)
            parent_modules = source_modules.get(parent_src, set())
            for referenced_child in children - parent_modules:
                generated_parent_cache.setdefault(referenced_child, set()).update(parent_modules)
        return generated_parent_cache.get(child, set())

    # A regenerated child can add projected ports discovered only during complete
    # emission. Regenerate every transitive parent so no existing optimized header
    # retains the old child contract after an incremental conversion.
    selected = set(files)
    pending = [module for src in files for module in source_modules.get(src, ())]
    seen_modules = set(pending)
    while pending:
        child = pending.pop()
        # Older incremental output may predate dependency metadata for a source. Its
        # generated member declarations still identify child classes, allowing this
        # run to refresh the parsed edge instead of leaving stale parent headers.
        parents = set(parents_by_child.get(child, ())) | generated_parents(child)
        for parent in parents:
            parent_src = module_owner.get(parent)
            if parent_src is None:
                continue
            selected.add(parent_src)
            for module in source_modules.get(parent_src, ()):
                if module not in seen_modules:
                    seen_modules.add(module)
                    pending.append(module)
    return [src for src in all_files if src in selected]


def sources_with_stale_projected_traits(all_files, work_root: Path):
    """Find owners whose generated headers predate persisted field contracts."""
    module_owner = {}
    for src in all_files:
        rel = rel_source(src)
        modules_path = work_root / "_source_modules" / rel.parent / f"{rel.name}.txt"
        if not modules_path.exists():
            continue
        for module in modules_path.read_text().splitlines():
            if module.strip():
                module_owner[module.strip()] = src

    traits_path = OUT / "cva6_merged_module_traits.tsv"
    if not traits_path.exists():
        return []

    # These models are replaced by intentional testharness stubs after conversion.
    synthetic_modules = {"acc_dispatcher", "fpu_wrap"}
    header_text = {}
    stale = set()
    for line in traits_path.read_text().splitlines():
        fields = line.split("\t")
        if len(fields) < 2:
            continue
        module = fields[0].strip()
        trait = fields[1].strip()
        if module in synthetic_modules or module not in module_owner:
            continue
        if trait.startswith("output_field."):
            direction = "out"
            path = trait[len("output_field."):]
        elif trait.startswith("input_field."):
            direction = "in"
            path = trait[len("input_field."):]
        else:
            continue
        parts = path.split(".")
        if len(parts) < 2:
            continue
        identifier = f"{parts[0]}_{direction}__field_{'_'.join(parts[1:])}"
        src = module_owner[module]
        header = generated_header_for_source(src)
        text = header_text.setdefault(
            header,
            header.read_text(errors="replace") if header.exists() else "",
        )
        if not re.search(
            rf"(?<![A-Za-z0-9_]){re.escape(identifier)}(?![A-Za-z0-9_])",
            text,
        ):
            stale.add(src)
    return [src for src in all_files if src in stale]


def refresh_incremental_metadata(files, work_root: Path) -> tuple[Path, Path]:
    metadata_tmp = work_root / "_metadata_incremental"
    if metadata_tmp.exists():
        shutil.rmtree(metadata_tmp)
    metadata_tmp.mkdir(parents=True)

    categories = {
        "ports": "cva6_auto_port_types.tsv",
        "params": "cva6_auto_module_params.tsv",
        "widths": "cva6_auto_type_widths.tsv",
        "traits": "cva6_auto_module_traits.tsv",
        "callables": "cva6_auto_package_callables.tsv",
        "dependencies": "cva6_auto_module_dependencies.tsv",
    }
    results = {}

    def run_one(item):
        idx, src = item
        rel = rel_source(src)
        work = work_root / "_metadata" / rel.parent
        work.mkdir(parents=True, exist_ok=True)
        outputs = {name: metadata_tmp / f"{idx:04d}.{name}.tsv" for name in categories}
        env = os.environ.copy()
        setup_hdlcpp_env(
            env,
            OUT / "cva6_port_types.tsv",
            OUT / "cva6_module_params.tsv",
        )
        env["HDLCPP_WRITE_PORT_TYPES"] = str(outputs["ports"])
        env["HDLCPP_WRITE_MODULE_PARAMS"] = str(outputs["params"])
        env["HDLCPP_WRITE_TYPE_WIDTHS"] = str(outputs["widths"])
        env["HDLCPP_WRITE_MODULE_TRAITS"] = str(outputs["traits"])
        env["HDLCPP_WRITE_PACKAGE_CALLABLES"] = str(outputs["callables"])
        env["HDLCPP_WRITE_MODULE_DEPENDENCIES"] = str(outputs["dependencies"])
        env["HDLCPP_METADATA_ONLY"] = "1"
        proc = subprocess.run(
            [str(HDLCPP), str(src)],
            cwd=work,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        return idx, rel, proc.returncode, proc.stdout[-1000:], outputs

    failures = []
    with ThreadPoolExecutor(max_workers=hdlcpp_jobs()) as executor:
        futures = [executor.submit(run_one, item) for item in enumerate(files)]
        for future in as_completed(futures):
            idx, rel, code, log, outputs = future.result()
            results[idx] = outputs
            if code != 0:
                failures.append((rel, code, log))
    if failures:
        detail = "\n".join(f"{rel}: exit={code}\n{log}" for rel, code, log in failures[:5])
        raise SystemExit(f"incremental metadata prepass failed:\n{detail}")

    new_lines = {name: [] for name in categories}
    changed_modules = set()
    for idx in range(len(files)):
        write_source_module_map(files[idx], work_root, results[idx])
        for name, path in results[idx].items():
            if not path.exists():
                continue
            lines = [line for line in path.read_text().splitlines() if line.strip()]
            new_lines[name].extend(lines)
            for line in lines:
                key = line.split("\t", 1)[0]
                changed_modules.add(key.split(".", 1)[0])

    for category, filename in categories.items():
        path = OUT / filename
        old_lines = path.read_text().splitlines() if path.exists() else []
        kept = [
            line for line in old_lines
            if not line.strip() or metadata_line_module(
                line, category in {"ports", "widths", "callables"}
            ) not in changed_modules
        ]
        combined = kept + new_lines[category]
        path.write_text("\n".join(combined) + ("\n" if combined else ""))

    auto_ports = (OUT / categories["ports"]).read_text()
    manual_ports_path = OUT / "cva6_port_types.tsv"
    manual_ports = manual_ports_path.read_text() if manual_ports_path.exists() else ""
    merged_ports = OUT / "cva6_merged_port_types.tsv"
    merged_ports.write_text(
        auto_ports + ("\n" if auto_ports and not auto_ports.endswith("\n") else "") + manual_ports
    )

    auto_params = (OUT / categories["params"]).read_text()
    manual_params_path = OUT / "cva6_module_params.tsv"
    manual_params = manual_params_path.read_text() if manual_params_path.exists() else ""
    manual_module_names = {
        line.split("\t", 1)[0]
        for line in manual_params.splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }
    filtered_auto_params = "\n".join(
        line for line in auto_params.splitlines()
        if not line.strip() or line.split("\t", 1)[0] not in manual_module_names
    )
    merged_params = OUT / "cva6_merged_module_params.tsv"
    merged_params.write_text(
        manual_params +
        ("\n" if manual_params and filtered_auto_params else "") +
        filtered_auto_params +
        ("\n" if filtered_auto_params else "")
    )

    auto_widths = (OUT / categories["widths"]).read_text()
    manual_widths_path = OUT / "cva6_type_widths.tsv"
    manual_widths = manual_widths_path.read_text() if manual_widths_path.exists() else ""
    (OUT / "cva6_merged_type_widths.tsv").write_text(
        auto_widths + ("\n" if auto_widths and not auto_widths.endswith("\n") else "") + manual_widths
    )

    auto_traits = (OUT / categories["traits"]).read_text()
    manual_traits_path = OUT / "cva6_module_traits.tsv"
    manual_traits = manual_traits_path.read_text() if manual_traits_path.exists() else ""
    (OUT / "cva6_merged_module_traits.tsv").write_text(
        auto_traits +
        ("\n" if auto_traits and manual_traits and not auto_traits.endswith("\n") else "") +
        manual_traits
    )
    return merged_ports, merged_params


def convert_one(src: Path, work_root: Path, gen_root: Path):
    rel = rel_source(src)
    work = work_root / rel.parent
    work.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    port_types, module_params = source_filtered_metadata(src, work, work_root)
    setup_hdlcpp_env(env, port_types, module_params)
    final_traits = work / ".cpphdl_final_module_traits.tsv"
    final_traits.write_text("")
    env["HDLCPP_APPEND_FINAL_MODULE_TRAITS"] = str(final_traits)
    proc = subprocess.run(
        [str(HDLCPP), str(src)],
        cwd=work,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    log = proc.stdout
    if os.environ.get("HDLCPP_DEBUG_ASSIGN_ADAPT") and log:
        print(log, end="" if log.endswith("\n") else "\n")
    if proc.returncode == 0 and final_traits.exists():
        discovered = [line for line in final_traits.read_text().splitlines() if line.strip()]
        for traits_path in (
            OUT / "cva6_auto_module_traits.tsv",
            OUT / "cva6_merged_module_traits.tsv",
        ):
            old_lines = traits_path.read_text().splitlines() if traits_path.exists() else []
            known = set(old_lines)
            additions = [line for line in discovered if line not in known]
            if additions:
                traits_path.write_text("\n".join(old_lines + additions) + "\n")
    generated = work / "generated" / f"{src.stem}.h"
    generated_cc = work / "generated" / f"{src.stem}.cc"
    dst_dir = gen_root / rel.parent
    dst_dir.mkdir(parents=True, exist_ok=True)
    if generated.exists():
        shutil.copy2(generated, dst_dir / generated.name)
    if generated_cc.exists():
        shutil.copy2(generated_cc, dst_dir / generated_cc.name)
    return proc.returncode, log, generated.exists()


def write_disabled_fpu_wrap(gen_root: Path):
    if "imac" not in TARGET and "ima_sv32" not in TARGET:
        return
    path = gen_root / "core" / "fpu_wrap.h"
    cc_path = path.with_suffix(".cc")
    if cc_path.exists():
        cc_path.unlink()
    path.parent.mkdir(parents=True, exist_ok=True)
    write_if_changed(path, """#pragma once

#include \"cpphdl.h\"

using namespace cpphdl;
using namespace ariane_pkg;

template<config_pkg::cva6_cfg_t CVA6Cfg = config_pkg::cva6_cfg_empty, typename exception_t = bool, typename fu_data_t = bool>
class fpu_wrap : public Module
{
public:
    _PORT(logic<1>) rst_ni_in;
    _PORT(logic<1>) flush_i_in;
    _PORT(logic<1>) fpu_valid_i_in;
    _PORT(fu_data_t) fu_data_i_in;
    _PORT(logic<2>) fpu_fmt_i_in;
    _PORT(logic<3>) fpu_rm_i_in;
    _PORT(logic<3>) fpu_frm_i_in;
    _PORT(logic<7>) fpu_prec_i_in;
    logic<1> fpu_ready_o = 1;
    logic<CVA6Cfg.TRANS_ID_BITS> fpu_trans_id_o = {};
    logic<CVA6Cfg.XLEN> result_o = {};
    logic<1> fpu_valid_o = {};
    exception_t fpu_exception_o = {};
    logic<1> fpu_early_valid_o = {};
    logic<1>& fpu_ready_o_out() { return fpu_ready_o; }
    logic<CVA6Cfg.TRANS_ID_BITS>& fpu_trans_id_o_out() { return fpu_trans_id_o; }
    logic<CVA6Cfg.XLEN>& result_o_out() { return result_o; }
    logic<1>& fpu_valid_o_out() { return fpu_valid_o; }
    exception_t& fpu_exception_o_out() { return fpu_exception_o; }
    logic<1>& fpu_early_valid_o_out() { return fpu_early_valid_o; }
    fpu_wrap() {}
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};
""")


def write_disabled_instr_tracer(gen_root: Path):
    path = gen_root / "common" / "local" / "util" / "instr_tracer.h"
    cc_path = path.with_suffix(".cc")
    if cc_path.exists():
        cc_path.unlink()
    path.parent.mkdir(parents=True, exist_ok=True)
    write_if_changed(path, """#pragma once

#include \"cpphdl.h\"

using namespace cpphdl;

template<config_pkg::cva6_cfg_t CVA6Cfg = config_pkg::cva6_cfg_empty, typename bp_resolve_t = bool, typename scoreboard_entry_t = bool, typename interrupts_t = bool, typename exception_t = bool, interrupts_t INTERRUPTS = {}>
class instr_tracer : public Module
{
public:
    instr_tracer() {}
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};
""")




def write_disabled_acc_dispatcher(gen_root: Path):
    if "imac" not in TARGET and "ima_sv32" not in TARGET:
        return
    path = gen_root / "core" / "acc_dispatcher.h"
    cc_path = path.with_suffix(".cc")
    if cc_path.exists():
        cc_path.unlink()
    path.parent.mkdir(parents=True, exist_ok=True)
    write_if_changed(path, """#pragma once

#include "cpphdl.h"

using namespace cpphdl;
using namespace ariane_pkg;

template<config_pkg::cva6_cfg_t CVA6Cfg = config_pkg::cva6_cfg_empty, typename dcache_req_i_t = bool, typename dcache_req_o_t = bool, typename exception_t = bool, typename fu_data_t = bool, typename scoreboard_entry_t = bool, typename acc_req_t = bool, typename acc_resp_t = bool, typename accelerator_req_t = bool, typename accelerator_resp_t = bool, typename acc_mmu_req_t = bool, typename acc_mmu_resp_t = bool, typename acc_cfg_t = bool, acc_cfg_t AccCfg = {}>
class acc_dispatcher : public Module
{
public:
    logic<1> flush_pipeline_o_out() { return {}; }
    logic<1> single_step_o_out() { return {}; }
    logic<1> acc_fflags_valid_o_out() { return {}; }
    logic<5> acc_fflags_o_out() { return {}; }
    logic<1> dirty_v_state_o_out() { return {}; }
    logic<1> issue_stall_o_out() { return {}; }
    logic<CVA6Cfg.TRANS_ID_BITS> acc_trans_id_o_out() { return {}; }
    logic<CVA6Cfg.XLEN> acc_result_o_out() { return {}; }
    logic<1> acc_valid_o_out() { return {}; }
    exception_t acc_exception_o_out() { return {}; }
    logic<1> acc_valid_ex_o_out() { return {}; }
    logic<1> acc_stall_st_pending_o_out() { return {}; }
    acc_mmu_req_t acc_mmu_req_o_out() { return {}; }
    logic<1> ctrl_halt_o_out() { return {}; }
    array<2,dcache_req_i_t> acc_dcache_req_ports_o_out() { return {}; }
    logic<1> inval_valid_o_out() { return {}; }
    logic<64> inval_addr_o_out() { return {}; }
    acc_req_t acc_req_o_out() { return {}; }
    acc_dispatcher() {}
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
	};
	""")


def write_xlnx_axi_stubs(gen_root: Path):
    parent = gen_root / "corev_apu" / "tb" / "ariane_peripherals.h"
    if not parent.exists():
        return None
    text = parent.read_text(errors="replace")
    instances = {
        "xlnx_axi_clock_converter": "i_xlnx_axi_clock_converter_spi",
        "xlnx_axi_quad_spi": "i_xlnx_axi_quad_spi",
    }
    classes = {}
    for cls, inst in instances.items():
        ports = sorted(set(re.findall(rf"\b{re.escape(inst)}\.([A-Za-z_][A-Za-z0-9_]*(?:_in|_out))\b", text)))
        if ports:
            classes[cls] = ports
    if not classes:
        return None

    path = gen_root / "corev_apu" / "tb" / "xlnx_axi_stubs.h"
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "#pragma once",
        "",
        "#include \"cpphdl.h\"",
        "",
        "using namespace cpphdl;",
        "",
    ]
    for cls, ports in classes.items():
        lines.append(f"class {cls} : public Module")
        lines.append("{")
        lines.append("public:")
        for port in ports:
            if port.endswith("_out"):
                lines.append(f"    _PORT(logic<64>) {port} = _ASSIGN(logic<64>{{}});")
            else:
                lines.append(f"    _PORT(logic<64>) {port};")
        lines.append("")
        lines.append("    void _work(bool) {}")
        lines.append("    void _strobe() {}")
        lines.append("    void _assign() {}")
        lines.append("};")
        lines.append("")
    write_if_changed(path, "\n".join(lines))
    return path


def write_uart_bus_stub(gen_root: Path):
    path = gen_root / "corev_apu" / "tb" / "common" / "uart.h"
    cc_path = path.with_suffix(".cc")
    if cc_path.exists():
        cc_path.unlink()
    path.parent.mkdir(parents=True, exist_ok=True)
    write_if_changed(path, """#pragma once

#include "cpphdl.h"

using namespace cpphdl;

template<unsigned BAUD_RATE = 115200, unsigned PARITY_EN = 0>
class uart_bus : public Module
{
public:
    _PORT(logic<1>) rx_in;
    _PORT(logic<1>) tx_out = _ASSIGN(logic<1>(0b1));
    _PORT(logic<1>) rx_en_in;

    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};
""")


def write_dpi_adapter_header(gen_root: Path):
    path = gen_root / "corev_apu" / "tb" / "dpi_adapters.h"
    path.parent.mkdir(parents=True, exist_ok=True)
    write_if_changed(path, """#pragma once

#include <cstdint>
#include <type_traits>
#include "cpphdl.h"

using svBit = unsigned char;

extern "C" int debug_tick(svBit* debug_req_valid, svBit debug_req_ready,
                           int* debug_req_bits_addr, int* debug_req_bits_op,
                           int* debug_req_bits_data, svBit debug_resp_valid,
                           svBit* debug_resp_ready, int debug_resp_bits_resp,
                           int debug_resp_bits_data);
extern "C" int jtag_tick(svBit* jtag_TCK, svBit* jtag_TMS, svBit* jtag_TDI,
                         svBit* jtag_TRSTn, svBit jtag_TDO);

template<typename T>
static inline uint64_t cpphdl_dpi_u64(const T& value)
{
    return static_cast<uint64_t>(value);
}

template<typename T>
static inline void cpphdl_dpi_assign(T& dst, uint64_t value)
{
    if constexpr (requires(const T& current) { current.pack(); }) {
        using value_t = std::remove_cvref_t<decltype(dst.pack())>;
        dst = value_t(value);
    } else {
        dst = T(value);
    }
}

template<typename ReqValid, typename ReqReady, typename ReqAddr, typename ReqOp,
         typename ReqData, typename RespValid, typename RespReady,
         typename RespBitsResp, typename RespBitsData>
static inline int debug_tick(ReqValid& debug_req_valid, const ReqReady& debug_req_ready,
                             ReqAddr& debug_req_bits_addr, ReqOp& debug_req_bits_op,
                             ReqData& debug_req_bits_data, const RespValid& debug_resp_valid,
                             RespReady& debug_resp_ready, const RespBitsResp& debug_resp_bits_resp,
                             const RespBitsData& debug_resp_bits_data)
{
    svBit req_valid = static_cast<svBit>(cpphdl_dpi_u64(debug_req_valid) & 1u);
    svBit req_ready = static_cast<svBit>(cpphdl_dpi_u64(debug_req_ready) & 1u);
    int req_addr = static_cast<int>(cpphdl_dpi_u64(debug_req_bits_addr));
    int req_op = static_cast<int>(cpphdl_dpi_u64(debug_req_bits_op));
    int req_data = static_cast<int>(cpphdl_dpi_u64(debug_req_bits_data));
    svBit resp_valid = static_cast<svBit>(cpphdl_dpi_u64(debug_resp_valid) & 1u);
    svBit resp_ready = static_cast<svBit>(cpphdl_dpi_u64(debug_resp_ready) & 1u);
    int resp_bits_resp = static_cast<int>(cpphdl_dpi_u64(debug_resp_bits_resp));
    int resp_bits_data = static_cast<int>(cpphdl_dpi_u64(debug_resp_bits_data));
    int ret = ::debug_tick(&req_valid, req_ready, &req_addr, &req_op, &req_data,
                           resp_valid, &resp_ready, resp_bits_resp, resp_bits_data);
    cpphdl_dpi_assign(debug_req_valid, req_valid);
    cpphdl_dpi_assign(debug_req_bits_addr, static_cast<uint32_t>(req_addr));
    cpphdl_dpi_assign(debug_req_bits_op, static_cast<uint32_t>(req_op));
    cpphdl_dpi_assign(debug_req_bits_data, static_cast<uint32_t>(req_data));
    cpphdl_dpi_assign(debug_resp_ready, resp_ready);
    return ret;
}

template<typename TCK, typename TMS, typename TDI, typename TRSTn, typename TDO>
static inline int jtag_tick(TCK& jtag_TCK, TMS& jtag_TMS, TDI& jtag_TDI,
                            TRSTn& jtag_TRSTn, const TDO& jtag_TDO)
{
    svBit tck = static_cast<svBit>(cpphdl_dpi_u64(jtag_TCK) & 1u);
    svBit tms = static_cast<svBit>(cpphdl_dpi_u64(jtag_TMS) & 1u);
    svBit tdi = static_cast<svBit>(cpphdl_dpi_u64(jtag_TDI) & 1u);
    svBit trstn = static_cast<svBit>(cpphdl_dpi_u64(jtag_TRSTn) & 1u);
    svBit tdo = static_cast<svBit>(cpphdl_dpi_u64(jtag_TDO) & 1u);
    int ret = ::jtag_tick(&tck, &tms, &tdi, &trstn, tdo);
    cpphdl_dpi_assign(jtag_TCK, tck);
    cpphdl_dpi_assign(jtag_TMS, tms);
    cpphdl_dpi_assign(jtag_TDI, tdi);
    cpphdl_dpi_assign(jtag_TRSTn, trstn);
    return ret;
}
""")
    return path


def write_tech_clock_cells(gen_root: Path):
    tc_path = gen_root / "vendor" / "pulp-platform" / "tech_cells_generic" / "src" / "rtl" / "tc_clk.h"
    tc_path.parent.mkdir(parents=True, exist_ok=True)
    write_if_changed(tc_path, """#pragma once

#include "cpphdl.h"

using namespace cpphdl;

class tc_clk_and2 : public Module {
public:
    _PORT(logic<1>) clk0_i_in;
    _PORT(logic<1>) clk1_i_in;
    _PORT(logic<1>) clk_o_out = _ASSIGN_COMB(clk_o_comb_func());
private:
    logic<1> clk_o_comb;
    logic<1>& clk_o_comb_func() { clk_o_comb = clk0_i_in() & clk1_i_in(); return clk_o_comb; }
public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

class tc_clk_buffer : public Module {
public:
    _PORT(logic<1>) clk_i_in;
    _PORT(logic<1>) clk_o_out = _ASSIGN_COMB(clk_o_comb_func());
private:
    logic<1> clk_o_comb;
    logic<1>& clk_o_comb_func() { clk_o_comb = clk_i_in(); return clk_o_comb; }
public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

template<uint64_t IS_FUNCTIONAL = 1>
class tc_clk_gating : public Module {
public:
    _PORT(logic<1>) en_i_in;
    _PORT(logic<1>) test_en_i_in;
    _PORT(logic<1>) clk_o_out = _ASSIGN_COMB(clk_o_comb_func());
private:
    reg<logic<1>> clk_en;
    logic<1> clk_o_comb;
    logic<1>& clk_o_comb_func() { clk_o_comb = clk_en; return clk_o_comb; }
public:
    void _work(bool) {
        clk_en._next = clk_en;
        clk_en._next = en_i_in() | test_en_i_in();
    }
    void _strobe() { clk_en.strobe(); }
    void _assign() {}
};

class tc_clk_inverter : public Module {
public:
    _PORT(logic<1>) clk_i_in;
    _PORT(logic<1>) clk_o_out = _ASSIGN_COMB(clk_o_comb_func());
private:
    logic<1> clk_o_comb;
    logic<1>& clk_o_comb_func() { clk_o_comb = logic<1>(~(uint64_t)clk_i_in()); return clk_o_comb; }
public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

class tc_clk_mux2 : public Module {
public:
    _PORT(logic<1>) clk0_i_in;
    _PORT(logic<1>) clk1_i_in;
    _PORT(logic<1>) clk_sel_i_in;
    _PORT(logic<1>) clk_o_out = _ASSIGN_COMB(clk_o_comb_func());
private:
    logic<1> clk_o_comb;
    logic<1>& clk_o_comb_func() { clk_o_comb = clk_sel_i_in() ? clk1_i_in() : clk0_i_in(); return clk_o_comb; }
public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

class tc_clk_xor2 : public Module {
public:
    _PORT(logic<1>) clk0_i_in;
    _PORT(logic<1>) clk1_i_in;
    _PORT(logic<1>) clk_o_out = _ASSIGN_COMB(clk_o_comb_func());
private:
    logic<1> clk_o_comb;
    logic<1>& clk_o_comb_func() { clk_o_comb = clk0_i_in() ^ clk1_i_in(); return clk_o_comb; }
public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

class tc_clk_or2 : public Module {
public:
    _PORT(logic<1>) clk0_i_in;
    _PORT(logic<1>) clk1_i_in;
    _PORT(logic<1>) clk_o_out = _ASSIGN_COMB(clk_o_comb_func());
private:
    logic<1> clk_o_comb;
    logic<1>& clk_o_comb_func() { clk_o_comb = clk0_i_in() | clk1_i_in(); return clk_o_comb; }
public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

template<unsigned Delay = 300>
class tc_clk_delay : public Module {
public:
    _PORT(logic<1>) in_i_in;
    _PORT(logic<1>) out_o_out = _ASSIGN_COMB(out_o_comb_func());
private:
    logic<1> out_o_comb;
    logic<1>& out_o_comb_func() { out_o_comb = in_i_in(); return out_o_comb; }
public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};
""")

    pulp_path = gen_root / "vendor" / "pulp-platform" / "tech_cells_generic" / "src" / "deprecated" / "pulp_clk_cells.h"
    pulp_path.parent.mkdir(parents=True, exist_ok=True)
    write_if_changed(pulp_path, """#pragma once

#include "cpphdl.h"

using namespace cpphdl;

class pulp_clock_and2 : public Module {
public:
    _PORT(logic<1>) clk0_i_in;
    _PORT(logic<1>) clk1_i_in;
    _PORT(logic<1>) clk_o_out = _ASSIGN_COMB(clk_o_comb_func());
private:
    logic<1> clk_o_comb;
    logic<1>& clk_o_comb_func() { clk_o_comb = clk0_i_in() & clk1_i_in(); return clk_o_comb; }
public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

class pulp_clock_buffer : public Module {
public:
    _PORT(logic<1>) clk_i_in;
    _PORT(logic<1>) clk_o_out = _ASSIGN_COMB(clk_o_comb_func());
private:
    logic<1> clk_o_comb;
    logic<1>& clk_o_comb_func() { clk_o_comb = clk_i_in(); return clk_o_comb; }
public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

class pulp_clock_gating : public Module {
public:
    _PORT(logic<1>) en_i_in;
    _PORT(logic<1>) test_en_i_in;
    _PORT(logic<1>) clk_o_out = _ASSIGN_COMB(clk_o_comb_func());
private:
    tc_clk_gating<> i_tc_clk_gating;
    logic<1> clk_o_comb;
    logic<1>& clk_o_comb_func() { clk_o_comb = i_tc_clk_gating.clk_o_out(); return clk_o_comb; }
public:
    void _work(bool reset) { i_tc_clk_gating._work(reset); }
    void _strobe() { i_tc_clk_gating._strobe(); }
    void _assign() {
        i_tc_clk_gating.en_i_in = _ASSIGN_COMB(en_i_in());
        i_tc_clk_gating.test_en_i_in = _ASSIGN_COMB(test_en_i_in());
        i_tc_clk_gating._assign();
    }
};

class pulp_clock_inverter : public Module {
public:
    _PORT(logic<1>) clk_i_in;
    _PORT(logic<1>) clk_o_out = _ASSIGN_COMB(clk_o_comb_func());
private:
    logic<1> clk_o_comb;
    logic<1>& clk_o_comb_func() { clk_o_comb = logic<1>(~(uint64_t)clk_i_in()); return clk_o_comb; }
public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

class pulp_clock_mux2 : public Module {
public:
    _PORT(logic<1>) clk0_i_in;
    _PORT(logic<1>) clk1_i_in;
    _PORT(logic<1>) clk_sel_i_in;
    _PORT(logic<1>) clk_o_out = _ASSIGN_COMB(clk_o_comb_func());
private:
    logic<1> clk_o_comb;
    logic<1>& clk_o_comb_func() { clk_o_comb = clk_sel_i_in() ? clk1_i_in() : clk0_i_in(); return clk_o_comb; }
public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

class pulp_clock_xor2 : public Module {
public:
    _PORT(logic<1>) clk0_i_in;
    _PORT(logic<1>) clk1_i_in;
    _PORT(logic<1>) clk_o_out = _ASSIGN_COMB(clk_o_comb_func());
private:
    logic<1> clk_o_comb;
    logic<1>& clk_o_comb_func() { clk_o_comb = clk0_i_in() ^ clk1_i_in(); return clk_o_comb; }
public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

template<unsigned Delay = 300>
class pulp_clock_delay : public Module {
public:
    _PORT(logic<1>) in_i_in;
    _PORT(logic<1>) out_o_out = _ASSIGN_COMB(out_o_comb_func());
private:
    logic<1> out_o_comb;
    logic<1>& out_o_comb_func() { out_o_comb = in_i_in(); return out_o_comb; }
public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};
""")

def write_rvfi_types(gen_root: Path):
    path = gen_root / "rvfi_types.h"
    write_if_changed(path, """#pragma once

#include "cpphdl.h"

using namespace cpphdl;

namespace cpphdl_rvfi {
#define CPPHDL_RVFI_ZERO_ASSIGN(TYPE) \
    template<typename V, typename std::enable_if_t<std::is_integral_v<V> || std::is_enum_v<V>, int> = 0> \
    TYPE& operator=(V) { *this = TYPE{}; return *this; }

template<auto Cfg>
struct instr_t {
    logic<config_pkg::NRET> valid;
    logic<config_pkg::NRET*64> order;
    logic<config_pkg::NRET*config_pkg::ILEN> insn;
    logic<config_pkg::NRET> trap;
    logic<config_pkg::NRET*Cfg.XLEN> cause;
    logic<config_pkg::NRET> halt;
    logic<config_pkg::NRET*Cfg.XLEN> intr;
    logic<config_pkg::NRET*2> mode;
    logic<config_pkg::NRET*2> ixl;
    logic<config_pkg::NRET*5> rs1_addr;
    logic<config_pkg::NRET*5> rs2_addr;
    logic<config_pkg::NRET*Cfg.XLEN> rs1_rdata;
    logic<config_pkg::NRET*Cfg.XLEN> rs2_rdata;
    logic<config_pkg::NRET*5> rd_addr;
    logic<config_pkg::NRET*Cfg.XLEN> rd_wdata;
    logic<config_pkg::NRET*Cfg.XLEN> pc_rdata;
    logic<config_pkg::NRET*Cfg.XLEN> pc_wdata;
    logic<config_pkg::NRET*Cfg.VLEN> mem_addr;
    logic<config_pkg::NRET*Cfg.PLEN> mem_paddr;
    logic<config_pkg::NRET*(Cfg.XLEN/8)> mem_rmask;
    logic<config_pkg::NRET*(Cfg.XLEN/8)> mem_wmask;
    logic<config_pkg::NRET*Cfg.XLEN> mem_rdata;
    logic<config_pkg::NRET*Cfg.XLEN> mem_wdata;
    CPPHDL_RVFI_ZERO_ASSIGN(instr_t)
};

template<auto Cfg>
struct csr_elmt_t {
    logic<Cfg.XLEN> rdata;
    logic<Cfg.XLEN> rmask;
    logic<Cfg.XLEN> wdata;
    logic<Cfg.XLEN> wmask;
    CPPHDL_RVFI_ZERO_ASSIGN(csr_elmt_t)
};

template<auto Cfg>
struct probes_instr_t {
    array<Cfg.NrIssuePorts,logic<Cfg.TRANS_ID_BITS>> issue_pointer;
    array<Cfg.NrCommitPorts,logic<Cfg.TRANS_ID_BITS>> commit_pointer;
    logic<1> flush_unissued_instr;
    logic<Cfg.NrIssuePorts> decoded_instr_valid;
    logic<Cfg.NrIssuePorts> decoded_instr_ack;
    logic<1> flush;
    logic<Cfg.NrIssuePorts> issue_instr_ack;
    logic<Cfg.NrIssuePorts> fetch_entry_valid;
    array<Cfg.NrIssuePorts,logic<32>> instruction;
    logic<Cfg.NrIssuePorts> is_compressed;
    array<Cfg.NrIssuePorts,logic<Cfg.XLEN>> rs1;
    array<Cfg.NrIssuePorts,logic<Cfg.XLEN>> rs2;
    array<Cfg.NrCommitPorts,logic<Cfg.VLEN>> commit_instr_pc;
    array<Cfg.NrCommitPorts,ariane_pkg::fu_op> commit_instr_op;
    array<Cfg.NrCommitPorts,logic<ariane_pkg::REG_ADDR_SIZE>> commit_instr_rs1;
    array<Cfg.NrCommitPorts,logic<ariane_pkg::REG_ADDR_SIZE>> commit_instr_rs2;
    array<Cfg.NrCommitPorts,logic<ariane_pkg::REG_ADDR_SIZE>> commit_instr_rd;
    array<Cfg.NrCommitPorts,logic<Cfg.XLEN>> commit_instr_result;
    logic<Cfg.NrCommitPorts> commit_instr_valid;
    logic<Cfg.NrCommitPorts> commit_drop;
    logic<Cfg.XLEN> ex_commit_cause;
    logic<1> ex_commit_valid;
    riscv::priv_lvl_t priv_lvl;
    logic<Cfg.VLEN> lsu_ctrl_vaddr;
    ariane_pkg::fu_t lsu_ctrl_fu;
    logic<(Cfg.XLEN/8)> lsu_ctrl_be;
    logic<Cfg.TRANS_ID_BITS> lsu_ctrl_trans_id;
    array<Cfg.NrWbPorts,logic<Cfg.XLEN>> wbdata;
    logic<Cfg.NrCommitPorts> commit_ack;
    logic<Cfg.PLEN> mem_paddr;
    logic<1> debug_mode;
    array<Cfg.NrCommitPorts,logic<Cfg.XLEN>> wdata;
    logic<1> branch_valid;
    logic<1> is_taken;
    logic<Cfg.XLEN> tval;
    logic<Cfg.TRANS_ID_BITS> branch_trans_id;
    CPPHDL_RVFI_ZERO_ASSIGN(probes_instr_t)
};

template<auto Cfg>
struct probes_csr_t {
    riscv::fcsr_t fcsr_q;
    riscv::dcsr_t dcsr_q;
    logic<Cfg.XLEN> jvt_q;
    logic<Cfg.XLEN> dpc_q;
    logic<Cfg.XLEN> dscratch0_q;
    logic<Cfg.XLEN> dscratch1_q;
    logic<Cfg.XLEN> mie_q;
    logic<Cfg.XLEN> mip_q;
    logic<Cfg.XLEN> stvec_q;
    logic<Cfg.XLEN> scounteren_q;
    logic<Cfg.XLEN> sscratch_q;
    logic<Cfg.XLEN> sepc_q;
    logic<Cfg.XLEN> scause_q;
    logic<Cfg.XLEN> stval_q;
    logic<Cfg.XLEN> satp_q;
    logic<Cfg.XLEN> mstatus_extended;
    logic<Cfg.XLEN> medeleg_q;
    logic<Cfg.XLEN> mideleg_q;
    logic<Cfg.XLEN> mtvec_q;
    logic<Cfg.XLEN> mcounteren_q;
    logic<Cfg.XLEN> mscratch_q;
    logic<Cfg.XLEN> mepc_q;
    logic<Cfg.XLEN> mcause_q;
    logic<Cfg.XLEN> mtval_q;
    logic<1> fiom_q;
    logic<ariane_pkg::MHPMCounterNum+3> mcountinhibit_q;
    logic<64> cycle_q;
    logic<64> instret_q;
    logic<Cfg.XLEN> dcache_q;
    logic<Cfg.XLEN> icache_q;
    logic<Cfg.XLEN> acc_cons_q;
    array<64,riscv::pmpcfg_t> pmpcfg_q;
    // SV [Cfg.PLEN-3:0] includes both endpoints.
    // Its width is (Cfg.PLEN-3)-0+1, or Cfg.PLEN-2.
    // Missing this +1 makes a legal top-bit read fail at runtime.
    array<64,logic<(Cfg.PLEN-2)>> pmpaddr_q;
    CPPHDL_RVFI_ZERO_ASSIGN(probes_csr_t)
};

template<auto Cfg>
struct probes_t {
    probes_csr_t<Cfg> csr;
    probes_instr_t<Cfg> instr;
    CPPHDL_RVFI_ZERO_ASSIGN(probes_t)
};

template<auto Cfg>
struct csr_t {
    csr_elmt_t<Cfg> fflags, frm, fcsr, jvt, ftran, dcsr, dpc, dscratch0, dscratch1;
    csr_elmt_t<Cfg> sstatus, sie, sip, stvec, scounteren, sscratch, sepc, scause, stval, satp;
    csr_elmt_t<Cfg> mstatus, mstatush, misa, medeleg, mideleg, mie, mtvec, mcounteren;
    csr_elmt_t<Cfg> mscratch, mepc, mcause, mtval, mip, menvcfg, menvcfgh;
    csr_elmt_t<Cfg> mvendorid, marchid, mhartid, mcountinhibit, mcycle, mcycleh, minstret, minstreth;
    csr_elmt_t<Cfg> cycle, cycleh, instret, instreth, dcache, icache, acc_cons;
    csr_elmt_t<Cfg> pmpcfg0, pmpcfg1, pmpcfg2, pmpcfg3;
    array<16,csr_elmt_t<Cfg>> pmpaddr;
    CPPHDL_RVFI_ZERO_ASSIGN(csr_t)
};

template<auto Cfg>
struct to_iti_t {
    logic<Cfg.NrCommitPorts> valid;
    array<Cfg.NrCommitPorts,logic<Cfg.VLEN>> pc;
    array<Cfg.NrCommitPorts,ariane_pkg::fu_op> op;
    logic<Cfg.NrCommitPorts> is_compressed;
    logic<Cfg.NrCommitPorts> branch_valid;
    logic<Cfg.NrCommitPorts> is_taken;
    logic<1> ex_valid;
    logic<Cfg.XLEN> tval;
    logic<Cfg.XLEN> cause;
    riscv::priv_lvl_t priv_lvl;
    logic<64> cycles;
    CPPHDL_RVFI_ZERO_ASSIGN(to_iti_t)
};

template<auto Cfg>
struct encoder_t {
    logic<Cfg.NrCommitPorts> valid;
    array<Cfg.NrCommitPorts,logic<32>> iretire;
    array<Cfg.NrCommitPorts,logic<1>> ilastsize;
    array<Cfg.NrCommitPorts,logic<3>> itype;
    logic<5> cause;
    logic<Cfg.XLEN> tval;
    riscv::priv_lvl_t priv;
    array<Cfg.NrCommitPorts,logic<Cfg.XLEN>> iaddr;
    logic<64> cycles;
    CPPHDL_RVFI_ZERO_ASSIGN(encoder_t)
};
#undef CPPHDL_RVFI_ZERO_ASSIGN
}
""")

def write_project_makefile(out: Path, gen_sources):
    if gen_sources:
        sources = " \\\n    ".join(src.as_posix() for src in gen_sources)
        gen_src_block = f"GEN_SRCS := {sources}\n"
    else:
        gen_src_block = "GEN_SRCS :=\n"
    write_if_changed(out / "Makefile", f"""CXX ?= g++
CPPHDL_INCLUDE ?= /home/me/cpphdl/include
CXXFLAGS ?= -std=c++23 -O0 -g0 -I$(CPPHDL_INCLUDE) -I$(CURDIR)
LDFLAGS ?=

{gen_src_block}
GEN_OBJS := $(patsubst generated/%.cc,build/obj/%.o,$(GEN_SRCS))
RUNNER := run_cpphdl_matrix
RUNNER_OBJ := build/obj/run_cpphdl_matrix.o

.PHONY: all clean objects

all: $(RUNNER)

objects: $(GEN_OBJS)

build/obj/%.o: generated/%.cc all_generated.h
\t@mkdir -p $(dir $@)
\t$(CXX) $(CXXFLAGS) -include all_generated.h -c $< -o $@

$(RUNNER_OBJ): run_cpphdl_matrix.cpp all_generated.h
\t@mkdir -p $(dir $@)
\t$(CXX) $(CXXFLAGS) -c run_cpphdl_matrix.cpp -o $@

$(RUNNER): $(RUNNER_OBJ) $(GEN_OBJS)
\t$(CXX) $(CXXFLAGS) $(RUNNER_OBJ) $(GEN_OBJS) -o $@ $(LDFLAGS)

clean:
\trm -rf build/obj $(RUNNER)
""")

def main():
    if not HDLCPP.exists():
        raise SystemExit(f"hdlcpp not found: {HDLCPP}")
    files, incdirs = collect_sources()
    os.environ.setdefault("HDLCPP_INCLUDE_DIRS", os.pathsep.join(str(path) for path in incdirs))
    os.environ.setdefault("HDLCPP_DEFINES", "VERILATOR")
    OUT.mkdir(exist_ok=True)
    (OUT / "logs").mkdir(exist_ok=True)
    manifest = OUT / "manifest.txt"
    manifest.write_text("\n".join(str(p) for p in files) + "\n")
    (OUT / "incdirs.txt").write_text("\n".join(str(p) for p in incdirs) + "\n")
    all_conversion_files = [src for src in files if include_source_for_cpphdl(src)]
    conversion_files = list(all_conversion_files)
    finalize_only = os.environ.get("CPPHDL_CVA6_FINALIZE_ONLY", "").strip() == "1"
    resume_traits = os.environ.get("CPPHDL_CVA6_RESUME_TRAITS", "").strip() == "1"
    keep_incremental_metadata = os.environ.get("CPPHDL_CVA6_KEEP_METADATA", "").strip() == "1"
    only_raw = os.environ.get("CPPHDL_CVA6_ONLY", "").strip()
    incremental = bool(only_raw)
    if keep_incremental_metadata and not incremental:
        raise SystemExit("CPPHDL_CVA6_KEEP_METADATA requires CPPHDL_CVA6_ONLY")
    if sum((finalize_only, resume_traits, incremental)) > 1:
        raise SystemExit("finalize, trait-resume, and source-incremental modes are exclusive")
    if incremental:
        requested = {item.strip() for item in only_raw.split(",") if item.strip()}
        conversion_files = [src for src in conversion_files if rel_source(src).as_posix() in requested]
        found = {rel_source(src).as_posix() for src in conversion_files}
        missing = sorted(requested - found)
        if missing:
            raise SystemExit("unknown CPPHDL_CVA6_ONLY sources: " + ", ".join(missing))
        conversion_files = expand_sources_with_dependents(
            conversion_files, all_conversion_files, OUT / "work"
        )
        if os.environ.get("CPPHDL_CVA6_SKIP_STALE_TRAIT_SCAN", "").strip() != "1":
            stale_trait_sources = sources_with_stale_projected_traits(
                all_conversion_files, OUT / "work"
            )
            if stale_trait_sources:
                print(f"stale_projected_trait_sources={len(stale_trait_sources)}")
                conversion_files = expand_sources_with_dependents(
                    list(set(conversion_files) | set(stale_trait_sources)),
                    all_conversion_files,
                    OUT / "work",
                )
    skipped_files = len(files) - len(conversion_files)

    gen_root = OUT / "generated"
    work_root = OUT / "work"
    if gen_root.exists() and not incremental and not finalize_only and not resume_traits:
        shutil.rmtree(gen_root)
    if work_root.exists() and not incremental and not finalize_only and not resume_traits:
        shutil.rmtree(work_root)
    gen_root.mkdir(parents=True, exist_ok=True)
    work_root.mkdir(parents=True, exist_ok=True)

    if resume_traits:
        metadata_traits = set()
        for path in (work_root / "_metadata_tsv").glob("*.traits.tsv"):
            metadata_traits.update(line for line in path.read_text().splitlines() if line.strip())
        traits_path = OUT / "cva6_merged_module_traits.tsv"
        current_traits = set(traits_path.read_text().splitlines()) if traits_path.exists() else set()
        discovered_modules = {
            line.split("\t", 1)[0]
            for line in current_traits - metadata_traits
            if "\t" in line
        }
        module_owner = {}
        for src in all_conversion_files:
            rel = rel_source(src)
            modules_path = work_root / "_source_modules" / rel.parent / f"{rel.name}.txt"
            if modules_path.exists():
                for module in modules_path.read_text().splitlines():
                    if module.strip():
                        module_owner[module.strip()] = src
        demanded_sources = [module_owner[name] for name in discovered_modules if name in module_owner]
        conversion_files = expand_sources_with_dependents(
            demanded_sources, all_conversion_files, work_root
        )
        conversion_files = order_sources_by_module_dependencies(conversion_files, work_root)

    jobs = hdlcpp_jobs()
    print(f"hdlcpp_jobs={jobs}")
    print(f"conversion_sources={len(conversion_files)} skipped={skipped_files}")
    if finalize_only or resume_traits:
        port_types = OUT / "cva6_merged_port_types.tsv"
        module_params = OUT / "cva6_merged_module_params.tsv"
        if not port_types.exists() or not module_params.exists():
            raise SystemExit("resume conversion requires existing merged metadata")
        if finalize_only:
            conversion_files = []
    elif incremental:
        prior_port_types = OUT / "cva6_merged_port_types.tsv"
        prior_module_params = OUT / "cva6_merged_module_params.tsv"
        if not prior_port_types.exists() or not prior_module_params.exists():
            raise SystemExit("incremental conversion requires existing merged metadata")
        if keep_incremental_metadata:
            port_types, module_params = prior_port_types, prior_module_params
        else:
            port_types, module_params = refresh_incremental_metadata(conversion_files, work_root)
        # Incremental emission also appends field traits that are only known after a
        # child's complete comb model is generated. Preserve child-before-parent order
        # so every selected parent sees those final traits in the same invocation.
        conversion_files = order_sources_by_module_dependencies(conversion_files, work_root)
    else:
        port_types, module_params = build_metadata(conversion_files, work_root)
        conversion_files = order_sources_by_module_dependencies(conversion_files, work_root)
    print(f"port_types={port_types}")
    print(f"module_params={module_params}")

    failures = []
    def run_convert(item):
        idx, src = item
        if not src.exists():
            return idx, src, "", False, "missing source"
        code, log, ok = convert_one(src, work_root, gen_root)
        reason = ""
        if code != 0 or not ok:
            reason = f"exit={code} generated={ok}"
        return idx, src, log, ok, reason

    module_owner = {}
    for src in all_conversion_files:
        rel = rel_source(src)
        modules_path = work_root / "_source_modules" / rel.parent / f"{rel.name}.txt"
        if modules_path.exists():
            for module in modules_path.read_text().splitlines():
                if module.strip():
                    module_owner[module.strip()] = src

    # Complete emission can discover aggregate field contracts absent from the metadata
    # prepass. Iterate until those contracts stop growing, adding a newly demanded child's
    # source and all affected parents before the next child-first emission pass.
    latest_results = {}
    touched_sources = set()
    dirty_sources = list(conversion_files)
    traits_path = OUT / "cva6_merged_module_traits.tsv"
    for emission_pass in range(1, 13):
        before_traits = set(traits_path.read_text().splitlines()) if traits_path.exists() else set()
        pass_results = {}
        for item in enumerate(dirty_sources, 1):
            idx, src, log, ok, reason = run_convert(item)
            pass_results[idx] = (src, log, ok, reason)
            latest_results[src] = (log, ok, reason)
            touched_sources.add(src)
        if any(item[3] for item in pass_results.values()):
            break

        after_traits = set(traits_path.read_text().splitlines()) if traits_path.exists() else set()
        additions = {line for line in after_traits - before_traits if line.strip()}
        demanded_sources = {
            module_owner[line.split("\t", 1)[0]]
            for line in additions
            if "\t" in line and line.split("\t", 1)[0] in module_owner
        }
        next_dirty = expand_sources_with_dependents(
            list(demanded_sources), all_conversion_files, work_root
        )
        next_dirty = order_sources_by_module_dependencies(next_dirty, work_root)
        print(f"emission_pass={emission_pass} new_traits={len(additions)} sources={len(next_dirty)}")
        if not additions:
            break
        if not next_dirty:
            raise SystemExit("new module field traits have no owning conversion sources")
        dirty_sources = next_dirty
    else:
        raise SystemExit("module field traits did not converge after 12 emission passes")

    conversion_files = order_sources_by_module_dependencies(list(touched_sources), work_root)
    for idx, src in enumerate(conversion_files, 1):
        log, ok, reason = latest_results[src]
        rel = rel_source(src)
        log_path = OUT / "logs" / (str(rel).replace("/", "__") + ".log")
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text(log)
        if reason:
            failures.append((src, reason))
        print(f"[{idx:03d}/{len(conversion_files):03d}] {rel}")

    write_disabled_fpu_wrap(gen_root)
    write_disabled_instr_tracer(gen_root)
    write_disabled_acc_dispatcher(gen_root)
    write_uart_bus_stub(gen_root)
    dpi_adapter = write_dpi_adapter_header(gen_root)
    write_tech_clock_cells(gen_root)
    write_rvfi_types(gen_root)
    xlnx_stubs = write_xlnx_axi_stubs(gen_root)

    aggregate_header = OUT / "all_generated.h"
    aggregate = OUT / "all_generated.cpp"
    includes = []
    def include_priority(src: Path):
        order = {
            "cf_math_pkg": -20,
            "cv32a6_imac_sv32_config_pkg": -20,
            "cvxif_instr_pkg": -20,
            "config_pkg": -20,
            "riscv_pkg": -20,
            "ariane_pkg": -20,
            "axi_pkg": -20,
            "wt_cache_pkg": -20,
            "std_cache_pkg": -20,
            "hpdcache_pkg": -20,
            "hwpf_stride_pkg": -20,
            "dummy_l15_pkg": -20,
            "instr_tracer_pkg": -20,
            "build_config_pkg": -20,
            "aes_pkg": -20,
            "triggers_pkg": -20,
            "lzc": 10,
            "rr_arb_tree": 11,
            "delta_counter": 12,
            "stream_arbiter_flushable": 20,
            "stream_arbiter": 21,
            "stream_mux": 22,
            "counter": 23,
            "spill_register": 23,
            "spill_register_flushable": 23,
            "exp_backoff": 23,
            "lfsr_8bit": 23,
            "cva6_fifo_v3": 24,
            "SyncDpRam_ind_r_w": 24,
            "shift_reg": 25,
            "compressed_instr_decoder": 30,
            "instr_decoder": 31,
            "copro_alu": 32,
            "cvxif_example_coprocessor": 40,
            "ariane_regfile_ff": 50,
            "ariane_regfile_fpga": 50,
            "scoreboard": 51,
            "multiplier": 52,
            "serdiv": 53,
            "mult": 54,
            "pmp_entry": 55,
            "pmp": 56,
            "pmp_data_if": 57,
            "cva6_tlb": 58,
            "lfsr": 59,
            "cva6_shared_tlb": 60,
            "cva6_ptw": 61,
            "cva6_mmu": 62,
            "load_unit": 63,
            "store_buffer": 64,
            "amo_buffer": 65,
            "store_unit": 66,
            "lsu_bypass": 67,
            "load_store_unit": 68,
            "axi_shim": 69,
            "axi_adapter": 70,
            "tc_sram": 24,
            "tc_sram_wrapper": 25,
            "tc_sram_wrapper_cache_techno": 25,
            "sram": 26,
            "sram_cache": 26,
            "tag_cmp": 72,
            "wt_dcache_ctrl": 73,
            "raw_checker": 73,
            "wt_dcache_mem": 74,
            "wt_dcache_wbuffer": 75,
            "wt_dcache_missunit": 76,
            "wt_dcache": 77,
            "hpdcache_demux": 77,
            "hpdcache_lfsr": 77,
            "hpdcache_sync_buffer": 77,
            "hpdcache_fifo_reg": 77,
            "hpdcache_fifo_reg_initialized": 77,
            "hpdcache_prio_1hot_encoder": 76,
            "hpdcache_prio_bin_encoder": 76,
            "hpdcache_1hot_to_binary": 76,
            "hpdcache_sram_1rw": 76,
            "hpdcache_sram_wbyteenable_1rw": 76,
            "hpdcache_sram_wmask_1rw": 76,
            "hpdcache_fxarb": 77,
            "hpdcache_rrarb": 77,
            "hpdcache_mux": 77,
            "hpdcache_decoder": 77,
            "hpdcache_sram": 77,
            "hpdcache_sram_wbyteenable": 77,
            "hpdcache_sram_wmask": 77,
            "hpdcache_regbank_wbyteenable_1rw": 77,
            "hpdcache_regbank_wmask_1rw": 77,
            "hpdcache_data_downsize": 77,
            "hpdcache_data_upsize": 77,
            "hpdcache_data_resize": 77,
            "hwpf_stride": 78,
            "hwpf_stride_arb": 78,
            "hwpf_stride_wrapper": 78,
            "hpdcache_mem_req_read_arbiter": 79,
            "hpdcache_mem_req_write_arbiter": 79,
            "hpdcache_mem_req_demux": 79,
            "hpdcache_mem_resp_demux": 79,
            "hpdcache_mem_to_axi_read": 79,
            "hpdcache_mem_to_axi_write": 79,
            "hpdcache_l15_req_arbiter": 79,
            "hpdcache_l15_resp_demux": 79,
            "hpdcache_to_l15": 79,
            "hpdcache_rsp_demux": 79,
            "hpdcache_to_axi_read": 79,
            "hpdcache_to_axi_write": 79,
            "hpdcache_to_axi": 79,
            "hpdcache_cmo": 80,
            "hpdcache_amo": 80,
            "hpdcache_cbuf": 80,
            "hpdcache_core_arbiter": 80,
            "hpdcache_ctrl_pe": 80,
            "hpdcache_mshr": 80,
            "hpdcache_nline": 80,
            "hpdcache_rtab": 80,
            "hpdcache_victim_plru": 79,
            "hpdcache_victim_random": 79,
            "hpdcache_victim_sel": 79,
            "hpdcache_wbuf": 80,
            "hpdcache_memctrl": 80,
            "hpdcache_miss_handler": 81,
            "hpdcache_uncached": 81,
            "hpdcache_flush": 81,
            "hpdcache_ctrl": 81,
            "hpdcache": 82,
            "cva6_hpdcache_if_adapter": 83,
            "cva6_hpdcache_subsystem_axi_arbiter": 83,
            "cva6_hpdcache_subsystem_l15_adapter": 83,
            "cva6_hpdcache_wrapper": 84,
            "cva6_hpdcache_subsystem": 85,
            "miss_handler": 78,
            "wt_axi_adapter": 79,
            "std_nbdcache": 80,
            "cva6_icache": 81,
            "wt_cache_subsystem": 82,
            "fpu_wrap": 83,
            "trigger_module": 90,
            "csr_regfile": 91,
            "cva6_accel_first_pass_decoder_stub": 92,
            "tc_clk": 93,
            "cluster_clk_cells": 94,
            "pulp_clk_cells": 95,
            "sync": 96,
            "sync_wedge": 97,
            "edge_detect": 98,
            "fifo_v3": 98,
            "fifo_v2": 99,
            "fifo_v1": 100,
            "fifo": 100,
            "stream_delay": 99,
            "axi_cut": 100,
            "axi_multicut": 101,
            "axi_atop_filter": 101,
            "axi_burst_splitter": 101,
            "axi_single_slice": 101,
            "axi_delayer": 102,
            "axi_to_axi_lite": 102,
            "axi_err_slv": 102,
            "axi_mux": 103,
            "axi_demux": 103,
            "axi_xbar": 104,
            "axi_ar_buffer": 102,
            "axi_aw_buffer": 102,
            "axi_b_buffer": 102,
            "axi_r_buffer": 102,
            "axi_w_buffer": 102,
            "axi_slice": 103,
            "axi_slice_wrap": 104,
            "axi_res_tbl": 105,
            "axi_riscv_amos_alu": 106,
            "axi_riscv_amos": 107,
            "axi_riscv_lrsc": 108,
            "axi_riscv_atomics": 109,
            "axi_riscv_amos_wrap": 110,
            "axi_riscv_lrsc_wrap": 111,
            "axi_riscv_atomics_wrap": 112,
            "axi2apb": 115,
            "axi2apb_64_32": 116,
            "axi2apb_wrap": 117,
            "timer": 118,
            "apb_timer": 119,
            "debug_rom": 120,
            "dm_csrs": 121,
            "dm_mem": 122,
            "dm_sba": 123,
            "dm_top": 124,
            "cdc_2phase": 125,
            "dmi_jtag_tap": 126,
            "dmi_cdc": 127,
            "dmi_jtag": 128,
            "clint": 130,
            "cva6": 1000,
            "ariane": 1001,
        }
        return order.get(src.stem, 100)

    runner_only_sources = set() if NATIVE_HARNESS else {
        "corev_apu/tb/ariane_axi_pkg.sv",
        "corev_apu/src/ariane.sv",
    }
    include_files = [
        src for src in all_conversion_files
        if rel_source(src).as_posix() not in runner_only_sources
    ]
    ordered_for_include = sorted(enumerate(include_files), key=lambda item: (include_priority(item[1]), item[0]))
    ordered_for_include = [src for _, src in ordered_for_include]
    ordered_for_include = topo_refine_include_order(ordered_for_include)

    for src in ordered_for_include:
        rel = rel_source(src)
        header = Path("generated") / rel.parent / f"{src.stem}.h"
        includes.append(f'#include "{header.as_posix()}"')
    # ordered_for_include has already been refined from these same generated headers.
    rvfi_include = '#include "generated/rvfi_types.h"'
    insert_at = 0
    for idx, line in enumerate(includes):
        if any(pkg in line for pkg in ("config_pkg.h", "riscv_pkg.h", "ariane_pkg.h", "build_config_pkg.h")):
            insert_at = idx + 1
    includes.insert(insert_at, rvfi_include)
    if xlnx_stubs is not None:
        stub_include = '#include "generated/corev_apu/tb/xlnx_axi_stubs.h"'
        insert_at = 0
        for idx, line in enumerate(includes):
            if "generated/corev_apu/tb/ariane_peripherals.h" in line:
                insert_at = idx
                break
        includes.insert(insert_at, stub_include)
    if dpi_adapter is not None:
        dpi_include = '#include "generated/corev_apu/tb/dpi_adapters.h"'
        insert_at = len(includes)
        for idx, line in enumerate(includes):
            if ("generated/corev_apu/tb/common/SimDTM.h" in line or
                    "generated/corev_apu/tb/common/SimJTAG.h" in line):
                insert_at = idx
                break
        includes.insert(insert_at, dpi_include)
    write_if_changed(aggregate_header, "#pragma once\n\n" + "\n".join(includes) + "\n")
    write_if_changed(aggregate, '#include "all_generated.h"\n\nint main() { return 0; }\n')
    gen_sources = []
    for src in ordered_for_include:
        rel = rel_source(src)
        cc_path = Path("generated") / rel.parent / f"{src.stem}.cc"
        if (OUT / cc_path).exists():
            gen_sources.append(cc_path)
    write_project_makefile(OUT, gen_sources)

    summary = OUT / "conversion-summary.txt"
    write_if_changed(summary,
        f"target={TARGET}\n"
        f"sources={len(files)}\n"
        f"conversion_sources={len(conversion_files)}\n"
        f"skipped={skipped_files}\n"
        f"generated={len(list(gen_root.rglob('*.h')))}\n"
        f"failures={len(failures)}\n"
        + "\n".join(f"{src}: {reason}" for src, reason in failures)
        + ("\n" if failures else "")
    )
    print(summary.read_text())
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
