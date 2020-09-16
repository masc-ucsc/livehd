#!/usr/bin/python

def main():
    import subprocess
    nums = [100, 10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000]
    for num in nums:
      f = open("add_" + str(num) + ".cpp", "w+")

      f.write("int add_chain(int a, int b) {\n");

      for i in range(num):
          if i <= 1:
              f.write("    int t%d = a + b;\n" % (i))
          elif i == num-1:
              f.write("    return t%d ^ a;\n" % (i-1))
          elif (i&0xFF) == 7:
              f.write("    int t%d = t%d & (0xF0+1+2+3+4+5);\n" % (i, i-2))
          elif (i&0xFF) == 8:
              f.write("    int t%d = (t%d>>8) & 255;\n" % (i, i-2))
          else:
              f.write("    int t%d = t%d + t%d;\n" % (i, i-1, i-2))

      f.write("}\n");
      f.close()

if __name__ == "__main__":
    main()
