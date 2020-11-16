
## Chisel Compiler

The best place to start with Chisel is the [chisel-template repo](https://github.com/freechipsproject/chisel-template). It has everything needed to help start making hardware designs, plus an example GCD design. Once you've locally cloned it and are in the directory, you can build the GCD example by typing

To obtain a Chirrtl version of the design (highest level of FIRRTL), type this in the template directory:
```
sbt 'test:runMain gcd.GCDMain'
```

If you instead want Chirrtl and Verilog, do:
```
sbt 'test:runMain gcd.GCDMain --backend-name verilator'
```

After running these commands, any recent runs can be found in the `test_run_dir` directory.

The relevant files can be found at `src/main/scala/gcd/GCD.scala`, `src/test/scala/gcd/GCDMain.scala`, and `src/test/scala/gcd/GCDUnitTest.scala`.

At the top of the `GCD.scala` file, you may notice there being `package gcd`. That is where the `gcd.` portion comes from in the `sbt` command. `GCDMain`, the thing that elaborates the design to FIRRTL, comes from the file with that name.

### Building your own Chisel designs
If you want to add your own designs, you'll need to make note of what package you specify and what you name the function that elaborates your design. If you change the package name or the elaboration function name, you'll have to adjust those in the commands above as well.

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
Note that the file will not be put into the `test_run_dir` directory and will instead be placed in the directory you are currently in.

## FIRRTL Compiler
The [FIRRTL compiler](https://github.com/freechipsproject/firrtl) allows for us to take in a FIRRTL file and translate it to different levels of FIRRTL. The different levels that exist in FIRRTL are, from highest to lowest: Chirrtl, HiFIRRTL, MidFIRRTL, and LoFIRRTL. More information can be found here: [https://www2.eecs.berkeley.edu/Pubs/TechRpts/2016/EECS-2016-9.pdf](https://www2.eecs.berkeley.edu/Pubs/TechRpts/2016/EECS-2016-9.pdf).

When you clone the FIRRTL repository, you can build the compiler by doing:
```
sbt compile
sbt assembly
```

Then, if you have a FIRRTL file that you want to lower, you can use the following command in the FIRRTL repository:
```
./utils/bin/firrtl -i GCD.fir -X low
```
or if you instead want to get Verilog you can use:
```
./utils/bin/firrtl -i GCD.fir -X verilog
```

If you need help with the compiler commands, use `./utils/bin/firrtl --help` and it will tell you more.

### Getting Protobuf
In the `inou/firrtl` directory, I've added `WritePB.scala` which is a way to get Protobuf from a FIRRTL file using their compiler. You'll want to add this file into the FIRRTL directory at `firrtl/src/main/scala/firrtl/transforms/` then run `sbt compile` and `sbt assembly`.

You can use this functionality by doing the following command:
```
./utils/bin/firrtl -i GCD.fir -X low --custom-transforms firrtl.transforms.WriteLowPB
```

Note that this command will create FIRRTL-Protobuf file with the extension of `lo.pb` with. You can instead create HiFIRRTL or CHIRRTL by replacing `WriteLowPB` with either `WriteHighPB` or `WriteChPB`. If you do so, the extension will change to either `hi.pb` or `ch.pb` respectively. Note that if the FIRRTL file you have is already in LoFIRRTL form, calling `WriteHighPB` may not work since the FIRRTL compiler will never perform anything related to HiFIRRTL.

## Relevant LiveHD commands
`tests/lofirrtl_verif.sh` in this directory has the commands needed to go from Protobuf to LNAST to LGraph to Verilog. `tests/ver_to_fir_verif.sh` has the commands necessary to go from Verilog to FIRRTL-Protobuf.

After building the LiveHD repo, you can go from Protobuf to Verilog by entering in our shell:
```
./bazel-bin/main/lgshell
```
and then doing the following using a `SimpleBitOps` example:
```
inou.firrtl.tolnast files:inou/firrtl/tests/proto/SimpleBitOps.lo.pb |> pass.lnast_tolg

lgraph.open name:SimpleBitOps |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.bitwidth

lgraph.open name:SimpleBitOps |> inou.yosys.fromlg
```

You can check and see if the generated `SimpleBitOps.v` matches the Chisel/FIRRTL generated Verilog by doing
```
./lgcheck -implementation=SimpleBitOps.v --reference=[insert_path_to_orig].v
```

Note: The FIRRTL interface with LNAST is still really being worked on and may not work right now. Try to stick to LoFIRRTL Protobufs for better success...
