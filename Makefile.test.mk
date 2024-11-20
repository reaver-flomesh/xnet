#!make

SHELL = bash

.PHONY: test-up
test-up: test-reset
	@echo 1 > /proc/sys/net/ipv4/ip_forward
	@sudo iptables -t nat -A POSTROUTING -o ens33 -j MASQUERADE
	@# Configure load-balancer end-point h1
	@sudo ip netns add h1
	@sudo ip link add cni1 type veth peer name eth0 netns h1
	@sudo ip link set cni1 up
	@sudo ip addr add 10.0.0.1/24 dev cni1
	@sudo ip -n h1 link set eth0 up
	@sudo ip netns exec h1 ifconfig eth0 10.0.0.2/24 up
	@sudo ip netns exec h1 ip route add default via 10.0.0.1
	@sudo ip netns exec h1 ifconfig lo up
	@# Configure load-balancer end-point h1 Done
	@# Configure load-balancer end-point h2
	@sudo ip netns add h2
	@sudo ip link add cni2 type veth peer name eth0 netns h2
	@sudo ip link set cni2 up
	@sudo ip addr add 20.0.0.1/24 dev cni2
	@sudo ip -n h2 link set eth0 up
	@sudo ip netns exec h2 ifconfig eth0 20.0.0.2/24 up
	@sudo ip netns exec h2 ip route add default via 20.0.0.1
	@sudo ip netns exec h2 ifconfig lo up
	@# Configure load-balancer end-point h2 Done

.PHONY: test-reset
test-reset:
	@sudo ip link del cni1 > /dev/null 2>&1 || true
	@sudo ip netns del h1 > /dev/null 2>&1 || true
	@sudo ip link del cni2 > /dev/null 2>&1 || true
	@sudo ip netns del h2 > /dev/null 2>&1 || true
	@sudo iptables -t nat -F || true

.PHONY: fgw-inbound
fgw-inbound:
	@pipy -e "pipy().listen(15003).serveHTTP(new Message('hi, it works as fgw inbound listener.\n'))"

.PHONY: fgw-outbound
fgw-outbound:
	@pipy -e "pipy().listen(15001).serveHTTP(new Message('hi, it works as fgw outbound listener.\n'))"

.PHONY: fgw-demo
fgw-demo:
	@sudo nohup pipy -e "pipy().listen(15001).serveHTTP(new Message('hi, it works as fgw outbound listener in sys.\n'))" > /dev/null 2>&1 &
	@sudo nohup pipy -e "pipy().listen(15003).serveHTTP(new Message('hi, it works as fgw inbound listener in sys.\n'))" > /dev/null 2>&1 &

.PHONY: h1-curl-demo
h1-curl-demo:
	@sudo ip netns exec h1 curl 10.0.0.1:8080

.PHONY: h2-curl-demo
h2-curl-demo:
	@sudo ip netns exec h2 curl 20.0.0.1:8080

.PHONY: h1-pipy-demo
h1-pipy-demo:
	@sudo nohup ip netns exec h1 pipy -e "pipy().listen(8080).serveHTTP(new Message('hi, it works as demo in h1.\n'))" > /dev/null 2>&1 &

.PHONY: curl-h1-demo
curl-h1-demo:
	@curl 10.0.0.2:8080

.PHONY: h2-curl-h1-demo
h2-curl-h1-demo:
	@sudo ip netns exec h2 curl 10.0.0.2:8080

.PHONY: h2-pipy-demo
h2-pipy-demo:
	@sudo nohup ip netns exec h2 pipy -e "pipy().listen(8080).serveHTTP(new Message('hi, it works as demo in h2.\n'))" > /dev/null 2>&1 &

.PHONY: curl-h2-demo
curl-h2-demo:
	@curl 20.0.0.2:8080

.PHONY: h1-curl-h2-demo
h1-curl-h2-demo:
	@sudo ip netns exec h1 curl 20.0.0.2:8080

.PHONY: test-tcp-outbound
test-tcp-outbound:
	@bin/xnat bpf detach --namespace=h1 --dev=eth0 > /dev/null 2>&1 || true
	@bin/xnat prog init
	@bin/xnat cfg set --debug-on
	@bin/xnat cfg set --opt-on
	@bin/xnat nat add --addr=0.0.0.0 --port=0 --proto-tcp --tc-ingress --ep-addr=192.168.226.22 --ep-port=15003 --ep-mac=36:b0:48:52:da:23
	@bin/xnat nat add --addr=0.0.0.0 --port=0 --proto-tcp --tc-egress --ep-addr=192.168.226.22 --ep-port=15001 --ep-mac=36:b0:48:52:da:23
	@bin/xnat bpf attach --namespace=h1 --dev=eth0
	@sudo ip netns exec h1 curl 20.0.0.2:8080

.PHONY: test-tcp-inbound
test-tcp-inbound:
	@bin/xnat bpf detach --namespace=h1 --dev=eth0 > /dev/null 2>&1 || true
	@bin/xnat prog init
	@bin/xnat cfg set --debug-on
	@bin/xnat nat add --addr=0.0.0.0 --port=0 --proto-tcp --tc-ingress --ep-addr=192.168.226.22 --ep-port=15003 --ep-mac=36:b0:48:52:da:23
	@bin/xnat nat add --addr=0.0.0.0 --port=0 --proto-tcp --tc-egress --ep-addr=192.168.226.22 --ep-port=15001 --ep-mac=36:b0:48:52:da:23
	@bin/xnat bpf attach --namespace=h1 --dev=eth0
	@curl 10.0.0.2:8080

