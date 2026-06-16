#ifndef FLOW_MANAGER_H
#define FLOW_MANAGER_H

/**
 * flow-manager.h
 * --------------
 * Installs application-layer traffic on a set of host nodes.
 *
 *  - Elephant flows : BulkSendApplication (TCP) sending 50 MB each.
 *  - Mouse flows    : OnOffApplication (UDP) sending 512-byte packets at
 *                     10 Mbps with exponential on/off intervals.
 *  - Sinks          : PacketSink on every host for both TCP and UDP.
 *
 * Usage:
 *   FlowManager fm(hosts, hostAddrs, seed);
 *   fm.InstallSinks();
 *   fm.InstallElephantFlows(10, 5.0);
 *   fm.InstallMouseFlows(40, 5.0);
 */

#include "ns3/node-container.h"
#include "ns3/ipv4-address.h"
#include "ns3/random-variable-stream.h"

#include <vector>
#include <cstdint>

namespace ns3 {

class FlowManager
{
public:
    /**
     * \param hosts  NodeContainer of all hosts (sources and sinks)
     * \param addrs  IP address of each host, in same order as \p hosts
     * \param seed   RNG seed (also used as run number)
     */
    FlowManager (NodeContainer hosts,
                 std::vector<Ipv4Address> addrs,
                 uint32_t seed);

    /**
     * Install a PacketSink on every host node for both TCP port 5000 and
     * UDP port 6000.  Should be called before InstallElephantFlows /
     * InstallMouseFlows.
     */
    void InstallSinks ();

    /**
     * Install \p count BulkSend elephant flows (TCP, 50 MB each).
     * Start times are uniformly distributed in [0, 0.5] seconds.
     * Per-flow sinks are also created on the destination hosts.
     *
     * \param count   Number of elephant flows to create
     * \param simTime Simulation end time (flows stop at simTime)
     */
    void InstallElephantFlows (uint32_t count, double simTime);

    /**
     * Install \p count OnOff mouse flows (UDP, 512-byte packets, 10 Mbps).
     * Start times are uniformly distributed in [0, 1.0] seconds.
     * Per-flow sinks are also created on the destination hosts.
     *
     * \param count   Number of mouse flows to create
     * \param simTime Simulation end time (flows stop at simTime)
     */
    void InstallMouseFlows (uint32_t count, double simTime);

private:
    NodeContainer            m_hosts;
    std::vector<Ipv4Address> m_addrs;
    Ptr<UniformRandomVariable> m_rng;

    uint16_t m_tcpPort{5001};   ///< next TCP port to allocate
    uint16_t m_udpPort{6001};   ///< next UDP port to allocate
};

} // namespace ns3
#endif // FLOW_MANAGER_H
