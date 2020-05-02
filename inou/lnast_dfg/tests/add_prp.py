#!/usr/bin/python

def main():
    import subprocess
    nums = [10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000]
    for num in nums:
      f = open("add_" + str(num) + ".prp", "w+")

      for i in range(num):
          if num <= 2:
              f.write("    %s = $a + $b\n")
              break
          if i <= 1:
              f.write("    t%d = $a + $b\n" % (i))
          elif i == num-1:
              f.write("    %%s = t%d ^ $a\n" % (i-1))
          else:
              f.write("    t%d = t%d + t%d\n" % (i, i-1, i-2))

      f.close()
      # bashCommand =  "prp xor_" + str(num) + ".prp | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x xor_" + str(num) + ".cfg"
      # subprocess.check_call(bashCommand, shell=True)
if __name__ == "__main__":
    main()
