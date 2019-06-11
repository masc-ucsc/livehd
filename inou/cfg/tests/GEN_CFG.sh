#!/bin/bash
rm -f *.cfg
./prp ast_test.prp         > y && column -t y > x && rm -f y && mv x ast_test.cfg
./prp logic_bitwise_op.prp > y && column -t y > x && rm -f y && mv x logic_bitwise_op.cfg
./prp sp_punch.prp         > y && column -t y > x && rm -f y && mv x sp_punch.cfg
./prp sp_and.prp           > y && column -t y > x && rm -f y && mv x sp_and.cfg
./prp for.prp              > y && column -t y > x && rm -f y && mv x for.cfg
./prp rca.prp              > y && column -t y > x && rm -f y && mv x rca.cfg
./prp fa.prp               > y && column -t y > x && rm -f y && mv x fa.cfg
./prp top_inline_add.prp   > y && column -t y > x && rm -f y && mv x top_inline_add.cfg
./prp sp_add.prp           > y && column -t y > x && rm -f y && mv x sp_add.cfg
./prp sp_assign.prp        > y && column -t y > x && rm -f y && mv x sp_assign.cfg
./prp sp_assign2.prp       > y && column -t y > x && rm -f y && mv x sp_assign2.cfg
./prp top.prp              > y && column -t y > x && rm -f y && mv x top.cfg
./prp constant_pos.prp     > y && column -t y > x && rm -f y && mv x constant_pos.cfg
./prp constant_neg.prp     > y && column -t y > x && rm -f y && mv x constant_neg.cfg
./prp sp_if_0.prp          > y && column -t y > x && rm -f y && mv x sp_if_0.cfg       
./prp sp_if_1.prp          > y && column -t y > x && rm -f y && mv x sp_if_1.cfg       
./prp nested_if_0.prp      > y && column -t y > x && rm -f y && mv x nested_if_0.cfg 
./prp nested_if_1.prp      > y && column -t y > x && rm -f y && mv x nested_if_1.cfg 
./prp nested_if_2.prp      > y && column -t y > x && rm -f y && mv x nested_if_2.cfg 
./prp nested_if_3.prp      > y && column -t y > x && rm -f y && mv x nested_if_3.cfg 
./prp if_elif_else.prp     > y && column -t y > x && rm -f y && mv x if_elif_else.cfg 

cp top.cfg        top_ooo.cfg

rm -f yyy

