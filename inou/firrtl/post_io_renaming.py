import sys
vfile = sys.argv[1]
fin = open(vfile, "rt")

data = fin.read()

data = data.replace('.', '_')
data = data.replace('\inp_', '')
data = data.replace('\out_', '')

fin.close()

fin = open(vfile, "wt")
fin.write(data)
fin.close()
