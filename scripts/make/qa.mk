# SPDX-License-Identifier: GPL-3.0-only
# Deprecated wrapper — canonical QA/smoke targets live in scripts/make/testing.mk
# (included by default from the root Makefile).
#
# Kept for scripts/ir0-qa.sh and docs that mention IR0_INCLUDE_QA=1.

ifndef IR0_INCLUDE_QA
$(error scripts/make/qa.mk: set IR0_INCLUDE_QA=1 or use scripts/ir0-qa.sh / plain make)
endif

include scripts/make/testing.mk
