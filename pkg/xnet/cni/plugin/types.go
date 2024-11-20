package plugin

import (
	"fmt"

	"github.com/flomesh-io/xnet/pkg/logger"
)

const (
	PluginName       = "xcni"
	pluginListSuffix = "list"
)

var (
	log = logger.New("fsm-xnet-cni-plugin")

	kubeConfigFileName = fmt.Sprintf(`ZZZ-%s-kubeconfig`, PluginName)

	cniConfigTemplate = fmt.Sprintf(`# KubeConfig file for CNI plugin.
apiVersion: v1
kind: Config
clusters:
- name: local
  cluster:
    server: {{.KubernetesServiceProtocol}}://[{{.KubernetesServiceHost}}]:{{.KubernetesServicePort}}
    {{.TLSConfig}}
users:
- name: %s
  user:
    token: "{{.ServiceAccountToken}}"
contexts:
- name: %s-context
  context:
    cluster: local
    user: %s
current-context: %s-context
`,
		PluginName,
		PluginName,
		PluginName,
		PluginName)
)
