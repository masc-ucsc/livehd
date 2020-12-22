# fplan

fplan is a floorplanner that takes advantage of hierarchy and regularity in LiveHD designs.  It implements an algorithm known as [HiReg](https://www.cs.upc.edu/~jordicf/gavina/BIB/files/floorplan_iccad2014.pdf) in order to do this effectively.

## Sample Usage
1. Import LGraph: `livehd> inou.yosys.tolg files:./pass/fplan/tests/hier_test.v root:hier_test`
2. Run Floorplan: `livehd> lgraph.open name:hier_test |> pass.fplan.makefp <options>`

## Commands
- `pass.fplan.makefp` generates a floorplan and sends it to stdout (will send to file in the future)
   - When generating a floorplan, a 'top' module must be defined.
- `pass.fplan.dumphier` dumps the imported hierarchy to a DOT file called `hier_dump.dot`.
- `pass.fplan.dumptree` dumps the uncollapsed internal hierarchy tree to a DOT file called `tree_dump.dot`.

All dump commands take the same relevant options as `makefp`.

:warning: This command is in active development, use at your own risk!

## High-level operation
1. *Hierarchy discovery*: the incoming hierarchy is transformed into a binary tree of modules called a hierarchy tree.
2. *Hierarchy tree collapsing*: a copy of the tree is made, and nodes are collapsed if they are under the minimum area threshold for that tree.
3. *Regularity discovery*: patterns in the collapsed hierarchy are discovered, with only the most frequent patterns being kept.
3.5. *Regular hierarchy generation*: a "regular" hierarchy is created where each node is a pattern and each leaf node is a module.
4. *Bounding curve construction*: all regular hierarchies are folded into an outline of all floorplans discovered.
4.5. *Point selection*: the designer picks a set of points from the bounding curve that have good metrics.
5. *Floorplan construction*: a floorplan is constructed beginning with the set of points collected in the previous step.

## Options
- `min_tree_nodes` controls how many LGraph nodes a module must have in order for fplan to analyze and split it.  Setting this option to `0` will disable reorganization of the hierarchy.
- `num_collapsed_hiers` controls the number of collapsed hierarchies fplan will generate.
- `min_tree_area` controls how big a node needs to be in order to get collapsed together with another node.  In the future this will be measured in mm^2, but for now fplan just uses the `size()` attribute of an LGraph node.
- `max_pats` controls the number of patterns to keep out of all patterns found in the hierarchy.
- `max_optimal_nodes` controls the point at which fplan switches from exhaustively searching for the smallest floorplan outline to using a heuristic.

## Papers used:
1. HiReg: A hierarchical approach for generating regular floorplans
   https://www.cs.upc.edu/~jordicf/gavina/BIB/files/floorplan_iccad2014.pdf
2. Min cut: An Efficient Heuristic Procedure for Partitioning Graphs
   https://janders.eecg.utoronto.ca/1387/readings/kl.pdf
3. Branch and bound: BloBB (Block-Packing with Branch-and-Bound)
   http://vlsicad.eecs.umich.edu/BK/BloBB/
