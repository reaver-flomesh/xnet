package controller

import (
	"net"
	"strings"

	"github.com/mitchellh/hashstructure/v2"
	corev1 "k8s.io/api/core/v1"

	"github.com/flomesh-io/xnet/pkg/xnet/bpf/maps"
	"github.com/flomesh-io/xnet/pkg/xnet/util"
)

var (
	corev1Protos = map[corev1.Protocol]corev1.Protocol{
		corev1.ProtocolTCP:  corev1.ProtocolTCP,
		corev1.ProtocolSCTP: corev1.ProtocolTCP,
		corev1.ProtocolUDP:  corev1.ProtocolUDP,
	}

	supportedProtos = []corev1.Protocol{corev1.ProtocolTCP, corev1.ProtocolUDP}
	supportedTcdirs = []maps.TcDir{maps.TC_DIR_IGR, maps.TC_DIR_EGR}

	policies map[corev1.Protocol]map[maps.TcDir]*NatPolicy = nil
)

type NatPolicy struct {
	hash   uint64
	natKey *maps.NatKey
	natVal *maps.NatVal
}

func init() {
	tcpIgrPolicy := new(NatPolicy)
	tcpIgrNatKey := new(maps.NatKey)
	tcpIgrNatKey.TcDir = uint8(maps.TC_DIR_IGR)
	tcpIgrNatKey.Proto = uint8(maps.IPPROTO_TCP)
	tcpIgrNatKey.Daddr = [4]uint32{0, 0, 0, 0}
	tcpIgrNatKey.Dport = util.HostToNetShort(0)
	tcpIgrNatKey.V6 = 0
	tcpIgrPolicy.natKey = tcpIgrNatKey

	tcpEgrPolicy := new(NatPolicy)
	tcpEgrNatKey := new(maps.NatKey)
	tcpEgrNatKey.TcDir = uint8(maps.TC_DIR_EGR)
	tcpEgrNatKey.Proto = uint8(maps.IPPROTO_TCP)
	tcpEgrNatKey.Daddr = [4]uint32{0, 0, 0, 0}
	tcpEgrNatKey.Dport = util.HostToNetShort(0)
	tcpEgrNatKey.V6 = 0
	tcpEgrPolicy.natKey = tcpEgrNatKey

	udpIgrPolicy := new(NatPolicy)
	udpIgrNatKey := new(maps.NatKey)
	udpIgrNatKey.TcDir = uint8(maps.TC_DIR_IGR)
	udpIgrNatKey.Proto = uint8(maps.IPPROTO_UDP)
	udpIgrNatKey.Daddr = [4]uint32{0, 0, 0, 0}
	udpIgrNatKey.Dport = util.HostToNetShort(0)
	udpIgrNatKey.V6 = 0
	udpIgrPolicy.natKey = udpIgrNatKey

	udpEgrPolicy := new(NatPolicy)
	udpEgrNatKey := new(maps.NatKey)
	udpEgrNatKey.TcDir = uint8(maps.TC_DIR_EGR)
	udpEgrNatKey.Proto = uint8(maps.IPPROTO_UDP)
	udpEgrNatKey.Daddr = [4]uint32{0, 0, 0, 0}
	udpEgrNatKey.Dport = util.HostToNetShort(0)
	udpEgrNatKey.V6 = 0
	udpEgrPolicy.natKey = udpEgrNatKey

	policies = map[corev1.Protocol]map[maps.TcDir]*NatPolicy{
		corev1.ProtocolTCP: {
			maps.TC_DIR_IGR: tcpIgrPolicy,
			maps.TC_DIR_EGR: tcpEgrPolicy,
		},
		corev1.ProtocolUDP: {
			maps.TC_DIR_IGR: udpIgrPolicy,
			maps.TC_DIR_EGR: udpEgrPolicy,
		},
	}
}

