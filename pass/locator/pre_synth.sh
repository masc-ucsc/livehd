#!/bin/bash


LGSHELL=./bazel-bin/main/lgshell
LGCHECK=./inou/yosys/lgcheck
PRP_PATH=./inou/pyrope/tests
PATTERN_PATH=./inou/firrtl/tests/proto
if [ ! -f $LGSHELL ]; then
    if [ -f ./main/lgshell ]; then
        LGSHELL=./main/lgshell
        echo "lgshell is in $(pwd)"
    else
        echo "ERROR: could not find lgshell binary in $(pwd)";
    fi
fi

create_pre-synth_verilog_from_firrtl () {
  
  rm -rf lgdb*
  rm -r pre_synth


  echo ""
  echo ""
  echo "===================================================="
  echo "Input firrtl to get LN with src location; And output a verilog for synth "
  echo "FRTL -> LN -> LG -> V"
  echo "===================================================="

  for pt in $1
  do
    file=$(basename $pt)
    if [ "${file#*.}" == "hi.pb" ]; then
      echo "Using High Level FIRRTL"
      FIRRTL_LEVEL='hi'
    elif [ "${file#*.}" == "lo.pb" ]; then
      echo "Using Low Level FIRRTL"
      FIRRTL_LEVEL='lo'
    elif [ "${file#*.}" == "ch.pb" ]; then
      FIRRTL_LEVEL='ch'
      echo "Warning: Experimental Chirrtl extension"
    else
      echo "Illegal FIRRTL extension. Either ch.pb, hi.pb or lo.pb"
      exit 1
    fi

    if [ ! -f "${PATTERN_PATH}/${file}" ]; then
      echo "Could not access test ${pt} at path ${PATTERN_PATH}"
      exit 1
    fi

    pt=$(basename -s .${file#*.} $pt)

    if [ ! -f ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb ]; then
        echo "ERROR: could not find ${pt}.${FIRRTL_LEVEL}.pb in ${PATTERN_PATH}"
        exit 1
    fi
    rm -rf ./lgdb_${pt}

    ${LGSHELL} "inou.firrtl.tolnast path:lgdb_${pt} files:${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb|> pass.compiler gviz:true top:${pt} firrtl:true path:lgdb_${pt} |> lgraph.save hier:true"
    ret_val=$?
    if [ $ret_val -ne 0 ]; then
      echo "ERROR: could not compile with pattern: ${pt}.${FIRRTL_LEVEL}.pb!"
      exit $ret_val
    fi
  done #end of for
    
  for pt in $1
  do
    pt=$(basename -s .${file#*.} $pt)
    echo ""
    echo ""
    echo ""
    echo "----------------------------------------------------"
    echo "LGraph -> Verilog"
    echo "----------------------------------------------------"

    rm -rf tmp_firrtl
    ${LGSHELL} "lgraph.open path:lgdb_${pt} name:${pt} hier:true |> inou.cgen.verilog odir:pre_synth/"
    cat pre_synth/*.v > pre_synth/top_${pt}.v
    ret_val=$?
    if [ $ret_val -eq 0 ] && [ -f "pre_synth/top_${pt}.v" ]; then
        echo "Successfully generate Verilog: pre_synth/top_${pt}.v"
    else
        echo "ERROR: Firrtl compiler failed: verilog generation, testcase: ${PATTERN_PATH}/${pt}.${FIRRTL_LEVEL}.pb"
        exit $ret_val
    fi
  done
    
}

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
    
    num_files=$(find pre_synth/ -type f | wc -l)
    if [ $num_files -ne 1 ]; then
      echo "WARNING: Multiple files found!"
      echo "Concatenating the files into a single .v file."
      cat pre_synth/*.v > pre_synth/concatenated_${pt}.v
      exit 1
    fi
    
    if [ ! -f pre_synth/${pt}.v ]; then
        echo "ERROR: could not find ${pt}.v in pre_synth/"
        exit 1
    fi

  done

}


create_synth-verilog () {

  cd pass/locator

	name=$1
  #FIRRTL# export VERILOG_FILES=../../pre_synth/top_${name}.v    
  #PRP#
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
  export SAVE_NETLIST=${DESIGN_NAME}.v                     
  mkdir -p out                                       

  #if [ -f netlist.v ]; then
  #  rm netlist.v
  #fi
  if [ -f ${DESIGN_NAME}.v && -f out/synthesis.sdc ]; then
    rm ${DESIGN_NAME}.v
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
     echo "** POST-SYNTH NETLIST FORMED in pass/locator:            **"
     echo "** ./${DESIGN_NAME}.v                                    **"
     echo "** for : $VERILOG_FILES                                  **"
  fi
  
  mv ./*0.chk.rpt out/.
  mv ./*0.stat.rpt out/.
  mv ./*_pre.stat out/.
  mv ./*_dff.stat out/.

  cd ../../
  echo ""
  echo ""
}


post_synth () {
  
  echo ""
  echo ""
  rm -r tmp_graphs

  for pt in $1
  do
    if [ ! -f pass/locator/${pt}.v ]; then

        echo "ERROR: could not find pass/locator/${pt}.v"
        exit 1
    fi
     echo "===================================================="
     echo "Synthesized verilog to LG "
     echo "synth V -> LG"
     echo "===================================================="
     ${LGSHELL} "inou.yosys.tolg files:pass/locator/${pt}.v script:pp.ys top:${pt} |> lgraph.dump |> inou.graphviz.from odir:tmp_graphs"
     
     ret_val=$?
     if [ $ret_val -ne 0 ]; then
       echo "ERROR: inou.yosys.tolg could not succeed"
       exit $ret_val
     fi
    

  done

}

#pts='BundleConnect.hi.pb SingleEvenFilter.hi.pb Xor6Thread2.hi.pb XorSelfThread1.hi.pb Mux4.hi.pb Life.hi.pb Cell_alone.hi.pb LFSR16.hi.pb LogShifter.hi.pb Test2.hi.pb Accumulator.hi.pb Coverage.hi.pb TrivialAdd.hi.pb VendingMachineSwitch.hi.pb VendingMachine.hi.pb Trivial.hi.pb Tail.hi.pb TrivialArith.hi.pb Shifts.hi.pb Darken.hi.pb HiLoMultiplier.hi.pb AddNot.hi.pb GCD_3bits.hi.pb Test3.hi.pb Register.hi.pb RegisterSimple.hi.pb Parity.hi.pb ResetShiftRegister.hi.pb SimpleALU.hi.pb ByteSelector.hi.pb MaxN.hi.pb Max2.hi.pb Flop.hi.pb EnableShiftRegister.hi.pb Decrementer.hi.pb Counter.hi.pb RegXor.hi.pb PlusAnd.hi.pb'

#pts='Adder4.ch.pb IntXbar.ch.pb SimpleClockGroupSource.ch.pb FixedClockBroadcast.ch.pb ClockGroupAggregator.ch.pb IntSyncSyncCrossingSink.ch.pb AMOALU.ch.pb Top.ch.pb Arbiter_10.ch.pb FlipSimple2.ch.pb NotAnd.ch.pb BreakpointUnit.ch.pb DebugCustomXbar.ch.pb MaxPeriodFibonacciLFSR.ch.pb'

#################FOR PRP
pts='test4'
create_pre-synth_verilog "$pts"
create_synth-verilog "$pt"
#post_synth "$pts"
#################FOR PRP

# rm pre_synth/*
# rm -r lgdb_*
# rm *dot
# rm -f lgcheck*
# 
# for pt in $pts
# do
#   create_pre-synth_verilog_from_firrtl "$pt"
#   create_synth-verilog "$pt"
# done
# #post_synth "$pts"
