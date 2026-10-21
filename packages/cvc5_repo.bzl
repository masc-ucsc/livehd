# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Source-built cvc5 1.4.2 and an optional host-matched prebuilt escape hatch.
# The stable @cvc5 alias selects source unless --define=cvc5_prebuilt=1.
# Dependency archives are fetched by Bazel; CMake builds fully offline.

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

_CVC5_VERSION = "1.4.2"
_CVC5_BASE = "https://github.com/cvc5/cvc5/releases/download/cvc5-" + _CVC5_VERSION

def _artifact(prefix, sha256):
    return struct(prefix = prefix, sha256 = sha256)

# Keyed by (module_ctx.os.name, module_ctx.os.arch), both lowercased. Arch names
# vary by JVM ("amd64"/"x86_64", "aarch64"/"arm64"), so list every spelling.
_CVC5_ARTIFACTS = {
    ("linux", "amd64"): _artifact(
        "cvc5-Linux-x86_64-static",
        "7eb18f8c814c36a46c5a7d003983e9cfbc4b0a9828d6dd596239291bd4cc61d8",
    ),
    ("linux", "x86_64"): _artifact(
        "cvc5-Linux-x86_64-static",
        "7eb18f8c814c36a46c5a7d003983e9cfbc4b0a9828d6dd596239291bd4cc61d8",
    ),
    ("linux", "aarch64"): _artifact(
        "cvc5-Linux-arm64-static",
        "765ebee59d9e5efccb60a445fa66fbddfb07a8de9fe7a5b6d128e5c2aafec974",
    ),
    ("linux", "arm64"): _artifact(
        "cvc5-Linux-arm64-static",
        "765ebee59d9e5efccb60a445fa66fbddfb07a8de9fe7a5b6d128e5c2aafec974",
    ),
    ("mac os x", "aarch64"): _artifact(
        "cvc5-macOS-arm64-static",
        "24397f7d022e755876ca836193d4c5f9c1d4326f0f6c01f7d7fc1ddd6bc5096c",
    ),
    ("mac os x", "arm64"): _artifact(
        "cvc5-macOS-arm64-static",
        "24397f7d022e755876ca836193d4c5f9c1d4326f0f6c01f7d7fc1ddd6bc5096c",
    ),
}

# Source and dependency archives are fetched by Bazel, never by the build action.
# Keeping the upstream ExternalProjects with local archives retains cvc5's
# tested dependency configuration while making the CMake action network-free.
_SOURCE_DEPS = {
    "CaDiCaL": ("https://github.com/arminbiere/cadical/archive/rel-2.1.3-elevate.tar.gz", "15e1e82f7f9a9da0e97070cb8ac41d5b32139f65d54f72d2ff84849b0466ef92"),
    "SymFPU": ("https://github.com/martin-cs/symfpu/archive/40bdec00e99f8ea1b96c3dac0a05eed11c541639.tar.gz", "ba17877fbf0c851e113fddaab225152f1c0b2044429396b56b2f113832e36ce5"),
    "Poly": ("https://github.com/SRI-CSL/libpoly/archive/refs/tags/v0.2.1.tar.gz", "f9920afc876f998633348b9cbfcf180757ada48cc872040256c60ad0707b5a0f"),
    "Kissat": ("https://github.com/arminbiere/kissat/archive/sc2021.tar.gz", "ad1945cc6980cc6d8b7049cf0a298f9f806ac3c9ca1ccb51f1bc533253d285cc"),
    "CryptoMiniSat": ("https://github.com/msoos/cryptominisat/archive/refs/tags/5.11.21.tar.gz", "288fd53d801909af797c72023361a75af3229d1806dbc87a0fcda18f5e03763b"),
}

