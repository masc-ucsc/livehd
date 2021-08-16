# Introduction

!!!WARNING
    This document explains the future Pyrope, some features are still not
    implemented. They are documented to guide the designers.

Pyrope is a modern hardware description language, with these focus points:

* Fast parallel and incremental elaboration. 
* Help hardware verification:
    - Powerful synthesizable type system
    - Hot-Reload support, powerful assertions
    - Allows Pyrope 2 Verilog, edit Verilog, Verilog 2 Pyrope, edit Pyrope...
    - Static checks as long as they not produce false positives
* Modern and concise language
    - Avoiding hardware specific artifacts
    - Synthesis and simulation must be equal and deterministic
    - Zero cost abstraction


## Hello World

Create a directory for the project:
```
$ mkdir hello
$ cd hello
$ mkdir src
```

Populate the Pyrope code

`src/hello.prp`
```
test "quite empty" {
  puts "hello world"
}
```

Run
```
$prp test
```

All the pyrope files reside in `src` directory. The `prp` builder calls LiveHD to
elaborate the pyrope files and run all the tests.


## Trivial GCD

Populate the Pyrope code

=== "Pyrope"

    ```coffescript linenums="1"
    // src/gcd.prp
    if $load_values {
      #x = $value1
      #y = $value2
    }else{
      #x = if #x > #y { #x - #y } else { #y - #x }
      %z = #x
      %v = #y == 0
    }

    test "16bits gcd" {
      for i in 1..=100 {
        for j in 1..=100 {
          $value1      = i
          $value2      = j
          $load_values = true

          puts "trying gcd({},{})", $value1, $value2

          step 1 // advance 1 clock

          $load_values = false // deactivate load

          waitfor %z == true

          puts "result is {}", %v
          assert %v == __my_cpp_gcd(v1=$value1, v2=$value2)
        }
      }
    }
    
    // src/my_cpp_gcd.cpp
    void my_gcd_cpp(const Lbundle &inp, Lbundle &out) {
      auto [x,ok1] = inp.get_const("v1");
      auto [y,ok2] = inp.get_const("v2");

      assert(ok1 && ok2); // both must be defined

      while (y > 0) {
        if (x > y) {
          x -= y
        }
        else {
          y -= x
        }
      }

      out.add_const(x);
    }
    ```

=== "CHISEL"

    ```scala linenums="1"
    import Chisel._
    import firrtl_interpreter.InterpretiveTester
    import org.scalatest.{Matchers, FlatSpec}

    object GCDCalculator {
      def computeGcd(a: Int, b: Int): (Int, Int) = {
        var x = a
        var y = b
        while(y > 0 ) {
          if (x > y) {
            x -= y
          }
          else {
            y -= x
          }
        }
        x
      }
    }

    class GCD extends Module {
      val io = new Bundle {
        val a  = UInt(INPUT,  16)
        val b  = UInt(INPUT,  16)
        val e  = Bool(INPUT)
        val z  = UInt(OUTPUT, 16)
        val v  = Bool(OUTPUT)
      }
      val x  = Reg(UInt())
      val y  = Reg(UInt())
      when   (x > y) { x := x - y }
      unless (x > y) { y := y - x }
      when (io.e) { x := io.a; y := io.b }
      io.z := x
      io.v := y === UInt(0)
    }


    class InterpreterUsageSpec extends FlatSpec with Matchers {

      "GCD" should "return correct values for a range of inputs" in {
        val s = Driver.emit(() => new GCD)

        val tester = new InterpretiveTester(s)

        for {
          i <- 1 to 100
          j <- 1 to 100
        } {
          tester.poke("io_a", i)
          tester.poke("io_b", j)
          tester.poke("io_e", 1)
          tester.step()
          tester.poke("io_e", 0)

          while (tester.peek("io_v") != BigInt(1)) {
            tester.step()
          }
          tester.expect("io_z", BigInt(GCDCalculator.computeGcd(i, j)._1))
        }
        tester.report()
      }
    }
    ```


Run
```
$prp test gcd
```

The `gcd.prp` includes the top level module (`gcd`) and the unit test. To understand the differences
with alternative HDLs, the same GCD with CHISEL:


Some of the visible differences:

* Pyrope has global type inference. The gcd.prp file doe not specify any size. The size
is inferred from instantiation, in this case the test.
* Pyrope has special variable markers: $ is for inputs, % for outputs, and # for registers.
* CHISEL is a DSL. E.g: the `=` are SCALA, the `===` is generated HDL. The GCDCalculator is
a SCALA program, and the GCD is a generated CHISEL module.


Some not so visible differences:

Pyrope is not a DSL. There are several DSL Hardware Description Languages (HDL)
like CHISEL, pyMTL, pyRTL, CλaSH. In all the DSL cases, there is a host
language (SCALA, or Python, or Haskell) that must be executed. The result of
the execution is the hardware description which can be Verilog or some internal
IR like FIRRTL in CHISEL. The advantage of the DSL is that it can leverage the
existing language to have a nice hardware generator. The disadvantage is that
there are 2 languages at once, the DSL and the host language, and that it is
difficult to do incremental because the generated executable from host language
must be executed to generate the design.

