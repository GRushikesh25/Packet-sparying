# DCN_1b — Design Document

## 1. Project Objectives

This project implements and evaluates **Packet Spraying** as an alternative to
**ECMP (Equal-Cost Multi-Path)** routing for elephant flows in data-centre
networks.  The study uses NS-3 (version 3.40) to simulate two canonical
data-centre topologies:

- **Fat-Tree** (k-ary, k=4 by default): 4-tier, 16 hosts, 4 core / 8 agg / 8 edge switches.
- **Spine-Leaf** (2-tier Clos, default 4 spine × 8 leaf × 4 hosts/leaf = 32 hosts).

Each topology is simulated twice — once with ECMP and once with Packet Spraying
— and the results (throughput, FCT, delay, packet loss) are compared.

### Research Questions

1. Does per-packet spreading (spraying) achieve better link utilisation than
   flow-level ECMP for elephant flows sharing a fat-tree fabric?
2. How does the benefit translate to a spine-leaf topology where every leaf
   already has equal-cost paths to every spine?
3. What is the impact of spraying on small (mouse) flows that share the fabric?

---

## 2. Topology Design Decisions

### 2.1 Fat-Tree (k=4)

```
          [core 0] [core 1] [core 2] [core 3]
             |   \/   |       |  \/   |
           [agg]      [agg] [agg]   [agg]      <- 2 per pod, 4 pods
             |          |     |        |
           [edge]    [edge] [edge]  [edge]      <- 2 per pod
             |          |     |        |
           h h          h h   h h     h h      <- 2 per edge = 16 hosts total
```

- **k=4**: 4 pods, each with k/2=2 aggregation and k/2=2 edge switches.
- **(k/2)^2 = 4** core switches; **k^3/4 = 16** hosts.
- A core switch `c` connects to aggregation switch `a` in every pod at index
  `c = a * (k/2) + j` for `j ∈ [0, k/2)`, creating the regular Clos structure.
- Link bandwidths: **10 Gbps** on core-agg and agg-edge; **1 Gbps** on edge-host.
  Propagation delays: **5 us** for fabric links, **10 us** for edge-host links.

### 2.2 Spine-Leaf

```
      [spine 0] [spine 1] [spine 2] [spine 3]
         |  |  |  |  | … full mesh … | |
      [leaf 0] [leaf 1] … [leaf 7]
         |   |    |   |       |  |
       h h  h h  h h  h h   h  h     <- 4 hosts per leaf = 32 total
```

- Every spine is connected to every leaf (full mesh), giving `numSpine`
  equal-cost paths between any two hosts on different leaves.
- Link bandwidths: **10 Gbps** spine-leaf, **1 Gbps** leaf-host.

### 2.3 IP Addressing

All point-to-point links are numbered from a shared **10.x.y.0/30** pool.
A static 16-bit counter (`g_ftSubnet` / `g_slSubnet`) increments after each
link so every link gets a unique subnet.  The `/30` mask provides exactly
two usable host addresses per link (`.1` and `.2`).

---

## 3. Spray Routing Algorithm

The custom `SprayRouting` class (`src/packet-spraying/spray-routing.h/.cc`)
implements a per-packet load-balancing routing protocol as an NS-3
`Ipv4RoutingProtocol`.

### 3.1 Route Table

Routes are stored as a flat `std::vector<RouteEntry>` where each entry holds:

```
{ dest (masked), mask, gateway, interface, metric }
```

Multiple entries with the **same (dest, mask)** but different gateways form an
**ECMP set** for that prefix.  The metric field selects the "best" tier; only
entries with the minimum metric for a prefix enter the active ECMP set.

Routes are populated at simulation startup by `FatTreeBuilder` /
`SpineLeafBuilder` via explicit `AddRoute()` calls — there is no dynamic
routing protocol.

### 3.2 Forwarding Decision

`LookupRoute(dest, isElephant)`:

1. **Longest-prefix match**: scan all routes, keep those matching `dest &
   mask == entry.dest`.  Retain only entries with the longest prefix length.
2. **Metric filter**: from the LPM candidates, keep only those with the
   minimum metric (the "ECMP tier").
3. **Path selection**:
   - If `isElephant == true` and `m_sprayElephantOnly == true` and there are
     multiple ECMP routes:
     → **Round-robin** over the candidate set, keyed per destination.
     A per-destination counter `m_rrCounter[dest]` increments on every packet.
   - Otherwise (mouse flow or single path):
     → **Hash-based** selection: `h = (dest.Get() * 2654435761u) >> 16`,
       `selected = ecmpSet[h % size]`.  This is stable per destination and
       approximates per-flow ECMP.

### 3.3 Elephant Detection

`RouteOutput()` and `RouteInput()` check for an `ElephantTag` on the packet
using `PeekPacketTag()`.  If the tag is present, `isElephant = true` is passed
to `LookupRoute`.

The tag (`ElephantTag`) is a minimal NS-3 `Tag` subclass serialising a 4-byte
flow ID.  In the current simulation, elephant flows are created with
`BulkSendHelper` and the elephant tag would be added at the socket level
(future work: subclass `BulkSendApplication` to stamp each packet).  The
routing code is fully prepared to act on the tag whenever it is present.

---

## 4. Elephant Flow Detection and Traffic Model

### 4.1 Elephant Flows (TCP, 50 MB)

