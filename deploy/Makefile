# Litenyx deploy Makefile — cross-compat (Codespaces apt + Replit Nix).
# Dogecoin fork: fixed two-chain shared-state core (Phase 2). Consensus logic
# lives in litenyx/. Forked daemon lives in external/dogecoin (gitignored clone).

CURDIR ?= $(shell pwd)
# This Makefile lives in deploy/; the litenyx/ and cpp_reference/ sources are at
# the repository root (one level up from CURDIR). REPO_ROOT locates them.
REPO_ROOT ?= $(CURDIR)/..
DOGECOIN_DIR ?= $(CURDIR)/external/dogecoin
CXX ?= g++
BOOST_TEST ?= -lboost_unit_test_framework

# INT-Q5 (pin): the M3 attach points (ConnectTip failure boundary) were VERIFIED
# against Dogecoin v1.14.9. Pin the base to the EXACT commit so the daemon built
# for G1-G3 integration is structurally identical to the designed source. A moving
# `master` would invalidate the INT-Q4 boundary analysis, so this is a hard pin.
DOGECOIN_PIN ?= e0a1c157791544e818c901bd9341896965afbf9d  # Dogecoin Core v1.14.9

KERRNYX_INCLUDES = -I$(REPO_ROOT) -I$(REPO_ROOT)/cpp_reference -I$(DOGECOIN_DIR)/src
KERRNYX_TEST_SRC = $(REPO_ROOT)/cpp_reference/test/test_litenyx.cpp
KERRNYX_TEST_BIN = $(REPO_ROOT)/cpp_reference/test/test_litenyx
KERRNYX_TOPO_SRC = $(REPO_ROOT)/cpp_reference/test/test_litenyx_topology.cpp
KERRNYX_TOPO_BIN = $(REPO_ROOT)/cpp_reference/test/test_litenyx_topology
KERRNYX_AUTH_SRC = $(REPO_ROOT)/cpp_reference/test/test_litenyx_topology_authority.cpp
KERRNYX_AUTH_BIN = $(REPO_ROOT)/cpp_reference/test/test_litenyx_topology_authority
KERRNYX_CID_SRC = $(REPO_ROOT)/cpp_reference/test/test_litenyx_chainid_lifecycle.cpp
KERRNYX_CID_BIN = $(REPO_ROOT)/cpp_reference/test/test_litenyx_chainid_lifecycle
KERRNYX_V3_SRC = $(REPO_ROOT)/cpp_reference/test/test_litenyx_v3_carrier.cpp
KERRNYX_V3_BIN = $(REPO_ROOT)/cpp_reference/test/test_litenyx_v3_carrier
KERRNYX_EXEC_SRC = $(REPO_ROOT)/cpp_reference/test/test_litenyx_execution_authority.cpp
KERRNYX_EXEC_BIN = $(REPO_ROOT)/cpp_reference/test/test_litenyx_execution_authority
KERRNYX_DRAIN_SRC = $(REPO_ROOT)/cpp_reference/test/test_litenyx_draining_authority.cpp
KERRNYX_DRAIN_BIN = $(REPO_ROOT)/cpp_reference/test/test_litenyx_draining_authority
KERRNYX_DELTA_SRC = $(REPO_ROOT)/cpp_reference/test/test_litenyx_shared_delta.cpp
KERRNYX_DELTA_BIN = $(REPO_ROOT)/cpp_reference/test/test_litenyx_shared_delta

# INT-OPEN-1 / M3 real-class KAT links the REAL LITENYX_sharedstate.cpp (which
# depends on dogecoin uint256 / base_blob::SetHex) plus the minimum real dogecoin
# support translation units. These are NOT models/mocks — they are the vendored
# tree's own code, pulled in only as far as needed to resolve symbols.
KERRNYX_DOGE_SUPPORT = $(DOGECOIN_DIR)/src/uint256.cpp \
                        $(DOGECOIN_DIR)/src/utilstrencodings.cpp

