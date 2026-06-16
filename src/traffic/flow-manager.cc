/**
 * flow-manager.cc
 * ---------------
 * Implementation of FlowManager: application installation helpers for
 * elephant (TCP BulkSend) and mouse (UDP OnOff) traffic.
 */

#include "ns3/flow-manager.h"

#include "ns3/bulk-send-helper.h"
#include "ns3/on-off-helper.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/inet-socket-address.h"
#include "ns3/data-rate.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/string.h"
#include "ns3/simulator.h"
#include "ns3/log.h"
#include "ns3/rng-seed-manager.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("FlowManager");

// ── Constructor ──────────────────────────────────────────────────────
FlowManager::FlowManager (NodeContainer hosts,
                           std::vector<Ipv4Address> addrs,
                           uint32_t seed)
    : m_hosts (hosts), m_addrs (std::move (addrs))
{
    NS_ASSERT_MSG (m_hosts.GetN () == m_addrs.size (),
                   "hosts and addrs must have the same size");

    RngSeedManager::SetSeed (seed);
    RngSeedManager::SetRun  (seed);

    m_rng = CreateObject<UniformRandomVariable> ();
    m_rng->SetAttribute ("Min", DoubleValue (0.0));
    m_rng->SetAttribute ("Max", DoubleValue (1.0));
}

// ── InstallSinks ─────────────────────────────────────────────────────
void
FlowManager::InstallSinks ()
{
    PacketSinkHelper sinkTcp ("ns3::TcpSocketFactory",
                               InetSocketAddress (Ipv4Address::GetAny (), 5000));
    PacketSinkHelper sinkUdp ("ns3::UdpSocketFactory",
                               InetSocketAddress (Ipv4Address::GetAny (), 6000));

    for (uint32_t i = 0; i < m_hosts.GetN (); ++i) {
        sinkTcp.Install (m_hosts.Get (i)).Start (Seconds (0.0));
        sinkUdp.Install (m_hosts.Get (i)).Start (Seconds (0.0));
    }
    NS_LOG_INFO ("FlowManager: installed base sinks on " << m_hosts.GetN () << " hosts");
}

// ── InstallElephantFlows ─────────────────────────────────────────────
void
FlowManager::InstallElephantFlows (uint32_t count, double simTime)
{
    uint32_t numHosts = m_hosts.GetN ();

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t srcIdx = i % numHosts;
        uint32_t dstIdx = (i + 1 + (i % (numHosts - 1))) % numHosts;
        if (srcIdx == dstIdx) dstIdx = (dstIdx + 1) % numHosts;

        double start = m_rng->GetValue () * 0.5;   // uniform in [0, 0.5] s

        BulkSendHelper bulk ("ns3::TcpSocketFactory",
                              InetSocketAddress (m_addrs[dstIdx], m_tcpPort));
        bulk.SetAttribute ("MaxBytes", UintegerValue (50 * 1024 * 1024));  // 50 MB
        bulk.SetAttribute ("SendSize", UintegerValue (1448));              // ~MSS

        ApplicationContainer app = bulk.Install (m_hosts.Get (srcIdx));
        app.Start (Seconds (start));
        app.Stop  (Seconds (simTime));

        // Per-flow sink at destination
        PacketSinkHelper sk ("ns3::TcpSocketFactory",
                              InetSocketAddress (Ipv4Address::GetAny (), m_tcpPort));
        sk.Install (m_hosts.Get (dstIdx)).Start (Seconds (0.0));

        NS_LOG_DEBUG ("Elephant flow " << i
            << " src=" << m_addrs[srcIdx]
            << " dst=" << m_addrs[dstIdx] << ":" << m_tcpPort
            << " start=" << start << "s");

        ++m_tcpPort;
    }

    NS_LOG_INFO ("FlowManager: installed " << count << " elephant flows");
}

// ── InstallMouseFlows ─────────────────────────────────────────────────
void
FlowManager::InstallMouseFlows (uint32_t count, double simTime)
{
    uint32_t numHosts = m_hosts.GetN ();

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t srcIdx = (i * 3 + 2) % numHosts;
        uint32_t dstIdx = (i * 5 + 7) % numHosts;
        if (srcIdx == dstIdx) dstIdx = (dstIdx + 1) % numHosts;

        double start = m_rng->GetValue () * 1.0;   // uniform in [0, 1.0] s

        OnOffHelper onoff ("ns3::UdpSocketFactory",
                            InetSocketAddress (m_addrs[dstIdx], m_udpPort));
        onoff.SetAttribute ("DataRate",   DataRateValue (DataRate ("10Mbps")));
        onoff.SetAttribute ("PacketSize", UintegerValue (512));
        onoff.SetAttribute ("OnTime",
            StringValue ("ns3::ExponentialRandomVariable[Mean=0.1]"));
        onoff.SetAttribute ("OffTime",
            StringValue ("ns3::ExponentialRandomVariable[Mean=0.1]"));

        ApplicationContainer app = onoff.Install (m_hosts.Get (srcIdx));
        app.Start (Seconds (start));
        app.Stop  (Seconds (simTime));

        // Per-flow sink at destination
        PacketSinkHelper sk ("ns3::UdpSocketFactory",
                              InetSocketAddress (Ipv4Address::GetAny (), m_udpPort));
        sk.Install (m_hosts.Get (dstIdx)).Start (Seconds (0.0));

        NS_LOG_DEBUG ("Mouse flow " << i
            << " src=" << m_addrs[srcIdx]
            << " dst=" << m_addrs[dstIdx] << ":" << m_udpPort
            << " start=" << start << "s");

        ++m_udpPort;
    }

    NS_LOG_INFO ("FlowManager: installed " << count << " mouse flows");
}

} // namespace ns3
