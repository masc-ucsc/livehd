#!/usr/bin/env python3

import os
import json
import sys


def log_accuracy_data(log_file_path):

    log_acc_dict = {}
    accuracy_index = [0,0,0,0]
    graph_flatten_file = open(log_file_path,'r')
    pass_flag = True
    count = 0
    for data in graph_flatten_file.readlines():
        # print(data)
        
        if pass_flag:
            if "matching map" in data:
                pass_flag = False
        else:
            if data == "":
                continue
            else:
                
                my_data = data.split(':::')
                if len(my_data) > 1:
                    
                    key = my_data[0]
                    val = my_data[1].strip()

                    if val:
                        
                        count += 1
                        # key, val = my_data
                        key = key.strip().replace('_changedForEval','')
                        val = val.strip()
                        val = val.split(' ')
                        val_set = sorted(set(val))
                        
                        if key in val_set:

                            if str(val_set) not in log_acc_dict:
                                
                                log_acc_dict[str(val_set)] = set()
                            
                            log_acc_dict[str(val_set)].add(key)
                        else:
                            #print("no matches:")
                            #print(my_data) #:print no matches
                            #print("--")
                            accuracy_index[3] += 1


    
    for k, v in log_acc_dict.items():
        val_list = sorted(v)
        k = k[1::]
        k = k[:-1]
        k = k.replace("'",'')
        k = k.replace(' ','')
        
        k_list = k.split(',')
        
        if k_list == val_list:
            if len(val_list) > 1:
                
                accuracy_index[1] += (len(k_list) * 1)
            else:
                
                accuracy_index[0] += 1
        else:
            # print('hi')
            k_set = set(k_list)
            v_set = set(val_list)
            inter = k_set.intersection(v_set)
            
            if len(inter) > 0:
                # accuracy_index[2] += (1 * len(k_list))
                continue
            else:
                #print no matches:
                # print('------------')
                # print(k_list)
                # print('*')
                # print(val_list)
                # print('------------')
                accuracy_index[3] += (1 * len(val_list)) 
    # print(log_acc_dict)
    accuracy_index[2] = count - (accuracy_index[0]+accuracy_index[1]+accuracy_index[3])
    #print(count)
    # print(len(log_acc_dict.keys()))
    print("'Full Match', 'Alias Match', 'Partial Match', 'No Match'")
    print(accuracy_index)

    #to get the plots:
    accuracy_percentage = ((accuracy_index[0]+accuracy_index[1]+accuracy_index[2])/count)*100
    print("acc%:", accuracy_percentage)
    return accuracy_percentage

def main():

    n = len(sys.argv)

    if n != 3:
        print("Enter log_file_path to obtain the matching map and noise percentage value to serve as x-val.")
        exit(1)

    log_file_path = sys.argv[1] #'/soe/sgarg3/code_gen/new_dir/livehd/pass/locator/tests/dummy/MaxPeriodFibonacciLFSR_100.log' #SingleCycleCPU_flattened_100.for_graphs.log'
    accuracy_flop_plot_data_collection_file = sys.argv[2]

    x_val = log_accuracy_data(log_file_path)

    file_acc_flop_plot_data = open(accuracy_flop_plot_data_collection_file, "a")
    file_acc_flop_plot_data.write(str(x_val))
    file_acc_flop_plot_data.close



if __name__ == "__main__":
    main()
