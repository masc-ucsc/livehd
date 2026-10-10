# Helper makefile for CORE-ET filelist extraction (read-only).
#
# core-et has no bender / fusesoc / .f filelists: each block's compile set lives
# in a GNU make variable (`NEW_RTL` in dv/rtlcosim/<m>/Makefile, `RTL_SRCS` in
# hw/ip/<ip>/dv/Makefile), composed from $(PRIM_*) variables that mk/prim.mk
# resolves per TECH.  Parsing that as text is wrong; ask make to expand it.
#
# Usage (never writes into core-et -- an explicit goal means no build runs):
#   make -C <core-et>/dv/rtlcosim/<module> \
#        -f Makefile -f <livehd>/scripts/coreet_print.mk \
#        --no-print-directory print-NEW_RTL_FINAL
#
# NEW_RTL_FINAL is what mk/rtlcosim.mk actually hands verilator: NEW_RTL plus
# the NEW_RTL_AUTO package/primitive dependencies it back-fills.

print-%: ; @echo '$($*)'
