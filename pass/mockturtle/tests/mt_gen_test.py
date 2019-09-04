#!/usr/bin/python

def main():
    nums = [1, 10, 100, 1000, 5000, 10000, 25000, 50000, 75000, 100000, 250000, 500000, 750000, 1000000]
    for num in nums:
      f = open("mt_xor_" + str(num) + ".v", "w+")

      f.write("module mt_xor_" + str(num) + "(\n")
      f.write("    input a,\n")
      f.write("    input b,\n")
      f.write("    output wire z\n")
      f.write("    );\n\n")

      for i in range(num):
          if num == 1:
              f.write("    wire z = a ^ b;\n")
              break
          if i == 0:
              f.write("    wire t0 = a ^ b;\n")
          elif i == num-1:
              f.write("    wire z = t%d ^ a;\n" % (i-1))
          else:
              f.write("    wire t%d = t%d ^ a;\n" % (i, i-1))
      f.write("\n\nendmodule")
      f.close()

    for num in nums:
      f = open("mt_or_" + str(num) + ".v", "w+")

      f.write("module mt_or_" + str(num) + "(\n")
      f.write("    input a,\n")
      f.write("    input b,\n")
      f.write("    output wire z\n")
      f.write("    );\n\n")

      for i in range(num):
          if num == 1:
              f.write("    wire z = a | b;\n")
              break
          if i == 0:
              f.write("    wire t0 = a | b;\n")
          elif i == num-1:
              f.write("    wire z = t%d | a;\n" % (i-1))
          else:
              f.write("    wire t%d = t%d | a;\n" % (i, i-1))
      f.write("\n\nendmodule")
      f.close()

if __name__ == "__main__":
    main()
