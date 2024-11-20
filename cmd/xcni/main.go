// Package main implements fsm cni plugin.
package main

import (
	"fmt"

	"github.com/containernetworking/cni/pkg/skel"
	"github.com/containernetworking/cni/pkg/version"

	"github.com/flomesh-io/xnet/pkg/logger"
	"github.com/flomesh-io/xnet/pkg/xnet/cli"
)

func init() {
	_ = logger.SetLogLevel("warn")
	cli.SetLogFile("/tmp/xcni.log")
}

func main() {
	skel.PluginMainFuncs(skel.CNIFuncs{
		Add:   cli.CmdAdd,
		Check: cli.CmdCheck,
		Del:   cli.CmdDelete},
		version.All,
		fmt.Sprintf("CNI plugin xcni %v", "1.1.1"),
	)
}
