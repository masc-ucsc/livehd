#!/usr/bin/python

def main():
    import subprocess
    nums = [10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000]
    for num in nums:
      f = open("xor_" + str(num) + ".v", "w+")

      f.write("module xor_" + str(num) + "(\n")
      f.write("    input a,\n")
      f.write("    input b,\n")
      f.write("    output wire s\n")
      f.write("    );\n\n")

      for i in range(num):
          if num == 1:
              f.write("    wire s = a ^ b;\n")
              break
          if i == 0:
              f.write("    wire t0 = a ^ b;\n")
          elif i == num-1:
              f.write("    wire s = t%d ^ a;\n" % (i-1))
          else:
              f.write("    wire t%d = t%d ^ a;\n" % (i, i-1))
      f.write("\n\nendmodule")
      f.close()
if __name__ == "__main__":
    main()