.PHONY: test-acl-outbound
test-acl-outbound:
	@bin/xnat bpf detach --namespace=h1 --dev=eth0 > /dev/null 2>&1 || true
	@bin/xnat prog init
	@bin/xnat cfg set --ipv4_acl_check_on=1
	@bin/xnat acl add --proto-tcp --port=0 --addr=10.0.0.1 --acl=trusted
	@bin/xnat acl add --proto-tcp --port=0 --addr=20.0.0.2 --acl=trusted
	@bin/xnat bpf attach --namespace=h1 --dev=eth0
	@sudo ip netns exec h1 curl 20.0.0.2:8080

.PHONY: test-acl-inbound
test-acl-inbound:
	@bin/xnat bpf detach --namespace=h1 --dev=eth0 > /dev/null 2>&1 || true
	@bin/xnat prog init
	@bin/xnat cfg set --ipv4_acl_check_on=1
	@bin/xnat acl add --proto-tcp --port=0 --addr=10.0.0.1 --acl=trusted
	@bin/xnat acl add --proto-tcp --port=0 --addr=20.0.0.2 --acl=trusted
	@bin/xnat bpf attach --namespace=h1 --dev=eth0
	@curl 10.0.0.2:8080

.PHONY: test-dns-outbound
test-dns-outbound:
	bin/xnat bpf detach --namespace=h1 --dev=eth0 > /dev/null 2>&1 || true
	bin/xnat prog init
	bin/xnat cfg set --debug-on
	bin/xnat cfg set --opt-on
	bin/xnat cfg set --ipv4_udp_nat_by_port_on=1
	bin/xnat nat add --addr=0.0.0.0 --port=53 --proto-udp --tc-ingress --ep-addr=8.8.8.8 --ep-port=53 --ep-mac=36:b0:48:52:da:23
	bin/xnat nat add --addr=0.0.0.0 --port=53 --proto-udp --tc-egress --ep-addr=8.8.8.8 --ep-port=53 --ep-mac=36:b0:48:52:da:23
	bin/xnat bpf attach --namespace=h1 --dev=eth0
	nslookup www.baidu.com 10.0.0.2

.PHONY: test-dns-inbound
test-dns-inbound:
	bin/xnat bpf detach --namespace=h1 --dev=eth0 > /dev/null 2>&1 || true
	bin/xnat prog init
	bin/xnat cfg set --debug-on
	bin/xnat cfg set --opt-on
	bin/xnat cfg set --ipv4_udp_nat_by_port_on=1
	bin/xnat nat add --addr=0.0.0.0 --port=53 --proto-udp --tc-ingress --ep-addr=8.8.8.8 --ep-port=53 --ep-mac=36:b0:48:52:da:23
	bin/xnat nat add --addr=0.0.0.0 --port=53 --proto-udp --tc-egress --ep-addr=8.8.8.8 --ep-port=53 --ep-mac=36:b0:48:52:da:23
	bin/xnat bpf attach --namespace=h1 --dev=eth0
	ip netns exec h1 nslookup www.baidu.com 20.0.0.2

.PHONY: init-prog-map
init-prog-map:
	bin/xnat prog init

.PHONY: init-nat-map
init-nat-map:
	bin/xnat nat add --addr=0.0.0.0 --port=0 --proto-tcp --tc-ingress --ep-addr=192.168.226.22 --ep-port=15003
	bin/xnat nat add --addr=0.0.0.0 --port=0 --proto-tcp --tc-egress --ep-addr=192.168.226.22 --ep-port=15001

.PHONY: init-acl-map
init-acl-map:
	bin/xnat acl add --proto-tcp --port=0 --addr=10.0.0.1 --acl=trusted
	bin/xnat acl add --proto-tcp --port=0 --addr=20.0.0.2 --acl=trusted

.PHONY: init-trace-ip-map
init-trace-ip-map:
	bin/xnat tr ip add --addr=10.0.0.2 --tc-ingress --tc-egress

.PHONY: init-trace-port-map
init-trace-port-map:
	bin/xnat trace port add --port=8080 --tc-ingress --tc-egress

.PHONY: show-tcp-flow-map
show-tcp-flow-map:
	bin/xnat flow tcp list | jq

.PHONY: show-udp-flow-map
show-udp-flow-map:
	bin/xnat flow udp list | jq

.PHONY: show-tcp-opt-map
show-tcp-opt-map:
	bin/xnat opt tcp list | jq

.PHONY: show-udp-opt-map
show-udp-opt-map:
	bin/xnat opt udp list | jq

.PHONY: show-acl-map
show-acl-map:
	bin/xnat acl list | jq

.PHONY: show-nat-map
show-nat-map:
	bin/xnat nat list | jq

.PHONY: show-cfg-map
show-cfg-map:
	bin/xnat cfg list | jq

.PHONY: show-prog-map
show-prog-map:
	bin/xnat prog list | jq

.PHONY: show-trace-ip-map
show-trace-ip-map:
	bin/xnat tr ip list | jq

.PHONY: show-trace-port-map
show-trace-port-map:
	bin/xnat tr port list | jq

.PHONY: show-tc
show-tc:
	@bin/xnat bpf list --namespace=h1 --dev=eth0 | jq