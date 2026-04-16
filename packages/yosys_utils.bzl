"""Yosys utilities"""

def _yosys_gen_action(ctx, mnemonic, tool, env):
    args = ctx.actions.args()

    out_replaces = {}
    for file, label in zip(ctx.outputs.outs, ctx.attr.outs):
        _, _, relative = str(label).partition("//")
        out_replaces["$(execpath {})".format(label)] = file
        out_replaces["$(execpath {})".format(relative)] = file

    for arg in ctx.attr.args:
        if arg in out_replaces:
            args.add(out_replaces[arg])
            continue

        expanded = ctx.expand_location(arg, ctx.attr.srcs)
        args.add(expanded)

    ctx.actions.run(
        executable = tool,
        arguments = [args],
        inputs = ctx.files.srcs,
        outputs = ctx.outputs.outs,
        mnemonic = mnemonic,
        env = env,
    )

    return [DefaultInfo(
        files = depset(ctx.outputs.outs),
    )]

def _find_m4(runfiles, label):
    m4 = None
    for file in runfiles:
        if file.basename in ["m4", "m4.exe"]:
            m4 = file
            break

    if not m4:
        fail("Failed to find m4 binary in runfiles of {}".format(label))

    return m4

def _yosys_bison_impl(ctx):
    m4 = _find_m4(ctx.attr.bison[DefaultInfo].default_runfiles.files.to_list(), ctx.attr.bison.label)

    env = {
        "BISON_PKGDATADIR": "{}.runfiles/{}/data".format(
            ctx.executable.bison.path,
            ctx.executable.bison.owner.workspace_name,
        ),
        "M4": "{}.runfiles/{}".format(
            ctx.executable.bison.path,
            m4.short_path[len("../"):],
        ),
    }

    return _yosys_gen_action(
        ctx = ctx,
        mnemonic = "YosysBisonGen",
        tool = ctx.executable.bison,
        env = env,
    )

yosys_bison = rule(
    doc = "An internal rule for running bison in the yosys project.",
    implementation = _yosys_bison_impl,
    attrs = {
        "args": attr.string_list(
            doc = "Arguments to pass to bison.",
            mandatory = True,
        ),
        "bison": attr.label(
            doc = "The bison binary.",
            cfg = "exec",
            executable = True,
            mandatory = True,
        ),
        "outs": attr.output_list(
            doc = "Outputs from bison.",
            mandatory = True,
        ),
        "srcs": attr.label_list(
            doc = "Sources to provide to bison.",
            mandatory = True,
            allow_files = True,
        ),
    },
)

def _yosys_flex_impl(ctx):
    m4 = _find_m4(ctx.attr.flex[DefaultInfo].default_runfiles.files.to_list(), ctx.attr.flex.label)

    env = {
        "M4": "{}.runfiles/{}".format(
            ctx.executable.flex.path,
            m4.short_path[len("../"):],
        ),
    }

    if len(ctx.outputs.outs) != 1:
        fail("yosys_flex expects exactly one output")

    args = ctx.actions.args()
    out = ctx.outputs.outs[0]
    out_tmp = "{}.tmp".format(out.path)

    out_replaces = {}
    for file, label in zip(ctx.outputs.outs, ctx.attr.outs):
        _, _, relative = str(label).partition("//")
        out_replaces["$(execpath {})".format(label)] = "{}.tmp".format(file.path)
        out_replaces["$(execpath {})".format(relative)] = "{}.tmp".format(file.path)

    for arg in ctx.attr.args:
        if arg in out_replaces:
            args.add(out_replaces[arg])
            continue

        expanded = ctx.expand_location(arg, ctx.attr.srcs)
        if expanded == out.path:
            args.add(out_tmp)
            continue

        args.add(expanded)

    ctx.actions.run_shell(
        command = """
set -e
"$1" "${@:4}"
if [ "$(uname)" = "Darwin" ]; then
  sed -e 's/int yyFlexLexer::LexerInput( char\\* buf, int/size_t yyFlexLexer::LexerInput( char* buf, size_t/' \
      -e 's/void yyFlexLexer::LexerOutput( const char\\* buf, int size/void yyFlexLexer::LexerOutput( const char* buf, size_t size/' \
      "$2" > "$3"
else
  cp "$2" "$3"
fi
rm "$2"
""",
        arguments = [ctx.executable.flex.path, out_tmp, out.path, args],
        inputs = ctx.files.srcs,
        outputs = [out],
        tools = [ctx.executable.flex] + ctx.attr.flex[DefaultInfo].default_runfiles.files.to_list(),
        mnemonic = "YosysFlexGen",
        env = env,
    )

    return [DefaultInfo(
        files = depset(ctx.outputs.outs),
    )]

yosys_flex = rule(
    doc = "An internal rule for running flex in the yosys project.",
    implementation = _yosys_flex_impl,
    attrs = {
        "args": attr.string_list(
            doc = "Arguments to pass to flex.",
            mandatory = True,
        ),
        "flex": attr.label(
            doc = "The flex binary.",
            cfg = "exec",
            executable = True,
            mandatory = True,
        ),
        "outs": attr.output_list(
            doc = "Outputs from bison.",
            mandatory = True,
        ),
        "srcs": attr.label_list(
            doc = "Sources to provide to bison.",
            mandatory = True,
            allow_files = True,
        ),
    },
)
