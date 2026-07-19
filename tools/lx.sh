#!/usr/bin/env bash
#
# lx.sh - Litenyx task entry point.
#
# One script for the recurring dev/CI chores. Every run captures combined
# stdout+stderr to log/<cmd>-<timestamp>.log (log/ is gitignored) and echoes
# the log path at the end.
#
# Usage:
#   tools/lx.sh <command> [options]
#
# Commands:
#   ci-poll            Poll the latest CI run for this branch until it finishes.
#   ci-latest          Show the latest CI run (status/conclusion) and exit.
#   ci-log [RUN_ID]    Fetch full failed-step logs (latest run if RUN_ID omitted).
#   syntax             -fsyntax-only -Wall -Wextra on the Litenyx C++ headers.
#   cpp-smoke          Compile + run the standalone header smoke driver.
#   help               This help.
#
# Global options:
#   -t, --timeout SEC  Max seconds for ci-poll (default 1500) or a compile
#                      (default 120). 0 = no timeout.
#   -i, --interval SEC Poll interval for ci-poll (default 20).
#   -b, --branch NAME  Branch for CI commands (default: current git branch).
#   -q, --quiet        Only print the final result line + log path.
#   -h, --help         Show help.
#
# Requires: gh (for ci-*), g++ (for syntax/cpp-smoke).

set -u

# --- locate repo root (script lives in tools/) ------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="$REPO_ROOT/log"
mkdir -p "$LOG_DIR"

# --- defaults ---------------------------------------------------------------
TIMEOUT=""
INTERVAL=20
BRANCH=""
QUIET=0
CMD=""

die()  { printf '\033[31merror:\033[0m %s\n' "$*" >&2; exit 2; }
info() { [ "$QUIET" -eq 1 ] || printf '%s\n' "$*"; }

usage() { sed -n '2,40p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

# --- parse args -------------------------------------------------------------
[ $# -ge 1 ] || { usage; exit 1; }
CMD="$1"; shift
while [ $# -gt 0 ]; do
    case "$1" in
        -t|--timeout)  TIMEOUT="${2:-}"; shift 2 ;;
        -i|--interval) INTERVAL="${2:-}"; shift 2 ;;
        -b|--branch)   BRANCH="${2:-}"; shift 2 ;;
        -q|--quiet)    QUIET=1; shift ;;
        -h|--help)     usage; exit 0 ;;
        *)             EXTRA_ARG="$1"; shift ;;
    esac
done

