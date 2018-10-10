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

def _impl_verilator(ctx):
  tree = ctx.actions.declare_directory("obj_dir") # ctx.attr.name + ".cc")
  src_files = cxt.files.srcs
  ctx.actions.run(
    inputs = src_files,
    outputs = [ tree ],
    arguments = [ tree.path ],
    progress_message = "Generating verilator files into '%s'" % tree.path,
    executable = "/usr/bin/verilator", #ctx.executable.verilator,
  )

  return [ DefaultInfo(files = depset([ tree ])) ]

verilator = rule(
  implementation = _impl_verilator,
  attrs = {
    "verilator": attr.label(
      mandatory = False,
      executable = True,
      doc = "Verilator binary path",
      cfg = "host",
      allow_files = True,
    )
    "srcs": attr.label_list(
      mandatory = True,
      executable = True,
      doc = "Source Verilog files passed to verilator",
      allow_files = True,
      allow_empty = False,
      cfg = "host",
    )
    "defines": attr.string_list(
      mandatory = False,
      doc = "Verilator compile defines",
      allow_empty = True,
    )
  }
)
