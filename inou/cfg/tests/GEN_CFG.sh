#!/bin/bash
rm -f *.cfg
./prp lnast_utest.prp          | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x lnast_utest.cfg
./prp logic_bitwise_op.prp     | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x logic_bitwise_op.cfg
./prp sp_punch.prp             | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x sp_punch.cfg
./prp sp_and.prp               | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x sp_and.cfg
./prp for.prp                  | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x for.cfg
./prp rca.prp                  | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x rca.cfg
./prp fa.prp                   | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x fa.cfg
./prp top_inline_add.prp       | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x top_inline_add.cfg
./prp sp_add.prp               | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x sp_add.cfg
./prp sp_assign.prp            | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x sp_assign.cfg
./prp sp_assign2.prp           | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x sp_assign2.cfg
./prp top.prp                  | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x top.cfg
./prp constant_pos.prp         | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x constant_pos.cfg
./prp constant_neg.prp         | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x constant_neg.cfg
./prp sp_if_0.prp              | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x sp_if_0.cfg       
./prp sp_if_1.prp              | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x sp_if_1.cfg       
./prp nested_if_0.prp          | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x nested_if_0.cfg 
./prp nested_if_1.prp          | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x nested_if_1.cfg 
./prp nested_if_2.prp          | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x nested_if_2.cfg 
./prp nested_if_3.prp          | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x nested_if_3.cfg 
./prp if_elif_elif.prp         | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x if_elif_elif.cfg 
./prp if_elif_elif_else.prp    | sort -n -tK -k2 > y && column -t y > x && rm -f y && mv x if_elif_elif_else.cfg 

cp top.cfg        top_ooo.cfg

rm -f yyy

