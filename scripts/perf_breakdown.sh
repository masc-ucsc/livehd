

perf report >pp

for a in 0xfff " Lgraph" " Node" " Lconst" " absl::" " std::" " Bitwidth" " Fwd_edge" " Fast_edge"  malloc free
do
  echo $a
  grep $a pp | add.pl
done
