#!/bin/bash
rm -f *.cfg
~/pyrope/parser/bin/prp logic_bitwise_op.prp > yyy && column -t yyy > xxx && rm -f yyy && mv xxx logic_bitwise_op.cfg
~/pyrope/parser/bin/prp sp_punch.prp         > yyy && column -t yyy > xxx && rm -f yyy && mv xxx sp_punch.cfg
~/pyrope/parser/bin/prp sp_and.prp           > yyy && column -t yyy > xxx && rm -f yyy && mv xxx sp_and.cfg
~/pyrope/parser/bin/prp for.prp              > yyy && column -t yyy > xxx && rm -f yyy && mv xxx for.cfg
~/pyrope/parser/bin/prp rca.prp              > yyy && column -t yyy > xxx && rm -f yyy && mv xxx rca.cfg
~/pyrope/parser/bin/prp fa.prp               > yyy && column -t yyy > xxx && rm -f yyy && mv xxx fa.cfg
~/pyrope/parser/bin/prp top_inline_add.prp   > yyy && column -t yyy > xxx && rm -f yyy && mv xxx top_inline_add.cfg
~/pyrope/parser/bin/prp sp_add.prp           > yyy && column -t yyy > xxx && rm -f yyy && mv xxx sp_add.cfg
~/pyrope/parser/bin/prp sp_assign.prp        > yyy && column -t yyy > xxx && rm -f yyy && mv xxx sp_assign.cfg
~/pyrope/parser/bin/prp sp_assign2.prp       > yyy && column -t yyy > xxx && rm -f yyy && mv xxx sp_assign2.cfg
~/pyrope/parser/bin/prp top.prp              > yyy && column -t yyy > xxx && rm -f yyy && mv xxx top.cfg
~/pyrope/parser/bin/prp constant_pos.prp     > yyy && column -t yyy > xxx && rm -f yyy && mv xxx constant_pos.cfg
~/pyrope/parser/bin/prp constant_neg.prp     > yyy && column -t yyy > xxx && rm -f yyy && mv xxx constant_neg.cfg
~/pyrope/parser/bin/prp sp_if_0.prp          > yyy && column -t yyy > xxx && rm -f yyy && mv xxx sp_if_0.cfg       
~/pyrope/parser/bin/prp sp_if_1.prp          > yyy && column -t yyy > xxx && rm -f yyy && mv xxx sp_if_1.cfg       
~/pyrope/parser/bin/prp nested_if_0.prp      > yyy && column -t yyy > xxx && rm -f yyy && mv xxx nested_if_0.cfg 
~/pyrope/parser/bin/prp nested_if_1.prp      > yyy && column -t yyy > xxx && rm -f yyy && mv xxx nested_if_1.cfg 
~/pyrope/parser/bin/prp nested_if_2.prp      > yyy && column -t yyy > xxx && rm -f yyy && mv xxx nested_if_2.cfg 
~/pyrope/parser/bin/prp nested_if_3.prp      > yyy && column -t yyy > xxx && rm -f yyy && mv xxx nested_if_3.cfg 
~/pyrope/parser/bin/prp if_elif_else.prp     > yyy && column -t yyy > xxx && rm -f yyy && mv xxx if_elif_else.cfg 

cp top.cfg        top_ooo.cfg

rm -f yyy

