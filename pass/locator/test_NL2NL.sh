#!/bin/bash

# NOTE:
# This script works for flattened netlists only. 
# The python script will need to be adjusted for multiple modules in single file.

#CONSTANT ENTITIES:
# #bbmdbg:
# CXX=clang++ CC=clang bazel build -c dbg //main:lgshell
# bbm with opt:
# CXX=clang++ CC=clang bazel build --define=profiling=1 -c opt //main:lgshell
# currentl;y working bbm with opt:
#CXX=g++-11 CC=gcc-11 bazel build -c opt //main:all
CXX=clang++-14 CC=clang-14 bazel build -c opt //...
ret_val=$?
if [ $ret_val -ne 0 ]; then
  echo "\n--------Compilation failed!--------\n\n"
  exit $ret_val
fi
SRCLOCATION=/home/sgarg3/livehd/pass/locator/tests
DESTLOCATION=/home/sgarg3/livehd/pass/locator/tests/dummy_SCdino_FlopChange_8nov2023 #change line 80/81 in test_NL2NL.py for flops/no_flops as well
export LIVEHD_THREADS=1

if [ ! -d "$DESTLOCATION" ]; then
  mkdir $DESTLOCATION
fi

#starting the file for plots:
echo "import matplotlib.pyplot as plt" > ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
echo "import matplotlib.pyplot as plt" > ${DESTLOCATION}/nl2nl_time_flop_plot_data.py