func (s *server) configPolicies() {
	for _, proto := range supportedProtos {
		for _, tcdir := range supportedTcdirs {
			policies[proto][tcdir].natVal = new(maps.NatVal)
		}
	}

	trustedAddrs := make(map[uint32]map[uint16]uint8)
	existsAcls := maps.GetAclEntries()

	pods := s.kubeController.ListSidecarPods()
	for _, pod := range pods {
		if corev1.PodRunning != pod.Status.Phase {
			continue
		}

		podAddr := net.ParseIP(pod.Status.PodIP)
		if podAddr.To4() == nil || podAddr.IsUnspecified() || podAddr.IsMulticast() {
			log.Error().Msgf(`invalid sidecar's addr: %s'`, pod.Status.PodIP)
			continue
		}

		podMac, found := s.findHwAddrByPodIP(pod.Status.PodIP)
		if !found {
			log.Error().Msgf(`invalid sidecar's mac addr: %s'`, pod.Status.PodIP)
			continue
		}

		podAddrNb, _ := util.IPv4ToInt(podAddr)

		trustedAddrs[podAddrNb] = map[uint16]uint8{
			util.HostToNetShort(0): uint8(maps.ACL_TRUSTED),
		}

		for _, c := range pod.Spec.Containers {
			for _, port := range c.Ports {
				if port.ContainerPort > 0 {
					portLe := uint16(port.ContainerPort)
					portBe := util.HostToNetShort(portLe)
					if s.isTargetPort(port, s.filterPortInbound) {
						trustedAddrs[podAddrNb][portBe] = uint8(maps.ACL_AUDIT)
						policies[corev1Protos[port.Protocol]][maps.TC_DIR_IGR].natVal.
							AddEp(podAddr, portLe, podMac, false)
					}
					if s.isTargetPort(port, s.filterPortOutbound) {
						trustedAddrs[podAddrNb][portBe] = uint8(maps.ACL_AUDIT)
						policies[corev1Protos[port.Protocol]][maps.TC_DIR_EGR].natVal.
							AddEp(podAddr, portLe, podMac, false)
					}
				}
			}
		}
	}

	for addrNb, ports := range trustedAddrs {
		aclKey := new(maps.AclKey)
		aclKey.Addr[0] = addrNb

		aclVal := new(maps.AclVal)
		aclVal.Flag = sidecarAclFlag
		aclVal.Id = sidecarAclId

		for portBe, acl := range ports {
			aclKey.Port = portBe
			aclVal.Acl = acl
			for _, proto := range []uint8{uint8(maps.IPPROTO_TCP), uint8(maps.IPPROTO_UDP)} {
				aclKey.Proto = proto
				if err := maps.AddAclEntry(aclKey, aclVal); err != nil {
					log.Error().Err(err).Msgf(`failed to add acl: %s`, aclKey.String())
				}
			}
		}
	}

	for aclKey, aclVal := range existsAcls {
		if aclVal.Flag != sidecarAclFlag || aclVal.Id != sidecarAclId {
			continue
		}
		if ports, trustedAddr := trustedAddrs[aclKey.Addr[0]]; trustedAddr {
			if _, trustedPort := ports[aclKey.Port]; trustedPort {
				continue
			}
		}
		if err := maps.DelAclEntry(&aclKey); err != nil {
			log.Error().Err(err).Msgf(`failed to del acl: %s`, aclKey.String())
		}
	}

	for _, proto := range supportedProtos {
		for _, tcdir := range supportedTcdirs {
			policy := policies[proto][tcdir]
			chash, _ := hashstructure.Hash(policy.natVal, hashstructure.FormatV2,
				&hashstructure.HashOptions{
					ZeroNil:         true,
					IgnoreZeroValue: true,
					SlicesAsSets:    true,
				})
			if policy.hash != chash {
				policy.hash = chash
				if err := maps.AddNatEntry(policy.natKey, policy.natVal); err != nil {
					log.Error().Err(err).Msg(policy.natKey.String())
				}
			}
		}
	}
}

func (s *server) isTargetPort(port corev1.ContainerPort, flag string) bool {
	return strings.Contains(strings.ToLower(port.Name), flag)
}
