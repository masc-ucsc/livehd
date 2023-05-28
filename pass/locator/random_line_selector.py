#!/usr/bin/env python3

import os
import re
import random

import insertDontTouch

'''
what to NOT select from all_grepped_for_pipelinedCPU.log
(whatever is in the netlist?) -- SynthFile
(whatever is in the matching map?) --MatchingMapFile
'''
'''
SynthFile = open(os.getcwd() + '/pass/locator/tests/PipelinedCPU_flattened.v','r') #/../bazel_rules_hdl_test/PipelinedCPU_flatted.v
                 #/pass/locator/tests/PipelinedCPU.v','r')
synthVarSet = set()
for synthLines in SynthFile.readlines():
    varList = re.split(',| |\(|\)|\.', synthLines)
    for items in varList:
        if items != "":
            items1=items.replace(";","") #remove ";" if any
            synthVarSet.add(items1.strip())
    # print(synthLines.split(" "))
# print(synthVarSet)
# print(len(synthVarSet))
'''
MatchingMapFile = open(os.getcwd() + '/eval_files_tmp/default_matching_map.log', 'r')
matchedVarSet = set()
for matchingMapLines in MatchingMapFile.readlines():
    varStr = re.split(",",matchingMapLines)[0]
    if "." in varStr:
        varStr=varStr.split(".")[1]
    if "[" in varStr:
        varStr=varStr.split("[")[0]
    matchedVarSet.add(varStr.strip())
#print(matchedVarSet)



chiselAssignments = open(os.getcwd() + '/eval_files_tmp/all_grepped_for_pipelinedCPU.log','r') #/eval_files_tmp/all_grepped_for_pipelinedCPU1.log

optimizedEntriesDict = {}

for chiselAssignment in chiselAssignments.readlines():
    chiselLHS = []
    #print(chiselAssignment)
    if ":=" in chiselAssignment:
        chiselLHS.append(chiselAssignment.split(": ")[1].split(":=")[0].strip())
    else:
        chiselList = re.split('===|=/=',chiselAssignment)
        if len(chiselList) > 1:
            chiselList = chiselList[:-1]
        
        #print(chiselAssignment)
        #print(chiselList)
    #print(chiselLHS)

    for eachChiselLHS in chiselLHS:

        if eachChiselLHS.replace(".","_")  not in matchedVarSet:
            # print(eachChiselLHS)
            fName = chiselAssignment.split(":",1)[0].strip() #fname is file name 
            fData = chiselAssignment.split(":",1)[1].strip() #line number: assignment op with LHS and RHS

            if fName not in optimizedEntriesDict: 
                optimizedEntriesDict[fName] = set()
            
            if "true.B" not in fData:
                if "false.B" not in fData:
                    optimizedEntriesDict[fName].add(fData)
            # print(chiselAssignment)
# print(optimizedEntriesDict)

for key, val in optimizedEntriesDict.items():
    print(key)
    roundVal = round(0.10 * len(val))
#    print(random.sample(list(val),10))
    rand_list=random.sample(list(val),int(roundVal))
    for entry in val:
        print("        "+entry)
    #print(random.sample(list(val),int(roundVal)))

##To insert DT on a random line per file in dictionary:-
# for scala_fname, val in optimizedEntriesDict.items():
#     # print(scala_fname)
#     
#     samples = random.sample(val,1)
#     for eachSample in samples:
#     
#         insertDontTouch.processFile(str(scala_fname), int(eachSample.split(":")[0].strip()), "dontTouch("+eachSample.split(":")[1].strip()+")")
# 