for FILENAME in SingleCycleCPU_flattened #RocketTile_yosys_DT2 SingleCycleCPU_flattened PipelinedCPU_flattened MaxPeriodFibonacciLFSR 
do
  if [ ${FILENAME} == "MaxPeriodFibonacciLFSR" ]
    then 
    rm -r lgdb/
    ./bazel-bin/main/lgshell "inou.liberty files:sky130.lib"
    MODULE_NAME=MaxPeriodFibonacciLFSR    
  elif [ ${FILENAME} == "SingleCycleCPU_flattened" ]
    then
    rm -r lgdb/ 
    ./bazel-bin/main/lgshell "inou.liberty files:sky130_fd_sc_hd__ff_100C_1v95.lib"
    MODULE_NAME=SingleCycleCPU 
  elif [ ${FILENAME} == "PipelinedCPU_flattened" ]
    then
    rm -r lgdb/ 
    ./bazel-bin/main/lgshell "inou.liberty files:sky130_fd_sc_hd__ff_100C_1v95.lib"
    MODULE_NAME=PipelinedCPU  
  elif [ ${FILENAME} == "RocketTile_yosys_DT2" ]
    then
    rm -r lgdb/
    ./bazel-bin/main/lgshell "inou.liberty files:sky130_fd_sc_hd__ff_100C_1v95.lib"
    MODULE_NAME=RocketTile
  fi

  printf "\ny${MODULE_NAME} = [" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  printf "\ny${MODULE_NAME} = [" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py

  perc_flop_change_arr=()
  num_flop_calculated_arr=()
  num_cells_calculated_arr=()
  for PERCENTAGE_CHANGE in 0 20 40 60 80 100
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
    sed 's/(\*.*\*)//g' ${SRCLOCATION}/${FILENAME}.v > ${DESTLOCATION}/${FILENAME}_1.v #remove (*...*)
    sed -i 's/\/\*.*\*\///g' ${DESTLOCATION}/${FILENAME}_1.v #remove /*...*/
    sed -i 's/\/\/.*//g' ${DESTLOCATION}/${FILENAME}_1.v #remove //...

    #This is to incorporate noise in the file:
    flops_changed_percentage=`python3 pass/locator/test_NL2NL.py ${DESTLOCATION}/${FILENAME}_1 ${PERCENTAGE_CHANGE} ${FILENAME}`
    # python3 pass/locator/test_NL2NL.py ${DESTLOCATION}/${FILENAME}_1 ${PERCENTAGE_CHANGE}
    perc_flop_change_arr+=( "${flops_changed_percentage}" )
    echo "perc_flop_change_arr: ${perc_flop_change_arr[@]}"

    sed -z 's/\([^;]\)\n/\1 /g' ${DESTLOCATION}/${FILENAME}_1_new.v > ${DESTLOCATION}/${FILENAME}_1_new_fileChangedForFlopCountOnly.v
    flops_changed_count=`grep "dfxtp.*\.Q.*changedForEval" ${DESTLOCATION}/${FILENAME}_1_new_fileChangedForFlopCountOnly.v | wc -l`
    total_cells_changed_count=`grep "sky130.*changedForEval.[^,]*;" ${DESTLOCATION}/${FILENAME}_1_new_fileChangedForFlopCountOnly.v | wc -l`
    echo "flops_changed_count: ${flops_changed_count}"
    flops_changed_perc=0.0
    total_cells_changed_perc=0.0
    if [ ${FILENAME} == "MaxPeriodFibonacciLFSR" ]
    then 
      flops_changed_perc=`python -c "print((${flops_changed_count}/3)*100.0)"`
      total_cells_changed_perc=`python -c "print((${total_cells_changed_count}/16)*100.0)"`
    elif [ ${FILENAME} == "SingleCycleCPU_flattened" ]
    then
      flops_changed_perc=`python -c "print((${flops_changed_count}/1056)*100.0)"`
      total_cells_changed_perc=`python -c "print((${total_cells_changed_count}/8371)*100.0)"`
    elif [ ${FILENAME} == "PipelinedCPU_flattened" ]
    then
      flops_changed_perc=`python -c "print((${flops_changed_count}/2810)*100.0)"`
      total_cells_changed_perc=`python -c "print((${total_cells_changed_count}/19241)*100.0)"`
    elif [ ${FILENAME} == "RocketTile_yosys_DT2" ]
    then
      flops_changed_perc=`python -c "print((${flops_changed_count}/15254)*100.0)"`
      total_cells_changed_perc=`python -c "print((${total_cells_changed_count}/116550)*100.0)"`
    fi
    num_flop_calculated_arr+=( "${flops_changed_perc}" )
    num_cells_calculated_arr+=( "${total_cells_changed_perc}" )
    echo "num_flop_calculated_arr: ${num_flop_calculated_arr[@]}"
    echo "num_cells_calculated_arr: ${num_cells_calculated_arr[@]}"
    total_flops=`grep "dfxtp" ${DESTLOCATION}/${FILENAME}_1_new_fileChangedForFlopCountOnly.v | wc -l`
    echo "total_flops=${total_flops}"
    #new file generated: @ DESTLOCATION : FILENAME_1_new.v
    rm ${DESTLOCATION}/${FILENAME}_1.v ${DESTLOCATION}/${FILENAME}_1_new_fileChangedForFlopCountOnly.v
    if [ ${FILENAME} == "RocketTile_yosys_DT2" ]
    then
      sed -i 's/module R\(.*\)(/module R\1_changedForEval(/g' ${DESTLOCATION}/${FILENAME}_1_new.v
    else
      sed -i 's/module \(.*\)(/module \1_changedForEval(/g' ${DESTLOCATION}/${FILENAME}_1_new.v
    fi

    if [ ! -f "${DESTLOCATION}/${FILENAME}_1_new.v" ];
    then
      echo "Could not find ${DESTLOCATION}/${FILENAME}_1_new.v"
      exit 1
    fi
    ORIG_NL=${MODULE_NAME}
    NEW_NL=${ORIG_NL}_changedForEval

    echo "FIRST: ${SRCLOCATION}/${FILENAME}.v -- ${ORIG_NL}"
    echo "SECOND: ${DESTLOCATION}/${FILENAME}_1_new.v -- ${NEW_NL}"
    echo "LOG: ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}.log"

    if [ ${FILENAME} == "MaxPeriodFibonacciLFSR" ]
    then 
      echo ""
      echo "Running for MaxPeriodFibonacciLFSR:"
      perf record -g /usr/bin/time -o ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}_time.log ./bazel-bin/main/lgshell " inou.yosys.tolg files:${SRCLOCATION}/${FILENAME}.v |> pass.bitwidth |> pass.cprop |> pass.bitwidth                           
      inou.yosys.tolg files:${DESTLOCATION}/${FILENAME}_1_new.v |> pass.bitwidth |> pass.cprop |> pass.bitwidth                                                           
      lgraph.open name:${ORIG_NL} |> lgraph.open name:${NEW_NL} |> inou.graphviz.from odir:tmp_1 |> inou.traverse_lg LGorig:${ORIG_NL} LGsynth:${NEW_NL} 
      " > ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}.log   
    elif [ ${FILENAME} == "PipelinedCPU_flattened" ]
    then
      echo ""
      echo "Running for PipelinedCPU_flattened:"
      perf record -g /usr/bin/time -o ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}_time.log ./bazel-bin/main/lgshell " inou.yosys.tolg files:${SRCLOCATION}/${FILENAME}.v |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth                           
      inou.yosys.tolg files:${DESTLOCATION}/${FILENAME}_1_new.v |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth                                                           
      lgraph.open name:${ORIG_NL} |> lgraph.open name:${NEW_NL} |> inou.graphviz.from odir:tmp_1 |> inou.traverse_lg LGorig:${ORIG_NL} LGsynth:${NEW_NL} 
      " > ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}.log 
    elif [ ${FILENAME} == "SingleCycleCPU_flattened" ]
    then
      echo ""
      echo "Running for SingleCycleCPU_flattened:"
      perf record -g /usr/bin/time -o ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}_time.log ./bazel-bin/main/lgshell " inou.yosys.tolg files:${SRCLOCATION}/${FILENAME}.v |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth                           
      inou.yosys.tolg files:${DESTLOCATION}/${FILENAME}_1_new.v |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth                                                           
      lgraph.open name:${ORIG_NL} |> lgraph.open name:${NEW_NL} |> inou.graphviz.from odir:tmp_1 |> inou.traverse_lg LGorig:${ORIG_NL} LGsynth:${NEW_NL} 
      " > ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}.log
    elif [ ${FILENAME} == "RocketTile_yosys_DT2" ]
    then
      echo ""
      echo "Running for RocketTile_yosys_DT2:"
      perf record -g /usr/bin/time -o ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}_time.log ./bazel-bin/main/lgshell " inou.yosys.tolg files:${SRCLOCATION}/${FILENAME}.v |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth                           
      inou.yosys.tolg files:${DESTLOCATION}/${FILENAME}_1_new.v |> pass.cprop |> pass.bitwidth |> pass.cprop |> pass.bitwidth                                                           
      lgraph.open name:${ORIG_NL} |> lgraph.open name:${NEW_NL} |> inou.graphviz.from odir:tmp_1 |> inou.traverse_lg LGorig:${ORIG_NL} LGsynth:${NEW_NL} 
      " > ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}.log
    fi
    perf report > ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}_perf_report.log
    echo "--done matching--"
    python3 pass/locator/nl2nlPlots.py ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}.log  ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
    python3 pass/locator/nl2nlTimePlot.py ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}_time.log ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}_perf_report.log ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
    # rm ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}_time.log 
    # rm ${DESTLOCATION}/${FILENAME}_${PERCENTAGE_CHANGE}_perf_report.log #just for this run. to get the split.
  done

  echo "]" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  printf "x${MODULE_NAME} = [" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  i=1
  printf ${perc_flop_change_arr} >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  while [ $i -lt ${#perc_flop_change_arr[@]} ]
  do
    printf "," >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
    printf ${perc_flop_change_arr[$i]} >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
    i=`expr $i + 1`
  done
  echo "]" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  #num_flop_calculated_arr
  printf "z${MODULE_NAME} = [" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  i=1
  printf ${num_flop_calculated_arr} >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  while [ $i -lt ${#num_flop_calculated_arr[@]} ]
  do
    printf "," >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
    printf ${num_flop_calculated_arr[$i]} >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
    i=`expr $i + 1`
  done
  echo "]" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  #num_cells_calculated_arr
  printf "w${MODULE_NAME} = [" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  i=1
  printf ${num_cells_calculated_arr} >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  while [ $i -lt ${#num_cells_calculated_arr[@]} ]
  do
    printf "," >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
    printf ${num_cells_calculated_arr[$i]} >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
    i=`expr $i + 1`
  done
  echo "]" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
  echo "plt.plot(w${MODULE_NAME}, y${MODULE_NAME}, label = \"${MODULE_NAME}\", linestyle='dashed', marker='o')" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py

  echo "]" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  printf "x${MODULE_NAME} = [" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  i=1
  printf ${perc_flop_change_arr} >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  while [ $i -lt ${#perc_flop_change_arr[@]} ]
  do
    printf "," >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
    printf ${perc_flop_change_arr[$i]} >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
    i=`expr $i + 1`
  done
  echo "]" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  #num_flop_calculated_arr
  printf "z${MODULE_NAME} = [" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  i=1
  printf ${num_flop_calculated_arr} >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  while [ $i -lt ${#num_flop_calculated_arr[@]} ]
  do
    printf "," >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
    printf ${num_flop_calculated_arr[$i]} >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
    i=`expr $i + 1`
  done
  echo "]" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  #num_cells_calculated_arr
  printf "w${MODULE_NAME} = [" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  i=1
  printf ${num_cells_calculated_arr} >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  while [ $i -lt ${#num_cells_calculated_arr[@]} ]
  do
    printf "," >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
    printf ${num_cells_calculated_arr[$i]} >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
    i=`expr $i + 1`
  done
  echo "]" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
  echo "plt.plot(w${MODULE_NAME}, y${MODULE_NAME}, label = \"${MODULE_NAME}\", linestyle='dashed', marker='o')" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py

done

echo "plt.xlabel('Noise (%)')" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
echo "plt.ylabel(' Accuracy (%)')" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
echo "plt.title('Accuracy graph for netlist to netlist analysis. ')" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
echo "plt.legend()" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
echo "# function to show the plot" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
echo "plt.savefig(\"${DESTLOCATION}/nl2nl_acc_flop_plot_data.pdf\", format=\"pdf\", bbox_inches=\"tight\")" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
echo "plt.savefig(\"${DESTLOCATION}/nl2nl_acc_flop_plot_data.svg\", format=\"svg\", bbox_inches=\"tight\")" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
echo "plt.show()" >> ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py

echo "plt.xlabel(' Noise (%)')" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
echo "plt.ylabel('Time (s)')" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
echo "plt.title('Performance graph for netlist to netlist analysis.')" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
echo "plt.legend()" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
echo "# function to show the plot" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
echo "plt.savefig(\"${DESTLOCATION}/nl2nl_time_flop_plot_data.pdf\", format=\"pdf\", bbox_inches=\"tight\")" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
echo "plt.savefig(\"${DESTLOCATION}/nl2nl_time_flop_plot_data.svg\", format=\"svg\", bbox_inches=\"tight\")" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
echo "plt.show()" >> ${DESTLOCATION}/nl2nl_time_flop_plot_data.py

python3 ${DESTLOCATION}/nl2nl_time_flop_plot_data.py
python3 ${DESTLOCATION}/nl2nl_acc_flop_plot_data.py
