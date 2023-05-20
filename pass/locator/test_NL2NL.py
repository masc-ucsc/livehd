#!/usr/bin/env python3

import os
import re
import random
import json
import sys
import math
import subprocess

n = len(sys.argv)

if n != 3:
    print("Enter netlist file name and noise percentage value.")
    exit(1)

cell_dict = {}
flop_set = set()

def readNetList(fname):
    # to remove multi line comments
    f = open(fname,'r')
    readData = []
    commentFlag = False
    for data in f.readlines():
        if '/*' in data:
            commentFlag = True
        elif '*/' in data:
            commentFlag = False
            continue
        
        if not commentFlag:
            readData.append(data)
    
    return readData

def writeNetList(fname, rData):

    f = open( fname, 'w+')

    
    for items in rData:   
        f.write(items)

def createGraphJsonDict():

    graph_json_file = open(os.getcwd() + '/lgdb/graph_library.json','r')

    lgdb_data = json.load(graph_json_file)

    allCells = lgdb_data['Lgraph']
    

    for eachCell in allCells:

        for eachPin in eachCell['io_pins']:
            if eachPin['dir'] == 'out':

                if eachCell['name'] not in cell_dict:
                    cell_dict[eachCell['name']] = set()
                cell_dict[eachCell['name']].add(eachPin['name'])     
    
def getDPins(fname):
    f = open(fname,'r')
    allDPins = set()
    isComboCell = False
    currCell = ''
    graph_io = set()
    
    isFlopCell = False
    for data in f.readlines():

        if "module" in data and "(" in data:
            all_io = data.split("(")[1].split(")")[0].strip()
            all_io_list = all_io.split(",")

            for each_io in all_io_list:
                graph_io.add(each_io.strip()) 

        # elif 'sky130' in data and 'dfxtp' not in data: #do not change the flops (dummy_withoutFlopsChanged)
        elif 'sky130' in data: #change the flops as well (dummy_withFlopsChanged)
            isComboCell = True
            if "dfxtp" in data:
                isFlopCell = True
            currCell = data.strip().split(" ")[0]
            continue
        elif ';' in data:
            isComboCell = False
            isFlopCell = False
            continue
        if isComboCell:
            pin = data.split("(")[0]
            pin = pin.replace(".","").strip()

            if pin in cell_dict[currCell]:

                # print(currCell)
                # print(pin)
                dpin = data.split("(")[1].split(")")[0].strip()
                if "[" in dpin:
                    dpin = dpin.split("[")[0]
                if "\\" in dpin:
                    dpin = dpin.replace("\\","")
                # print(dpin)
                # print(graph_io)
                if dpin not in graph_io:
                    allDPins.add(dpin.strip())
                if isFlopCell:
                    flop_set.add(dpin.strip())

    return list(allDPins)

netlistFile = sys.argv[1]

nlistData = readNetList(netlistFile+".v")#Get Original File Data

newNetListName = netlistFile  + "_new.v"
writeNetList(newNetListName, nlistData)#Removes multi line comments

createGraphJsonDict()

dpins = getDPins(newNetListName)
# print(dpins)
ceilVal = math.ceil((int(sys.argv[2])/100) * len(dpins)) #sys.argv[2] % dpins will be changed
randomDPins = random.sample(dpins,int(ceilVal))
flop_select_count = 0
for eachRDPin in randomDPins:
    if eachRDPin in flop_set:
        flop_select_count += 1
    eachRDPinNew = eachRDPin + "_changedForEval"
    # print("sed -i 's/" + eachRDPin + "/" + eachRDPinNew + "/g' " + newNetListName)
    subprocess.call(["sed -i 's/" + eachRDPin + "/" + eachRDPinNew + "/g' " + newNetListName], shell=True)

# print("flop_select_count", flop_select_count)
# print("len(flop_set)", len(flop_set))
try:
    # print("flop % = ", (float(flop_select_count)/float(len(flop_set)))*100, "%")
    print((float(flop_select_count)/float(len(flop_set)))*100)
except:
    # print("flop % = ",0)
    print(0)
# print(cell_dict_new)
# print(nlistData)