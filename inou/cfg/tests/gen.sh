#!/bin/bash
#rm -f *.cfg
#~/pyrope/parser/bin/prp sp_add.prp             > yyy && column -t yyy > xxx && mv xxx sp_add.cfg
#~/pyrope/parser/bin/prp top.prp                > yyy && column -t yyy > xxx && mv xxx top.cfg
#~/pyrope/parser/bin/prp const.prp              > yyy && column -t yyy > xxx && mv xxx const.cfg
~/pyrope/parser/bin/prp sp_if_0.prp            > yyy && column -t yyy > xxx && mv xxx sp_if_0.cfg
~/pyrope/parser/bin/prp sp_if_1.prp            > yyy && column -t yyy > xxx && mv xxx sp_if_1.cfg
~/pyrope/parser/bin/prp sp_if_2.prp            > yyy && column -t yyy > xxx && mv xxx sp_if_2.cfg
~/pyrope/parser/bin/prp sp_if_3.prp            > yyy && column -t yyy > xxx && mv xxx sp_if_3.cfg
~/pyrope/parser/bin/prp nested_if_0.prp        > yyy && column -t yyy > xxx && mv xxx nested_if_0.cfg
~/pyrope/parser/bin/prp nested_if_1.prp        > yyy && column -t yyy > xxx && mv xxx nested_if_1.cfg
~/pyrope/parser/bin/prp nested_if_2.prp        > yyy && column -t yyy > xxx && mv xxx nested_if_2.cfg
~/pyrope/parser/bin/prp nested_if_3.prp        > yyy && column -t yyy > xxx && mv xxx nested_if_3.cfg
rm -f yyy
