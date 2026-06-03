-- Pyrope syntax highlighting for Neovim via nvim-treesitter (main branch).
--
-- Drop this file into your lazy.nvim plugins directory, e.g.
--   ~/.config/nvim/lua/plugins/pyrope.lua
-- After restarting Neovim, run `:TSInstall pyrope` once.
--
-- For local grammar development, replace the url/branch below with a path:
--   install_info = {
--     path = vim.fn.expand("~/projs/tree-sitter-pyrope"),
--     queries = "queries",
--   },

-- Register the parser. nvim-treesitter's main branch reloads its parser table
-- from disk on every install/update (wiping runtime registrations) and re-fires
-- the `User TSUpdate` event so out-of-tree parsers can re-register. Registering
-- on that event -- not once at startup -- is what avoids the error
-- "skipping unsupported language: pyrope".
local function register_pyrope()
  require("nvim-treesitter.parsers").pyrope = {
    install_info = {
      url = "https://github.com/masc-ucsc/tree-sitter-pyrope",
      branch = "main",
      queries = "queries", -- installs this repo's queries/*.scm automatically
    },
  }
end

return {
  {
    "nvim-treesitter/nvim-treesitter",
    init = function()
      vim.filetype.add({ extension = { prp = "pyrope" } })
      vim.api.nvim_create_autocmd("User", {
        pattern = "TSUpdate", -- fired by nvim-treesitter before every install/update
        callback = register_pyrope,
      })
      -- Pyrope uses C-style comments (`//` and `/* */`); set commentstring so
      -- `gc`/`gcc` commenting works (otherwise: "Option 'commentstring' is empty").
      vim.api.nvim_create_autocmd("FileType", {
        pattern = "pyrope",
        callback = function(args)
          vim.bo.commentstring = "// %s"
          vim.lsp.start({
            name      = "livehd",
            cmd       = { "prplsp" },  -- on $PATH; falls back to `lgshell` outside a checkout
            -- launch in the file's dir so prplsp detects an enclosing livehd checkout
            cmd_cwd   = vim.fn.fnamemodify(args.file, ":h"),
            root_dir  = vim.fn.getcwd(),
          })
        end,
      })
    end,
    opts = function(_, opts)
      opts.ensure_installed = opts.ensure_installed or {}
      vim.list_extend(opts.ensure_installed, { "pyrope" })
    end,
  },
}
