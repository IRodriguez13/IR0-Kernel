# SPDX-License-Identifier: GPL-3.0-only
#
# Contributor local gate — no publish.
#
#   make pre-submit
#   make pre-submit SUBSYSTEM=mm
#

.PHONY: pre-submit help-pre-submit

SUBSYSTEM ?= all

pre-submit:
	@chmod +x $(KERNEL_ROOT)/scripts/pre_submit.sh
	@$(KERNEL_ROOT)/scripts/pre_submit.sh "$(SUBSYSTEM)"

help-pre-submit:
	@echo "IR0 pre-submit (local only — no push)"
	@echo "  make pre-submit                 # kernel + arch-guard + hygiene + tests/host + fmt"
	@echo "  make pre-submit SUBSYSTEM=mm    # + smoke-mm-cow-lazy"
	@echo "  make pre-submit SUBSYSTEM=net   # + smoke-stream-sock"
	@echo "  make pre-submit SUBSYSTEM=arm64 # + smoke-arm64"
	@echo "  Ends with PRE_SUBMIT_OK or PRE_SUBMIT_FAIL"
	@echo "  See CONTRIBUTING.md"
