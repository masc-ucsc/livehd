#!/usr/bin/env bash
# RETIRED, DO NOT USE.  This version wrote every phase into a FIXED directory
# per row (so a retry overwrote the failed attempt) and gated phases on `-d lg`
# after `|| true` (so a failed compile that still created the directory was
# admitted).  Both defects are recorded in MASTER_SIM_ATTEMPT_LOSS.txt.
# Use fp_cov_queue_cva6.sh or fp_cov_queue_coreet.sh instead.
echo "RETIRED: use fp_cov_queue_cva6.sh or fp_cov_queue_coreet.sh" >&2; exit 2
