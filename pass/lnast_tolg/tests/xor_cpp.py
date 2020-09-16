#!/usr/bin/python

def main():
    import subprocess
    nums = [10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000]
    for num in nums:
      f = open("xor_" + str(num) + ".cpp", "w+")

      f.write("int xor_chain(int a, int b) {\n");

      for i in range(num):
          if num == 1:
              f.write("    int %s = a ^ b;\n")
              break
          if i == 0:
              f.write("    int t0 = a ^ b;\n")
          elif i == num-1:
              f.write("    return t%d ^ a;\n" % (i-1))
          else:
              f.write("    int t%d = t%d ^ a;\n" % (i, i-1))

      f.write("}\n");
      f.close()
      # bashCommand =  "prp xor_" + str(num) + ".prp | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x xor_" + str(num) + ".cfg"
      # subprocess.check_call(bashCommand, shell=True)
if __name__ == "__main__":
    main()
