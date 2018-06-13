import sys
import os

base_dir = os.path.dirname(sys.argv[0]) or '.'
package_dir_a = os.path.join(base_dir, '__main__')
sys.path.insert(0, package_dir_a)

from pyth import lgraph

opts =	{
  "lgdb": "lgdb",
  "seed": 8,
  "size": 4,
  "eratio": 1.3,
  "graph_name": "potato"
}

p = lgraph.Inou_rand(opts).generate()
print("ID = {}\n", p[0].lg_id())

t1 = lgraph.find_lgraph("lgdb","potato") # Should succedd, but now it fails
t2 = lgraph.find_lgraph("lgdb","weird")

assert t1.lg_id() == p[0].lg_id()
assert t2 is None

