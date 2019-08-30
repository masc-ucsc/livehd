def main():
    num = 100
    f = open("xor_" + str(num) + ".prp", "w+")
#Heading

    for i in range(num):
        if num == 1:
            f.write("    %%s = $a ^ $b\n")
            break
        if i == 0:
            f.write("    t0 = $a ^ $b\n")
        elif i == num-1:
            f.write("    %%s = t%d ^ $a\n" % (i-1))
        else:
            f.write("    t%d = t%d ^ $a\n" % (i, i-1))
    f.close()
if __name__ == "__main__":
    main()
