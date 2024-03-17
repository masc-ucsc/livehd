#!/usr/bin/env python3
import os
import json
import sys
import subprocess

def main():

    n = len(sys.argv)

    if n != 3:
        print("Enter run.log and the output file path")
        exit(1)

    logs_path = sys.argv[1] 
    time_flop_plot_data_collection_file = sys.argv[2] 

    grep_command = 'grep "TOTAL_TIME_OF_ALGO" ' + logs_path
    grepped_data = subprocess.run(grep_command, shell=True, capture_output=True)
    grepped_data = grepped_data.stdout.decode().strip().split('\n')
    #assuming only 1 line had this greeped string in each file
    total_time_for_this_run = float(grepped_data[0].split(':')[1].strip().replace('s',''))


    file_time_flop_plot_data = open(time_flop_plot_data_collection_file, "a")
    file_time_flop_plot_data.write(str(total_time_for_this_run))
    file_time_flop_plot_data.close
   

if __name__ == "__main__":
    main()

