# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Host-platform selector for the prebuilt cvc5 static libraries (pass/lec's SMT
# backend). cvc5 ships a per-OS/-arch prebuilt; we consume the one matching the
# *host* (we build `lhd` for the host, not cross-compile), and expose it under
# the stable repo name `@cvc5` so the consumer (pass/lec) is platform-agnostic.
# Bazel fetches only the single artifact the host needs (others never download).
#
# To add a platform: drop another `(os, arch): struct(...)` row below. The
# prefix/url follow cvc5's release asset naming; keep the non-GPL `-static`
# artifact (never `-static-gpl`, whose bundled GMP is LGPL -- see cvc5.BUILD).

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

_CVC5_VERSION = "1.3.4"
_CVC5_BASE = "https://github.com/cvc5/cvc5/releases/download/cvc5-" + _CVC5_VERSION

def _artifact(prefix, sha256):
    return struct(prefix = prefix, sha256 = sha256)

# Keyed by (module_ctx.os.name, module_ctx.os.arch), both lowercased. Arch names
# vary by JVM ("amd64"/"x86_64", "aarch64"/"arm64"), so list every spelling.
_CVC5_ARTIFACTS = {
    ("linux", "amd64"): _artifact(
        "cvc5-Linux-x86_64-static",
        "dcdbfada0ce493ee98259c0816e0daafc561c223aadb3af298c2968e73ea39c6",
    ),
    ("linux", "x86_64"): _artifact(
        "cvc5-Linux-x86_64-static",
        "dcdbfada0ce493ee98259c0816e0daafc561c223aadb3af298c2968e73ea39c6",
    ),
    ("linux", "aarch64"): _artifact(
        "cvc5-Linux-arm64-static",
        "2a4c108367f20b0c8990abd6b9535a5d62e08908d471d4671c00734e408f85bc",
    ),
    ("linux", "arm64"): _artifact(
        "cvc5-Linux-arm64-static",
        "2a4c108367f20b0c8990abd6b9535a5d62e08908d471d4671c00734e408f85bc",
    ),
    ("mac os x", "aarch64"): _artifact(
        "cvc5-macOS-arm64-static",
        "3840aa53f6ee6fc357415dcfe291d7f5ffec6cfb1ccca6fef64120a0d2be4cb6",
    ),
    ("mac os x", "arm64"): _artifact(
        "cvc5-macOS-arm64-static",
        "3840aa53f6ee6fc357415dcfe291d7f5ffec6cfb1ccca6fef64120a0d2be4cb6",
    ),
}

def _cvc5_repo_impl(module_ctx):
    os_name = module_ctx.os.name.lower()
    arch = module_ctx.os.arch.lower()
    artifact = _CVC5_ARTIFACTS.get((os_name, arch))
    if artifact == None:
        fail(
            "pass/lec: no prebuilt cvc5 {} for host ({!r}, {!r}). ".format(_CVC5_VERSION, os_name, arch) +
            "Add the matching cvc5 release asset to _CVC5_ARTIFACTS in packages/cvc5_repo.bzl.",
        )
    http_archive(
        name = "cvc5",
        build_file = "//packages:cvc5.BUILD",
        sha256 = artifact.sha256,
        strip_prefix = artifact.prefix,
        urls = [_CVC5_BASE + "/" + artifact.prefix + ".zip"],
    )

cvc5_repo = module_extension(
    implementation = _cvc5_repo_impl,
    doc = "Registers @cvc5 with the prebuilt static libs matching the host OS/arch.",
)
