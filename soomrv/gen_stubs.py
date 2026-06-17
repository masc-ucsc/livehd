#!/usr/bin/env python3
"""gen_stubs.py — generate (* blackbox *) port-only stubs for every soomrv module,
and a submodule-dependency map, so a module that instantiates others can be
compiled per-file with its children blackboxed (LEC blackbox-collapses them).

Outputs (under soomrv/stubs/):
  <Module>.stub.sv   — `(* blackbox *) module <Module> #(..) (ports..);  endmodule`
  deps.txt           — one line per module: `<Module>: child1 child2 ...`
"""
import os, re, sys, glob

SRC = "/mada/users/renau/projs/soomrv/repo/src"
OUT = "/mada/users/renau/projs/livehd/soomrv/stubs"
os.makedirs(OUT, exist_ok=True)

# discover all module names + their source files
files = glob.glob(SRC + "/*.sv") + glob.glob(SRC + "/lib/*.sv")
mod_file = {}          # module name -> file
mod_header = {}        # module name -> stub text
for f in files:
    txt = open(f).read()
    # strip // and /* */ comments for parsing (keep a copy)
    nc = re.sub(r"//[^\n]*", "", txt)
    nc = re.sub(r"/\*.*?\*/", "", nc, flags=re.S)
    for m in re.finditer(r"\bmodule\s+(\w+)", nc):
        name = m.group(1)
        start = m.start()
        # capture from 'module' to the ';' that closes the header (paren depth 0)
        i = m.end(); depth = 0; hdr_end = None
        while i < len(nc):
            c = nc[i]
            if c == '(': depth += 1
            elif c == ')': depth -= 1
            elif c == ';' and depth == 0:
                hdr_end = i; break
            i += 1
        if hdr_end is None:
            continue
        header = nc[start:hdr_end].strip()
        mod_file[name] = f
        mod_header[name] = header

# write stubs
for name, header in mod_header.items():
    with open(os.path.join(OUT, name + ".stub.sv"), "w") as o:
        o.write("(* blackbox *)\n" + header + ";\nendmodule\n")

# submodule deps: for each module, scan its file body for instantiations of
# OTHER known modules.  An instantiation looks like `<Mod> [#(...)] <inst> (`.
modnames = set(mod_header)
deps = {}
for name, f in mod_file.items():
    txt = open(f).read()
    nc = re.sub(r"//[^\n]*", "", txt)
    nc = re.sub(r"/\*.*?\*/", "", nc, flags=re.S)
    found = set()
    for m in re.finditer(r"\b([A-Z]\w+)\s*(?:#\s*\([^;]*?\))?\s*\w+\s*\(", nc):
        cand = m.group(1)
        if cand in modnames and cand != name:
            found.add(cand)
    deps[name] = sorted(found)

with open(os.path.join(OUT, "deps.txt"), "w") as o:
    for name in sorted(deps):
        o.write(f"{name}: {' '.join(deps[name])}\n")

print(f"stubs: {len(mod_header)} modules")
print("modules with submodule deps:", sum(1 for d in deps.values() if d))
