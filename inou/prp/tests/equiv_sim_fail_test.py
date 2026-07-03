#!/usr/bin/env python3

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(os.getcwd())


def find_lhd():
    for path in (ROOT / "bazel-bin/lhd/lhd", ROOT / "lhd/lhd"):
        if path.exists():
            return path
    print("failed to find lhd binary")
    sys.exit(3)


def sim_include_dirs(tmp_dir):
    roots = []
    for env in ("TEST_SRCDIR", "RUNFILES_DIR"):
        val = os.environ.get(env)
        if val and os.path.isdir(val):
            roots.append(val)
    roots.append(str(tmp_dir))
    wanted = ("slop.hpp", "iassert.hpp")
    found = {}
    for root in roots:
        for dirpath, _dirs, files in os.walk(root):
            for name in wanted:
                if name in files and name not in found:
                    found[name] = dirpath
        if len(found) == len(wanted):
            break
    return [found[name] for name in wanted] if len(found) == len(wanted) else []


LHD = find_lhd()


def run(cmd, timeout=60):
    proc = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True, timeout=timeout)
    return proc.returncode, proc.stdout + proc.stderr


def header_value(prp, key):
    pat = re.compile(rf"^:{re.escape(key)}:\s*(.+)")
    for line in Path(prp).read_text().splitlines():
        m = pat.match(line)
        if m:
            return m.group(1).strip()
    return ""


def fields(simdir, top, struct):
    h = (simdir / f"{top}.hpp").read_text()
    m = re.search(rf"  struct {struct} \{{(.*?)  \}};", h, re.S)
    if not m:
        return []
    return [(name, int(width)) for width, name in re.findall(r"Slop<(\d+)>\s+(\w+)\{\}", m.group(1))]


def cpp_class_name(simdir, top):
    h = (simdir / f"{top}.hpp").read_text()
    m = re.search(r"(?:class|struct)\s+([A-Za-z_]\w*)\s*\{", h)
    if not m:
        raise RuntimeError(f"could not find generated C++ class in {top}.hpp")
    return m.group(1)


def salt(name):
    return int.from_bytes(hashlib.sha256(name.encode()).digest()[:8], "little")


def gen_driver(simdir, top):
    cls = cpp_class_name(simdir, top)
    ins = fields(simdir, top, "In")
    outs = fields(simdir, top, "Out")
    lines = [
        f'#include "{top}.hpp"',
        "#include <filesystem>",
        "#include <fstream>",
        "#include <map>",
        "#include <string>",
        "",
        "static unsigned long long splitmix64(unsigned long long x) {",
        "  x += 0x9e3779b97f4a7c15ULL;",
        "  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;",
        "  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;",
        "  return x ^ (x >> 31);",
        "}",
        "template<int W> Slop<W> stim(long cycle, unsigned long long salt) {",
        "  std::string bits; bits.resize(W);",
        "  for (int pos = W - 1; pos >= 0; --pos) {",
        "    auto r = splitmix64(salt ^ (unsigned long long)cycle * 0xd1342543de82ef95ULL ^ (unsigned long long)(pos / 64) * 0x9ddfea08eb382d69ULL);",
        "    bits[W - 1 - pos] = ((r >> (pos & 63)) & 1) ? '1' : '0';",
        "  }",
        "  return Slop<W>::from_binary(bits, true);",
        "}",
        "int main(int argc, char **argv) {",
        "  const long cycles = argc > 1 ? std::stol(argv[1]) : 8;",
        "  const std::string outdir = argc > 2 ? argv[2] : \"run\";",
        "  std::filesystem::create_directories(outdir);",
        f"  {cls} dut;",
        "  dut.reset_cycle();",
        "  std::ofstream out(outdir + \"/outputs.txt\");",
        "  for (long cycle = 0; cycle < cycles; ++cycle) {",
        f"    {cls}::In in;",
    ]
    for name, width in ins:
        if name == "reset":
            lines.append(f"    in.{name} = Slop<{width}>::create_integer(cycle < 2 ? 1 : 0);")
        elif name == "clock":
            lines.append(f"    in.{name} = Slop<{width}>::create_integer(cycle & 1);")
        elif name == "valid":
            lines.append(f"    in.{name} = Slop<{width}>::create_integer(0);")
        else:
            lines.append(f"    in.{name} = stim<{width}>(cycle, {salt(name)}ULL);")
    lines += [
        "    auto o = dut.cycle(in);",
        "    if (cycle == cycles - 1) { out << cycle;",
    ]
    for name, _ in outs:
        lines.append(f"      out << \" {name}=\" << o.{name}.to_pyrope();")
    lines += [
        "      out << \"\\n\"; }",
        "  }",
        "  std::map<std::string, std::string> regs;",
        "  dut.dump_state(\"\", regs, outdir);",
        "  std::ofstream st(outdir + \"/state.txt\");",
        "  for (const auto &[k,v] : regs) st << k << \"=\" << v << \"\\n\";",
        "  return 0;",
        "}",
    ]
    (simdir / "drv_compare.cpp").write_text("\n".join(lines) + "\n")


