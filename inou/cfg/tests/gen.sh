#!/bin/bash
rm -f pt*.cfg
~/pyrope/parser/bin/prp pt1.prp  > yyy && column -t yyy > xxx && mv xxx pt1.cfg
~/pyrope/parser/bin/prp pt2.prp  > yyy && column -t yyy > xxx && mv xxx pt2.cfg
~/pyrope/parser/bin/prp pt3.prp  > yyy && column -t yyy > xxx && mv xxx pt3.cfg
~/pyrope/parser/bin/prp pt4.prp  > yyy && column -t yyy > xxx && mv xxx pt4.cfg
~/pyrope/parser/bin/prp pt5.prp  > yyy && column -t yyy > xxx && mv xxx pt5.cfg
~/pyrope/parser/bin/prp pt6.prp  > yyy && column -t yyy > xxx && mv xxx pt6.cfg
~/pyrope/parser/bin/prp pt7.prp  > yyy && column -t yyy > xxx && mv xxx pt7.cfg
~/pyrope/parser/bin/prp pt8.prp  > yyy && column -t yyy > xxx && mv xxx pt8.cfg
~/pyrope/parser/bin/prp pt9.prp  > yyy && column -t yyy > xxx && mv xxx pt9.cfg
~/pyrope/parser/bin/prp pt10.prp > yyy && column -t yyy > xxx && mv xxx pt10.cfg
~/pyrope/parser/bin/prp pt11.prp > yyy && column -t yyy > xxx && mv xxx pt11.cfg
~/pyrope/parser/bin/prp pt12.prp > yyy && column -t yyy > xxx && mv xxx pt12.cfg
~/pyrope/parser/bin/prp pt13.prp > yyy && column -t yyy > xxx && mv xxx pt13.cfg
~/pyrope/parser/bin/prp pt14.prp > yyy && column -t yyy > xxx && mv xxx pt14.cfg
~/pyrope/parser/bin/prp simple_add.prp > yyy && column -t yyy > xxx && mv xxx simple_add.cfg
~/pyrope/parser/bin/prp top.prp > yyy && column -t yyy > xxx && mv xxx top.cfg
~/pyrope/parser/bin/prp pipeline.prp > yyy && column -t yyy > xxx && mv xxx pipeline.cfg


rm -f yyy