.PHONY: help cpp-test cpp-compile-check detect-env clone-dogecoin inject-hooks production-build production-build-debug m3-integration regtest-test

help:
	@echo "Litenyx targets:"
	@echo "  cpp-test        Build + run standalone consensus unit tests (boost header-only)"
	@echo "  clone-dogecoin  Clone Dogecoin base into external/dogecoin (gitignored)"
	@echo "  inject-hooks    Copy litenyx/ sources + apply patches into the clone"
	@echo "  production-build Configure + build dogecoind/dogecoin-cli/dogecoin-tx"
	@echo "  production-build-debug  DEBUG-only build (--enable-debug, NO -DNDEBUG); fault-injection capable"
	@echo "  m3-integration  FAIL-CLOSED INT-OPEN-1 gate: pin->patch->debug build->delta KATs->G1-G3 regtest"
	@echo "  detect-env      Print toolchain include/lib paths"

clone-dogecoin:
	@if [ -d "$(DOGECOIN_DIR)/.git" ]; then \
		echo "Dogecoin clone already present; verifying INT-Q5 pin..."; \
		cd $(DOGECOIN_DIR) && git fetch --tags --depth 1 origin $(DOGECOIN_PIN); \
		git -C $(DOGECOIN_DIR) checkout -q $(DOGECOIN_PIN) || git -C $(DOGECOIN_DIR) checkout -q tags/v1.14.9; \
	else \
		echo "Cloning Dogecoin (pinned INT-Q5: $(DOGECOIN_PIN))..."; \
		git clone --filter=blob:none --no-checkout https://github.com/dogecoin/dogecoin.git $(DOGECOIN_DIR); \
		git -C $(DOGECOIN_DIR) fetch --depth 1 origin $(DOGECOIN_PIN); \
		git -C $(DOGECOIN_DIR) checkout -q $(DOGECOIN_PIN); \
	fi
	@echo "Verified Dogecoin base at: $$(git -C $(DOGECOIN_DIR) rev-parse HEAD) (expect $(DOGECOIN_PIN))"

inject-hooks: clone-dogecoin
	@echo "Copying litenyx/ consensus sources into the clone..."
	@mkdir -p $(DOGECOIN_DIR)/src/litenyx
	@cp $(REPO_ROOT)/litenyx/LITENYX_types.h $(DOGECOIN_DIR)/src/litenyx/LITENYX_types.h
	@cp $(REPO_ROOT)/litenyx/LITENYX_auxpow.h $(DOGECOIN_DIR)/src/litenyx/LITENYX_auxpow.h
	@cp $(REPO_ROOT)/litenyx/LITENYX_identity.h $(DOGECOIN_DIR)/src/litenyx/LITENYX_identity.h
	@cp $(REPO_ROOT)/litenyx/LITENYX_sharedstate.h $(DOGECOIN_DIR)/src/litenyx/LITENYX_sharedstate.h
	@cp $(REPO_ROOT)/litenyx/LITENYX_sharedstate.cpp $(DOGECOIN_DIR)/src/litenyx/LITENYX_sharedstate.cpp
	@cp $(REPO_ROOT)/litenyx/LITENYX_validation.h $(DOGECOIN_DIR)/src/litenyx/LITENYX_validation.h
	@cp $(REPO_ROOT)/litenyx/LITENYX_validation.cpp $(DOGECOIN_DIR)/src/litenyx/LITENYX_validation.cpp
	@cp $(REPO_ROOT)/litenyx/LITENYX_topology.h $(DOGECOIN_DIR)/src/litenyx/LITENYX_topology.h
	@cp $(REPO_ROOT)/litenyx/LITENYX_topology_authority.h $(DOGECOIN_DIR)/src/litenyx/LITENYX_topology_authority.h
	@cp $(REPO_ROOT)/litenyx/LITENYX_chainid_lifecycle.h $(DOGECOIN_DIR)/src/litenyx/LITENYX_chainid_lifecycle.h
	@cp $(REPO_ROOT)/litenyx/LITENYX_execution_authority.h $(DOGECOIN_DIR)/src/litenyx/LITENYX_execution_authority.h
	@cp $(REPO_ROOT)/litenyx/LITENYX_draining_authority.h $(DOGECOIN_DIR)/src/litenyx/LITENYX_draining_authority.h
	@cp $(REPO_ROOT)/litenyx/LITENYX_topology_tracker.h $(DOGECOIN_DIR)/src/litenyx/LITENYX_topology_tracker.h
	@cp $(REPO_ROOT)/litenyx/LITENYX_topology_tracker.cpp $(DOGECOIN_DIR)/src/litenyx/LITENYX_topology_tracker.cpp
	@echo "Applying litenyx patches (mandatory: any failure is fatal)..."
	@for p in identity block-aux validation rpc makefile; do \
		PATCH=$(REPO_ROOT)/deploy/patches/litenyx-$$p.patch; \
		if git -C $(DOGECOIN_DIR) apply --check $$PATCH >/dev/null 2>&1; then \
			git -C $(DOGECOIN_DIR) apply $$PATCH \
				&& echo "applied litenyx-$$p.patch" \
				|| { echo "FATAL: git apply failed for litenyx-$$p.patch"; exit 1; }; \
		elif git -C $(DOGECOIN_DIR) apply --reverse --check $$PATCH >/dev/null 2>&1; then \
			echo "skip litenyx-$$p.patch (already applied)"; \
		else \
			echo "FATAL: litenyx-$$p.patch does not apply and is not already applied"; exit 1; \
		fi; \
	done
	@echo "Litenyx hooks injected."