cur_branch() { git -C "$REPO_ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null; }
[ -n "$BRANCH" ] || BRANCH="$(cur_branch)"

ts() { date +%Y%m%d-%H%M%S; }
new_log() { echo "$LOG_DIR/$1-$(ts).log"; }

need() { command -v "$1" >/dev/null 2>&1 || die "'$1' not found in PATH"; }

# The header files to syntax-check (extend as engine grows).
CPP_HEADERS=(
    "litenyx/LITENYX_topology_authority.h"
    "litenyx/LITENYX_topology.h"
    "litenyx/LITENYX_types.h"
    "litenyx/LITENYX_auxpow.h"
)

# ---------------------------------------------------------------------------
cmd_ci_latest() {
    need gh
    local log; log="$(new_log ci-latest)"
    {
        echo "# branch: $BRANCH"
        gh run list --branch "$BRANCH" --workflow "Litenyx CI" --limit 1 \
            --json databaseId,status,conclusion,headSha,createdAt \
            -q '.[] | "run \(.databaseId)  \(.status)/\(.conclusion // "pending")  sha \(.headSha[0:7])  \(.createdAt)"'
    } 2>&1 | tee "$log"
    info "log: $log"
}

cmd_ci_poll() {
    need gh
    local to="${TIMEOUT:-1500}"
    local log; log="$(new_log ci-poll)"
    local start; start="$(date +%s)"
    local run_id status concl
    run_id="$(gh run list --branch "$BRANCH" --workflow "Litenyx CI" --limit 1 --json databaseId -q '.[0].databaseId')"
    [ -n "$run_id" ] || die "no 'Litenyx CI' run found for branch '$BRANCH'"
    {
        echo "# polling run $run_id on $BRANCH (timeout=${to}s interval=${INTERVAL}s)"
        while :; do
            status="$(gh run view "$run_id" --json status -q .status 2>/dev/null)"
            concl="$(gh run view "$run_id" --json conclusion -q '.conclusion // ""' 2>/dev/null)"
            echo "[$(date +%H:%M:%S)] $status / ${concl:-pending}"
            [ "$status" = "completed" ] && break
            if [ "$to" != "0" ]; then
                local now; now="$(date +%s)"
                [ $((now - start)) -ge "$to" ] && { echo "TIMEOUT after ${to}s"; break; }
            fi
            sleep "$INTERVAL"
        done
        if [ "$status" = "completed" ]; then
            echo "--- steps ---"
            gh run view "$run_id" --json jobs \
                -q '.jobs[] | .name + ": " + .conclusion, (.steps[] | "  - " + .name + ": " + .conclusion)'
        fi
    } 2>&1 | tee "$log"
    info "log: $log"
    [ "$concl" = "success" ]
}

cmd_ci_log() {
    need gh
    local run_id="${EXTRA_ARG:-}"
    [ -n "$run_id" ] || run_id="$(gh run list --branch "$BRANCH" --workflow "Litenyx CI" --limit 1 --json databaseId -q '.[0].databaseId')"
    [ -n "$run_id" ] || die "no run id"
    local log; log="$(new_log ci-log-$run_id)"
    gh run view "$run_id" --log-failed 2>&1 | tee "$log"
    info "log: $log"
}

cmd_syntax() {
    need g++
    local log; log="$(new_log syntax)"
    local rc=0 h
    {
        for h in "${CPP_HEADERS[@]}"; do
            echo "== $h =="
            if g++ -std=c++20 -fsyntax-only -Wall -Wextra \
                -DKERRNYX_STANDALONE_TEST -I "$REPO_ROOT" -x c++ "$REPO_ROOT/$h"; then
                echo "  OK"
            else
                echo "  FAIL"; rc=1
            fi
        done
        echo "syntax rc=$rc"
    } 2>&1 | tee "$log"
    info "log: $log"
    return $rc
}

cmd_cpp_smoke() {
    need g++
    local log; log="$(new_log cpp-smoke)"
    local drv="$REPO_ROOT/log/_smoke.cpp"
    local bin="$REPO_ROOT/log/_smoke.exe"
    cat > "$drv" <<'EOF'
#include <litenyx/LITENYX_topology_authority.h>
#include <cstdio>
#define CK(x) do{ if(!(x)){ printf("FAIL: %s\n", #x); return 1; } }while(0)
int main(){
  CK(LitenyxDemandV1(LITENYX_MAX_BLOCK_WEIGHT)==LITENYX_DEMAND_SCALE);
  CK(LitenyxDemandV1(LITENYX_MAX_BLOCK_WEIGHT/2)==5000);
  CK(LitenyxMcToControllerInput(8099)==80);
  CK(LitenyxMcToControllerInput(8100)==81);
  std::vector<LitenyxCommittedBlock> w={{0,LITENYX_MAX_BLOCK_WEIGHT},{0,LITENYX_MAX_BLOCK_WEIGHT},{1,LITENYX_MAX_BLOCK_WEIGHT/2}};
  auto mc=LitenyxReconstructMcV1Window(w,3);
  CK(mc[0]==10000); CK(mc[1]==5000); CK(mc[2]==0);
  auto rt=LitenyxTopoActivationRegtest();
  CK(rt.RegimeAt(99)==LitenyxTopoRegime::PreDerivation);
  CK(rt.RegimeAt(300)==LitenyxTopoRegime::HardAuthority);
  CK(LitenyxTopoActivationMainnet().IsDisabled());
  printf("CPP SMOKE OK\n");
  return 0;
}
EOF
    local to="${TIMEOUT:-120}"
    {
        echo "# compiling smoke driver"
        if g++ -std=c++20 -O0 -Wall -Wextra -DKERRNYX_STANDALONE_TEST \
            -I "$REPO_ROOT" "$drv" -o "$bin" 2>&1; then
            echo "# running"
            "$bin"
        else
            echo "COMPILE FAILED"
        fi
    } 2>&1 | tee "$log"
    local rc=${PIPESTATUS[0]}
    rm -f "$drv" "$bin"
    info "log: $log"
    return $rc
}

case "$CMD" in
    ci-poll)   cmd_ci_poll ;;
    ci-latest) cmd_ci_latest ;;
    ci-log)    EXTRA_ARG="${EXTRA_ARG:-}"; cmd_ci_log ;;
    syntax)    cmd_syntax ;;
    cpp-smoke) cmd_cpp_smoke ;;
    help|-h|--help) usage ;;
    *) die "unknown command '$CMD' (try: tools/lx.sh help)" ;;
esac
