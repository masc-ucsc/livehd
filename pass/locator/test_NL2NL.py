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

def readNetList(fname):

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
    allDPins = []
    isComboCell = False
    currCell = ''
    graph_io = set()
    for data in f.readlines():

        if "module" in data and "(" in data:
            all_io = data.split("(")[1].split(")")[0].strip()
            all_io_list = all_io.split(",")

            for each_io in all_io_list:
                graph_io.add(each_io.strip()) 

        elif 'sky130' in data and 'dfxtp' not in data:
            isComboCell = True
            currCell = data.strip().split(" ")[0]
            continue

        
        elif ';' in data:
            isComboCell = False
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
                # print(dpin)
                # print(graph_io)
                if dpin not in graph_io:
                    allDPins.append(dpin)
        

    return allDPins

netlistFile = sys.argv[1]

nlistData = readNetList(netlistFile+".v")#Get Original File Data

newNetListName = netlistFile  + "_new.v"
writeNetList(newNetListName, nlistData)#Removes multi line comments

createGraphJsonDict()

dpins = getDPins(newNetListName)
# print(dpins)
ceilVal = math.ceil((int(sys.argv[2])/100) * len(dpins)) #sys.argv[2] % dpins will be changed
randomDPins = random.sample(dpins,int(ceilVal))
for eachRDPin in randomDPins:
    eachRDPinNew = eachRDPin + "_changedForEval"
    subprocess.call(["sed -i 's/" + eachRDPin + "/" + eachRDPinNew + "/g' " + newNetListName], shell=True)

# print(cell_dict_new)
# print(nlistData)