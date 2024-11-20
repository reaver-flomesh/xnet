package plugin

import "path"

const (
	cniSock = ".xnet.sock"

	// CNICreatePodURL is the route for cni plugin for creating pod
	CNICreatePodURL = "/v1/cni/create-pod"
	// CNIDeletePodURL is the route for cni plugin for deleting pod
	CNIDeletePodURL = "/v1/cni/delete-pod"
)

func GetCniSock(runDir string) string {
	return path.Join(runDir, cniSock)
}
