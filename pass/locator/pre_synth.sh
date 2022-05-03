#!/bin/bash

pts='scalar_tuple '

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
 #   #FIXME:the lnast loaded has no name!!
 #   ret_val=$?
 #   if [ $ret_val -ne 0 ]; then
 #     echo "ERROR: could not create/save LG for pattern: ${pt}!"
 #     exit $ret_val
 #   fi
    
     echo ""
     echo "----------------------------------------------------"
     echo "PRP -> HL LNAST -> LG -> save LG "
     echo "----------------------------------------------------"
     ${LGSHELL} "inou.pyrope files:${PRP_PATH}/${pt}.prp |> pass.lnast_tolg |> pass.bitwidth |> pass.cprop |> lgraph.save hier:true "
     
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
      exit 0
    fi
    
    if [ ! -f pre_synth/${pt}.v ]; then
        echo "ERROR: could not find ${pt}.v in pre_synth/"
        exit 1
    fi

  done

}


rm -rf lgdb*
rm -r pre_synth
create_pre-synth_verilog "$pts"
