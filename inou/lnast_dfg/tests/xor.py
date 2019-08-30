#!/usr/bin/python

def main():
    import subprocess
    nums = [1, 2, 10, 100, 1000, 10000, 100000]
    for num in nums:
      f = open("xor_" + str(num) + ".prp", "w+")

      for i in range(num):
          if num == 1:
              f.write("    %s = $a ^ $b\n")
              break
          if i == 0:
              f.write("    t0 = $a ^ $b\n")
          elif i == num-1:
              f.write("    %%s = t%d ^ $a\n" % (i-1))
          else:
              f.write("    t%d = t%d ^ $a\n" % (i, i-1))

      f.close()
      bashCommand =  "prp xor_" + str(num) + ".prp | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x xor_" + str(num) + ".cfg"
      subprocess.check_call(bashCommand, shell=True)
if __name__ == "__main__":
    main()
