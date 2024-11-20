package events

import "github.com/flomesh-io/xnet/pkg/announcements"

// PubSubMessage represents a common messages abstraction to pass through the PubSub interface
type PubSubMessage struct {
	Kind   announcements.Kind
	OldObj interface{}
	NewObj interface{}
}
