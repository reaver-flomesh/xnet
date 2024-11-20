# 1 搭建模拟网络环境

```
#开启本地主机IPv4转发,以便从 nentns h1 访问外网
echo 1 > /proc/sys/net/ipv4/ip_forward
sudo iptables -t nat -A POSTROUTING -o ens33 -j MASQUERADE

# 配置 h1 netns模拟另外一台虚机
sudo ip netns add h1
sudo ip link add cni1 type veth peer name eth0 netns h1
sudo ip link set cni1 up
sudo ip addr add 10.0.0.1/24 dev cni1
sudo ip -n h1 link set eth0 up
sudo ip netns exec h1 ifconfig eth0 10.0.0.2/24 up
sudo ip netns exec h1 ip route add default via 10.0.0.1
sudo ip netns exec h1 ifconfig lo up

# 配置 h2 netns模拟另外一台虚机
sudo ip netns add h2
sudo ip link add cni2 type veth peer name eth0 netns h2
sudo ip link set cni2 up
sudo ip addr add 20.0.0.1/24 dev cni2
sudo ip -n h2 link set eth0 up
sudo ip netns exec h2 ifconfig eth0 20.0.0.2/24 up
sudo ip netns exec h2 ip route add default via 20.0.0.1
sudo ip netns exec h2 ifconfig lo up
```

# 2 测试场景一

## 2.1 访问路径

```
local host -> vm h1 -> local host -> 8.8.8.8
```

## 2.2 启动 xnat

```
make build-cli
make clean bpf load
bin/xnat bpf detach --namespace=h1 --dev=eth0 || true
bin/xnat prog init
bin/xnat cfg set --ipv4_udp_nat_by_port_on=1
bin/xnat nat add --addr=0.0.0.0 --port=53 --proto-udp --tc-ingress --ep-addr=8.8.8.8 --ep-port=53
bin/xnat nat add --addr=0.0.0.0 --port=53 --proto-udp --tc-egress --ep-addr=8.8.8.8 --ep-port=53
bin/xnat bpf attach --namespace=h1 --dev=eth0
```

## 2.3 测试指令

```
#将 dns 服务器指向 vm h1, 将被拦截到 8.8.8.8
nslookup www.baidu.com 10.0.0.2
```

## 2.4 指令返回

```
Server:         10.0.0.2
Address:        10.0.0.2#53

Non-authoritative answer:
www.baidu.com   canonical name = www.a.shifen.com.
Name:   www.a.shifen.com
Address: 39.156.66.18
Name:   www.a.shifen.com
Address: 39.156.66.14
Name:   www.a.shifen.com
Address: 2409:8c00:6c21:104f:0:ff:b03f:3ae
Name:   www.a.shifen.com
Address: 2409:8c00:6c21:1051:0:ff:b0af:279a
```

# 3 测试场景二

## 3.1 访问路径

```
vm h1 -> local host -> 8.8.8.8
```

## 3.2 启动 xnat

```
make build-cli
make clean bpf load
bin/xnat bpf detach --namespace=h1 --dev=eth0 || true
bin/xnat prog init
bin/xnat cfg set --ipv4_udp_nat_by_port_on=1
bin/xnat nat add --addr=0.0.0.0 --port=53 --proto-udp --tc-ingress --ep-addr=8.8.8.8 --ep-port=53
bin/xnat nat add --addr=0.0.0.0 --port=53 --proto-udp --tc-egress --ep-addr=8.8.8.8 --ep-port=53
bin/xnat bpf attach --namespace=h1 --dev=eth0
```

## 3.3 测试指令

```
#将 dns 服务器指向 vm h2, 将被拦截到 8.8.8.8
ip netns exec h1 nslookup www.baidu.com 20.0.0.2
```

## 3.4 指令返回

```
Server:         20.0.0.2
Address:        20.0.0.2#53

Non-authoritative answer:
www.baidu.com   canonical name = www.a.shifen.com.
Name:   www.a.shifen.com
Address: 39.156.66.14
Name:   www.a.shifen.com
Address: 39.156.66.18
Name:   www.a.shifen.com
Address: 2409:8c00:6c21:1051:0:ff:b0af:279a
Name:   www.a.shifen.com
Address: 2409:8c00:6c21:104f:0:ff:b03f:3ae
```
