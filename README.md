# DCN_1b — Packet Spraying of Elephant Flows
### NS-3 Simulation — Fat-Tree & Spine-Leaf Data-Centre Topologies

**Course project** comparing **ECMP** vs **Packet Spraying** for handling elephant
flows in data-centre networks.

---

## For Evaluators — Run in One Command

```bash
# Clone the repo
git clone https://github.com/grushikesh25/packet-sparying.git
cd packet-sparying

# Run everything (setup + simulate + plots)
bash run.sh
```

That single script:
1. Checks your system has `g++`, `python3`, `cmake`, `ninja`, `wget`
2. Downloads NS-3.40 (~90 MB, one-time)
3. Assembles the `dcn-spray` NS-3 module from `src/` and builds NS-3 (~10 min first run)
4. Copies `simulations/*.cc` to NS-3 scratch/
5. Runs all 4 simulation scenarios
6. Generates comparison plots in `results/`

**After the first run, re-running takes ~1 minute** (NS-3 is already built).

### System Requirements

| Requirement | Version |
|-------------|---------|
| OS | Ubuntu 20.04 / 22.04 / Debian (Linux) |
| C++ compiler | g++ 9+ |
| Python | 3.8+ |
| RAM | ≥ 4 GB |
| Disk | ≥ 2 GB free |

Install dependencies (if needed):
```bash
sudo apt update
sudo apt install -y g++ python3 cmake ninja-build wget tar
pip3 install matplotlib numpy
```

### Output Files

After running, check the `results/` folder:

| File | Description |
|------|-------------|
| `fat-tree-ecmp-flowmon.xml` | Raw NS-3 flow statistics (Fat-Tree, ECMP) |
| `fat-tree-spray-flowmon.xml` | Raw NS-3 flow statistics (Fat-Tree, Spray) |
| `spine-leaf-ecmp-flowmon.xml` | Raw NS-3 flow statistics (Spine-Leaf, ECMP) |
| `spine-leaf-spray-flowmon.xml` | Raw NS-3 flow statistics (Spine-Leaf, Spray) |
| `throughput_comparison.png` | Average throughput: elephant vs mouse flows |
| `fct_comparison.png` | Flow Completion Time comparison |
| `delay_comparison.png` | Average packet delay |
| `throughput_cdf.png` | CDF of per-flow throughput |
| `packet_loss.png` | Packet loss rate per scenario |

---

## Project Overview

### Problem Statement

In data-centre networks, **elephant flows** (large TCP transfers ≥ 1 MB) cause severe
congestion on a single network path when standard **ECMP routing** is used.
ECMP selects one path per flow using a 5-tuple hash — if two elephant flows
hash to the same path, that path saturates while others stay idle.

**Packet Spraying** solves this by sending each *packet* of an elephant flow on
the *next available equal-cost path* (round-robin), spreading the load across
all paths simultaneously.

---

## Topologies Simulated

### 1. Fat-Tree (k=4)

```
                    ┌──────────────────────────────┐
  Core Layer        │  [C0]  [C1]  [C2]  [C3]      │   4 core switches
                    └────┼────┼────┼────┼───────────┘
                     ╱╲  │   │   │  ╱╲
                    /  ╲ │   │   │ /  ╲
  Aggregation  [A00][A01][A10][A11][A20][A21][A30][A31]  8 agg switches
               ╲╱  ╲╱  ╲╱  ╲╱   ╲╱  ╲╱  ╲╱  ╲╱
  Edge Layer  [E00][E01][E10][E11][E20][E21][E30][E31]  8 edge switches
               ||   ||   ||   ||   ||   ||   ||   ||
  Hosts       H H  H H  H H  H H  H H  H H  H H  H H  16 hosts
```

- **k pods**, each with k/2 aggregation + k/2 edge switches
- **(k/2)² = 4 core** switches
- **k³/4 = 16 hosts** (k=4), **128 hosts** (k=8)
- Each host pair has **4 equal-cost paths** through the core
- Link speeds: host↔edge = **1 Gbps**, all others = **10 Gbps**

### 2. Spine-Leaf

```
  Spine  [S0]──[S1]──[S2]──[S3]
          │╲   │╲    │╲    │╲
          │ ╲  │ ╲   │ ╲   │ ╲     (full mesh: every leaf connects to every spine)
          │  ╲ │  ╲  │  ╲  │  ╲
  Leaf  [L0][L1][L2][L3][L4][L5][L6][L7]
          │    │    │                       8 leaf switches
        H H  H H  H H  ...               32 hosts total
```

- Every inter-leaf traffic can use **any of 4 spine switches** (4 equal-cost paths)
- Link speeds: host↔leaf = **1 Gbps**, leaf↔spine = **10 Gbps**

---

## Routing Strategies

### ECMP (Equal-Cost Multi-Path) — Baseline

```
Flow F1:   A ──[hash=path2]──────────────────────► B
Flow F2:   A ──[hash=path2]──(COLLISION!)──────────► C
                               ↑
                         path2 is now congested
                         path0, path1, path3 are idle
```

All packets of a flow follow the **same single path**, selected by hashing
`(src_ip, dst_ip, src_port, dst_port, protocol)`. Two elephant flows can
hash to the same path and saturate it.

### Packet Spraying — Proposed Solution

