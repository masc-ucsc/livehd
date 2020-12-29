# fplan

fplan is a floorplanner that takes advantage of hierarchy and regularity in LiveHD designs.  It uses [ArchFP](http://lava.cs.virginia.edu/archfp/) to generate the actual floorplan files.

## Sample Usage
1. Import LGraph: `livehd> inou.yosys.tolg files:./pass/fplan/tests/hier_test.v root:hier_test`
2. Run Floorplan: `livehd> lgraph.open name:hier_test |> pass.fplan.makefp <options>`

## Commands
- `pass.fplan.makefp` generates a floorplan and sends it to stdout (will send to file in the future)
   - When generating a floorplan, a 'top' module must be defined.
