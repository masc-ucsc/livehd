load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain")

#load("@bazel_skylib//lib:versions.bzl", "versions")

def get_libs_for_static_executable(dep):
    """
    Finds the libraries used for linking an executable statically.
    This replaces the old API dep.cc.libs
    Args:
      dep: Target
    Returns:
      A list of File instances, these are the libraries used for linking.
    """
    libraries_to_link = dep[CcInfo].linking_context.libraries_to_link
    libs = []
    if "to_list" in dir(libraries_to_link):
      list_to_link = libraries_to_link.to_list()
    else:
      list_to_link = libraries_to_link

    for library_to_link in list_to_link:
        if library_to_link.static_library != None:
            libs.append(library_to_link.static_library)
        elif library_to_link.pic_static_library != None:
            libs.append(library_to_link.pic_static_library)
        elif library_to_link.interface_library != None:
            libs.append(library_to_link.interface_library)
        elif library_to_link.dynamic_library != None:
            libs.append(library_to_link.dynamic_library)
    #print("libs:",libs)
    return depset(libs)

def _impl(ctx):
  output = ctx.outputs.out
  src_libs    = [f for f in ctx.files.srcs if f.path.endswith('a')]

  #print("src_libs:",src_libs)

  resolved_srcs = depset()

  for dep in ctx.attr.srcs:
    resolved_srcs = depset(transitive = [resolved_srcs, dep.files])

  for dep in ctx.attr.deps:
    #print("src_libs:",dep.files)
    resolved_srcs = depset(transitive = [resolved_srcs, dep.files])
    resolved_srcs = depset(transitive = [resolved_srcs, get_libs_for_static_executable(dep)])

  src_libs2    = [f for f in resolved_srcs.to_list() if f.path.endswith('a')]
  #print("src_libs2:",src_libs2)
  #print("src_objs:",[f.dirname for f in src_libs])

  #versions.check(minimum_bazel_version = "0.5.4")

  if "ar_executable" not in dir(ctx.fragments.cpp):
    cc_toolchain = find_cpp_toolchain(ctx)
    ar_executable = cc_toolchain.ar_executable()
    compiler_executable = cc_toolchain.compiler_executable()
  else:
    print("linkso.bzl switching to fragments interface for older bazel versions")
    ar_executable = ctx.fragments.cpp.ar_executable
    compiler_executable = ctx.fragments.cpp.compiler_executable

  #print(compiler_executable)
  #print(ar_executable)

  args = [output.path] + [compiler_executable] + [ar_executable] + [f.path for f in src_libs2] + ["-Wl,-Bstatic"] + ["-lstdc++"] + ["-Wl,-Bdynamic"] + ["-lrt", "-lgcov", "-lpthread"]

  ctx.actions.run(
      inputs=src_libs2,
      outputs=[output],
      arguments = args,
      #tools=[ld_file],
      executable = ctx.executable._linkso_tool)

linkso = rule(
    implementation=_impl,
    attrs={"srcs": attr.label_list(mandatory=True, allow_files=True),
           "deps": attr.label_list(
               providers = [],  # CcSkylarkApiProvider
               mandatory = True,
               allow_empty = False,
               ),
           "_cc_toolchain": attr.label(
             default = Label("@bazel_tools//tools/cpp:current_cc_toolchain")
           ),
           "_linkso_tool": attr.label(
             executable = True,
             cfg = "host",
             allow_files = True,
             default = Label("//tools:linkso_tool"),
            ),
           },
    fragments = ["cpp"], # only for bazel versions under 0.19.1
    outputs={"out": "%{name}.so"},
)

