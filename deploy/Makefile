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

.PHONY: help cpp-test cpp-compile-check detect-env clone-dogecoin inject-hooks production-build regtest-test

help:
	@echo "Litenyx targets:"
	@echo "  cpp-test        Build + run standalone consensus unit tests (boost header-only)"
	@echo "  clone-dogecoin  Clone Dogecoin base into external/dogecoin (gitignored)"
	@echo "  inject-hooks    Copy litenyx/ sources + apply patches into the clone"
	@echo "  production-build Configure + build dogecoind/dogecoin-cli/dogecoin-tx"
	@echo "  detect-env      Print toolchain include/lib paths"

clone-dogecoin:
	@if [ -d "$(DOGECOIN_DIR)/.git" ]; then \
		echo "Dogecoin clone already present."; \
	else \
		echo "Cloning Dogecoin..."; \
		git clone --depth 1 https://github.com/dogecoin/dogecoin.git $(DOGECOIN_DIR); \
	fi

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
