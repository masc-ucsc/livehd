
## Chisel Compiler

The best place to start with Chisel is the [chisel-template repo](https://github.com/freechipsproject/chisel-template). It has everything needed to help start making hardware designs, plus an example GCD design. Once you've locally cloned it and are in the directory, you can build the GCD example by typing

To obtain a HiFIRRTL and LoFIRRTL version of the design, type this in the template directory:
```
sbt 'test:runMain gcd.GCDMain'
```

If you instead only want a HiFIRRTL file and Verilog, do:
```
sbt 'test:runMain gcd.GCDMain --backend_name verilator'
```

After running these commands, any recent runs can be found in the `test_run_dir` directory.


The relevant files can be found at `src/main/scala/gcd/GCD.scala`, `src/test/scala/gcd/GCDMain.scala`, and `src/test/scala/gcd/GCDUnitTest.scala`.

At the top of the `GCD.scala` file, you may notice there being `package gcd`. That is where the `gcd.` portion comes from in the `sbt` command. `GCDMain`, the thing that elaborates the design to FIRRTL, comes from the file with that name.

### Building your own Chisel designs
If you want to add your own designs, you'll need to make note of what package you specify and what you name the thing that elaborates your design.


### Getting Protobuf Output
If you want to get Protobuf output from the Chisel compiler, you'll need to call the `dumpProto` function. Let us use GCD as an example. You would want to add this to `GCDMain.scala`.

```
import java.io.File

object GCDPB extends App {
  val f = new File("GCD.pb")
  chisel3.Driver.dumpProto(chisel3.Driver.elaborate(() => new GCD), Option (f))
}
```

You can then get the Protobuf version of the FIRRTL design using the command:
```
sbt 'test:runMain gcd.GCDPB'
```
Note that the file will not be put into the `test_run_dir` directory.

## FIRRTL Compiler
The [FIRRTL compiler](https://github.com/freechipsproject/firrtl) allows for us to take in a FIRRTL file and translate it to different levels of FIRRTL.

When you clone this, you can build the compiler by doing:
```
sbt compile
sbt assembly
```

Then, if you have a FIRRTL file that you want to lower, you can use the following command in the cloned FIRRTL repository:
```
./utils/bin/firrtl -i GCD.fir -X low
```
or if you instead want to get Verilog you can use:
```
./utils/bin/firrtl -i GCD.fir -X verilog
```

If you need help with the compiler commands, use `sbt --help` and it will tell you more.

### Getting Protobuf
In this directory, I've added `WriteLowPB.scala` which is a way to get Protobuf from a FIRRTL file using their compiler. You'll want to add this file into the FIRRTL directory at `firrtl/src/main/scala/firrtl/transforms/` then run `sbt compile` and `sbt assembly`.

You can use this functionality by doing the following command:
```
./utils/bin/firrtl -i GCD.fir -X low --custom-transforms firrtl.transforms.WriteLowPB
```

Note that it currently only accepts LoFIRRTL as input into the transform, so you'll have to modify it for yourself if you want to allow any kind of FIRRTL to write to Protobuf.

## Relevant LiveHD commands
`firrtl_verif.sh` in this directory has the commands needed to go from Protobuf to LNAST to LGraph to Verilog.

After building the LiveHD repo, you can go from Protobuf to Verilog by entering in our shell:
```
./bazel-bin/main/lgshell
```
and then doing the following using a `SimpleBitOps` example:
```
inou.firrtl.tolnast files:inou/firrtl/tests/proto/SimpleBitOps.lo.pb |> inou.lnast_dfg.tolg

lgraph.open name:SimpleBitOps |> inou.lnast_dfg.resolve_tuples |> pass.bitwidth |> inou.lnast_dfg.assignment_or_elimination |> inou.lnast_dfg.dce

lgraph.open name:SimpleBitOps |> inou.yosys.fromlg
```

You can check and see if the generated `SimpleBitOps.v` matches the Chisel/FIRRTL generated Verilog by doing
```
./lgcheck -implementation=SimpleBitOps.v --reference=[insert_path_to_orig].v
```

Note: The FIRRTL->LNAST is still really being worked on and may not work right now. Try to stick to LoFIRRTL Protobufs for better success...
