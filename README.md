# Packet Spraying of Elephant Flows — NS-3 Simulation

Academic project comparing **ECMP** vs **Packet Spraying** for elephant flow
load balancing on **Fat-Tree** and **Spine-Leaf** data-centre topologies using
[NS-3](https://www.nsnam.org/).

---

## Project Structure

```
Packet-sparying/
├── src/
│   └── spray-routing/              NS-3 module (copy to <ns3>/src/)
│       ├── model/
│       │   ├── spray-routing.h/.cc  Custom Ipv4RoutingProtocol
│       │   ├── elephant-tag.h/.cc   PacketTag marking elephant flows
│       └── helper/
│           └── spray-routing-helper.h/.cc
│       └── wscript                 NS-3 WAF build file
│
├── scratch/                        NS-3 simulation scripts
│   ├── fat-tree-simulation.cc      Fat-Tree topology (k-ary)
│   └── spine-leaf-simulation.cc    Spine-Leaf topology
│
├── scripts/
│   ├── setup.sh                    Download NS-3, install module, build
│   ├── run-simulations.sh          Run all 4 scenarios + generate plots
│   ├── run-single.sh               Run one scenario interactively
│   └── analyze.py                  Parse FlowMonitor XML → plots
│
└── results/                        Output: XML files + PNG plots
```

---

## Topologies

### Fat-Tree (k-ary)

```
         [core_0] [core_1] [core_2] [core_3]
            |  \  /  |      |  \  /  |
     [agg]  pod0     pod1   pod2     pod3
            |                           |
     [edge] pod0/e0  pod0/e1  ...
            |
     [host] h0 h1
```

- **k** pods, each with k/2 aggregation + k/2 edge switches
- **(k/2)²** core switches
- **k³/4** hosts total (k=4 → 16 hosts)
- Equal-cost paths between any two hosts: `(k/2)²`

### Spine-Leaf

```
  [spine_0] [spine_1] [spine_2] [spine_3]
      |   ×   |   ×   |   ×   |    (full mesh)
  [leaf_0] [leaf_1] ... [leaf_7]
      |         |
    h0,h1     h0,h1
```

- `numSpine` × `numLeaf` full-mesh uplinks
- Every inter-leaf flow can use any spine switch (ECMP set = numSpine)

---

## Routing Strategies

| Strategy | Elephant flows | Mouse flows |
|----------|---------------|-------------|
| **ECMP** | single path (5-tuple hash) | single path (5-tuple hash) |
| **Packet Spraying** | **round-robin across all equal-cost paths** | single path (hash) |

The custom `SprayRouting` (`src/spray-routing`) is a full
`Ipv4RoutingProtocol` implementation.  It stores multiple next-hops per
destination prefix and selects the outgoing path per-packet based on an
`ElephantTag` packet tag.

---

## Quick Start

### 1. Prerequisites (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install -y g++ python3 cmake ninja-build git \
    libgsl-dev python3-pip wget
pip3 install matplotlib numpy seaborn pandas
```

### 2. Setup (download NS-3, install module, build)

```bash
bash scripts/setup.sh         # uses NS-3.40 by default
bash scripts/setup.sh 3.38    # specify a version
```

### 3. Run all simulations

```bash
bash scripts/run-simulations.sh ns-allinone-3.40/ns-3.40
```

This runs **4 scenarios** and writes results to `results/`:

| Scenario | XML file |
|----------|----------|
| Fat-Tree ECMP   | `results/fat-tree-ecmp-flowmon.xml`   |
| Fat-Tree Spray  | `results/fat-tree-spray-flowmon.xml`  |
| Spine-Leaf ECMP | `results/spine-leaf-ecmp-flowmon.xml` |
| Spine-Leaf Spray| `results/spine-leaf-spray-flowmon.xml`|

### 4. Run a single scenario

```bash
# Fat-Tree, packet spray, k=8
bash scripts/run-single.sh ns-allinone-3.40/ns-3.40 fat-tree spray --k=8

# Spine-Leaf, ECMP, longer simulation
bash scripts/run-single.sh ns-allinone-3.40/ns-3.40 spine-leaf ecmp --simTime=10
```

### 5. Analyse results

```bash
python3 scripts/analyze.py --results-dir results
```

Plots written to `results/`:

| Plot | Description |
|------|-------------|
| `throughput_comparison.png` | Avg throughput per flow class per scenario |
| `fct_comparison.png`        | Average Flow Completion Time |
| `delay_comparison.png`      | Average packet delay |
| `throughput_cdf.png`        | CDF of per-flow throughput |
| `packet_loss.png`           | Packet loss rate |

---

## Simulation Parameters

### Fat-Tree

| Flag | Default | Description |
|------|---------|-------------|
| `--k` | 4 | Pod count (must be even) |
| `--routing` | spray | `ecmp` or `spray` |
| `--simTime` | 5 | Simulation seconds |
| `--elephants` | 10 | Elephant (BulkSend TCP) flows |
| `--mice` | 40 | Mouse (OnOff UDP) flows |
| `--seed` | 1 | RNG seed |
| `--flowmon` | true | Write FlowMonitor XML |
| `--pcap` | false | Capture PCAP traces |

### Spine-Leaf

Same as Fat-Tree plus:

| Flag | Default | Description |
|------|---------|-------------|
| `--numSpine` | 4 | Spine switch count |
| `--numLeaf` | 8 | Leaf switch count |
| `--hostsPerLeaf` | 4 | Hosts per leaf |

---

## Key Source Files

| File | Role |
|------|------|
| `src/spray-routing/model/spray-routing.h/.cc` | Custom per-packet spray routing protocol |
| `src/spray-routing/model/elephant-tag.h/.cc` | Packet tag identifying elephant flows |
| `src/spray-routing/helper/spray-routing-helper.h/.cc` | NS-3 routing helper |
| `scratch/fat-tree-simulation.cc` | Fat-Tree topology + flow setup |
| `scratch/spine-leaf-simulation.cc` | Spine-Leaf topology + flow setup |
| `scripts/analyze.py` | FlowMonitor XML parser + matplotlib plots |

---

## References

1. Al-Fares, M., Loukissas, A., & Vahdat, A. (2008). *A scalable, commodity data center network architecture.* ACM SIGCOMM.
2. Dixit, A., et al. (2013). *Is it time for networks to change?* HotNets.
3. Cao, J., et al. (2013). *Per-packet load-balanced, low-latency routing for Clos-based data center networks.* CoNEXT.
4. Benson, T., Akella, A., & Maltz, D. A. (2010). *Network traffic characteristics of data centers in the wild.* IMC.
5. NS-3 documentation: https://www.nsnam.org/documentation/
