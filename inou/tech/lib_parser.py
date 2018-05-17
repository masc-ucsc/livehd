#!/bin/python2.7

'''This is lib_parser.py by Jim Wang from GitHub
    Check timing_parser.py for usage'''

import re
import os
from fnmatch import fnmatch

################################################################
# lib_file_t
################################################################

class lib_file_t:
    """ top class for lib file parser. 3 steps:
        1. read_file: >> (line_t)ls_all_line
        2. parse_syntax: >> (lib_obj_t) top_obj
        3. parse_semantic: >> (library_t) (cell_t) (bus_t) (pin_t) (timing_t)
    """
    def __init__(self, lib_fpath):
        assert os.path.isfile(lib_fpath), 'lib file (%s) does not exist' % lib_fpath
        self.lib_fpath = lib_fpath
        print '** lib_parser: begin to parse %s' % lib_fpath

        print '** lib_parser: 1. read_file'
        self._read_file()
        print '** lib_parser: 2. parse_syntax'
        self._parse_syntax()
        print '** lib_parser: 3. parse_semantic'
        self._parse_semantic()

        print '** lib_parser: end parsing %s' % lib_fpath

    def _read_file(self):
        'read LIB file >> (line_t) ls_all_line ' # {{{
        self.ls_all_line = list()

        f = open(self.lib_fpath, 'r')
        is_continue = False
        is_comment = False
        for (i, string) in enumerate(f):
            line_num = i+1
            line = line_t(string, line_num, is_comment=is_comment)
            line.is_continue = is_continue
            if (hasattr(line, 'is_comment_begin') and line.is_comment_begin):
                is_comment = True
            elif (hasattr(line, 'is_comment_end') and line.is_comment_end):
                is_comment = False
            self.ls_all_line.append(line)

            # deal with continue lines: concat to last !is_continue line
            if (line.is_continue):
                j = i-1
                while(self.ls_all_line[j].is_continue):
                    j -= 1
                self.ls_all_line[j].string += line.string
                line.string = ''
                line.is_empty = True

            # if this line has "\" on tail, then the next line continues to this one
            if (line.has_continue):
                is_continue = True
            else:
                is_continue = False
        f.close()

        # end of read }}}

    def _parse_syntax(self):
        '>> (lib_obj_t) self.top_obj' # {{{
        ls_pat = list()
        ls_pat.append(dt_pat['multi_begin'])
        ls_pat.append(dt_pat['multi_end'])
        ls_pat.append(dt_pat['single_colon'])
        ls_pat.append(dt_pat['single_paren'])

        # find line match >> line.syntax_type/ls_syntax_match
        for line in self.ls_all_line:

            line.syntax_type = ''
            line.ls_syntax_match = None

            if line.is_empty:
                continue

            # matching
            for pat in ls_pat:
                m = pat.match(line.string)
                if (m != None):
                    line.syntax_type = pat.name
                    line.ls_syntax_match = m
                    break

            # if no match
            assert (line.syntax_type != ''), 'syntax error @ line %d (%s)' % (line.num, line.string)

        # create (lib_obj_t) top_obj: recursion start point
        for line in self.ls_all_line:
            if not line.is_empty:
                break

        self.top_obj = lib_obj_t(line.num, self.ls_all_line)

        # end of parse_syntax }}}

    def _parse_semantic(self):
        '>> (library_t) self.library' # {{{
        # create semantic instance: recursion start point
        self.library = library_t(self.top_obj)

        # end of parse_semantic }}}

    def write_back(self, new_lib_fpath):
        # {{{
        f = open(new_lib_fpath, 'w')
        for line in self.ls_all_line:
            f.write(str(line))
        f.close()
        # }}}

#===========================================================
# pat_t
#===========================================================
class pat_t:
    'compiled pattern object and their type names'
    def __init__(self, name, re_str):
        # {{{
        self.name = name
        self.re_str = re_str
        self.re = re.compile(re_str, re.IGNORECASE)
        self.sub = self.re.sub
        # }}}

    def is_found(self, line):
        '>> True/False' # {{{
        m = self.re.search(line)
        if (m == None):
            return False
        else:
            return True
        # }}}

    def match(self, line):
        '>> None or ls_matched_string' # {{{
        m = self.re.match(line)
        if (m == None):
            return None
        else:
            ls = list()
            for s in m.groups():
                ls.append(s.strip('"'))
            return ls
        # }}}

#===========================================================
# line_t
#===========================================================

pat_comment = pat_t('comment', r'\/\*.*\*\/')
pat_comment_begin = pat_t('comment_begin', r'\/\*.*$')
pat_comment_end = pat_t('comment_begin', r'^.*\*\/')

