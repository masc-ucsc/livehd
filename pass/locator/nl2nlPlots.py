#!/usr/bin/env python3

import os
import json

def log_accuracy_data(path):

    log_acc_dict = {}
    accuracy_index = [0,0,0,0]
    graph_flatten_file = open('/soe/sgarg3/code_gen/new_dir/livehd/pass/locator/tests/dummy/SingleCycleCPU_flattened_100.for_graphs.log','r')
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
                    val = my_data[1]
                    # key, val = my_data
                    key = key.strip().replace('_changedForEval','')
                    val = val.strip()
                    val = val.split(' ')
                    val_set = sorted(set(val))
                    
                    if str(val_set) not in log_acc_dict:
                        count += 1
                        log_acc_dict[str(val_set)] = set()
                    
                    log_acc_dict[str(val_set)].add(key)


        
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
            k_set = set(k_list)
            v_set = set(val_list)
            inter = k_set.intersection(v_set)
            
            if len(inter) > 0:
                
                accuracy_index[2] += (1 * len(k_list))
            else:
                print('----')
                print(k_list)
                print(val_list)
                print('----')
                accuracy_index[3] += (1 * len(k_list)) 
    # print(log_acc_dict)
    print(count)
    print(len(log_acc_dict.keys()))
    print("'Full Match', 'Alias Match', 'Partial Match', 'No Match'")
    print(accuracy_index)

def main():
    path = '/soe/sgarg3/code_gen/new_dir/livehd/pass/locator/tests/dummy/SingleCycleCPU_flattened_100.for_graphs.log'

    log_accuracy_data(path)
if __name__ == "__main__":
    main()