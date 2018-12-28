
def _impl(ctx):
  output = ctx.outputs.out
  src_libs    = [f for f in ctx.files.srcs if f.path.endswith('a')]

  #print("src_libs:",src_libs)

  resolved_srcs = depset()

  for dep in ctx.attr.srcs:
    resolved_srcs = depset(transitive = [resolved_srcs, dep.files])
    resolved_srcs = depset(transitive = [resolved_srcs, dep.cc.libs])
    #print(" list:", dep.files.to_list())

  for dep in ctx.attr.deps:
    resolved_srcs = depset(transitive = [resolved_srcs, dep.files])
    resolved_srcs = depset(transitive = [resolved_srcs, dep.cc.libs])
    #print(" list:", dep.files.to_list())

  src_libs2    = [f for f in resolved_srcs.to_list() if f.path.endswith('a')]
  #print("src_libs2:",src_libs2)
  #print("src_objs:",[f.dirname for f in src_libs])

  #print(ctx.fragments.cpp.compiler_executable)
  #print(ctx.fragments.cpp.ar_executable)

  args = [output.path] + [ctx.fragments.cpp.compiler_executable] + [ctx.fragments.cpp.ar_executable] + [f.path for f in src_libs2] + ["-Wl,-Bstatic"] + ["-lstdc++"] + ["-Wl,-Bdynamic"] + ["-lrt", "-lgcov", "-lpthread"]

  ctx.actions.run(
      inputs=src_libs2,
      outputs=[output],
      arguments = args,
      #tools=[ld_file],
      executable = ctx.executable._linkso_tool)

linkso = rule(
    implementation=_impl,
    attrs={"srcs":   attr.label_list(mandatory=True, allow_files=True),
           "deps": attr.label_list(),
           "_linkso_tool": attr.label(
               executable = True,
               cfg = "host",
               allow_files = True,
               default = Label("//tools:linkso_tool"),
               ),
           },
    fragments = ["cpp"],
    outputs={"out": "%{name}.so"},
)

