/**
 * fat-tree-simulation.cc
 * ======================
 * NS-3 simulation of a k-ary Fat-Tree data-centre network comparing
 * ECMP vs Packet Spraying for elephant flow load balancing.
 *
 * Topology (k=4 default)
 * ----------------------
 *   (k/2)^2 = 4  core switches
 *   k=4 pods, each with k/2=2 aggregation and k/2=2 edge switches
 *   k^3/4 = 16 hosts
 *
 *   Link speeds:
 *     core  <-> agg  : 10 Gbps, 5  us
 *     agg   <-> edge : 10 Gbps, 5  us
 *     edge  <-> host : 1  Gbps, 10 us
 *
 * Command-line options
 * --------------------
 *   --k           Fat-tree pod count (even, >= 2)  [4]
 *   --routing     "ecmp" | "spray"                 [spray]
 *   --simTime     Simulation duration (s)           [5]
 *   --elephants   Number of elephant flows          [10]
 *   --mice        Number of mouse flows             [40]
 *   --seed        RNG run number                    [1]
 *   --pcap        Enable PCAP traces                [false]
 *   --flowmon     Output FlowMonitor XML            [true]
 *
 * Build & run
 * -----------
 *   Copy src/ -> ns3/src/dcn-spray/
 *   Copy simulations/*.cc -> ns3/scratch/
 *   cd <ns3-root> && ./ns3 build
 *   ./ns3 run "fat-tree-simulation --routing=spray --k=4"
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-address-helper.h"

// DCN-spray module headers (installed under ns3/ prefix by wscript)
#include "ns3/fat-tree-builder.h"
#include "ns3/flow-manager.h"

#include <string>
#include <iomanip>
#include <sstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("FatTreeSimulation");

// ─────────────────────────────────────────────────────────────────────
int
main (int argc, char *argv[])
{
    // ── Parameters ────────────────────────────────────────────────────
    uint32_t    k           = 4;
    std::string routing     = "spray";
    double      simTime     = 5.0;
    uint32_t    numElephant = 10;
    uint32_t    numMice     = 40;
    uint32_t    seed        = 1;
    bool        enablePcap  = false;
    bool        enableFM    = true;

    CommandLine cmd;
    cmd.AddValue ("k",         "Fat-tree pod count (even, >= 2)", k);
    cmd.AddValue ("routing",   "Routing: ecmp | spray",           routing);
    cmd.AddValue ("simTime",   "Simulation duration (s)",         simTime);
    cmd.AddValue ("elephants", "Number of elephant flows",        numElephant);
    cmd.AddValue ("mice",      "Number of mouse flows",           numMice);
    cmd.AddValue ("seed",      "RNG run number",                  seed);
    cmd.AddValue ("pcap",      "Enable PCAP capture",             enablePcap);
    cmd.AddValue ("flowmon",   "Enable FlowMonitor output",       enableFM);
    cmd.Parse (argc, argv);

    NS_ASSERT_MSG (k >= 2 && k % 2 == 0, "k must be even and >= 2");

    bool useSpray = (routing == "spray");

    NS_LOG_UNCOND ("[FatTree] k=" << k
        << "  routing=" << routing
        << "  hosts=" << (k * k * k / 4)
        << "  elephants=" << numElephant
        << "  mice=" << numMice);

    // ── Build topology ────────────────────────────────────────────────
    Ipv4AddressHelper addrHelper;
    FatTreeBuilder builder (k, useSpray);
    builder.Build (addrHelper);

    NodeContainer            hosts  = builder.GetHosts ();
    std::vector<Ipv4Address> addrs  = builder.GetHostAddresses ();

    // ── Install traffic ───────────────────────────────────────────────
    FlowManager fm (hosts, addrs, seed);
    fm.InstallSinks ();
    fm.InstallElephantFlows (numElephant, simTime);
    fm.InstallMouseFlows    (numMice,     simTime);

    // ── PCAP (optional) ───────────────────────────────────────────────
    if (enablePcap) {
        PointToPointHelper p2p;
        p2p.EnablePcapAll ("fat-tree-" + routing);
    }

    // ── FlowMonitor ───────────────────────────────────────────────────
    Ptr<FlowMonitor>  flowMonitor;
    FlowMonitorHelper fmHelper;
    if (enableFM) {
        flowMonitor = fmHelper.InstallAll ();
    }

    // ── Run ───────────────────────────────────────────────────────────
    Simulator::Stop (Seconds (simTime + 1.0));
    Simulator::Run  ();

    // ── Results ───────────────────────────────────────────────────────
    if (enableFM && flowMonitor) {
        std::string xmlFile = "results/fat-tree-" + routing + "-flowmon.xml";
        flowMonitor->CheckForLostPackets ();
        flowMonitor->SerializeToXmlFile  (xmlFile, true, true);
        NS_LOG_UNCOND ("[FatTree] FlowMonitor saved to " << xmlFile);

        Ptr<Ipv4FlowClassifier> classifier =
            DynamicCast<Ipv4FlowClassifier> (fmHelper.GetClassifier ());
        FlowMonitor::FlowStatsContainer stats = flowMonitor->GetFlowStats ();

        uint64_t totalTx = 0, totalRx = 0;
        double   totalDelay = 0.0;
        uint32_t nFlows = 0;

        NS_LOG_UNCOND ("\nFlowID  Proto  Src->Dst"
                       "                       TxPkts  RxPkts  ThroughputMbps  AvgDelayMs");
        NS_LOG_UNCOND (std::string (85, '-'));

        for (auto &kv : stats) {
            Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (kv.first);
            auto &s = kv.second;
            double dur  = (s.timeLastRxPacket - s.timeFirstTxPacket).GetSeconds ();
            double tput = (dur > 0) ? (s.rxBytes * 8.0) / dur / 1e6 : 0.0;
            double avgD = (s.rxPackets > 0)
                          ? s.delaySum.GetMilliSeconds () / s.rxPackets : 0.0;

            std::ostringstream sd;
            sd << t.sourceAddress << ":" << t.sourcePort
               << "->" << t.destinationAddress << ":" << t.destinationPort;

            NS_LOG_UNCOND (std::setw (6)  << kv.first
                << "  " << (t.protocol == 6 ? "TCP" : "UDP")
                << "  " << std::setw (32) << sd.str ()
                << "  " << std::setw (6)  << s.txPackets
                << "  " << std::setw (6)  << s.rxPackets
                << "  " << std::setw (14) << std::fixed << std::setprecision (2) << tput
                << "  " << std::setw (11) << std::fixed << std::setprecision (2) << avgD);

            totalTx    += s.txPackets;
            totalRx    += s.rxPackets;
            totalDelay += avgD;
            ++nFlows;
        }

        NS_LOG_UNCOND (std::string (85, '-'));
        NS_LOG_UNCOND ("Total flows: " << nFlows
            << "  TxPkts: " << totalTx
            << "  RxPkts: " << totalRx
            << "  AvgDelay: "
            << std::fixed << std::setprecision (2)
            << (nFlows ? totalDelay / nFlows : 0.0) << " ms");
    }

    Simulator::Destroy ();
    return 0;
}