def compile_driver(simdir):
    top = next(p.stem for p in simdir.glob("*.hpp"))
    cpp = [str(p) for p in simdir.glob("*.cpp") if p.name != "drv_compare.cpp"]
    incs = ["-I" + str(simdir)] + ["-I" + d for d in sim_include_dirs(simdir)]
    cmd = [
        "clang++",
        "-std=c++23",
        "-O2",
        "-DNDEBUG",
        *incs,
        *cpp,
        str(simdir / "drv_compare.cpp"),
        "-o",
        str(simdir / "drv_compare"),
    ]
    return top, run(cmd)


def hash_file(path):
    p = Path(path)
    return hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None


def compare_dirs(a, b):
    diffs = []
    for name in ("outputs.txt", "state.txt"):
        if hash_file(Path(a) / name) != hash_file(Path(b) / name):
            diffs.append(name)
    return diffs


def compile_sim(src, top, out, reader=None):
    srcs = [str(s) for s in src] if isinstance(src, (list, tuple)) else [str(src)]
    cmd = [str(LHD), "compile", *srcs]
    if reader:
        cmd += ["--reader", reader]
    cmd += ["--top", top, "--emit-dir", f"sim:{out}", "--workdir", str(out.parent / ("w_" + out.name))]
    return run(cmd)


def drive(simdir, top, rundir):
    """gen_driver + compile + run one side; returns (rc, output)."""
    gen_driver(simdir, top)
    _, (rc, out) = compile_driver(simdir)
    if rc:
        return rc, out
    return run([str(simdir / "drv_compare"), "8", str(rundir)])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=["lg-emit", "prp-emit", "exact-diff", "v2prp-sim"], required=True)
    ap.add_argument("--prp", required=True)
    ap.add_argument("--reader", default="yosys-verilog")
    args = ap.parse_args()

    prp = Path(args.prp)
    verilog = prp.with_suffix(".v")
    pyrope_top = header_value(prp, "pyrope_top")
    verilog_top = header_value(prp, "verilog_top")
    if not pyrope_top or not verilog_top:
        print("missing :pyrope_top: or :verilog_top:")
        return 2

    work = Path(tempfile.mkdtemp(prefix="lhd_equiv_sim_"))
    try:
        if args.mode == "lg-emit":
            rc, out = compile_sim(verilog, verilog_top, work / "lg_sim", "slang")
            print(out)
            return rc
        if args.mode == "prp-emit":
            rc, out = compile_sim(prp, pyrope_top, work / "prp_sim")
            print(out)
            return rc
        if args.mode == "v2prp-sim":
            # The XiangShan fp-convert-family SIM fail class (instance-output
            # struct dual identity, the ExeUnitImp_4 category): the .v lowers
            # CORRECTLY in memory (side A: slang -> sim), but the EMITTED
            # Pyrope TEXT carries a flat instance-output store + 0-init
            # per-field leaves, so the RECOMPILED Pyrope (side B) computes
            # from never-written leaves and its sim diverges. Both sides get
            # the same deterministic stimulus; they must match once the
            # emitter fix (lower_instance per-field output split) lands.
            lg_sim = work / "lg_sim"
            rc, out = compile_sim(verilog, verilog_top, lg_sim, "slang")
            if rc:
                print(out)
                return rc
            prp_dir = work / "prp_emit"
            rc, out = run([str(LHD), "compile", "--reader", "slang", str(verilog),
                           "--emit-dir", f"pyrope:{prp_dir}/",
                           "--workdir", str(work / "w_emit")])
            if rc:
                print(out)
                return rc
            emitted = sorted(prp_dir.glob("*.prp"))
            if not emitted:
                print("no .prp emitted from", verilog)
                return 1
            v2_sim = work / "v2_sim"
            rc, out = compile_sim(emitted, verilog_top, v2_sim)
            if rc:
                print(out)
                return rc
            for simdir, rundir in ((lg_sim, work / "lg_run"), (v2_sim, work / "v2_run")):
                rc, out = drive(simdir, verilog_top, rundir)
                if rc:
                    print(out)
                    return rc
            diffs = compare_dirs(work / "lg_run", work / "v2_run")
            if diffs:
                print("SIM v2prp diff:", ",".join(diffs))
                return 1
            print("SIM v2prp match")
            return 0

        lg_sim = work / "lg_sim"
        prp_sim = work / "prp_sim"
        rc, out = compile_sim(verilog, verilog_top, lg_sim, args.reader)
        if rc:
            print(out)
            return rc
        rc, out = compile_sim(prp, pyrope_top, prp_sim)
        if rc:
            print(out)
            return rc
        gen_driver(lg_sim, next(p.stem for p in lg_sim.glob("*.hpp")))
        gen_driver(prp_sim, next(p.stem for p in prp_sim.glob("*.hpp")))
        _, (rc, out) = compile_driver(lg_sim)
        if rc:
            print(out)
            return rc
        _, (rc, out) = compile_driver(prp_sim)
        if rc:
            print(out)
            return rc
        rc, out = run([str(lg_sim / "drv_compare"), "8", str(work / "lg_run")])
        if rc:
            print(out)
            return rc
        rc, out = run([str(prp_sim / "drv_compare"), "8", str(work / "prp_run")])
        if rc:
            print(out)
            return rc
        diffs = compare_dirs(work / "lg_run", work / "prp_run")
        if diffs:
            print("SIM exact diff:", ",".join(diffs))
            return 1
        print("SIM exact match")
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
