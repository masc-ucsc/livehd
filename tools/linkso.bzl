
def _impl(ctx):
  output = ctx.outputs.out
  src_libs    = [f for f in ctx.files.srcs if f.path.endswith('a')]
  static_libs = [f for f in ctx.files.static if f.path.endswith('.a')]

  #print("src_libs:",src_libs)
  #print("static_libs:",static_libs)
  #print("src_objs:",[f.dirname for f in src_libs])

  #print(ctx.fragments.cpp.compiler_executable)
  #print(ctx.fragments.cpp.ar_executable)

  args = [output.path] + [ctx.fragments.cpp.compiler_executable] + [ctx.fragments.cpp.ar_executable] + [output.dirname] + [f.path for f in src_libs] + ["-Wl,-Bstatic"] + [f.path for f in static_libs] + ["-Wl,-Bdynamic"] + ["-lrt", "-lpthread"]

  ctx.actions.run(
      inputs=static_libs + src_libs,
      outputs=[output],
      arguments = args,
      #tools=[ld_file],
      executable = ctx.executable._linkso_tool)

linkso = rule(
    implementation=_impl,
    attrs={"static": attr.label_list(mandatory=True, allow_files=True),
           "srcs":   attr.label_list(mandatory=True, allow_files=True),
           "_linkso_tool": attr.label(
               executable = True,
               cfg = "host",
               allow_files = True,
               default = Label("//tools:linkso_tool"),
               ),
           },
    fragments = ["cpp"],
    outputs={"out": "lib%{name}.so"},
)

