#!/usr/bin/env python3

import os
import re
import random

import insertDontTouch

SynthFile = open(os.getcwd() + '/../bazel_rules_hdl_test/PipelinedCPU_flatted.v','r')
                 #/pass/locator/tests/PipelinedCPU.v','r')
synthVarSet = set()
for synthLines in SynthFile.readlines():
    varList = re.split(',| |\(|\)|\.', synthLines)
    for items in varList:
        if items != "":
            
            synthVarSet.add(items.strip())
    # print(synthLines.split(" "))
# print(synthVarSet)
# print(len(synthVarSet))

chiselAssignments = open(os.getcwd() + '/eval_files_tmp/all_grepped_for_pipelinedCPU1.log','r')

optimizedEntriesDict = {}

for chiselAssignment in chiselAssignments.readlines():
    chiselLHS = []
    # print(chiselAssignment)
    if ":=" in chiselAssignment:
        chiselLHS.append(chiselAssignment.split(": ")[1].split(":=")[0].strip())
    else:
        chiselList = re.split('===|=/=',chiselAssignment)
        if len(chiselList) > 1:
            chiselList = chiselList[:-1]
        
        print(chiselAssignment)
        print(chiselList)
    for eachChiselLHS in chiselLHS:

        if eachChiselLHS.replace(".","_")  not in synthVarSet:
            # print(eachChiselLHS)
            fName = chiselAssignment.split(":",1)[0].strip() #fname is file name 
            fData = chiselAssignment.split(":",1)[1].strip() #line number: assignment op with LHS and RHS

            if fName not in optimizedEntriesDict: 
                optimizedEntriesDict[fName] = set()
            
            optimizedEntriesDict[fName].add(fData)
            # print(chiselAssignment)
# print(optimizedEntriesDict)
exit(0)
for key, val in optimizedEntriesDict.items():
    print(key)
#     roundVal = round(0.02 * len(val))
    print(random.sample(list(val),1))
#     print(random.sample(val,int(roundVal)))

##To insert DT on a random line per file in dictionary:-
# for scala_fname, val in optimizedEntriesDict.items():
#     # print(scala_fname)
#     
#     samples = random.sample(val,1)
#     for eachSample in samples:
#     
#         insertDontTouch.processFile(str(scala_fname), int(eachSample.split(":")[0].strip()), "dontTouch("+eachSample.split(":")[1].strip()+")")
# 

