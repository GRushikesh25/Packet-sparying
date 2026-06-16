/**
 * spine-leaf-simulation.cc
 * ========================
 * NS-3 simulation of a Spine-Leaf (2-tier Clos) data-centre network
 * comparing ECMP vs Packet Spraying for elephant flow load balancing.
 *
 * Topology (defaults)
 * -------------------
 *   numSpine=4 spine switches
 *   numLeaf=8  leaf switches — fully connected to every spine
 *   hostsPerLeaf=4 hosts per leaf  →  32 hosts total
 *
 *   Link speeds:
 *     spine <-> leaf : 10 Gbps, 5  us
 *     leaf  <-> host : 1  Gbps, 10 us
 *
 * Command-line options
 * --------------------
 *   --numSpine      Number of spine switches       [4]
 *   --numLeaf       Number of leaf switches        [8]
 *   --hostsPerLeaf  Hosts per leaf switch          [4]
 *   --routing       "ecmp" | "spray"               [spray]
 *   --simTime       Simulation duration (s)        [5]
 *   --elephants     Number of elephant flows       [10]
 *   --mice          Number of mouse flows          [40]
 *   --seed          RNG run number                 [1]
 *   --pcap          Enable PCAP traces             [false]
 *   --flowmon       Output FlowMonitor XML         [true]
 *
 * Build & run
 * -----------
 *   ./ns3 run "spine-leaf-simulation --routing=spray"
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-address-helper.h"

// DCN-spray module headers
#include "ns3/spine-leaf-builder.h"
#include "ns3/flow-manager.h"

#include <string>
#include <iomanip>
#include <sstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("SpineLeafSimulation");

// ─────────────────────────────────────────────────────────────────────
int
main (int argc, char *argv[])
{
    // ── Parameters ────────────────────────────────────────────────────
    uint32_t    numSpine     = 4;
    uint32_t    numLeaf      = 8;
    uint32_t    hostsPerLeaf = 4;
    std::string routing      = "spray";
    double      simTime      = 5.0;
    uint32_t    numElephant  = 10;
    uint32_t    numMice      = 40;
    uint32_t    seed         = 1;
    bool        enablePcap   = false;
    bool        enableFM     = true;

    CommandLine cmd;
    cmd.AddValue ("numSpine",     "Number of spine switches",     numSpine);
    cmd.AddValue ("numLeaf",      "Number of leaf switches",      numLeaf);
    cmd.AddValue ("hostsPerLeaf", "Hosts per leaf switch",        hostsPerLeaf);
    cmd.AddValue ("routing",      "Routing: ecmp | spray",        routing);
    cmd.AddValue ("simTime",      "Simulation duration (s)",      simTime);
    cmd.AddValue ("elephants",    "Number of elephant flows",     numElephant);
    cmd.AddValue ("mice",         "Number of mouse flows",        numMice);
    cmd.AddValue ("seed",         "RNG run number",               seed);
    cmd.AddValue ("pcap",         "Enable PCAP capture",          enablePcap);
    cmd.AddValue ("flowmon",      "Enable FlowMonitor output",    enableFM);
    cmd.Parse (argc, argv);

    bool useSpray = (routing == "spray");

    NS_LOG_UNCOND ("[SpineLeaf] spine=" << numSpine
        << "  leaf=" << numLeaf
        << "  hostsPerLeaf=" << hostsPerLeaf
        << "  routing=" << routing
        << "  elephants=" << numElephant
        << "  mice=" << numMice);

    // ── Build topology ────────────────────────────────────────────────
    Ipv4AddressHelper addrHelper;
    SpineLeafBuilder builder (numSpine, numLeaf, hostsPerLeaf, useSpray);
    builder.Build (addrHelper);

    NodeContainer            hosts = builder.GetHosts ();
    std::vector<Ipv4Address> addrs = builder.GetHostAddresses ();

    // ── Install traffic ───────────────────────────────────────────────
    FlowManager fm (hosts, addrs, seed);
    fm.InstallSinks ();
    fm.InstallElephantFlows (numElephant, simTime);
    fm.InstallMouseFlows    (numMice,     simTime);

    // ── PCAP (optional) ───────────────────────────────────────────────
    if (enablePcap) {
        PointToPointHelper p2p;
        p2p.EnablePcapAll ("spine-leaf-" + routing);
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
        std::string xmlFile = "results/spine-leaf-" + routing + "-flowmon.xml";
        flowMonitor->CheckForLostPackets ();
        flowMonitor->SerializeToXmlFile  (xmlFile, true, true);
        NS_LOG_UNCOND ("[SpineLeaf] FlowMonitor saved to " << xmlFile);

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
