import sys
import re

# step-1: replace any . with _ in the generated verilog
# step-2: removing the _0 postfix for the generated verilog
vfile = sys.argv[1]
fin = open(vfile, "rt")

data = fin.read()
data = re.sub(r"(\w)\.(\w)", r"\1_\2", data) # r means raw string, (\w) means a group of alnum; here the replace we use "backreference", \1 and \2 means the group
data = re.sub(r"(\w)\.(\w)", r"\1_\2", data) # r means raw string, (\w) means a group of alnum; here the replace we use "backreference", \1 and \2 means the group
data = re.sub(r"_0", r"", data)
fin.close()

fout = open(vfile, "wt")
fout.write(data)
fout.close()

# step-3: removing the _0 postfix for the golden verilog
vfile2 = sys.argv[2]
fin2 = open(vfile2, "rt")

data2 = fin2.read()
data2 = re.sub(r"\_0", r"", data2)
fin2.close()

fout2 = open(vfile2, "wt")
fout2.write(data2)
fout2.close()







