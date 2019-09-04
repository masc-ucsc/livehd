#!/usr/bin/python

def main():
    nums = [1, 10, 100, 1000, 5000, 10000, 20000, 30000, 40000, 50000, 60000, 65000, 67500, 70000, 72500, 75000]
    for num in nums:
      f = open("mt_xor_" + str(num) + ".v", "w+")

      f.write("module mt_xor_" + str(num) + "(\n")
      f.write("    input [" + str(num-1) + ":0]  a,\n")
      f.write("    output wire z\n")
      f.write("    );\n\n")

      for i in range(num):
          if num == 1:
              f.write("    wire z = a ^ a;\n")
              break
          if i == 0:
              f.write("    wire t0 = a[0] ^ a[1] ;\n")
          else:
              f.write("    wire t%d = a[" % (i) + str(i) + "] ^ t%d;\n" % (i-1))
              if i+1 == num:
                f.write("    wire z = t%d;\n" % (i))

      f.write("\n\nendmodule")
      f.close()

    for num in nums:
      f = open("mt_or_" + str(num) + ".v", "w+")

      f.write("module mt_or_" + str(num) + "(\n")
      f.write("    input [" + str(num-1) + ":0]  a,\n")
      f.write("    output wire z\n")
      f.write("    );\n\n")

      for i in range(num):
          if num == 1:
              f.write("    wire z = a | a;\n")
              break
          if i == 0:
              f.write("    wire t0 = a[0] | a[1] ;\n")
          else:
              f.write("    wire t%d = a[" % (i) + str(i) + "] | t%d;\n" % (i-1))
              if i+1 == num:
                f.write("    wire z = t%d;\n" % (i))

      f.write("\n\nendmodule")
      f.close()
if __name__ == "__main__":
    main()
