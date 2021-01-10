# fplan

fplan is a floorplanner that takes advantage of hierarchy and regularity in LiveHD designs.  It uses [ArchFP](http://lava.cs.virginia.edu/archfp/) to generate the actual floorplan files.

## Sample Usage
1. Import LGraph: `livehd> inou.yosys.tolg files:./pass/fplan/tests/hier_test.v root:hier_test`
2. Run floorplan: `livehd> lgraph.open name:hier_test |> pass.fplan.makefp <options>`
3. Render floorplan as .png file: `# view.py -i <floorplan>.flp`
4. View floorplan: `# open <floorplan>.png`

## Commands
- `pass.fplan.makefp` generates a floorplan and sends it to stdout (will send to file in the future)
   - When generating a floorplan, a 'top' module must be defined.

## Viewing
There are tools that come with ArchFP that convert a .flp file to a .pdf file for viewing, but they may or may not work.  The `view.py` script can be used instead, and should produce the same output.

## Traversal methods
 - flat_lg
    - Each LGraph module (verilog module) will be floorplanned without taking into accout the hierarchy of the modules
 - hier_lg
    - Same as flat_lg, but hierarchy will be taken into account
 - flat_node
    - same as flat_lg, but nodes will be floorplanned instead of modules.
    - This kind of traversal is only suitable for very small designs due to ArchFP limitations.
 - hier_node
    - same as hier_lg, but nodes will be floorplanned instead of modules.