class line_t:
    """ content of each liberty file line
        1. comment/space/\n removed
        2. continuous line combined
        3. open for future extension
    """
    def __init__(self, raw_string, num, is_comment=False):
        # self.string/is_empty/has_continue/is_continue {{{
        self.raw_string = raw_string # original string with \n
        self.num = num # line number, = idx+1

        # remove \n
        string = raw_string.strip()

        # remove comment
        self.is_comment_begin = False
        self.is_comment_end = False
        if (pat_comment.is_found(string)):
            # normal comment line
            string = pat_comment.re.sub('', string)
        elif (pat_comment_begin.is_found(string)):
            # multi-line comment begin
            assert (is_comment == False), 'Error: found multi-line comment begin while last multi-line comment has not ended @ line %d' % self.num
            self.is_comment_begin = True
            string = pat_comment_begin.re.sub('', string).strip()
        elif (pat_comment_end.is_found(string)):
            # multi-line comment end
            assert (is_comment == True), 'Error: found multi-line comment end without comment begin @ line %d' % self.num
            self.is_comment_end = True
            string = pat_comment_end.re.sub('', string).strip()
        elif (is_comment):
            string = ''


#         # lowercase
#         string = string.lower()

        # remove unwanted space, which doesn't include space between " "
        is_unwanted_space = True
        new_string = ''
        for c in string:
            if (c == '"'):
                is_unwanted_space = not is_unwanted_space
            if ((c == ' ') and is_unwanted_space):
                continue
            new_string += c

        self.string = new_string # content string without comment, space, \n

        if (self.string == ''):
            self.is_empty = True
        else:
            self.is_empty = False

        self.has_continue = False
        if (self.string.endswith('\\')):
            self.has_continue = True
            self.string.strip('\\')
        self.is_continue = False
        # }}}

    def append(self, new_raw_string):
        '>> ls_append_string. add one new_raw_string after current line' # {{{
        if not hasattr(self, 'ls_append_string'):
            self.ls_append_string = list()
        self.ls_append_string.append(new_raw_string)
        # }}}

    def replace(self, new_raw_string):
        '>> replace_string. replace current line with given new_raw_string' # {{{
#         self.replace_string = new_raw_string
        self.raw_string = new_raw_string
        # }}}

    def remove(self):
        'remove current line' # {{{
        self.is_removed = True
        # }}}

    def __str__(self):
        'for write back' # {{{
        if hasattr(self, 'is_removed') and self.is_removed:
            s = ''
        else:
            s = self.raw_string

#         if hasattr(self, 'replace_string'):
#             s = self.replace_string
        if hasattr(self, 'ls_append_string'):
            if (len(self.ls_append_string) == 1):
                s += self.ls_append_string[0]
            else:
                for append in self.ls_append_string:
                    s += append
        return s
        # }}}

#===========================================================
# lib_obj_t
#===========================================================
dt_pat = dict()
# the begin line of multi-line object
dt_pat['multi_begin'] = pat_t('multi_begin', r'^(.+)\((.*)\){$')
# the end line of multi-line object
dt_pat['multi_end'] = pat_t('multi_end', '^}$')
# single line object defined with colon
dt_pat['single_colon'] = pat_t('single_colon', r'^(.+):(.+);$')
# single line object defined with paren
dt_pat['single_paren'] = pat_t('single_paren', r'^(.+)\((.+)\);$')

