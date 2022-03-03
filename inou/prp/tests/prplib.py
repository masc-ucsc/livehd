#!/usr/bin/env python3

import argparse
import os
import re
import subprocess
import sys
import yaml

class PrpTest:
    """
    Pyrope Test Object
    """
    def __init__(self, prp_file):
        # Set default values
        self.params = {}
        self.params['name']       = os.path.basename(prp_file)
        self.params['files']      = prp_file
        self.params['incdirs']    = os.path.dirname(prp_file)
        self.params['top_module'] = 'top'
        self.params['defines']    = ''
        self.params['type']       = 'parsing'

        # Extract parameters in pyrope file
        try:
            with open(prp_file) as f:
                for line in f:
                    param = re.search(r'^:([a-zA-Z_-]+):\s*(.+)', line)
                    if param == None:
                        continue
                    
                    param_name = param[1]
                    param_value = param[2]
                    
                    self.params[param_name] = param_value
        except Exception as e:
            print('Failed to process "{}"'.format(prp_file))
            sys.exit(1)

        # Post-process
        self.params['files'] = self.params['files'].split()
        self.params['incdirs'] = self.params['incdirs'].split()
        self.params['type'] = self.params['type'].split()

class PrpRunner:
    """
    LiveHD Pyrope Compilation Runner
    """
    config : {}

    def __init__(self, config_file):
        try:
            with open(config_file) as f:
                self.config = yaml.load(f.read(), yaml.Loader)
        except Exception as e:
            print(e)
            print('Failed to process config file "{}"'.format(config_file))
            sys.exit(1)

    def lgshell_parse(self, test):
        lg_cmd = []
        
        lg_cmd.append('inou.prp')
        lg_cmd.append('files:{}'.format(','.join(test.params['files'])))
        
        return lg_cmd

    def lgshell_lgraph(self, test):
        lg_cmd = self.lgshell_parse(test)

        lg_cmd.append('|>')
        lg_cmd.append('pass.lnastopt')

        lg_cmd.append('|>')
        lg_cmd.append('pass.lnast_tolg')

        return lg_cmd

    def lgshell_lg_compile(self, test):
        lg_cmd = self.lgshell_lgraph(test)

        lg_cmd.append('|>')
        lg_cmd.append('pass.cprop')

        lg_cmd.append('|>')
        lg_cmd.append('pass.bitwidth')

        return lg_cmd

    def gen_lgshell_cmd(self, test, mode):
        gen_lg_cmd = {
            'parsing' : self.lgshell_parse,
            'lgraph'  : self.lgshell_lgraph,
            'compile' : self.lgshell_lg_compile
        }

        cmd = []
        cmd.append(self.config['lgshell']['bin'])
        cmd.append(' '.join(gen_lg_cmd[mode](test)))

        return cmd

    def run(self, tmp_dir, test: PrpTest):

        for mode in test.params['type']:
            cmd = []
            
            if mode == 'simulation':
                pass
            else:
                cmd = self.gen_lgshell_cmd(test, mode)

            proc = subprocess.Popen(
                cmd,
                cwd=tmp_dir,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT
            )
            
            try:
                log, _ = proc.communicate()
                rc = proc.returncode
            except:
                proc.kill()

            if rc == 0:
                print('{} - {} - success'.format(test.params['name'], mode))
            else:
                print('{} - {} - failed'.format(test.params['name'], mode))
                print(log.decode('utf-8', 'ignore'))

        return rc
