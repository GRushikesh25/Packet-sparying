#!/usr/bin/env bash
# run.sh — One-command setup and execution for DCN_1b.
#
# Project: Packet Spraying vs ECMP on Fat-Tree & Spine-Leaf topologies
#          implemented as an NS-3 C++ simulation.
#
# Usage:
#   bash run.sh
#
# What this script does:
#   1. Check system dependencies (g++, python3, cmake, ninja, wget, tar)
#   2. Download NS-3.40 (~90 MB) if not already present
#   3. Assemble the dcn-spray NS-3 module:
#        src/  ->  ns3/src/dcn-spray/
#        (packet-spraying/, topologies/, traffic/ sub-dirs preserved)
#        src/packet-spraying/wscript  ->  ns3/src/dcn-spray/wscript
#   4. Copy simulations/*.cc -> ns3/scratch/
#   5. Build NS-3 (one-time ~10 min; incremental afterwards)
#   6. Run all 4 scenarios:
#        fat-tree   ecmp  |  fat-tree   spray
#        spine-leaf ecmp  |  spine-leaf spray
#   7. Call scripts/analyze.py to generate comparison plots
#
# Total time: ~10-15 min first run, ~1-2 min subsequent runs.

set -euo pipefail

NS3_VERSION="3.40"
NS3_BUNDLE="ns-allinone-${NS3_VERSION}"
NS3_DIR="${NS3_BUNDLE}/ns-${NS3_VERSION}"
REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
RESULTS="${REPO_DIR}/results"

# ── Colour helpers ────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; BOLD='\033[1m'; RESET='\033[0m'

info()    { echo -e "${BLUE}[INFO]${RESET}  $*"; }
success() { echo -e "${GREEN}[OK]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[WARN]${RESET}  $*"; }
error()   { echo -e "${RED}[ERROR]${RESET} $*"; exit 1; }
banner()  { echo -e "\n${BOLD}$*${RESET}"; echo "$(printf '─%.0s' {1..60})"; }

# ── Header ────────────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}╔══════════════════════════════════════════════════════════╗${RESET}"
echo -e "${BOLD}║   DCN_1b — Packet Spraying of Elephant Flows (NS-3)     ║${RESET}"
echo -e "${BOLD}║   Fat-Tree & Spine-Leaf Topologies  |  ECMP vs Spray    ║${RESET}"
echo -e "${BOLD}╚══════════════════════════════════════════════════════════╝${RESET}"
echo ""

# ── Step 1: Check dependencies ────────────────────────────────────────
banner "Step 1/6 — Checking dependencies"

MISSING=()
for cmd in g++ python3 cmake ninja wget tar; do
    if command -v "$cmd" &>/dev/null; then
        success "$cmd found"
    else
        warn "$cmd NOT found"
        MISSING+=("$cmd")
    fi
done

