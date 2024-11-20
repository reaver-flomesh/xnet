package cli

import "os"

func SetLogFile(file string) {
	// #nosec G304
	logfile, err := os.OpenFile(file, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0600)
	if err == nil {
		log = log.Output(logfile)
	}
}