```
Elephant flow F1 packets:
  pkt1 ──► path0 ──┐
  pkt2 ──► path1 ──┤──► Destination (reorders in TCP buffer)
  pkt3 ──► path2 ──┤
  pkt4 ──► path3 ──┘

Mouse flows: still use single path (preserves TCP ordering for small flows)
```

The key insight: by distributing *packets* (not flows) across paths, no single
link becomes a bottleneck for elephant traffic.

---

## Repository Layout (DCN_1b)

```
DCN_1b/
├── src/
│   ├── packet-spraying/    spray-routing.h/.cc, elephant-tag.h/.cc,
│   │                       spray-routing-helper.h/.cc, wscript
│   ├── topologies/         fat-tree-builder.h/.cc, spine-leaf-builder.h/.cc
│   └── traffic/            flow-manager.h/.cc
├── simulations/            fat-tree-simulation.cc, spine-leaf-simulation.cc
├── scripts/                analyze.py, run-simulations.sh, run-single.sh
├── results/                (generated: XML + PNG)
├── docs/                   design.md
├── run.sh                  One-command setup + run
└── README.md
```

## Source Code Walkthrough

### `src/packet-spraying/` — Custom NS-3 Module

#### `elephant-tag.h/.cc`
A 4-byte `PacketTag` that gets attached to each packet of an elephant flow.
The routing layer reads this tag to decide spray vs hash.

#### `spray-routing.h/.cc`
The core contribution — a complete `Ipv4RoutingProtocol` implementation:

```
RouteOutput() [called for every outgoing packet]:
  │
  ├── Longest-prefix match: find all routes to destination
  ├── Filter to lowest-metric routes → ECMP candidate set
  │
  ├── Packet has ElephantTag?
  │     YES → index = round_robin_counter[dst]++  % |set|   ← SPRAY
  │     NO  → index = hash(dst)                  % |set|   ← ECMP
  │
  └── Return Ipv4Route with selected nexthop + interface

RouteInput() [called when forwarding at intermediate switches]:
  └── Same logic, then calls ucb(route, packet, header)
```

#### `spray-routing-helper.h/.cc`
Integrates `SprayRouting` with NS-3's `InternetStackHelper` so it can be
installed on nodes with one line:
```cpp
SprayRoutingHelper sh;
internet.SetRoutingHelper(sh);
internet.Install(allNodes);
```

### `src/topologies/`

#### `fat-tree-builder.h/.cc` — `FatTreeBuilder`
Encapsulates the full Fat-Tree construction: node creation, link wiring, IP
assignment and routing-table population. `Build(addr)` takes an
`Ipv4AddressHelper`; afterwards `GetHosts()` and `GetHostAddresses()` expose
the flat host list to the simulation.

#### `spine-leaf-builder.h/.cc` — `SpineLeafBuilder`
Same pattern for a Spine-Leaf (2-tier Clos) topology.

### `src/traffic/flow-manager.h/.cc` — `FlowManager`
Installs `BulkSendApplication` elephant flows (TCP, 50 MB) and `OnOffApplication`
mouse flows (UDP, 512 B, 10 Mbps), plus `PacketSink` on every host.

### `simulations/fat-tree-simulation.cc`
Short `main()` (~110 lines):
1. Parse command-line arguments
2. `FatTreeBuilder::Build()`
3. `FlowManager::InstallSinks()` + `InstallElephantFlows()` + `InstallMouseFlows()`
4. Run FlowMonitor, save XML to `results/fat-tree-{routing}-flowmon.xml`

### `simulations/spine-leaf-simulation.cc`
Same pattern using `SpineLeafBuilder`.

### `scripts/analyze.py`
- Parses FlowMonitor XML using Python's `xml.etree.ElementTree`
- Classifies flows as elephant (rxBytes ≥ 1 MB) or mouse
- Computes: throughput (Mbps), FCT (seconds), avg delay (ms), loss rate
- Generates 5 matplotlib comparison plots

See `docs/design.md` for a detailed design document.

---

## Configurable Parameters

```bash
# Fat-Tree options
./ns3 run "fat-tree-simulation \
  --k=4          # Pod count: 4→16 hosts, 6→54 hosts, 8→128 hosts
  --routing=spray    # ecmp | spray
  --simTime=5        # Simulation duration in seconds
  --elephants=10     # Number of elephant (BulkSend TCP 50 MB) flows
  --mice=40          # Number of mouse (OnOff UDP) flows
  --seed=1           # RNG seed for reproducibility
  --flowmon=true     # Save FlowMonitor XML
  --pcap=false       # Capture PCAP traces (creates .pcap files)"

# Spine-Leaf options (same + topology sizing)
./ns3 run "spine-leaf-simulation \
  --numSpine=4       # Spine switch count
  --numLeaf=8        # Leaf switch count
  --hostsPerLeaf=4   # Hosts per leaf switch"
```

---

## References

1. Al-Fares, M., Loukissas, A., & Vahdat, A. (2008). *A scalable, commodity
   data center network architecture.* ACM SIGCOMM.
2. Dixit, A., et al. (2013). *Is it time for networks to change?* HotNets.
3. Cao, J., et al. (2013). *Per-packet load-balanced, low-latency routing for
   Clos-based data center networks.* CoNEXT.
4. Benson, T., Akella, A., & Maltz, D. A. (2010). *Network traffic
   characteristics of data centers in the wild.* IMC.
5. NS-3 Documentation: https://www.nsnam.org/documentation/
