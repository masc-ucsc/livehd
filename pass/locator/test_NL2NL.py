#!/usr/bin/env python3

import os
import re
import random
import json
import sys
import math
import subprocess

n = len(sys.argv)

if n != 4:
    print("Enter netlist file name and noise percentage value, and design name.")
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

def read_by_delimiter(file_path, delimiter=';'):
    with open(file_path, 'r') as file:
        buffer = ''
        while True:
            chunk = file.read(1024)  # Read in chunks of 1KB
            if not chunk:  # End of file
                if buffer:
                    yield buffer
                break
            buffer += chunk
            while delimiter in buffer:
                position = buffer.find(delimiter)
                yield buffer[:position]
                buffer = buffer[position + len(delimiter):]

    
def getDPins(fname, designName):
    allDPins = set()
    isComboCell = False
    currCell = ''
    graph_io = set()

    module_string = "module "
    if designName == "RocketTile_yosys_DT2" or designName == "RocketTile_netlist_wired":
        module_string = "module RocketTile"
    if "netlist" in designName:
        module_string = "module "+designName.split('_')[0]

    isFlopCell = False
    top_module_found = False
    for data in read_by_delimiter(fname, delimiter=";"):
        #start reading only if we are in top module
        isComboCell = False
        isFlopCell = False
        if top_module_found == False:
            if module_string in data:
                top_module_found=True
            else:
                continue
        #lines in top module should be processed:
        if module_string in data and "(" in data:
            all_io = data.split("(")[1].split(")")[0].strip()
            all_io_list = all_io.split(",")

            for each_io in all_io_list:
                graph_io.add(each_io.strip()) 
        elif ('sky130' in data and 'dfxtp' not in data) or ('VT ' in data and 'DFF' not in data): #do not change the flops (dummy_withoutFlopsChanged)
        #elif ('sky130' in data) or ('VT ' in data): #change the flops as well (dummy_withFlopsChanged)
            
            isComboCell = True
            if ("dfxtp" in data) or ("DFF" in data):
                isFlopCell = True
            currCell = data.strip().split(" ")[0]
        if isComboCell:
            pin = data.split(",")[-1]
            pin = pin.split("(")[0].strip()
            pin = pin.replace(".","").strip()
            if pin in cell_dict[currCell]:

                dpin = data.split("(")[-1].split(")")[0].strip()
                if "[" in dpin:
                    dpin = dpin.split("[")[0]
                if "\\" in dpin:
                    dpin = dpin.replace("\\","")
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

designName = sys.argv[3]
dpins = getDPins(newNetListName, designName)
#print(dpins)
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

