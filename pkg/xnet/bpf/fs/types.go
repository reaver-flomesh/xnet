package fs

import (
	"os"
	"path"
	"unsafe"

	"github.com/cilium/ebpf/rlimit"

	"github.com/flomesh-io/xnet/pkg/logger"
	"github.com/flomesh-io/xnet/pkg/xnet/util"
	"github.com/flomesh-io/xnet/pkg/xnet/volume"
)

var (
	BPFFSPath = `/sys/fs/bpf`

	fsMagic int32
	log     = logger.New("fsm-xnet-bpf-fs")
)

func init() {
	magic := uint32(0xCAFE4A11)
	fsMagic = *(*int32)(unsafe.Pointer(&magic))
	if exists := util.Exists(volume.Sysfs.MountPath); exists {
		BPFFSPath = path.Join(volume.Sysfs.MountPath, `bpf`)
	}
	if exists := util.Exists(BPFFSPath); !exists {
		if err := os.MkdirAll(BPFFSPath, 0750); err != nil {
			log.Fatal().Msg(err.Error())
		}
	}
	if err := Mount(); err != nil {
		log.Fatal().Err(err).Msgf(`failed to Mount bpf fs at:%s`, BPFFSPath)
	}

	if err := rlimit.RemoveMemlock(); err != nil {
		log.Error().Msgf("remove mem lock error: %v", err)
	}
}
