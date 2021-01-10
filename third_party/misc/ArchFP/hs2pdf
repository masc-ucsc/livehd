#!/usr/bin/env bash

# error out if commands not found
set -e
set -o pipefail

./tofig.pl -a 1.0 -f 22 $1 | fig2dev -L ps | ps2pdf -dEPSCrop - $1.pdf