- Source: `BulkSendApplication` with `MaxBytes = 50 × 1024 × 1024`.
- Segment size: 1448 bytes (~MSS with TCP header).
- Start time: uniform in `[0, 0.5]` seconds.
- Pairs are selected deterministically: `src = i % numHosts`,
  `dst = (i + 1 + i % (numHosts-1)) % numHosts`, ensuring no self-loops.

### 4.2 Mouse Flows (UDP, 512 B, 10 Mbps)

- Source: `OnOffApplication` over UDP.
- On/Off intervals: exponential with mean 100 ms.
- Packet size: 512 bytes at 10 Mbps.
- Start time: uniform in `[0, 1.0]` seconds.
- Pairs selected via separate stride pattern (`src = (i*3+2) % N`,
  `dst = (i*5+7) % N`).

### 4.3 Sinks

`PacketSinkHelper` is installed on every host for:
- TCP port 5000 (base elephant sink)
- UDP port 6000 (base mouse sink)
- Per-flow sinks on ports 5001+ (TCP) and 6001+ (UDP)

---

## 5. Metrics Collected

All metrics are extracted from the **NS-3 FlowMonitor** XML output
(`results/*-flowmon.xml`) by `scripts/analyze.py`.

| Metric | Computation |
|--------|-------------|
| **Throughput (Mbps)** | `rxBytes × 8 / (lastRxTime - firstTxTime) / 1e6` |
| **Flow Completion Time (s)** | `lastRxTime - firstTxTime` (approximation) |
| **Average packet delay (ms)** | `delaySum / rxPackets / 1e6` |
| **Packet loss rate** | `lostPackets / txPackets` |

Flows are classified as **elephant** (`rxBytes >= 1 MB`) or **mouse** for
separate reporting.  Five plots are generated:

1. `throughput_comparison.png` — bar chart, elephant vs mouse, all scenarios.
2. `fct_comparison.png` — bar chart of average FCT.
3. `delay_comparison.png` — bar chart of average per-packet delay.
4. `throughput_cdf.png` — empirical CDF of per-flow throughput (elephant & mouse).
5. `packet_loss.png` — bar chart of packet loss rate per scenario.

---

## 6. Source Code Layout

```
DCN_1b/
├── src/
│   ├── packet-spraying/          NS-3 module: routing protocol
│   │   ├── spray-routing.h/.cc   SprayRouting Ipv4RoutingProtocol
│   │   ├── elephant-tag.h/.cc    Tag marking elephant-flow packets
│   │   ├── spray-routing-helper.h/.cc  InternetStack helper
│   │   └── wscript               WAF build descriptor (module: dcn-spray)
│   ├── topologies/               Topology builders
│   │   ├── fat-tree-builder.h/.cc
│   │   └── spine-leaf-builder.h/.cc
│   └── traffic/                  Application-layer traffic
│       └── flow-manager.h/.cc
├── simulations/                  Short main() entry points
│   ├── fat-tree-simulation.cc    (~110 lines)
│   └── spine-leaf-simulation.cc  (~110 lines)
├── scripts/
│   ├── analyze.py                FlowMonitor XML parser + 5 plots
│   ├── run-simulations.sh        Run all 4 scenarios (requires built NS-3)
│   └── run-single.sh             Run one scenario interactively
├── results/                      XML + PNG output (git-ignored)
├── docs/
│   └── design.md                 This document
├── run.sh                        One-command setup + run
└── README.md
```

### Module Assembly

`run.sh` copies the entire `src/` directory tree into `ns3/src/dcn-spray/` and
places `src/packet-spraying/wscript` at `ns3/src/dcn-spray/wscript`.  The
`wscript` declares a single NS-3 module named `dcn-spray` that lists all six
`.cc` source files across the three sub-directories.  Simulation `.cc` files
include module headers via the standard `ns3/` prefix (e.g.
`#include "ns3/fat-tree-builder.h"`).

---

## 7. Expected Results

### Elephant Flows

| Scenario | Expected behaviour |
|----------|--------------------|
| Fat-Tree ECMP | Flows hash to fixed paths; some paths may be overloaded if hashes collide, leading to lower throughput and higher FCT for some flows. |
| Fat-Tree Spray | Round-robin spreads each elephant flow across all k/2 = 2 core paths per agg, achieving better bandwidth utilisation and lower FCT. |
| Spine-Leaf ECMP | Static routing picks one spine per flow; under high load some spines are hotter than others. |
| Spine-Leaf Spray | Each packet is forwarded to a different spine, perfectly balancing load across all `numSpine` paths. |

### Mouse Flows

Mouse flows use hash-based ECMP in all cases (they are not tagged as elephant).
Spraying should not significantly affect mouse flow latency; a small increase in
out-of-order delivery is possible but UDP flows do not retransmit.

### Packet Loss

Both routing strategies should have near-zero packet loss under the default load
(10 elephant + 40 mouse flows across 16-32 hosts).  A small increase in loss
may appear for ECMP under hot-spot conditions.

---

## 8. References

1. Al-Fares et al., "A Scalable, Commodity Data Center Network Architecture,"
   ACM SIGCOMM 2008.
2. Dixit et al., "Towards an Elastic Distributed SDN Controller," ACM HotSDN
   2013.
3. Alizadeh et al., "CONGA: Distributed Congestion-Aware Load Balancing for
   Datacenters," ACM SIGCOMM 2014.
4. NS-3 Documentation: https://www.nsnam.org/docs/
5. NS-3 FlowMonitor module: https://www.nsnam.org/docs/models/html/flow-monitor.html
