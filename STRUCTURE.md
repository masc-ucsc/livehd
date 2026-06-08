# Documentation structure

This file is the map of LiveHD's documentation: **where each kind of doc lives,
and when to read which.** It is intentionally thin — it links out rather than
duplicating content. Read this first on a cold start.

## The layers

| Layer | Lives in | Format | Read it when… |
|---|---|---|---|
| **Current work** | [`todo/`](todo/index.html) | HTML | you want the open task list, a task's design, or to record/close a task |
| **LiveHD & Pyrope reference** | external `../docs` → [masc-ucsc.github.io/docs](https://masc-ucsc.github.io/docs/) | Markdown → mkdocs | you need the "why/what": language semantics, IR concepts, CLI usage, architecture |
| **Agent how-to + rules** | [`AGENTS.md`](AGENTS.md) | Markdown | you are building/testing/debugging, or need the change-gated coding rules |
| **Directory orientation** | `<dir>/README.md` | Markdown | you are editing a specific directory and need its layout / gotchas |
| **This map** | `STRUCTURE.md` | Markdown | cold start: figuring out where something belongs |

## Format principle

Documents a **human reads but does not edit** are HTML (the `todo/` hub; the
`../docs` site is Markdown source rendered to HTML by mkdocs). Documents that are
**agent-only / agent-mostly** are Markdown (`AGENTS.md`, directory `README.md`s,
this file).

## Layer details

### `todo/` — current work (HTML)
- [`todo/index.html`](todo/index.html) is the hub: a bullet summary of every open
  task, grouped by topic (**LiveHD**, **Pyrope**, **Verilog**) and by Group *N*.
- One file per task at `todo/<topic>/<id>.html`. A task's full design lives in its
  page; cross-task dependencies are visible via shared Group letters.
- **Lifecycle:** a task page is deleted the moment the *whole* task lands. This
  layer is strictly current work — it carries no history (git does).

#### Task page structure

Every `todo/<topic>/<id>.html` follows the same shape so tasks read uniformly:

1. **`<h1>`** — the task badge + a short title, then a 1–2 sentence summary.
2. **Depends on** — a bullet list of the tasks this one needs first, each with a
   one-sentence reason; `None.` if it has no prerequisites.
3. **Subtasks — pending** — the work left, as labelled items `A`, `B`, `C`, …,
   each with its own bullet list. The letter is a *stable id*.
4. **Subtasks — landed** — subtasks that were pending and have shipped, **moved
   here keeping their original letter** (not deleted), until the whole task is
   done and the page is removed. `None yet.` if empty.
5. *(optional)* **Design notes** — reference / spec material kept at the bottom.

When a subtask ships, mark it and move it from *pending* to *landed* with the
same letter; delete the whole page only once the task is fully complete. Skeleton:

```html
<h1><strong class="item-id">1d</strong> Demand-driven emit + DCE</h1>
<p>Short summary…</p>

<h2>Depends on</h2>
<ul><li><a class="xref" href="1f.html">1f</a> — why, in one sentence.</li></ul>
<!-- or: <p>None.</p> -->

<h2>Subtasks — pending</h2>
<ul class="subtasks">
  <li><strong>D — sea-of-nodes rewrite.</strong>
    <ul><li>detail</li></ul></li>
</ul>

<h2>Subtasks — landed</h2>
<ul class="subtasks">
  <li><strong>A — D1 range→sweep.</strong> <span class="status landed">landed</span></li>
</ul>
```

### `../docs` — the human-readable contract (Markdown → mkdocs)
- The LiveHD guide (`../docs/docs/livehd/*`) and the Pyrope language reference
  (`../docs/docs/pyrope/*`) are the canonical "why/what". The Pyrope pages are the
  de-facto language contract.
- **Conceptual only** — no references to specific source files or implementation
  details. **Change-gated:** content changes need human confirmation.

### `AGENTS.md` — agent how-to + change-gated rules
- Build/test commands, sibling-repo paths, running Pyrope/Yosys tests, debugging
  tips, and the **Contracts** section (compiler-warning policy, contract-test
  immutability). Enforcement scripts live in `scripts/contracts/`; baselines in
  `.contracts/`.
- There is **no in-repo `docs/contracts/` directory** — that role split into the
  `../docs` reference (human contract) and `AGENTS.md` (agent rules).

### `<dir>/README.md` — directory orientation (on-need)
- A short *Overview → Key files → Gotchas/debug hints* for a directory, in
  Markdown. Added or extended **only when there is a demonstrated need** (an agent
  misread the structure, a file's purpose is non-obvious, or there's a debug
  gotcha). Examples: [`graph/README.md`](graph/README.md),
  [`upass/README.md`](upass/README.md), `inou/attr/README.md`, `simlib/README.md`.

## Cross-links
- README for humans: [`README.md`](README.md).
- Global agent guidance: [`AGENTS.md`](AGENTS.md) (also pointed to by `CLAUDE.md`).
