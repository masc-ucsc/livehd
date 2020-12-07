def _impl_gencss(ctx):
  tree = ctx.actions.declare_directory(ctx.attr.name + ".cc")
  ctx.actions.run(
    inputs = [],
    outputs = [ tree ],
    arguments = [ tree.path ],
    progress_message = "Generating cc files into '%s'" % tree.path,
    executable = ctx.executable.generator,
  )

  return [ DefaultInfo(files = depset([ tree ])) ]

genccs = rule(
  implementation = _impl_gencss,
  attrs = {
    "generator": attr.label(
      executable = True,
      mandatory = True,
      cfg = "host",
      allow_files = True,
    )
  }
)

def _impl_verilator_run(ctx):
  tree = ctx.actions.declare_directory(ctx.attr.name + "obj_dir")
  v_src_files = [f.path for f in ctx.files.srcs if  f.path.endswith('v')]
  c_src_files = " ".join([f.path for f in ctx.files.srcs if not f.path.endswith('v')])
  args = v_src_files + ctx.attr.args + ["--Mdir", tree.path] + ["--cc"] + ["--exe"]
  args_cmd = " ".join([f for f in args])
  #print("args:",args)
  obj_dir = tree.path
  if c_src_files == "":
    fail("verilator_run srcs needs at least one c file to drive the simulations")

  cmd = """
echo {args_cmd} >{obj_dir}/foo.txt
/usr/bin/verilator {args_cmd}
cp {c_src_files} {obj_dir}
rm -f {obj_dir}/*.mk {obj_dir}/*.d {obj_dir}/*.dat
  """.format(
      obj_dir = obj_dir,
      args_cmd = args_cmd,
      c_src_files = c_src_files
      )
  ctx.actions.run_shell(
    inputs = ctx.files.srcs,
    outputs = [ tree ],
    arguments = [], #args,
    progress_message = "verilator %s files into '%s'" % (args_cmd, tree.path),
    command = cmd, # executable = "verilator", # ctx.executable.verilator,
  )

  return [ DefaultInfo(files = depset([ tree ])) ]

verilator_run = rule(
  implementation = _impl_verilator_run,
  attrs = {
    "verilator": attr.label(
      mandatory = False,
      executable = True,
      doc = "Verilator binary path",
      cfg = "host",
      allow_files = True,
    ),
    "srcs": attr.label_list(
      mandatory = True,
      doc = "Source Verilog files passed to verilator",
      allow_files = True,
      allow_empty = False,
      cfg = "host",
    ),
    "args": attr.string_list(
      mandatory = False,
      doc = "Verilator compile extra args",
      allow_empty = True,
    )
  },
)
