#!/bin/bash
rm -f *.cfg
~/pyrope/parser/bin/prp sp_add.prp             > yyy && column -t yyy > xxx && mv xxx sp_add.cfg
~/pyrope/parser/bin/prp top.prp                > yyy && column -t yyy > xxx && mv xxx top.cfg
~/pyrope/parser/bin/prp pipeline.prp           > yyy && column -t yyy > xxx && mv xxx pipeline.cfg
~/pyrope/parser/bin/prp const.prp              > yyy && column -t yyy > xxx && mv xxx const.cfg
~/pyrope/parser/bin/prp sp_if.prp              > yyy && column -t yyy > xxx && mv xxx sp_if.cfg
rm -f yyy
