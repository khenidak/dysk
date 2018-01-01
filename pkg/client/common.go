package client

import (
	"fmt"
	"regexp"
)

const ACCOUNT_NAME_LEN = 256
const ACCOUNT_KEY_LEN = 128
const DEVICE_NAME_LEN = 32
const BLOB_PATH_LEN = 1024
const HOST_LEN = 512
const IP_LEN = 32
const LEASE_ID_LEN = 64

func isValidDeviceName(deviceName string) error {
	if 0 == len(deviceName) {
		return fmt.Errorf("device name is empty")
	}

	if len(deviceName) > DEVICE_NAME_LEN {
		return fmt.Errorf("Device name %s is longer than %d chars", deviceName, DEVICE_NAME_LEN)
	}

	numbers_alpha := regexp.MustCompile(`^[A-Za-z0-9.]+$`).MatchString

	if !numbers_alpha(deviceName) {
		return fmt.Errorf("Device name:%s is invalid only alpha + numnbers")
	}
	return nil
}
