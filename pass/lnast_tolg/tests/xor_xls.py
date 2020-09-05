#!/usr/bin/python

# fn xor(x: u2, y: u2) -> u2 {
#   let t1 = x ^ y;
#     let t2 = t1 ^ x;
#       let t3 = t2 ^ x;
#         let t4 = t3 ^ x;
#           let t5 = t4 ^ x;
#             t5 ^ x
#             }

def main():
    import subprocess
    nums = [10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000]
    for num in nums:
      f = open("xor_" + str(num) + ".x", "w+")

      for i in range(num):
          if num == 1:
              f.write("fn xor(x: u2, y: u2) -> u2 {\n")
              f.write("    x ^ y\n")
              break
          if i == 0:
              f.write("fn xor(x: u2, y: u2) -> u2 {\n")
              f.write("    let t0 = x ^ y;\n")
          elif i == num-1:
              f.write("    t%d ^ x\n" % (i-1))
          else:
              f.write("    let t%d = t%d ^ x;\n" % (i, i-1))

      f.write("}")
      f.close()
      # bashCommand =  "prp xor_" + str(num) + ".prp | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x xor_" + str(num) + ".cfg"
      # subprocess.check_call(bashCommand, shell=True)
if __name__ == "__main__":
    main()
