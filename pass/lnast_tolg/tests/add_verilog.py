#!/usr/bin/python

def main():
    import subprocess
    nums = [100, 10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000]
    for num in nums:
      f = open("add_" + str(num) + ".v", "w+")

      f.write("module add_" + str(num) + "(\n")
      f.write("    input [3:0] a,\n")
      f.write("    input [3:0] b,\n")
      f.write("    output wire [29:0] s\n")
      f.write("    );\n\n")

      bits=4
      for i in range(num):
          if i <= 1:
              bits = bits + 1
              f.write("    wire [%d-1:0] t%d;\n" % (bits, i))
              f.write("    assign t%d = a + b;\n" % (i))
          elif i == num-1:
              f.write("    assign s = t%d ^ a;\n" % (i-1))
          elif (i&0xFF) == 7:
              bits = 8
              f.write("    wire [%d-1:0] t%d;\n" % (bits, i))
              f.write("    assign t%d = t%d & ('hf0+1+2+3+4+5);\n" % (i, i-2))
          elif (i&0xFF) == 8:
              bits = 8
              f.write("    wire [%d-1:0] t%d;\n" % (bits, i))
              f.write("    assign t%d = (t%d>>8) & 8'hFF;\n" % (i, i-2))
          else:
              bits = bits + 1
              f.write("    wire [%d-1:0] t%d;\n" % (bits, i))
              f.write("    assign t%d = t%d + t%d;\n" % (i, i-1, i-2))

      f.write("\n\nendmodule")
      f.close()

if __name__ == "__main__":
    main()