class lib_obj_t:
    """ object of LIB
        1. begin_line_num
        2. end_line_num
        3. ls_all_line/ls_line
        4. ls_sub_obj
    """
    def __init__(self, begin_line_num, ls_all_line):
        # begin_line_nu/end_line_num, ls_all_line, ls_sub_obj {{{
        self.begin_line_num = begin_line_num
        self.end_line_num = 0
        self.ls_all_line = ls_all_line # pointer to the ls_all_line, shared by all lib_obj in the same library

        self.find_end_line()
        self.ls_line = self.ls_all_line[self.begin_line_num-1:self.end_line_num]

        self.ls_sub_obj = list() # list of sub lib_obj_t
        self.find_sub_obj() # recursively find sub_obj
        # }}}

    def find_end_line(self):
        # {{{
        # single-line object
        line = self.ls_all_line[self.begin_line_num-1]
        if (line.syntax_type in ['single_colon', 'single_paren']):
            self.end_line_num = line.num
            return self.end_line_num

        # multi-line object
        level = 0
        for line in self.ls_all_line[self.begin_line_num-1:]:
            if (line.syntax_type == 'multi_begin'):
                level += 1
            elif (line.syntax_type == 'multi_end'):
                level -= 1
                if (level == 0):
                    self.end_line_num = line.num
                    return self.end_line_num

        # no found
        raise RuntimeError, 'cannot find end of lib_obj (%s:%s) begin @ %d' % (self.obj_type, self.name, self.begin_line_num)
        # }}}

    def find_sub_obj(self):
        # {{{
        i = self.begin_line_num + 1 - 1 # start from the line after the begin_line
        j = self.end_line_num - 1 - 1 # end at the line before the end_line
        while (i <= j):
            line = self.ls_all_line[i]
            if (line.syntax_type in ['multi_begin', 'single_colon', 'single_paren']):
                sub_obj = lib_obj_t(line.num, self.ls_all_line)
                self.ls_sub_obj.append(sub_obj)
                i = sub_obj.end_line_num - 1
            i += 1
        # }}}

    def get_sub_obj(self, lib_obj_name):
        '>> ls_lib_obj' # {{{
        ls_lib_obj = list()
        for lib_obj in self.ls_sub_obj:
            if (lib_obj.begin_line_num != lib_obj.end_line_num):
                line = lib_obj.ls_all_line[lib_obj.begin_line_num-1]
                assert (line.syntax_type == 'multi_begin'), 'Syntax error: line %d (%s)' % (line.num, line.string)
                if (fnmatch(line.ls_syntax_match[0], lib_obj_name)):
                    ls_lib_obj.append(lib_obj)
        return ls_lib_obj
        # }}}


#===========================================================
# instances: library/cell/bus/pin/timing/table
#===========================================================

class param_t:
    """ liberty parameter
        - name = string
        - value = string
        - lib_obj = lib_obj_t
        - line = line_t
    """
    def __init__(self, lib_obj):
        # {{{
        self.lib_obj = lib_obj
        self.line = lib_obj.ls_all_line[lib_obj.begin_line_num-1]
        assert (self.line.syntax_type in ['single_colon', 'single_paren']) and (len(self.line.ls_syntax_match) == 2), 'Syntax error: line %d (%s)' % (self.line.num, self.line.string)
        self.name = self.line.ls_syntax_match[0]
        self.value = self.line.ls_syntax_match[1]
        # }}}

    def __str__(self):
        return '%s=%s' % (self.name, self.value)

    def __eq__(self, other):
        return (self.name == other.name) and (self.value == other.value)

class inst_t:
    """ liberty instance
        - type = string (library|cell|bus|pin|timing|table|operating_conditions|lu_table_template)
        - name = string
        - ls_param = list of param_t
        - dt_param[name] = param_t
        - ls_{sub_inst_type} = list of sub inst_t
        - ls_sub_inst = a full list of all inst_t
        - dt_{sub_inst_type}[sub_inst_name] = sub inst_t
        - dt_sub_inst[sub_inst_type][sub_inst_name] = sub inst_t
        - lib_obj = lib_obj_t
        - title_line = line_t
    """
    def __init__(self, lib_obj):
        # {{{
        self.lib_obj = lib_obj
        assert (self.lib_obj.begin_line_num != self.lib_obj.end_line_num)

        # type
        self.type = ''

        # name
        self.title_line = self.lib_obj.ls_all_line[self.lib_obj.begin_line_num-1]
        self.name = self.title_line.ls_syntax_match[1]

        # parameter
        self.ls_param = list()
        self.dt_param = dict()
        for lib_obj in self.lib_obj.ls_sub_obj:
            if (lib_obj.begin_line_num == lib_obj.end_line_num):
                p = param_t(lib_obj)
                self.ls_param.append(p)
                self.dt_param[p.name] = p
        self.ls_param.sort(key=lambda x: x.name)

        # sub instance: run by each instance type
        self.ls_sub_inst = list()
        self.dt_sub_inst = dict()
        # }}}

    def init_sub_inst(self, sub_inst_type, sub_inst_pat=''):
        'use dynamic code to construct sub-instance members: ls_*, dt_*, ls_sub_inst, dt_sub_inst' # {{{

        class_name = sub_inst_type + '_t'

        if (sub_inst_pat == ''):
            sub_inst_pat = sub_inst_type

        self.dt_sub_inst[sub_inst_type] = dict()
        self.__dict__['ls_' + sub_inst_type] = list()
        self.__dict__['dt_' + sub_inst_type] = dict()
        for lib_obj in self.lib_obj.get_sub_obj(sub_inst_pat):
            n = eval(class_name + '(lib_obj)') # dynamic construct a new inst_t instance
            n.type = sub_inst_type
            # ls_*
            self.__dict__['ls_' + sub_inst_type].append(n)
            # dt_*
            #assert n.name not in self.__dict__['dt_' + sub_inst_type].keys()
            self.__dict__['dt_' + sub_inst_type][n.name] = n
            # dt_sub_inst
            self.dt_sub_inst[sub_inst_type][n.name] = n
        self.__dict__['ls_' + sub_inst_type].sort(key=lambda x: x.name)
        # ls_sub_inst
        self.ls_sub_inst += self.__dict__['ls_' + sub_inst_type]
        # }}}

    def __str__(self):
        # {{{
        return '%s(%s)' % (self.type, self.name)
        # }}}

    def __eq__(self, other):
        # {{{
        return (str(self) == str(other))
        # }}}

