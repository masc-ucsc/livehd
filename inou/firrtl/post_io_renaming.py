import sys
import re


vfile = sys.argv[1]
fin = open(vfile, "rt")

data = fin.read()
data = re.sub(r"(\w)\.(\w)", r"\1_\2", data) # r means raw string, (\w) means a group of alnum; here the replace we use "backreference", \1 and \2 means the group
fin.close()

fin = open(vfile, "wt")
fin.write(data)
fin.close()

# removing the _0 postfix for the first element of any vector output in the firrtl golden verilog
vfile2 = sys.argv[2]
fin2 = open(vfile, "rt")

data2 = fin2.read()
data2 = re.sub(r"(^\s*output.*)\_0(,*)", r"\1\2", data2)
fin2.close()

fin2 = open(vfile2, "wt")
fin2.write(data2)
fin2.close()







