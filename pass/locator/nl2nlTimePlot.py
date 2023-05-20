#!/usr/bin/env python3
import os
import json
import sys
def getPercFromRepo(path):
    perc_file = open(path,'r')
    my_perc = 0.0
    for data in perc_file.readlines():
        if "Traverse_lg::travers" not in data:
            continue

        else:
            my_perc = float(data.split('%')[0].split('--')[1])
            break
    return my_perc

def getTimeFromRepo(path):
    time_file = open(path,'r')
    
    count = 0
    my_time = ''
    for data in time_file.readlines():
        if count == 0:
            count += 1
            continue
        else:
            my_time = data.split('system ')[1].split('elapsed')[0].strip()
            break
    return my_time

def main():

    n = len(sys.argv)

    if n != 4:
        print("Enter time.log, perf.log and the output file path")
        exit(1)

    tpath = sys.argv[1] #'/soe/sgarg3/code_gen/new_dir/livehd/pass/locator/tests/dummy/MaxPeriodFibonacciLFSR_100.log' #SingleCycleCPU_flattened_100.for_graphs.log'
    time = getTimeFromRepo(tpath)
    full_time = float(time.split(':')[0]) * 60 + float(time.split(':')[1])
    print(full_time)
    ppath = sys.argv[2] 
    time_flop_plot_data_collection_file=sys.argv[3]
    try:
        percent_fromPerf = getPercFromRepo(ppath)
        print(percent_fromPerf)

        file_time_flop_plot_data = open(time_flop_plot_data_collection_file, "a")
        file_time_flop_plot_data.write(str(full_time * (percent_fromPerf/100)))
        file_time_flop_plot_data.close
    except:
        
        file_time_flop_plot_data = open(time_flop_plot_data_collection_file, "a")
        file_time_flop_plot_data.write(str(full_time* (1/100)))
        file_time_flop_plot_data.close

   

if __name__ == "__main__":
    main()