#-------------------------------------------------------

class library_t(inst_t):
    def __init__(self, lib_obj):
        # {{{
        inst_t.__init__(self, lib_obj)

        self.init_sub_inst('operating_conditions')
        self.init_sub_inst('lu_table_template')
        self.init_sub_inst('cell')
        # }}}

#-------------------------------------------------------

class lu_table_template_t(inst_t):
    def __init__(self, lib_obj):
        # {{{
        inst_t.__init__(self, lib_obj)
        # }}}

    def __eq__(self, other):
        # {{{
        if (self.name != other.name):
            return False
        if (len(self.ls_param) != len(other.ls_param)):
            return False
        for (self_param, other_param) in zip(self.ls_param, self.other_param):
            if (self_param != other_param):
                return False
        # }}}

#-------------------------------------------------------

class operating_conditions_t(inst_t):
    'name'
    def __init__(self, lib_obj):
        # {{{
        inst_t.__init__(self, lib_obj)
        # }}}

    def __eq__(self, other):
        # {{{
        return (self.name == other.name) and (float(self.dt_param['voltage']) == float(other.dt_param['voltage'])) and (float(self.dt_param['temperature']) == float(other.dt_param['temperature']))
        # }}}

#-------------------------------------------------------

class cell_t(inst_t):
    'name/ls_bus/ls_pin/ls_all_pin/dt_all_pin'
    def __init__(self, lib_obj):
        # {{{
        inst_t.__init__(self, lib_obj)

        self.init_sub_inst('bus')
        self.init_sub_inst('pin')

        self.ls_all_pin = list()
        self.ls_all_pin += self.ls_pin
        for bus in self.ls_bus:
            self.ls_all_pin += bus.ls_pin

        self.dt_all_pin = dict()
        for pin in self.ls_all_pin:
            self.dt_all_pin[pin.name] = pin
        # }}}

#-------------------------------------------------------

class bus_t(inst_t):
    'name/ls_pin'
    def __init__(self, lib_obj):
        # {{{
        inst_t.__init__(self, lib_obj)

        self.init_sub_inst('pin')
        self.init_sub_inst('timing')
        self.ls_timing.sort(key=lambda x: x.name)
        # }}}

#-------------------------------------------------------

class pin_t(inst_t):
    'name/ls_timing'
    def __init__(self, lib_obj):
        # {{{
        inst_t.__init__(self, lib_obj)
        self.init_sub_inst('timing')
        self.ls_timing.sort(key=lambda x: x.name)
        # }}}

#-------------------------------------------------------

class timing_t(inst_t):
    'name/ls_table'
    def __init__(self, lib_obj):
        # {{{
        inst_t.__init__(self, lib_obj)

        # distinguish name
	self.name = self.dt_param['related_pin'].value + '.'#+ self.dt_param['timing_type'].value
        if ('timing_sense' in self.dt_param.keys()):
            self.name += '.' + self.dt_param['timing_sense'].value

        self.init_sub_inst('table', sub_inst_pat='*')
        # }}}

#-------------------------------------------------------

class table_t(inst_t):
    'type/template'
    def __init__(self, lib_obj):
        # {{{
        inst_t.__init__(self, lib_obj)

        title_line = self.lib_obj.ls_all_line[lib_obj.begin_line_num-1]
        self.type = title_line.ls_syntax_match[0]
        self.template = title_line.ls_syntax_match[1]

        self.name = self.type
        # }}}

    def __str__(self):
        # {{{
        return '%s(%s)' % (self.type, self.template)
        # }}}

    def __eq__(self, other):
        # {{{
        if (str(self) != str(other)):
            return False
        if (self.ls_param['index_1'] != other.ls_param['index_1']):
            return False
        if ('index_2' in self.ls_param.keys()):
            if (self.ls_param['index_2'] != other.ls_param['index_2']):
                return False
        return True
        # }}}


################################################################

if __name__ == '__main__':
    lib_test = lib_file_t('./simple_Early.lib')
#     lib_test_err = lib_file_t('./test/test_error.lib')
#     lib_full = lib_file_t('./test/full.lib')
    lib_test.write_back('./new.lib')
    print 'done'
