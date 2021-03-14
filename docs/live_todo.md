
// Sample commands to support in lgshell

// Checkpoint time setup
live.setup check:dir files:foo.v,bar.prp,xxx.v script:flow.conf |> @c1
//
// Creates a "dir" directory (check:) which has many lgdb inside.
//   lgdb_elab
//   lgdb_synth
//   lgdb_place
//   lgdb_route
//   lgdb_parse     source and preparsed files (flatten, foo/bar.v -> verilog/foo.bar.v, foo/bar.prp -> prp/foo.bar.prp)
//   flow_tools?
//   final          bitstream if FPGA, OASIS if ASIC

// flow.conf has a flow setup for transforming lgraphs after parsing. E.g:
//
//  live.open check:{{setup_check}} |> @check
//
//  @check |> live.parse files:{{setup_files}} |> @parse_delta
//
//  @check |> live.files |> @cfiles  // files: inside check/lgparse/verilog/xxx*.v
//
//  @cfiles |> inou.verilog.tolg lgdb:{{setup_check}}/lgdb_elab |> @check_elab
//
//  @check_elab |> pass.abc lgdb:{{check_setup}/lgdb_synth
//
//  lgraph.open_all lgdb::{{setup_check}}/lgdb_synth |> @synth
//  @synth |> pass.abc

// Opens a checkpoint
live.open check:dir |> @c2

// Create a dependent checkpoint, for deltas, but it is a full checkpoint
live.setup check:dir2 link:dir |> @c3

// Triggers all the steps parse,elab,synth...bitstream
@c2 |> live.add files:foo.v,yyy.v script:incr.conf

// incr.conf specificies steps in delta/live synthesis
//
//  @input |> live.parse files::{{add_files}} |> @parse_delta
//
//  live.synth 

// Just resynthesis
@lg |> inou.vivado.bitstream odir:fpga

// Some way to authentificate
@c1 |> cloud.live.push url:foo.bar.com/mycheck1

cloud.live.pull url:foo.bar.com/mycheck1 |> @c2

@c2 |> live.add files:foo.v,yyy.v |> @c3

@c3 |> live.final |> inou.vivado.bitstream odir:urgent_fpga

