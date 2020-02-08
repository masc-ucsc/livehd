#!/usr/bin/python

def main():
    nums = [1, 10, 100, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 11000, 12000, 13000, 14000, 15000,
            16000, 17000, 18000, 19000, 20000, 21000, 22000, 23000, 24000,
            25000, 26000, 27000, 28000, 29000, 30000, 40000, 50000, 60000, 65000, 67500, 70000, 72500, 75000]
    for num in nums:
      f = open("prp_xor_" + str(num) + ".prp", "w+")


      for i in range(num):
          if num == 1:
              f.write("    z = a ^ a\n")
              break
          if i == 0:
              f.write("    t0 = $a0 ^ $a1 \n")
          else:
              if i == num -1:
                f.write("    %%z = $a%d  ^ t%d\n" %(i, i-1))
                continue
              f.write("    t%d = $a%d  ^ t%d\n" %(i, i, i-1))

      f.close()

if __name__ == "__main__":
    main()
