import sys
import re

vfile = sys.argv[1]
fin = open(vfile, "rt")

data = fin.read()

data = re.sub(r"(\w)\.(\w)", r"\1_\2", data)
# data = data.replace('\inp_', '')
# data = data.replace('\out_', '')

fin.close()

fin = open(vfile, "wt")
fin.write(data)
fin.close()
