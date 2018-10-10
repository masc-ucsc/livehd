def _impl(ctx):
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
  implementation = _impl,
  attrs = {
    "generator": attr.label(
      executable = True,
      mandatory = True,
      cfg = "host",
      allow_files = True,
    )
  }
)

