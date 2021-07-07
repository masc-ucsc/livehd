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

total_parent_cnt = 0
total_children_cnt = 0
for node in tree.expand_tree(mode=Tree.DEPTH):
    if (tree[node].is_leaf()):
        continue
    total_parent_cnt += 1
    total_children_cnt += len(tree.children(node))

print("total_parents_cnt ", total_parent_cnt)
print("total_children_cnt", total_children_cnt)
print("average children per module = ", total_children_cnt/total_parent_cnt)
