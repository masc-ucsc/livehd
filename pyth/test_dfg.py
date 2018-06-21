#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
import sys
import os

base_dir = os.path.dirname(sys.argv[0]) or '.'
package_dir_a = os.path.join(base_dir, '__main__')
sys.path.insert(0, package_dir_a)

from pyth import lgraph

cfg_name = "pt1"
dfg_name = "pt1_dfg"

cfg_opts = {
  "lgdb": "lgdb",
  "graph_name": cfg_name,
  "cfg_in": "../inou/cfg/test/%s.cfg" % cfg_name
}

cfg = lgraph.Inou_cfg(cfg_opts).generate()

dfg_opts = {
  "lgdb": "lgdb",
  "src": cfg_name,
  "graph_name": dfg_name
}

dfg = lgraph.Pass_dfg(dfg_opts).generate()