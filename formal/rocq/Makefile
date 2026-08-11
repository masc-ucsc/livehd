# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Thin wrapper over the Rocq-generated makefile.  `rocq makefile` is the Rocq 9
# spelling of `coq_makefile`; the legacy name still works through the
# compatibility shims but is not used here.
#
#   make          build every theory listed in _CoqProject
#   make clean    remove .vo/.glob/.aux and the generated makefile

ROCQ ?= rocq

all: Makefile.coq
	$(MAKE) -f Makefile.coq

Makefile.coq: _CoqProject
	$(ROCQ) makefile -f _CoqProject -o Makefile.coq

clean:
	@if [ -f Makefile.coq ]; then $(MAKE) -f Makefile.coq cleanall; fi
	rm -f Makefile.coq Makefile.coq.conf

.PHONY: all clean
