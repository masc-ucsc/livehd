//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Pyrope Language Server (task 1n). See the LiveHD docs.
//
// A minimal LSP server over stdio (JSON-RPC, Content-Length framed). It drives
// LiveHD's Pyrope front-end (`prp2lnast`) on the editor buffer and republishes
// the `core/diag` records as LSP diagnostics. Pyrope-only (`.prp`); it never
// touches the Verilog/Yosys path.

namespace livehd::lsp {

// Run the server loop on stdin/stdout until `exit`/EOF. Returns the process
// exit code (0 = clean shutdown). Reassigns fd 1 -> fd 2 internally so stray
// library stdout cannot corrupt the protocol stream.
int run_stdio();

}  // namespace livehd::lsp