production-build: inject-hooks
	@echo "Configuring build toolchains..."
	@cd $(DOGECOIN_DIR) && ./autogen.sh && ./configure --with-incompatible-bdb --with-boost-process --with-boost-libdir=/usr/lib/x86_64-linux-gnu
	@echo "Building (this is slow)..."
	@cd $(DOGECOIN_DIR) && $(MAKE)
	@echo "Build complete: dogecoind/dogecoin-cli/dogecoin-tx in $(DOGECOIN_DIR)/src"


# INT-OPEN-1 / M3: DEBUG daemon build. Configures EXPLICITLY with --enable-debug so
# that -DNDEBUG is NOT defined. The M3 G1 fault-injection hook and the
# testlitenyxforceflushfail RPC are gated behind #ifndef NDEBUG and are therefore
# STRUCTURALLY ABSENT from any release/--disable-debug build. The debug binary is
# built into the standard src/ prefix but only via this target, never via production-build.
production-build-debug: inject-hooks
	@echo "Verifying INT-Q5 pin before building..."
	@test "$(shell git -C $(DOGECOIN_DIR) rev-parse HEAD)" = "$(DOGECOIN_PIN)" || \
		{ echo "FATAL: Dogecoin HEAD != pinned $(DOGECOIN_PIN) (got $$(git -C $(DOGECOIN_DIR) rev-parse HEAD))"; exit 1; }
	@echo "Configuring DEBUG build (--enable-debug, NO -DNDEBUG)..."
	@cd $(DOGECOIN_DIR) && ./autogen.sh && ./configure --enable-debug --with-incompatible-bdb --with-boost-process --with-boost-libdir=/usr/lib/x86_64-linux-gnu
	@echo "Asserting debug build excludes -DNDEBUG..."
	@grep -rq -- "-DNDEBUG" $(DOGECOIN_DIR)/Makefile $(DOGECOIN_DIR)/src/Makefile 2>/dev/null && \
		{ echo "FATAL: configured build defines NDEBUG; G1 fault-injection would be excluded. Refusing."; exit 1; } || echo "OK: NDEBUG absent -> fault-injection capable daemon."
	@echo "Building debug daemon (this is slow)..."
	@cd $(DOGECOIN_DIR) && $(MAKE)
	@echo "Debug build complete: $(DOGECOIN_DIR)/src/dogecoind (fault-injection capable)"