def _source_impl(ctx):
    ctx.download_and_extract(
        url = "https://codeload.github.com/cvc5/cvc5/tar.gz/refs/tags/cvc5-" + _CVC5_VERSION,
        sha256 = "795099b277bfa79a3c9a11c8d3fec780452031dc89ddcc170003963aa3521343",
        stripPrefix = "cvc5-cvc5-" + _CVC5_VERSION,
        type = "tar.gz",
    )
    for name, (url, sha) in _SOURCE_DEPS.items():
        ctx.download(url, "deps/livehd/" + name + ".tar.gz", sha256 = sha)
        path = "cmake/Find" + name + ".cmake"
        text = ctx.read(path)
        # Replace the complete upstream URL line; variable versions differ by module.
        lines = []
        for line in text.splitlines():
            if line.strip().startswith("URL https:"):
                line = "    URL $${PROJECT_SOURCE_DIR}/deps/livehd/" + name + ".tar.gz"
                line = line.replace("$$", "$")
            lines.append(line)
        text = "\n".join(lines) + "\n"
        text = text.replace("if(" + name + "_INCLUDE_DIR AND " + name + "_LIBRARIES)", "if(FALSE)")
        text = text.replace("if(SymFPU_INCLUDE_DIR)", "if(FALSE)")
        text = text.replace("if(cryptominisat5_FOUND)", "if(FALSE)")
        text = text.replace("find_package(cryptominisat5 ${CryptoMiniSat_FIND_VERSION} QUIET)", "set(cryptominisat5_FOUND FALSE)")
        text = text.replace("if(Poly_INCLUDE_DIR\n   AND Poly_LIBRARIES\n   AND PolyXX_LIBRARIES\n)", "if(FALSE)")
        if name == "CryptoMiniSat":
            text = text.replace("CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release", "CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD_LIBRARIES=${CMAKE_CXX_STANDARD_LIBRARIES}")
        ctx.file(path, text)
    # Reuse the pinned dependency APIs for a placement solver bridge. These
    # headers never escape @cvc5_source; the bridge joins the isolated object.
    ctx.extract("deps/livehd/CaDiCaL.tar.gz", output = "satheaders/cadical", stripPrefix = "cadical-rel-2.1.3-elevate")
    ctx.extract("deps/livehd/CryptoMiniSat.tar.gz", output = "satheaders/cms", stripPrefix = "cryptominisat-5.11.21")
    ctx.file("livehd_sat_bridge.cpp", ctx.read(ctx.attr.sat_bridge))
    path = "cmake/deps-helper.cmake"
    ctx.file(path, ctx.read(path).replace("if(NOT ENABLE_AUTO_DOWNLOAD)", "if(NOT ENABLE_AUTO_DOWNLOAD AND NOT LIVEHD_OFFLINE_DEPS)"))
    ctx.download_and_extract(
        url = "https://files.pythonhosted.org/packages/05/e7/df2285f3d08fee213f2d041540fa4fc9ca6c2d44cf36d3a035bf2a8d2bcc/pyparsing-3.2.3-py3-none-any.whl",
        sha256 = "a749938e02d6fd0b59b356ca504a24982314bb090c383e3cf201c95ef7e2bfcf",
        output = "deps/python",
        type = "zip",
    )
    path = "cmake/FindGMP.cmake"
    text = ctx.read(path).replace("LINK_LIBRARIES ${GMP_LIBRARIES} ${GMPXX_LIBRARIES}", "LINK_LIBRARIES ${GMPXX_LIBRARIES} ${GMP_LIBRARIES} OUTPUT_VARIABLE GMP_USABILITY_LOG")
    text = text.replace('"-DINCLUDE_DIRECTORIES=${GMP_INCLUDE_DIR}"', '"-DINCLUDE_DIRECTORIES=${GMP_INCLUDE_DIR}" "-DCMAKE_CXX_STANDARD_LIBRARIES=${CMAKE_CXX_STANDARD_LIBRARIES}"')
    text = text.replace("if(NOT GMP_FOUND_SYSTEM)", "if(NOT GMP_FOUND_SYSTEM AND LIVEHD_OFFLINE_DEPS)\n  message(FATAL_ERROR \"Hermetic GMP import failed; network fallback is disabled: ${GMP_USABILITY_LOG}\")\nendif()\nif(NOT GMP_FOUND_SYSTEM)", 1)
    ctx.file(path, text)
    ctx.patch(ctx.attr.offline_patch, strip = 1)
    ctx.patch(ctx.attr.kissat_patch, strip = 1)
    ctx.patch(ctx.attr.bv_unknown_patch, strip = 1)
    ctx.file("BUILD.bazel", ctx.read(ctx.attr.build_file).replace("@LIVEHD_INPUTS@", str(ctx.attr.inputs_bzl)).replace("@CXX_LIB@", "c++" if ctx.os.name.lower() == "mac os x" else "stdc++"))

_source_repo = repository_rule(
    implementation = _source_impl,
    attrs = {
        "build_file": attr.label(default = "//packages:cvc5_src.BUILD"),
        "sat_bridge": attr.label(default = "//packages:cvc5_sat.cpp"),
        "inputs_bzl": attr.label(default = "//packages:cvc5_inputs.bzl"),
        "offline_patch": attr.label(default = "//packages:cvc5-offline.patch"),
        "kissat_patch": attr.label(default = "//packages:cvc5-kissat-fresh.patch"),
        "bv_unknown_patch": attr.label(default = "//packages:cvc5-bv-unknown.patch"),
    },
)

def _alias_impl(ctx):
    ctx.file("BUILD.bazel", '''
config_setting(name = "prebuilt", values = {"define": "cvc5_prebuilt=1"})
alias(name = "cvc5", actual = select({":prebuilt": "@cvc5_prebuilt//:cvc5", "//conditions:default": "@cvc5_source//:cvc5"}), visibility = ["//visibility:public"])
alias(name = "sat_solver", actual = select({":prebuilt": "@cvc5_source//:sat_standalone", "//conditions:default": "@cvc5_source//:sat_solver"}), visibility = ["//visibility:public"])
alias(name = "cvc5_headers", actual = select({":prebuilt": "@cvc5_prebuilt//:cvc5_headers", "//conditions:default": "@cvc5_source//:cvc5_headers"}), visibility = ["//visibility:public"])
''')

_alias_repo = repository_rule(implementation = _alias_impl)

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
        name = "cvc5_prebuilt",
        build_file = "//packages:cvc5.BUILD",
        sha256 = artifact.sha256,
        strip_prefix = artifact.prefix,
        urls = [_CVC5_BASE + "/" + artifact.prefix + ".zip"],
    )

    _source_repo(name = "cvc5_source")
    _alias_repo(name = "cvc5")

cvc5_repo = module_extension(
    implementation = _cvc5_repo_impl,
    doc = "Registers source-built @cvc5 plus a host-matched prebuilt escape hatch.",
)