if [ ${#MISSING[@]} -gt 0 ]; then
    echo ""
    echo -e "${RED}Missing dependencies: ${MISSING[*]}${RESET}"
    echo "Install with:"
    echo "  sudo apt update && sudo apt install -y g++ python3 cmake ninja-build wget tar"
    error "Please install missing packages and re-run."
fi

# Check Python packages
python3 -c "import matplotlib, numpy" 2>/dev/null || {
    warn "Python packages missing — installing matplotlib numpy..."
    pip3 install -q matplotlib numpy
}
success "Python packages OK"

# ── Step 2: Download NS-3 ─────────────────────────────────────────────
banner "Step 2/6 — Downloading NS-3 ${NS3_VERSION}"

if [ ! -d "${NS3_DIR}" ]; then
    if [ ! -f "${NS3_BUNDLE}.tar.bz2" ]; then
        info "Downloading NS-3 ${NS3_VERSION} (~90 MB)..."
        wget -q --show-progress \
            "https://www.nsnam.org/releases/${NS3_BUNDLE}.tar.bz2" \
            || error "Download failed. Check internet connection."
    else
        info "Archive already downloaded."
    fi
    info "Extracting..."
    tar -xjf "${NS3_BUNDLE}.tar.bz2"
    success "NS-3 extracted to ${NS3_DIR}"
else
    success "NS-3 ${NS3_VERSION} already present."
fi

# ── Step 3: Assemble dcn-spray NS-3 module ────────────────────────────
banner "Step 3/6 — Installing dcn-spray module into NS-3"

DCN_DST="${NS3_DIR}/src/dcn-spray"
rm -rf "${DCN_DST}"
mkdir -p "${DCN_DST}"

# Copy entire src/ tree (packet-spraying/, topologies/, traffic/)
cp -r "${REPO_DIR}/src/." "${DCN_DST}/"

# Place the module wscript at the module root
cp "${REPO_DIR}/src/packet-spraying/wscript" "${DCN_DST}/wscript"

success "dcn-spray module assembled at ${DCN_DST}"
info "  Sub-directories:"
ls "${DCN_DST}/" | sed 's/^/    /'

# ── Step 4: Copy simulation scripts ───────────────────────────────────
banner "Step 4/6 — Copying simulation scripts to NS-3 scratch/"

cp "${REPO_DIR}/simulations/fat-tree-simulation.cc"   "${NS3_DIR}/scratch/"
cp "${REPO_DIR}/simulations/spine-leaf-simulation.cc" "${NS3_DIR}/scratch/"

mkdir -p "${RESULTS}"
# Symlink results dir so NS-3 can write XML files there
ln -sfn "${RESULTS}" "${NS3_DIR}/results" 2>/dev/null || \
    { rm -f "${NS3_DIR}/results" && ln -s "${RESULTS}" "${NS3_DIR}/results"; }

success "Scripts copied to NS-3 scratch/"

# ── Step 5: Build NS-3 ────────────────────────────────────────────────
banner "Step 5/6 — Building NS-3 (first run ~10 min)"

cd "${NS3_DIR}"

BUILD_MARKER=".dcn_spray_built"
if [ ! -f "${BUILD_MARKER}" ]; then
    info "Configuring NS-3..."
    python3 ns3 configure --enable-examples --disable-python \
        2>&1 | grep -E "^(--| |Modules|Build)" | head -20 || true

    info "Building NS-3 — please wait..."
    python3 ns3 build 2>&1 | tail -5
    touch "${BUILD_MARKER}"
    success "NS-3 build complete."
else
    info "NS-3 already built. Rebuilding changed files only..."
    python3 ns3 build 2>&1 | tail -5
    success "Incremental build done."
fi

# ── Step 6: Run simulations ───────────────────────────────────────────
banner "Step 6/6 — Running all 4 scenarios"

run_sim() {
    local label="$1"; local script="$2"; local args="$3"
    info "Running: ${label}"
    python3 ns3 run "${script} ${args}" 2>&1 \
        | grep -v "^$" \
        | tee "${RESULTS}/${label// /-}.log" \
        | grep -E "^\[|FlowID|Total|^---" || true
    success "${label} — done"
    echo ""
}

run_sim "Fat-Tree ECMP" \
    "fat-tree-simulation" \
    "--k=4 --routing=ecmp --simTime=5 --elephants=10 --mice=40 --seed=1 --flowmon=true"

run_sim "Fat-Tree Spray" \
    "fat-tree-simulation" \
    "--k=4 --routing=spray --simTime=5 --elephants=10 --mice=40 --seed=1 --flowmon=true"

run_sim "Spine-Leaf ECMP" \
    "spine-leaf-simulation" \
    "--numSpine=4 --numLeaf=8 --hostsPerLeaf=4 --routing=ecmp --simTime=5 --elephants=10 --mice=40 --seed=1 --flowmon=true"

run_sim "Spine-Leaf Spray" \
    "spine-leaf-simulation" \
    "--numSpine=4 --numLeaf=8 --hostsPerLeaf=4 --routing=spray --simTime=5 --elephants=10 --mice=40 --seed=1 --flowmon=true"

# ── Analyse & plot ────────────────────────────────────────────────────
cd "${REPO_DIR}"
info "Generating comparison plots..."
python3 scripts/analyze.py --results-dir "${RESULTS}"

# ── Summary ───────────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}╔══════════════════════════════════════════════════════════╗${RESET}"
echo -e "${BOLD}║                    SIMULATION COMPLETE                  ║${RESET}"
echo -e "${BOLD}╚══════════════════════════════════════════════════════════╝${RESET}"
echo ""
echo -e "  Results saved to: ${GREEN}${RESULTS}/${RESET}"
echo ""
echo "  FlowMonitor data (XML):"
ls "${RESULTS}"/*.xml 2>/dev/null | while read f; do echo "    $f"; done || true
echo ""
echo "  Comparison plots (PNG):"
ls "${RESULTS}"/*.png 2>/dev/null | while read f; do echo "    $f"; done || true
echo ""
echo -e "  ${BOLD}Open the PNG files in any image viewer to see results.${RESET}"
echo ""
