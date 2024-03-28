#!/bin/bash

# NOTE:
# This script works for flattened netlists only. 
# Also, This works for sky130 NLs only.
# The python script will need to be adjusted for multiple modules in single file.

CXX=clang++-14 CC=clang-14 bazel build -c opt //...
ret_val=$?
if [ $ret_val -ne 0 ]; then
  echo "\n--------Compilation failed!--------\n\n"
  exit $ret_val
fi

# IMPORTANT: check these values before every run:
SRCLOCATION=/home/sgarg3/livehd/pass/locator/tests
DESTLOCATION=/home/sgarg3/livehd/pass/locator/tests/dummy_WoFlopChange_28march2024 #change line 80/81 in test_NL2NL.py for flops/no_flops as well
COMB_ONLY=true
NOISE_PERCENTAGE=(0 20 40 60 80 90 95 100)

export LIVEHD_THREADS=1

if [ ! -d "$DESTLOCATION" ]; then
  mkdir $DESTLOCATION
fi

#starting the file for plots:
echo "import matplotlib.pyplot as plt" > ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
echo "import matplotlib.pyplot as plt" > ${DESTLOCATION}/nl2nl_time_flop_plot_data.py

for FILENAME in RocketTile_yosys_DT2 RocketTile_netlist_wired  #RocketTile_yosys_DT2 SingleCycleCPU_yosysFlat PipelinedCPU_yosysFlat_DT30p MaxPeriodFibonacciLFSR PipelinedCPU_netlist_wired SingleCycleCPU_netlist_wired RocketTile_netlist_wired RocketTile_netlist_p1_again
do
  if [ ${FILENAME} == "MaxPeriodFibonacciLFSR" ]
    then 
    rm -r lgdb/
    ./bazel-bin/main/lgshell "inou.liberty files:sky130.lib"
    MODULE_NAME=MaxPeriodFibonacciLFSR    
    COMPLR=yosys
  elif [ ${FILENAME} == "SingleCycleCPU_yosysFlat" ]
    then
    rm -r lgdb/ 
    ./bazel-bin/main/lgshell "inou.liberty files:sky130_fd_sc_hd__ff_100C_1v95.lib"
    MODULE_NAME=SingleCycleCPU 
    COMPLR=yosys
  elif [ ${FILENAME} == "PipelinedCPU_yosysFlat_DT30p" ]
    then
    rm -r lgdb/ 
    ./bazel-bin/main/lgshell "inou.liberty files:sky130_fd_sc_hd__ff_100C_1v95.lib"
    MODULE_NAME=PipelinedCPU  
    COMPLR=yosys
  elif [ ${FILENAME} == "RocketTile_yosys_DT2" ]
    then
    rm -r lgdb/
    ./bazel-bin/main/lgshell "inou.liberty files:sky130_fd_sc_hd__ff_100C_1v95.lib"
    MODULE_NAME=RocketTile
    COMPLR=yosys
  elif [ ${FILENAME} == "SingleCycleCPU_netlist_wired" ]
    then
    rm -r lgdb/ 
    ./bazel-bin/main/lgshell "inou.liberty files:saed32hvt_ff1p16vn40c.lib
    inou.liberty files:saed32lvt_ff1p16vn40c.lib
    inou.liberty files:saed32rvt_ff1p16vn40c.lib
    inou.liberty files:gtech_lib.lib"
    MODULE_NAME=SingleCycleCPU 
    COMPLR=dc
  elif [ ${FILENAME} == "PipelinedCPU_netlist_wired" ]
    then
    rm -r lgdb/ 
    ./bazel-bin/main/lgshell "inou.liberty files:saed32hvt_ff1p16vn40c.lib
    inou.liberty files:saed32lvt_ff1p16vn40c.lib
    inou.liberty files:saed32rvt_ff1p16vn40c.lib
    inou.liberty files:gtech_lib.lib"
    MODULE_NAME=PipelinedCPU  
    COMPLR=dc
  elif [ ${FILENAME} == "RocketTile_netlist_wired" ]
    then
    rm -r lgdb/
    ./bazel-bin/main/lgshell "inou.liberty files:saed32hvt_ff1p16vn40c.lib
    inou.liberty files:saed32lvt_ff1p16vn40c.lib
    inou.liberty files:saed32rvt_ff1p16vn40c.lib
    inou.liberty files:gtech_lib.lib"
    MODULE_NAME=RocketTile
    COMPLR=dc
  elif [ ${FILENAME} == "RocketTile_netlist_p1_again" ]
    then
    rm -r lgdb/
    ./bazel-bin/main/lgshell "inou.liberty files:saed32hvt_ff1p16vn40c.lib
    inou.liberty files:saed32lvt_ff1p16vn40c.lib
    inou.liberty files:saed32rvt_ff1p16vn40c.lib
    inou.liberty files:gtech_lib.lib"
    MODULE_NAME=RocketTile
    COMPLR=dcUnflatten
  fi

  printf "\ny${MODULE_NAME}${COMPLR} = [" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  printf "\ny${MODULE_NAME}${COMPLR} = [" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py

  #NOT NEEDED?# perc_flop_change_arr=()
  #NOT NEEDED?# num_flop_calculated_arr=()
  #NOT NEEDED?# num_cells_calculated_arr=()
  num_default_matches_arr=()
  for PERCENTAGE_CHANGE in ${NOISE_PERCENTAGE[@]}
  do
    echo "************${PERCENTAGE_CHANGE}**********"
    if [ ${PERCENTAGE_CHANGE} != 0 ];
      then
      printf "," >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
      printf "," >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
    fi

    if [ ! -f "${SRCLOCATION}/${FILENAME}.v" ];
      then
        echo "Could not find ${SRCLOCATION}/${FILENAME}.v"
        exit 1
    fi
    #NOT NEEDED?# sed 's/(\*.*\*)//g' ${SRCLOCATION}/${FILENAME}.v > ${DESTLOCATION}/${FILENAME}_1.v #remove (*...*)
    #NOT NEEDED?# sed -i 's/\/\*.*\*\///g' ${DESTLOCATION}/${FILENAME}_1.v #remove /*...*/
    #NOT NEEDED?# sed -i 's/\/\/.*//g' ${DESTLOCATION}/${FILENAME}_1.v #remove //...

    #This is to incorporate noise in the file:
    #if [ ${COMPLR} == "dc" ]; then
    #NOT NEEDED?# sed -z -i 's/\([^;]\)\n/\1 /g' ${DESTLOCATION}/${FILENAME}_1.v #putting it all in single line
    #fi
    #NOT NEEDED?# flops_changed_percentage=`python3 pass/locator/test_NL2NL.py ${DESTLOCATION}/${FILENAME}_1 ${PERCENTAGE_CHANGE} ${FILENAME}`  # creates _1_new.v file
    #NOT NEEDED?# perc_flop_change_arr+=( "${flops_changed_percentage}" )
    #NOT NEEDED?# echo "perc_flop_change_arr: ${perc_flop_change_arr[@]}"
    #sed -z 's/\([^;]\)\n/\1 /g' ${DESTLOCATION}/${FILENAME}_1_new.v > ${DESTLOCATION}/${FILENAME}_1_new.v
    #NOT NEEDED?# flops_changed_count=`grep "dfxtp.*\.Q.*changedForEval" ${DESTLOCATION}/${FILENAME}_1_new.v | wc -l`
    #NOT NEEDED?# total_cells_changed_count=`grep "sky130.*changedForEval.[^,]*;" ${DESTLOCATION}/${FILENAME}_1_new.v | wc -l`
    #NOT NEEDED?# total_flops=`grep "dfxtp" ${DESTLOCATION}/${FILENAME}_1_new.v | wc -l`
    #NOT NEEDED?# total_cells_in_NL=`grep "sky130" ${SRCLOCATION}/${FILENAME}.v | wc -l`
    #NOT NEEDED?# if [ ${COMPLR} == "dc" ]
    #NOT NEEDED?# then
    #NOT NEEDED?#   flops_changed_count=`grep "DFF.*\.Q.*changedForEval" ${DESTLOCATION}/${FILENAME}_1_new.v | wc -l`
    #NOT NEEDED?#   total_cells_changed_count=`grep "_.VT.*changedForEval.[^,]*;" ${DESTLOCATION}/${FILENAME}_1_new.v | wc -l`
    #NOT NEEDED?#   total_flops=`grep "DFF" ${DESTLOCATION}/${FILENAME}_1_new.v | wc -l`
    #NOT NEEDED?#   total_cells_in_NL=`grep "_.VT" ${SRCLOCATION}/${FILENAME}.v | wc -l`
    #NOT NEEDED?# fi
    #NOT NEEDED?# echo "flops_changed_count: ${flops_changed_count}"
    #NOT NEEDED?# flops_changed_perc=0.0
    #NOT NEEDED?# total_cells_changed_perc=0.0
    #NOT NEEDED?# flops_changed_perc=`python -c "print((${flops_changed_count}/${total_flops})*100.0)"`
    #NOT NEEDED?# total_cells_changed_perc=`python -c "print((${total_cells_changed_count}/${total_cells_in_NL})*100.0)"`
    #NOT NEEDED?# num_flop_calculated_arr+=( "${flops_changed_perc}" )
    #NOT NEEDED?# num_cells_calculated_arr+=( "${total_cells_changed_perc}" )
    #NOT NEEDED?# echo "num_flop_calculated_arr: ${num_flop_calculated_arr[@]}"
    #NOT NEEDED?# echo "num_cells_calculated_arr: ${num_cells_calculated_arr[@]}"
    #NOT NEEDED?# echo "total_flops=${total_flops}"
    #NOT NEEDED?# mv ${DESTLOCATION}/${FILENAME}_1.v ${DESTLOCATION}/${FILENAME}_1_${PERCENTAGE_CHANGE}.v 

    if [[ ${FILENAME} == *"RocketTile"* ]]
    then
      sed 's/module RocketTile/module RocketTile_changedForEval/g' ${SRCLOCATION}/${FILENAME}.v > ${DESTLOCATION}/${FILENAME}.v
    elif [[ ${FILENAME} == *"Pipeline"* ]]
    then
      sed 's/module PipelinedCPU/module PipelinedCPU_changedForEval/g' ${SRCLOCATION}/${FILENAME}.v > ${DESTLOCATION}/${FILENAME}.v
    elif [[ ${FILENAME} == *"Singl"* ]]
    then
      sed 's/module SingleCycleCPU/module SingleCycleCPU_changedForEval/g' ${SRCLOCATION}/${FILENAME}.v > ${DESTLOCATION}/${FILENAME}.v
    fi

    if [ ! -f "${DESTLOCATION}/${FILENAME}.v" ]; then
      echo "Could not find ${DESTLOCATION}/${FILENAME}.v"
      exit 1
    fi
    ORIG_NL=${MODULE_NAME}
    NEW_NL=${ORIG_NL}_changedForEval

    echo "FIRST: ${SRCLOCATION}/${FILENAME}.v -- ${ORIG_NL}"
    echo "SECOND: ${DESTLOCATION}/${FILENAME}.v -- ${NEW_NL}"
    echo "LOG: ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}.log"

    #if [ ${FILENAME} == "MaxPeriodFibonacciLFSR" ]
    #then 
    #  echo ""
    #  echo "Running for MaxPeriodFibonacciLFSR:"
    #  ./bazel-bin/main/lgshell " inou.yosys.tolg files:${SRCLOCATION}/${FILENAME}.v |> pass.bitwidth |> pass.cprop |> pass.bitwidth
    #  inou.yosys.tolg files:${DESTLOCATION}/${FILENAME}_1_new.v |> pass.bitwidth |> pass.cprop |> pass.bitwidth
    #  lgraph.open name:${ORIG_NL} |> lgraph.open name:${NEW_NL} |> inou.graphviz.from odir:tmp_1 |> inou.traverse_lg LGorig:${ORIG_NL} LGsynth:${NEW_NL} 
    #  " > ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}.log   
    #el
    if [ ${FILENAME} == "PipelinedCPU_yosysFlat_DT30p" ]; then
      echo -e "\n Running for PipelinedCPU_yosysFlat_DT30p:"
    elif [ ${FILENAME} == "SingleCycleCPU_yosysFlat" ]; then
      echo -e "\n Running for SingleCycleCPU_yosysFlat:"
    elif [ ${FILENAME} == "RocketTile_yosys_DT2" ]; then
      echo -e "\n Running for RocketTile_yosys_DT2:"
    elif [ ${FILENAME} == "PipelinedCPU_netlist_wired" ]; then
      echo -e "\n Running for PipelinedCPU_netlist_wired:"
    elif [ ${FILENAME} == "SingleCycleCPU_netlist_wired" ]; then
      echo -e "\n Running for SingleCycleCPU_netlist_wired:"
    elif [ ${FILENAME} == "RocketTile_netlist_wired" ]; then
      echo -e "\n Running for RocketTile_netlist_wired:"
    elif [ ${FILENAME} == "RocketTile_netlist_p1_again" ]; then
      echo -e "\n Running for RocketTile_netlist_p1_again:"
    fi
    ./bazel-bin/main/lgshell "
    inou.yosys.tolg top:${NEW_NL} files:${DESTLOCATION}/${FILENAME}.v |> pass.randomize_dpins srcLG:${NEW_NL} comb_only:${COMB_ONLY} noise_perc:${PERCENTAGE_CHANGE} 
    inou.yosys.tolg top:${ORIG_NL} files:${SRCLOCATION}/${FILENAME}.v
    lgraph.open name:${ORIG_NL} |> lgraph.open name:${NEW_NL} |> lgraph.dump hier:true |> inou.traverse_lg LGorig:${ORIG_NL} LGsynth:${NEW_NL} 
    " > ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}.log
    echo "--done matching--"

    python3 pass/locator/nl2nlPlots.py ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}.log  ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
    python3 pass/locator/nl2nlTimePlot.py ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}.log ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
    # rm ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}_time.log 
    #mv ${DESTLOCATION}/${FILENAME}_1_new.v ${DESTLOCATION}/${FILENAME}_1_new_${PERCENTAGE_CHANGE}.v
    num_default_matches=`grep "IN_FUNC: netpin_to_ori" ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}.log | grep -oE '[0-9]+' | head -1`
    num_default_matches_arr+=( "${num_default_matches}" )
    echo "num_default_matches_arr: ${num_default_matches_arr[@]}"
  done

  echo "]" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py

  #num of default matches
  printf "dm${MODULE_NAME}${COMPLR} = [" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  i=1
  printf ${num_default_matches_arr} >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  while [ $i -lt ${#num_default_matches_arr[@]} ]
  do
    printf "," >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
    printf ${num_default_matches_arr[$i]} >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
    i=`expr $i + 1`
  done
  echo "]" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py

  #NOT NEEDED?# printf "x${MODULE_NAME}${COMPLR} = [" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  #NOT NEEDED?# i=1
  #NOT NEEDED?# printf ${perc_flop_change_arr} >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  #NOT NEEDED?# while [ $i -lt ${#perc_flop_change_arr[@]} ]
  #NOT NEEDED?# do
  #NOT NEEDED?#   printf "," >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  #NOT NEEDED?#   printf ${perc_flop_change_arr[$i]} >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  #NOT NEEDED?#   i=`expr $i + 1`
  #NOT NEEDED?# done
  #NOT NEEDED?# echo "]" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py

  #num_flop_calculated_arr
  #NOT NEEDED?# printf "z${MODULE_NAME}${COMPLR} = [" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  #NOT NEEDED?# i=1
  #NOT NEEDED?# printf ${num_flop_calculated_arr} >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  #NOT NEEDED?# while [ $i -lt ${#num_flop_calculated_arr[@]} ]
  #NOT NEEDED?# do
  #NOT NEEDED?#   printf "," >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  #NOT NEEDED?#   printf ${num_flop_calculated_arr[$i]} >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  #NOT NEEDED?#   i=`expr $i + 1`
 #NOT NEEDED?#  done
  #NOT NEEDED?# echo "]" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py

  #num_cells_calculated_arr
  #NOT NEEDED?# printf "w${MODULE_NAME}${COMPLR} = [" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  #NOT NEEDED?# i=1
  #NOT NEEDED?# printf ${num_cells_calculated_arr} >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  #NOT NEEDED?# while [ $i -lt ${#num_cells_calculated_arr[@]} ]
  #NOT NEEDED?# do
  #NOT NEEDED?#   printf "," >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  #NOT NEEDED?#   printf ${num_cells_calculated_arr[$i]} >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  #NOT NEEDED?#   i=`expr $i + 1`
  #NOT NEEDED?# done
  #NOT NEEDED?# echo "]" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  #noise percentage for plotting
  printf "np = [" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  i=1
  printf ${NOISE_PERCENTAGE} >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  while [ $i -lt ${#NOISE_PERCENTAGE[@]} ]
  do
    printf "," >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
    printf ${NOISE_PERCENTAGE[$i]} >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
    i=`expr $i + 1`
  done
  echo "]" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py

  #NOT NEEDED?# echo "plt.plot(w${MODULE_NAME}${COMPLR}, y${MODULE_NAME}${COMPLR}, label = \"${MODULE_NAME}${COMPLR}\", linestyle='dashed', marker='o')" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py

  echo "plt.plot(np, y${MODULE_NAME}${COMPLR}, label = \"${MODULE_NAME}${COMPLR}\", linestyle='dashed', marker='o')" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py


  echo "]" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  #num of default matches
  printf "dm${MODULE_NAME}${COMPLR} = [" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  i=1
  printf ${num_default_matches_arr} >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  while [ $i -lt ${#num_default_matches_arr[@]} ]
  do
    printf "," >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
    printf ${num_default_matches_arr[$i]} >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
    i=`expr $i + 1`
  done
  echo "]" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py

  #NOT NEEDED?# printf "x${MODULE_NAME}${COMPLR} = [" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  #NOT NEEDED?# i=1
  #NOT NEEDED?# printf ${perc_flop_change_arr} >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  #NOT NEEDED?# while [ $i -lt ${#perc_flop_change_arr[@]} ]
  #NOT NEEDED?# do
  #NOT NEEDED?#   printf "," >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  #NOT NEEDED?#   printf ${perc_flop_change_arr[$i]} >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  #NOT NEEDED?#   i=`expr $i + 1`
  #NOT NEEDED?# done
  #NOT NEEDED?# echo "]" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  #num_flop_calculated_arr
  #NOT NEEDED?# printf "z${MODULE_NAME}${COMPLR} = [" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  #NOT NEEDED?# i=1
  #NOT NEEDED?# printf ${num_flop_calculated_arr} >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  #NOT NEEDED?# while [ $i -lt ${#num_flop_calculated_arr[@]} ]
  #NOT NEEDED?# do
  #NOT NEEDED?#   printf "," >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  #NOT NEEDED?#   printf ${num_flop_calculated_arr[$i]} >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  #NOT NEEDED?#   i=`expr $i + 1`
  #NOT NEEDED?# done
  #NOT NEEDED?# echo "]" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  #num_cells_calculated_arr
  #NOT NEEDED?# printf "w${MODULE_NAME}${COMPLR} = [" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  #NOT NEEDED?# i=1
  #NOT NEEDED?# printf ${num_cells_calculated_arr} >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  #NOT NEEDED?# while [ $i -lt ${#num_cells_calculated_arr[@]} ]
  #NOT NEEDED?# do
  #NOT NEEDED?#   printf "," >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  #NOT NEEDED?#   printf ${num_cells_calculated_arr[$i]} >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  #NOT NEEDED?#   i=`expr $i + 1`
  #NOT NEEDED?# done
  #NOT NEEDED?# echo "]" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  #NOT NEEDED?# echo "plt.plot(w${MODULE_NAME}${COMPLR}, y${MODULE_NAME}${COMPLR}, label = \"${MODULE_NAME}${COMPLR}\", linestyle='dashed', marker='o')" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  #noise percentage for plotting
  printf "np = [" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  i=1
  printf ${NOISE_PERCENTAGE} >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  while [ $i -lt ${#NOISE_PERCENTAGE[@]} ]
  do
    printf "," >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
    printf ${NOISE_PERCENTAGE[$i]} >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
    i=`expr $i + 1`
  done
  echo "]" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py

  echo "plt.plot(np, y${MODULE_NAME}${COMPLR}, label = \"${MODULE_NAME}${COMPLR}\", linestyle='dashed', marker='o')" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py

done

echo "plt.xlabel('Noise (%)')" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
echo "plt.ylabel(' Accuracy (%)')" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
echo "plt.title('Accuracy graph for netlist to netlist analysis. ')" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
echo "plt.xticks(range(0, 101, 10))" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
#echo "plt.yticks(range(10, 101, 10))" >>  ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
echo "plt.legend()" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
echo "# function to show the plot" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
echo "plt.savefig(\"${DESTLOCATION}/nl2nl_acc_flop_plot_data.pdf\", format=\"pdf\", bbox_inches=\"tight\")" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
echo "plt.savefig(\"${DESTLOCATION}/nl2nl_acc_flop_plot_data.svg\", format=\"svg\", bbox_inches=\"tight\")" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
echo "plt.show()" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py

echo "plt.xlabel(' Noise (%)')" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
echo "plt.ylabel('Time (s)')" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
echo "plt.title('Performance graph for netlist to netlist analysis.')" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
echo "plt.xticks(range(0, 101, 10))" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
#echo "plt.yticks(range(10, 101, 10))" >>  ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
echo "plt.legend()" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
echo "# function to show the plot" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
echo "plt.savefig(\"${DESTLOCATION}/nl2nl_time_flop_plot_data.pdf\", format=\"pdf\", bbox_inches=\"tight\")" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
echo "plt.savefig(\"${DESTLOCATION}/nl2nl_time_flop_plot_data.svg\", format=\"svg\", bbox_inches=\"tight\")" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
echo "plt.show()" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py

python3 ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
python3 ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py


# #Annotate each marker with x and y values
# for i in range(len(xData)):
#     plt.annotate(f'({xData[i]}, {yData[i]})', (xData[i], yData[i]), textcoords="offset points", xytext=(0,10), ha='center')

