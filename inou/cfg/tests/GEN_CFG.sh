#!/bin/bash
rm -f *.cfg
~/pyrope/parser/bin/prp sp_add.prp          > yyy && column -t yyy > xxx && mv xxx sp_add.cfg
~/pyrope/parser/bin/prp top.prp             > yyy && column -t yyy > xxx && mv xxx top.cfg
~/pyrope/parser/bin/prp constant.prp        > yyy && column -t yyy > xxx && mv xxx constant.cfg
~/pyrope/parser/bin/prp sp_if_0.prp         > yyy && column -t yyy > xxx && mv xxx sp_if_0.cfg     && rm -f yyy  
~/pyrope/parser/bin/prp nested_if_0.prp     > yyy && column -t yyy > xxx && mv xxx nested_if_0.cfg && rm -f yyy
~/pyrope/parser/bin/prp nested_if_1.prp     > yyy && column -t yyy > xxx && mv xxx nested_if_1.cfg && rm -f yyy
~/pyrope/parser/bin/prp nested_if_2.prp     > yyy && column -t yyy > xxx && mv xxx nested_if_2.cfg && rm -f yyy
~/pyrope/parser/bin/prp nested_if_3.prp     > yyy && column -t yyy > xxx && mv xxx nested_if_3.cfg && rm -f yyy
~/pyrope/parser/bin/prp if_elif_else.prp    > yyy && column -t yyy > xxx && mv xxx if_elif_else.cfg && rm -f yyy

~/pyrope/parser/bin/prp sp_add_nb.prp       > yyy && column -t yyy > xxx && mv xxx sp_add_nb.cfg
~/pyrope/parser/bin/prp top_nb.prp          > yyy && column -t yyy > xxx && mv xxx top_nb.cfg
~/pyrope/parser/bin/prp constant_nb.prp     > yyy && column -t yyy > xxx && mv xxx constant_nb.cfg
~/pyrope/parser/bin/prp sp_if_0_nb.prp      > yyy && column -t yyy > xxx && mv xxx sp_if_0_nb.cfg     && rm -f yyy  
~/pyrope/parser/bin/prp nested_if_0_nb.prp  > yyy && column -t yyy > xxx && mv xxx nested_if_0_nb.cfg && rm -f yyy
~/pyrope/parser/bin/prp nested_if_1_nb.prp  > yyy && column -t yyy > xxx && mv xxx nested_if_1_nb.cfg && rm -f yyy
~/pyrope/parser/bin/prp nested_if_2_nb.prp  > yyy && column -t yyy > xxx && mv xxx nested_if_2_nb.cfg && rm -f yyy
~/pyrope/parser/bin/prp nested_if_3_nb.prp  > yyy && column -t yyy > xxx && mv xxx nested_if_3_nb.cfg && rm -f yyy
~/pyrope/parser/bin/prp if_elif_else_nb.prp > yyy && column -t yyy > xxx && mv xxx if_elif_else_nb.cfg && rm -f yyy

cp top.cfg        top_ooo.cfg
cp top_nb.cfg     top_ooo_nb.cfg


