
## Chisel Compiler

The best place to start with Chisel is the [chisel-templaterepo](https://github.com/freechipsproject/chisel-template). 
It has everything needed to help start making hardware designs, plus an example
GCD design. Once you've locally cloned it and are in the directory, you can
build the GCD example by typing


To get Chirrtl (highest level of FIRRTL) and Verilog, do the following

(1) define your package name and directory at
```
~/chisel/src/main/scala/your_package_name
```

(2) put your chisel designs under the package directory

```
~/chisel/src/main/scala/your_package_name/Foo.scala
```

(3) remember to modify your chisel design to specify the package name 

```
// Foo.scala
package your_package_name

import chisel3._ 

class Foo extends Module {
  your chisel design here  
}

```

(4) run this command at your chisel root directory

```
sbt 'runMain chisel3.stage.ChiselMain --module your_package_name.Foo'
```

Results can be found in your chisel root directory


(5) FIXME->sh
what is the case for a hierarchical design?


## FIRRTL Compiler
The [FIRRTL compiler](https://github.com/freechipsproject/firrtl) allows for us
to take in a FIRRTL file and translate it to different levels of FIRRTL. The
different levels that exist in FIRRTL are, from highest to lowest: Chirrtl,
HiFIRRTL, MidFIRRTL, and LoFIRRTL. More information can be found here:
[https://www2.eecs.berkeley.edu/Pubs/TechRpts/2016/EECS-2016-9.pdf](https://www2.eecs.berkeley.edu/Pubs/TechRpts/2016/EECS-2016-9.pdf).

When you clone the FIRRTL repository, you can build the compiler by doing:
```
sbt compile
sbt assembly
```

### Getting the lower firrtl design and Verilog
Then, if you have a FIRRTL file that you want to lower, you can use the
following command in the FIRRTL repository:
```
./utils/bin/firrtl -i GCD.fir -X low
```
or if you instead want to get Verilog you can use:
```
./utils/bin/firrtl -i GCD.fir -X verilog
```

If you need help with the compiler commands, use `./utils/bin/firrtl --help` and it will tell you more.

### Getting different level of Protobuf from Chirrtl
In the `inou/firrtl` directory, I've added `WritePB.scala` which is a way to get
Protobuf from a FIRRTL file using their compiler. You'll want to add this file
into the FIRRTL directory at `firrtl/src/main/scala/firrtl/transforms/` then run
`sbt compile` and `sbt assembly`.

You can get different level of Protobuf by doing the following command:
```
./utils/bin/firrtl -i GCD.fir -X none --custom-transforms firrtl.transforms.WriteChPB
./utils/bin/firrtl -i GCD.fir -X none --custom-transforms firrtl.transforms.WriteHighPB
./utils/bin/firrtl -i GCD.fir -X none --custom-transforms firrtl.transforms.WriteLowPB
```

Note that this command will create FIRRTL-Protobuf file with the extension of
`lo.pb` with. You can instead create HiFIRRTL or CHIRRTL by replacing
`WriteLowPB` with either `WriteHighPB` or `WriteChPB`. If you do so, the
extension will change to either `hi.pb` or `ch.pb` respectively. Note that if
the FIRRTL file you have is already in LoFIRRTL form, calling `WriteHighPB` may
not work since the FIRRTL compiler will never perform anything related to
HiFIRRTL.

