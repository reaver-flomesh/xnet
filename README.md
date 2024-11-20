## 编译环境

```
apt install -y clang llvm libelf-dev libbpf-dev clang-format
arch=$(arch | sed s/aarch64/arm64/ | sed s/x86_64/amd64/) && echo $arch && if [ "$arch" = "arm64" ] ; then apt install -y gcc-multilib-arm-linux-gnueabihf; else apt-get update && apt install -y gcc-multilib;fi
```

## 测试指令

```
make kern-trace

make -f Makefile.test.mk test-up
make -f Makefile.test.mk fgw-demo
make -f Makefile.test.mk h1-pipy-demo
make -f Makefile.test.mk h2-pipy-demo

make build-bpf build-cli

make clean load;make -f Makefile.test.mk test-tcp-outbound
make clean load;make -f Makefile.test.mk test-tcp-inbound

make clean load;make -f Makefile.test.mk test-acl-outbound
make clean load;make -f Makefile.test.mk test-acl-inbound

make clean load;make -f Makefile.test.mk test-dns-outbound
make clean load;make -f Makefile.test.mk test-dns-inbound

make -f Makefile.test.mk init-prog-map
make -f Makefile.test.mk init-cfg-map
make -f Makefile.test.mk init-nat-map
make -f Makefile.test.mk init-acl-map
make -f Makefile.test.mk init-trace-ip-map
make -f Makefile.test.mk init-trace-port-map

make -f Makefile.test.mk show-prog-map
make -f Makefile.test.mk show-cfg-map
make -f Makefile.test.mk show-nat-map
make -f Makefile.test.mk show-acl-map
make -f Makefile.test.mk show-tcp-flow-map
make -f Makefile.test.mk show-udp-flow-map
make -f Makefile.test.mk show-tcp-opt-map
make -f Makefile.test.mk show-udp-opt-map
make -f Makefile.test.mk show-trace-ip-map
make -f Makefile.test.mk show-trace-port-map
```
