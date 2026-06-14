/*
 * Linker version script for the lhd binary.
 *
 * lhd statically links slang v11 (@slang, the `--reader slang` SV front-end)
 * AND, at runtime, dlopens the yosys-slang plugin (slang.so) which embeds a
 * DIFFERENT slang (v10) plus its own fmt/boost. `-Wl,--export-dynamic` is
 * required so that yosys plugins resolve the *Yosys* symbols of the main
 * binary -- but it also exports lhd's slang/fmt/boost symbols, which then
 * interpose the plugin's incompatible embedded copies. The ABI mismatch
 * crashes the plugin (segfault inside slang::driver::Driver::parseCommandLine
 * the moment read_slang runs).
 *
 * Force-localize every slang/fmt/boost symbol so they are NOT placed in lhd's
 * dynamic symbol table. The plugin is self-contained for these (slang.so
 * defines all of its own slang/fmt/boost and imports only Yosys/std/libc from
 * the main binary -- those stay exported), so it binds to its own copies while
 * lhd keeps using its statically-linked v11 internally. The globs match the
 * Itanium-mangled namespace components: "slang" -> 5slang, "fmt" -> 3fmt,
 * "boost" -> 5boost (covers functions, typeinfo, vtables, and template
 * instantiations parameterized on those types).
 *
 * Symbols not matched here keep their default (global) binding and are still
 * exported by --export-dynamic, so the Yosys plugin ABI is unaffected.
 */
{
  local:
    *5slang*;
    *3fmt*;
    *5boost*;
};
