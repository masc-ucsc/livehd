#!/bin/bash

#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

OPT_LGRAPH_DIR=./
OPT_ANUBIS=external/anubis/

if [ ! -d ${OPT_ANUBIS} ]; then
  OPT_ANUBIS=bazel-lgraph/external/anubis/
  if [ ! -d ${OPT_ANUBIS} ]; then
    echo "ERROR: failed to find anubis files"
    exit 1
  fi
fi

pwd

declare -a benchmarks=("dlx" "alpha" "fpu" "mor1kx" "or1200")
declare -a benchmarks=("dlx" "alpha")
declare -a benchmarks=("dlx")

for input in ${benchmarks[@]}; do
  dir=${OPT_ANUBIS}/$input/
  top=$input
  include=""
  freq=100

  if [ "${input}" == "or1200" ]; then
    top="or1200_top"
    dir=${OPT_ANUBIS}/$input/rtl/verilog/
  elif [ "${input}" == "mor1kx" ] ; then
    top="mor1kx"
    dir=${OPT_ANUBIS}/$input/rtl/verilog/
    include="--incdir= ${dir}"
  elif [ "${input}" == "fpu" ] ; then
    top="fpu"
    dir=${OPT_ANUBIS}/$input/rtl/
  elif [ "${input}" == "alpha" ] ; then
    top="pipeline"
  elif [ "${input}" == "dlx" ] ; then
    top="cpu_bug"
  fi


  logdir=log_${input}
  bounds=${logdir}/bounds
  e_lgdb=lgdb_elab_${input}
  s_lgdb=lgdb_synth_${input}

  echo  "${OPT_LGRAPH_DIR}/cops/live/lgsetup --bounds=${bounds} --top=${top} --freq=${freq} --testdir=$dir --logdir=${logdir} --e_lgdb=${e_lgdb} --s_lgdb=${s_lgdb} ${include} --lib=fpga"
  ${OPT_LGRAPH_DIR}/cops/live/lgsetup --bounds=${bounds} --top=${top} --freq=${freq} --testdir=$dir --logdir=${logdir} --e_lgdb=${e_lgdb} --s_lgdb=${s_lgdb} ${include} --lib=fpga

done