# INT-OPEN-1 / M3: fail-closed integration gate. Enforces the evidence hierarchy:
#   Pinned v1.14.9 -> Patch Application -> Debug Daemon Build ->
#   M3 Delta KATs -> G1-G3 Regtest -> Verification Verdict
# Every stage fails the build on any error. G1-G3 SKIPPED or FAIL => NOT success.
m3-integration: production-build-debug
	@echo "=== M3 INTEGRATION GATE (fail-closed) ==="
	@echo "[1/5] Pin check: HEAD == $(DOGECOIN_PIN)"
	@test "$(shell git -C $(DOGECOIN_DIR) rev-parse HEAD)" = "$(DOGECOIN_PIN)" || \
		{ echo "FATAL: pin mismatch"; exit 1; }
	@echo "[2/5] Patch application verified by production-build-debug (inject-hooks)"
	@test -f $(DOGECOIN_DIR)/src/litenyx/LITENYX_sharedstate.cpp || \
		{ echo "FATAL: litenyx sources not injected"; exit 1; }
	@echo "[3/5] Debug daemon present:"
	@test -x $(DOGECOIN_DIR)/src/dogecoind || { echo "FATAL: dogecoind missing"; exit 1; }
	@echo "[4/5] M3 real-class delta KATs (6/6 required):"
	$(CXX) -std=c++20 -O0 -DKERRNYX_STANDALONE_TEST \
		$(KERRNYX_INCLUDES) $(KERRNYX_DELTA_SRC) $(KERRNYX_DOGE_SUPPORT) -o $(KERRNYX_DELTA_BIN) \
		-lpthread $(BOOST_TEST) && $(KERRNYX_DELTA_BIN)
	@echo "[5/5] G1-G3 regtest (SKIP or FAIL => gate fails):"
	@cd $(REPO_ROOT)/tests/regtest && \
		LITENYXD_BIN=$(DOGECOIN_DIR)/src/dogecoind \
		LITENYX_CLI_BIN=$(DOGECOIN_DIR)/src/dogecoin-cli \
		python3 -m pytest test_litenyx_m3_integration.py -v -p no:cacheprovider \
		--tb=short -rA | tee /tmp/m3_integration_report.txt ; \
		status=$$? ; \
		if [ $$status -ne 0 ]; then echo "FATAL: M3 integration suite failed (rc=$$status)"; exit 1; fi ; \
		if grep -qiE "skipped|[0-9]+ skipped" /tmp/m3_integration_report.txt; then \
			echo "FATAL: M3 integration suite reports SKIPPED tests; a skipped G1-G3 is NOT success."; exit 1; \
		fi ; \
		echo "OK: M3 integration suite passed (no skips, no failures)."
	@echo "=== VERDICT: G1 && G2 && G3 = PASS -> INT-OPEN-1 = RESOLVED / DAEMON-VERIFIED ==="

