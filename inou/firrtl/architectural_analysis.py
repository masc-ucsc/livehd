from treelib import Node, Tree
import sys
import re

fname = sys.argv[1]
file = open(fname, 'r')

loc = 0
loc_start = 0
loc_end   = 0
name2loc = {}
cur_module = ''
for line in file:
    if not line.strip(): # don't count empty line
        continue

    loc += 1
    if 'module' in line:
        loc_end = loc
        if (cur_module != ''):
            name2loc[cur_module] = loc_end - loc_start
        words = line.split()
        cur_module = words[1] # set up next module
        loc_start = loc
else:
    loc_end = loc
    name2loc[cur_module] = loc_end - loc_start


# for name in name2loc:
#     print(name, name2loc[name])

file.close()



file = open(fname, 'r')
tree = Tree()
circuit = ''
cur_module = ''
words = []
for line in file:
    words = line.split()
    if len(words) == 0:
        continue

    if words[0] == 'circuit': # create top
        words = line.split()
        # circuit = 'DUT' + ' (' + words[1]+')'
        circuit = 'DUT'
        tree.create_node(circuit, circuit) 

    if words[0] == 'module':
        words = line.split()
        module = words[1]
        tree.create_node(module + ' (' + str(name2loc[module]) + ')', module, parent = circuit)
        cur_module = module


    if words[0] == 'extmodule':
        words = line.split()
        extmodule = words[1]
        # print(extmodule)
        tree.create_node(extmodule + '(ext)', extmodule, parent = circuit)
        cur_module = extmodule


    if words [0] == 'inst': # @inst, re-assign the parent of inst_module from top to cur_module
        words = line.split()
        inst_name   = words[1]
        inst_module = words[3]
        # print(inst_module)
        # print(cur_module)
        tree.move_node(inst_module, cur_module)

tree.show()
print(tree.depth())

# tree.create_node("top", "top")
# tree.create_node("m0 (loc = 5)", "m0", parent = "top")
# tree.create_node("m1 (loc = 7)", "m1", parent = "top")
# tree.create_node("m2", "m2", parent = "top")
# tree.create_node("m3", "m3", parent = "top")
# tree.create_node("m4", "m4", parent = "top")
# tree.create_node("m5", "m5", parent = "top")
# tree.create_node("m6", "m6", parent = "top")
# tree.create_node("m7", "m7", parent = "top")

# tree.move_node("m7", "m5")
# tree.move_node("m7", "m5")
# tree.move_node("m6", "m5")

# tree.move_node("m2", "m1")
# tree.move_node("m3", "m1")

# tree.move_node("m4", "m2")
# tree.move_node("m5", "m3")
# print(tree.contains("m9"))




