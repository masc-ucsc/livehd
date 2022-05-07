#!/bin/bash


LGSHELL=./bazel-bin/main/lgshell
LGCHECK=./inou/yosys/lgcheck
PRP_PATH=./inou/pyrope/tests

if [ ! -f $LGSHELL ]; then
    if [ -f ./main/lgshell ]; then
        LGSHELL=./main/lgshell
        echo "lgshell is in $(pwd)"
    else
        echo "ERROR: could not find lgshell binary in $(pwd)";
    fi
fi


create_pre-synth_verilog () {
  
  rm -rf lgdb*
  rm -r pre_synth
  rm -r ${pts}/

  echo ""
  echo ""
  echo "===================================================="
  echo "Input pyrope to get LN with src location; And output a verilog for synth "
  echo "P -> LN -> LG -> V"
  echo "===================================================="

  for pt in $1
  do
    if [ ! -f ${PRP_PATH}/${pt}.prp ]; then
        echo "ERROR: could not find ${pt}.prp in ${PRP_PATH}"
        exit 1
    fi

 #   echo ""
 #   echo "----------------------------------------------------"
 #   echo "PRP -> HL LNAST -> save LN "
 #   echo "----------------------------------------------------"
 #   #PRP->LN
 #   ${LGSHELL} "inou.pyrope files:${PRP_PATH}/${pt}.prp |> pass.lnast_save "
 #   
 #   ret_val=$?
 #   if [ $ret_val -ne 0 ]; then
 #     echo "ERROR: could not compile with pattern: ${PRP_PATH}/${pt}.prp!"
 #     echo "LNAST for ${pt}.prp not saved"
 #     exit $ret_val
 #   fi
 #   
 #   echo ""
 #   echo "----------------------------------------------------"
 #   echo "saved LN -> (loaded using hif format)-> LGraph"
 #   echo "----------------------------------------------------"
 #   ${LGSHELL} "pass.lnast_load files:${pt} |> pass.lnast_tolg |> pass.cprop |> pass.bitwidth |> save.lgraph hier:true"
 #   ret_val=$?
 #   if [ $ret_val -ne 0 ]; then
 #     echo "ERROR: could not create/save LG for pattern: ${pt}!"
 #     exit $ret_val
 #   fi
    
     echo ""
     echo "----------------------------------------------------"
     echo "PRP -> HL LNAST -> LG -> save LG "
     echo "----------------------------------------------------"
     ${LGSHELL} "inou.pyrope files:${PRP_PATH}/${pt}.prp |> pass.lnast_tolg |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> lgraph.save hier:true "
     
     ret_val=$?
     if [ $ret_val -ne 0 ]; then
       echo "ERROR: could not compile with pattern: ${PRP_PATH}/${pt}.prp!"
       exit $ret_val
     fi
     #lgraph.open name:scalar_tuple |> lgraph.dump
    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "LGraph -> Verilog_pre-synth"
    echo "----------------------------------------------------"
    ${LGSHELL} "lgraph.open name:${pt} path:lgdb  |> inou.cgen.verilog odir:pre_synth/" 

    ret_val=$?
    if [ $ret_val -ne 0 ]; then
      echo "ERROR: could not generate verilog for pattern:lgdb/${pt}!"
      exit $ret_val
    else
      echo "Successfully generated Verilog: pre_synth/${pt}.v"
    fi
    
    if [ ! -f pre_synth/${pt}.v ]; then
        echo "ERROR: could not find ${pt}.v in pre_synth/"
        exit 1
    fi

  done

}


create_synth-verilog () {

  cd pass/locator

  echo ""
  echo ""
  echo "CURRENTLY IN:"
  pwd
  echo ""
  echo ""

	name=$1
  export VERILOG_FILES=../../pre_synth/${name}.v    
  export DESIGN_NAME=$name                          
  export SYNTH_BUFFERING=0                          
  export SYNTH_SIZING=0                             
  export LIB_SYNTH=sky130.lib                       
  export LIB_SYNTH_COMPLETE_NO_PG=sky130.lib        
  # In ns                                           
  export CLOCK_PERIOD=10                            
  export SYNTH_DRIVING_CELL="sky130_fd_sc_hs__inv_1"
  export SYNTH_CAP_LOAD=33.3                        
  export SYNTH_MAX_FANOUT=5                         
  export SYNTH_STRATEGY='DELAY 0'                   
  export synthesis_tmpfiles=out                     
  export synth_report_prefix=$DESIGN_NAME           
  export SYNTH_ADDER_TYPE="YOSYS"                   
  export SYNTH_NO_FLAT=0                            
  export SYNTH_SHARE_RESOURCES=0                    
  export SAVE_NETLIST=netlist.v                     
  mkdir -p out                                       

  if [ -f netlist.v && -f out/synthesis.sdc ]; then
    rm netlist.v
    rm -r out/
  fi
  
  if [ ! -f sky130.lib ]; then
    echo "Liberty file not found. follow README.md to see the steps to create the sky130.lib file"
    exit 1
  fi
  
  if [ ! -f run.sh ]; then
    echo "run.sh file not found!!"
    exit 1
  fi
  
  if [ ! -f synth.tcl ]; then
    echo "synth.tcl file not found."
    exit 1
  else
    echo ""
    echo ""
    echo "starting synth.tcl -- yosys --"
    echo ""
    echo ""
  fi
  
  yosys -c synth.tcl
  
     ret_val=$?
  if [ $ret_val -ne 0 ]; then
     echo "ERROR: yosys could not succeed"
     echo "ERROR: POST-SYNTH NETLIST NOT FORMED"
     exit $ret_val
  else
     echo "** POST-SYNTH NETLIST FORMED:            **"
     echo "** ./netlist.v                           **"
     echo "** ENSURED CORRECT DESIGN NAME IN RUN.SH?**"
  fi
  
  mv ./*0.chk.rpt out/.
  mv ./*0.stat.rpt out/.
  mv ./*_pre.stat out/.
  mv ./*_dff.stat out/.

  cd ../../
  echo ""
  echo ""
  echo "CURRENTLY IN:"
  pwd
  echo ""
  echo ""
}


post_synth () {
  
  echo ""
  echo ""

  for pt in $1
  do
    if [ ! -f pass/locator/netlist.v ]; then

        echo "ERROR: could not find pass/locator/netlist.v"
        exit 1
    fi
     echo "===================================================="
     echo "Synthesized verilog to LG "
     echo "synth V -> LG"
     echo "===================================================="
     ${LGSHELL} "inou.yosys.tolg files:pass/locator/netlist.v script:pp.ys top:${pt} |> lgraph.dump "
     
     ret_val=$?
     if [ $ret_val -ne 0 ]; then
       echo "ERROR: inou.yosys.tolg could not succeed"
       exit $ret_val
     fi
    

  done

}

pts='reg__q_pin' # scalar_tuple
create_pre-synth_verilog "$pts"
create_synth-verilog "$pts"
post_synth "$pts"
