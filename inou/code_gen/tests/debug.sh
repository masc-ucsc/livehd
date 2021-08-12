#!/bin/bash
rm -rf ./lgdb
rm -f ./*.v
rm -f lnast.dot.gld
rm -f lnast.nodes
rm -f lnast.nodes.gld
rm -f ./*.dot

pts_to_do='lhs_wire3 tuple funcall_unnamed2'
pts='firrtl_gcd hier_tuple_io logic
     reg_bits_set tuple_copy 
     hier_tuple hier_tuple2 hier_tuple3
     lhs_wire lhs_wire2 scalar_tuple attr_set
     firrtl_tail firrtl_tail2  firrtl_tail3
     adder_stage tuple_if reg__q_pin nested_if 
     capricious_bits2 capricious_bits4 capricious_bits
     out_ssa if2 if ssa_rhs bits_rhs counter counter_nested_if
     '

# make sure to call Pyrope_compile() in the end of script
# pts=''
pts_hier='sum funcall'  
pts_hier2='sum2 funcall2'

#inline function call
pts_hier4='funcall4'      
pts_hier5='funcall5'
pts_hier6='funcall_unnamed'

LGSHELL=./bazel-bin/main/lgshell
LGCHECK=./inou/yosys/lgcheck

if [ ! -f $LGSHELL ]; then
    if [ -f ./main/lgshell ]; then
        LGSHELL=./main/lgshell
        echo "lgshell is in $(pwd)"
    else
        echo "ERROR: could not find lgshell binary in $(pwd)";
    fi
fi

Pyrope_compile () {
    echo ""
    echo ""
    echo ""
    echo "===================================================="
    echo "Pyrope Full Compilation (C++ Parser)"
    echo "===================================================="


    for pt in $1
    do
        if [ ! -f inou/pyrope/tests/compiler/${pt}.prp ]; then
            echo "ERROR: could not find ${pt}.prp in /inou/pyrope/tests/compiler"
            exit 2
        fi

        # ln -s inou/pyrope/tests/compiler/${pt}.prp;

        echo "----------------------------------------------------"
        echo "Pyrope -> LNAST-SSA Graphviz debug"
        echo "----------------------------------------------------"

        ${LGSHELL} "inou.pyrope files:inou/pyrope/tests/compiler/${pt}.prp |> pass.lnast_tolg.dbg_lnast_ssa |> inou.graphviz.from"

        if [ -f ${pt}.lnast.dot ]; then
            echo "Successfully create a lnast from inou/pyrope/tests/compiler/${pt}.prp"
        else
            echo "ERROR: Pyrope compiler failed: LNAST generation, testcase: ${pt}.prp"
            exit 1
        fi

        if true ; then
            echo "----------------------------------------------------"
            echo "Pyrope -> LNAST -> LGraph"
            echo "----------------------------------------------------"

            ${LGSHELL} "inou.pyrope files:inou/pyrope/tests/compiler/${pt}.prp |> pass.lnast_tolg"
            if [ $? -eq 0 ]; then
                echo "Successfully create the inital LGraph: inou/pyrope/tests/compiler/${pt}.prp"
            else
                echo "ERROR: Pyrope compiler failed: LNAST -> LGraph, testcase: inou/pyrope/tests/compiler/${pt}.prp"
                exit 1
            fi

            ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"
            mv ${pt}.dot ${pt}.raw.dot

            echo ""
            echo ""
            echo ""
            echo "----------------------------------------------------"
            echo "Copy-Propagation And Tuple Chain Resolve"
            echo "----------------------------------------------------"
            ${LGSHELL} "lgraph.open name:${pt} |> pass.cprop |> pass.cprop |> pass.cprop |> pass.cprop"
            if [ $? -eq 0 ]; then
                echo "Successfully resolve the tuple chain: inou/pyrope/tests/compiler/${pt}.prp"
            else
                echo "ERROR: Pyrope compiler failed: resolve tuples, testcase: inou/pyrope/tests/compiler/${pt}.prp"
                exit 1
            fi

            ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"
            mv ${pt}.dot ${pt}.no_bits.dot


            echo ""
            echo ""
            echo ""
            echo "----------------------------------------------------"
            echo "Local Bitwidth Optimization(LGraph)"
            echo "----------------------------------------------------"

            ${LGSHELL} "lgraph.open name:${pt} |> pass.bitwidth |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> pass.cprop"

            if [ $? -eq 0 ]; then
                echo "Successfully optimize design bitwidth: inou/pyrope/tests/compiler/${pt}.prp"
            else
                echo "ERROR: Pyrope compiler failed: bitwidth optimization, testcase: inou/pyrope/tests/compiler/${pt}.prp"
                exit 1
            fi

            ${LGSHELL} "lgraph.open name:${pt} |> inou.graphviz.from verbose:false"
        fi
    done #end of for


    if [[ $2 == "hier" ]]; then
        #get the last pattern of pts_hier as the top module
        top_module=$(echo $1 | awk '{print $NF}')
        echo $top_module

        echo ""
        echo ""
        echo ""
        echo "----------------------------------------------------"
        echo "Hierarchical Bitwidth Optimization(LGraph)"
        echo "----------------------------------------------------"

        ${LGSHELL} "lgraph.open name:${top_module} |> pass.bitwidth hier:true |> pass.bitwidth hier:true"
        if [ $? -eq 0 ]; then
            echo "Successfully optimize hier-design bitwidth: inou/pyrope/tests/compiler/${top_module}.prp"
        else
            echo "ERROR: Pyrope compiler failed: hier-bitwidth optimization, testcase: inou/pyrope/tests/compiler/${top_module}.prp"
            exit 1
        fi

        ${LGSHELL} "lgraph.open name:${top_module} |> inou.graphviz.from verbose:false"
        mv ${top_module}.dot ${top_module}.hier.dot
    fi # end of hier bits



    # Verilog code generation
    for pt in $1
    do
        if [[ ${pt} == *_err* ]]; then
            echo "----------------------------------------------------"
            echo "Pass! This is a Compile Error Test, No Need to Generate Verilog Code "
            echo "----------------------------------------------------"
        else
            echo ""
            echo ""
            echo ""
            echo "----------------------------------------------------"
            echo "LGraph -> Verilog"
            echo "----------------------------------------------------"

            ${LGSHELL} "lgraph.open name:${pt} |> inou.yosys.fromlg hier:true"
            if [ $? -eq 0 ] && [ -f ${pt}.v ]; then
                echo "Successfully generate Verilog: ${pt}.v"
                rm -f  yosys_script.*
            else
                echo "ERROR: Pyrope compiler failed: verilog generation, testcase: inou/pyrope/tests/compiler/${pt}.prp"
                exit 1
            fi
        fi
    done


    # Logic Equivalence Check
    if [[ $2 == "hier" ]]; then
        #get the last pattern of pts_hier
        top_module=$(echo $1 | awk '{print $NF}')
        echo $top_module

        #concatenate every submodule under top_module.v
        for pt in $1
        do
            if [[ pt != $top_module ]]; then
                $(cat ${pt}.v >> ${top_module}.v)
            fi
        done


        echo ""
        echo ""
        echo ""
        echo "----------------------------------------------------"
        echo "Logic Equivalence Check: Hierarchical Design"
        echo "----------------------------------------------------"

        ${LGCHECK} --top $top_module --implementation ${top_module}.v --reference ./inou/pyrope/tests/compiler/verilog_gld/${top_module}.gld.v

        if [ $? -eq 0 ]; then
            echo "Successfully pass logic equivalence check!"
        else
            echo "FAIL: ${top_module}.v !== ${top_module}.gld.v"
            exit 1
        fi
    else
        for pt in $1
        do
            echo ""
            echo ""
            echo ""
            echo "----------------------------------------------------"
            echo "Logic Equivalence Check"
            echo "----------------------------------------------------"

            ${LGCHECK} --implementation ${pt}.v --reference ./inou/pyrope/tests/compiler/verilog_gld/${pt}.gld.v

            if [ $? -eq 0 ]; then
                echo "Successfully pass logic equivalence check!"
            else
                echo "FAIL: ${pt}.v !== ${pt}.gld.v"
                exit 1
            fi
        done
    fi
}




# Pyrope_compile "$pts_hier6" "hier"
# Pyrope_compile "$pts_hier5" "hier"
# Pyrope_compile "$pts"
Pyrope_compile "$pts_hier"  "hier"
# Pyrope_compile "$pts_hier2" "hier"
# Pyrope_compile "$pts_hier4" "hier"

${LGSHELL} "lgraph.open name:funcall |> inou.graphviz.from verbose:false"
mv funcall.dot funcall.dbg.dot
${LGSHELL} "lgraph.open name:funcall |> pass.lnast_fromlg |> lnast.dump |> inou.graphviz.from verbose:false |> inou.code_gen.prp"
mv funcall.lnast.dot funcall.lnast.raw.dot
${LGSHELL} "lgraph.open name:funcall |> pass.lnast_fromlg |> pass.lnast_tolg"

# rm -f *.v
# rm -f lnast.dot.gld
# rm -f lnast.nodes
# rm -f lnast.nodes.gld
# rm -f *.dot
