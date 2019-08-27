def main():
    num = 10000000
    f = open("xor_" + str(num) + ".v", "w+")
#Heading
    f.write("module xor_" + str(num) + "(\n")
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
            f.write("    wire z = t%d ^ t%d;\n" % (i-1, i-1))
        else:
            f.write("    wire t%d = t%d ^ t%d;\n" % (i, i-1, i-1))
    f.write("\n\nendmodule")
    f.close()
if __name__ == "__main__":
    main()