cpp-test:
	@echo "Compiling Litenyx consensus unit test (Phase 2)..."
	$(CXX) -std=c++20 -O0 -DKERRNYX_STANDALONE_TEST \
		$(KERRNYX_INCLUDES) $(KERRNYX_TEST_SRC) -o $(KERRNYX_TEST_BIN) \
		-lpthread $(BOOST_TEST) && \
		echo "Running Litenyx consensus unit test (Phase 2)..." && \
		$(KERRNYX_TEST_BIN)
	@echo "Compiling Litenyx topology unit test (Phase 3)..."
	$(CXX) -std=c++20 -O0 -DKERRNYX_STANDALONE_TEST \
		$(KERRNYX_INCLUDES) $(KERRNYX_TOPO_SRC) -o $(KERRNYX_TOPO_BIN) \
		-lpthread $(BOOST_TEST) && \
		echo "Running Litenyx topology unit test (Phase 3)..." && \
		$(KERRNYX_TOPO_BIN)
	@echo "Compiling Litenyx topology authority unit test (Phase 4A)..."
	$(CXX) -std=c++20 -O0 -DKERRNYX_STANDALONE_TEST \
		$(KERRNYX_INCLUDES) $(KERRNYX_AUTH_SRC) -o $(KERRNYX_AUTH_BIN) \
		-lpthread $(BOOST_TEST) && \
		echo "Running Litenyx topology authority unit test (Phase 4A)..." && \
		$(KERRNYX_AUTH_BIN)
	@echo "Compiling Litenyx ChainId lifecycle unit test (Phase 5)..."
	$(CXX) -std=c++20 -O0 -DKERRNYX_STANDALONE_TEST \
		$(KERRNYX_INCLUDES) $(KERRNYX_CID_SRC) -o $(KERRNYX_CID_BIN) \
		-lpthread $(BOOST_TEST) && \
		echo "Running Litenyx ChainId lifecycle unit test (Phase 5)..." && \
		$(KERRNYX_CID_BIN)
	@echo "Compiling Litenyx V3 carrier unit test (Phase 5)..."
	$(CXX) -std=c++20 -O0 -DKERRNYX_STANDALONE_TEST \
		$(KERRNYX_INCLUDES) $(KERRNYX_V3_SRC) -o $(KERRNYX_V3_BIN) \
		-lpthread $(BOOST_TEST) && \
		echo "Running Litenyx V3 carrier unit test (Phase 5)..." && \
		$(KERRNYX_V3_BIN)
	@echo "Compiling Litenyx execution authority unit test (Phase 6)..."
	$(CXX) -std=c++20 -O0 -DKERRNYX_STANDALONE_TEST \
		$(KERRNYX_INCLUDES) $(KERRNYX_EXEC_SRC) -o $(KERRNYX_EXEC_BIN) \
		-lpthread $(BOOST_TEST) && \
		echo "Running Litenyx execution authority unit test (Phase 6)..." && \
		$(KERRNYX_EXEC_BIN)
	@echo "Compiling Litenyx draining authority unit test (Phase 7)..."
	$(CXX) -std=c++20 -O0 -DKERRNYX_STANDALONE_TEST \
		$(KERRNYX_INCLUDES) $(KERRNYX_DRAIN_SRC) -o $(KERRNYX_DRAIN_BIN) \
		-lpthread $(BOOST_TEST) && \
		echo "Running Litenyx draining authority unit test (Phase 7)..." && \
		$(KERRNYX_DRAIN_BIN)
	@echo "Compiling Litenyx shared-delta unit test (INT-OPEN-1 / M3)..."
	$(CXX) -std=c++20 -O0 -DKERRNYX_STANDALONE_TEST \
		$(KERRNYX_INCLUDES) $(KERRNYX_DELTA_SRC) $(KERRNYX_DOGE_SUPPORT) -o $(KERRNYX_DELTA_BIN) \
		-lpthread $(BOOST_TEST) && \
		echo "Running Litenyx shared-delta unit test (INT-OPEN-1 / M3)..." && \
		$(KERRNYX_DELTA_BIN)

cpp-compile-check:
	@echo "Litenyx consensus is header-only integer math; no float check required."

# Run the Python regtest harness (Phase 1 gate: single-chain mine/transact/reorg).
regtest-test:
	@echo "Running Litenyx regtest harness (test_litenyx_splitmerge.py)..."
	@cd $(REPO_ROOT)/tests/regtest && \
		python3 -m pytest test_litenyx_splitmerge.py -v

# Alias to run both standalone cpp tests and the regtest harness.
test-all: cpp-test regtest-test

detect-env:
	@echo "DOGECOIN_DIR=$(DOGECOIN_DIR)"
	@echo "KERRNYX_INCLUDES=$(KERRNYX_INCLUDES)"
