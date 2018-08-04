#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
import sys
import os
import shutil
import subprocess

#To do 1: the pybind11 outputs are somehow out of order now, have to fix it later.
#To do 2: add some assert for yosys
#To do 3: need to redirect/cp terminal messages to a log file

base_dir = os.path.dirname(sys.argv[0]) or '.'
sys.path.insert(1, base_dir)
print("base_dir: " + base_dir)
package_dir_a = os.path.join(base_dir, '__main__')
sys.path.insert(1, package_dir_a)
print("package_dir_a: " + package_dir_a)

print("test running directory: ", os.getcwd())


shutil.rmtree("lgdb", ignore_errors=True)  # remove previous garbage

try:
  import lgraph

  cfg_name = 'pt_simple_add'
  dfg_name = cfg_name + '_dfg'

  cfg_opts = {
        "lgdb": "lgdb",
        "graph_name": cfg_name,
        "src": "inou/cfg/tests/%s.cfg" % cfg_name
        }

  cfg = lgraph.Inou_cfg(cfg_opts).generate()
  print("===================== cfg pass ======================")
  # sys.stdout.flush()

  dfg_opts = {
        "lgdb": "lgdb",
        "src": cfg_name,
        "graph_name": dfg_name
        }

  dfg = lgraph.Pass_dfg(dfg_opts).generate()

  assert not (dfg is None)
  assert not (dfg[0] is None)
  print("===================== dfg pass ======================")
  # sys.stdout.flush()


  g = lgraph.find_lgraph("lgdb",dfg_name)
  assert dfg[0].lg_id() == g.lg_id()
  g.dump()
  # sys.stdout.flush()
  print("==================== dump pass ======================")


  # Yosys related variables
  # don't forget extra space between command and /path/files
  opt_yosys = subprocess.check_output('which yosys', shell=True).decode('utf-8').strip()
  opt_log_dir = './logs'
  opt_verilog_dir = './verilog'
  opt_hierarchy = ''
  opt_inou_yosys = '../../../inou/yosys/liblgraph_yosys.so' 
  opt_graph_input = dfg_name
  yosys_log = opt_log_dir + '/' + opt_graph_input + '_to_yosys.log' 
  yosys_write_verilog = 'write_verilog ' + opt_graph_input + '_dirty.v; opt -fast; opt_clean -purge; write_verilog ' + opt_graph_input + '.v;' 
  yosys_cmds =  '" dump_yosys ' + opt_hierarchy + '-graph_name ' + opt_graph_input + '; ' + yosys_write_verilog + '"'

  bash_cmds =[ 'rm -f ' + opt_log_dir + "/*.*", 
               'mkdir '  + opt_log_dir,
                opt_yosys + ' -ql ' + yosys_log + ' -m  ' + opt_inou_yosys + ' -p  ' + yosys_cmds,
               'rm -f ' + opt_verilog_dir + "/*.*", 
               'mkdir ' + opt_verilog_dir,
               'mv *.v ' + opt_verilog_dir
                ]

  for cmd in bash_cmds:
      print('Executing bash cmd: ' + cmd)
      subprocess.run(cmd, shell=True)


except:
  print("lgraph raised exception. Test fails")
  assert(False)


