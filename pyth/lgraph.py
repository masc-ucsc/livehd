import sys
import os

base_dir = os.path.dirname(sys.argv[0]) or '.'
print('Base directory:', base_dir)
#print(os.listdir(base_dir))

# Insert the package_dir_a directory at the front of the path.
package_dir_a = os.path.join(base_dir, '__main__')
sys.path.insert(0, package_dir_a)

from pyth import lgraph

l = lgraph.open_lgraph("lgdb","0")
p = lgraph.Inou_rand()

thisdict =	{
  "lgdb": "lgdb",
  "seed": 1023,
  "eratio": 1.3,
  "graph_name": "potato"
}

p.set(thisdict)
p.generate()

