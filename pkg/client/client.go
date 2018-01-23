package client

import (
	"bytes"
	"encoding/base64"
	"encoding/binary"
	"fmt"
	"net"
	"os"
	"path"
	"regexp"
	"strconv"
	"strings"
	"syscall"
	"unsafe"

	"github.com/Azure/azure-sdk-for-go/storage"
	"github.com/rubiojr/go-vhd/vhd"
)

const (
	deviceFile = "/dev/dysk"
	// IOCTL Command Codes
	IOCTLMOUNTDYSK   = 9901
	IOCTLUNMOUNTDYSK = 9902
	IOCTGETDYSK      = 9903
	IOCTLISTDYYSKS   = 9904
	// All in/out commands are expecting 2048 buffers.
	IOCTL_IN_OUT_MAX = 2048

	// length as expected by the module
	ACCOUNT_NAME_LEN = 256
	ACCOUNT_KEY_LEN  = 128
	DEVICE_NAME_LEN  = 32
	BLOB_PATH_LEN    = 1024
	HOST_LEN         = 512
	IP_LEN           = 32
	LEASE_ID_LEN     = 64
)

type DyskClient interface {
	Mount(d *Dysk, autoLease, breakExistingLease bool) error
	Unmount(name string, breakLease bool) error
	Get(name string) (*Dysk, error)
	List() ([]*Dysk, error)
	CreatePageBlob(sizeGB uint, container string, pageBlobName string, is_vhd bool, lease bool) (string, error)
	DeletePageBlob(container string, pageBlobName string, leaseId string, breakExistingLease bool) error
	//LeaseAndValidate(d *Dysk, breakExistingLease bool) (string, error)
}

type moduleResponse struct {
	is_error bool
	response string
}

type dyskclient struct {
	storageAccountName string
	storageAccountKey  string
	blobClient         *storage.BlobStorageClient
	f                  *os.File
}

func CreateClient(account string, key string) DyskClient {
	c := dyskclient{
		storageAccountName: account,
		storageAccountKey:  key,
	}
	return &c
}

func (c *dyskclient) ensureBlobService() error {
	if nil == c.blobClient {
		storageClient, err := storage.NewBasicClient(c.storageAccountName, c.storageAccountKey)
		if err != nil {
			return err
		}
		blobClient := storageClient.GetBlobService()
		c.blobClient = &blobClient
	}
	return nil
}
func (c *dyskclient) DeletePageBlob(container string, pageBlobName string, leaseId string, breakExistingLease bool) error {

	if err := c.ensureBlobService(); nil != err {
		return err
	}

	blobContainer := c.blobClient.GetContainerReference(container)
	pageBlob := blobContainer.GetBlobReference(pageBlobName)
	if !breakExistingLease {
		var opts *storage.DeleteBlobOptions
		if 0 < len(leaseId) {
			opts = &storage.DeleteBlobOptions{
				LeaseID: leaseId,
			}
		}
		return pageBlob.Delete(opts)
	} else {
		// in case of breaking lease we really don't care about the return of break lease
		_, _ = pageBlob.BreakLeaseWithBreakPeriod(0, nil)
		return pageBlob.Delete(nil)
	}
}

func (c *dyskclient) CreatePageBlob(sizeGB uint, container string, pageBlobName string, is_vhd bool, lease bool) (string, error) {
	if err := c.ensureBlobService(); nil != err {
		return "", err
	}

	blobContainer := c.blobClient.GetContainerReference(container)
	sizeBytes := uint64(sizeGB * 1024 * 1024 * 1024)

	_, err := blobContainer.CreateIfNotExists(nil)
	if nil != err {
		return "", err
	}

	pageBlob := blobContainer.GetBlobReference(pageBlobName)

	pageBlob.Properties.ContentLength = int64(sizeBytes)
	err = pageBlob.PutPageBlob(nil)
	if nil != err {
		return "", err
	}

	//fmt.Fprintf(os.Stderr, "Created PageBlob in account:%s %s/%s(%dGiB)\n", c.storageAccountName, container, pageBlobName, sizeGB)

	// is it vhd?
	h := vhd.CreateFixedHeader(uint64(sizeBytes), &vhd.VHDOptions{})
	b := new(bytes.Buffer)
	err = binary.Write(b, binary.BigEndian, h)
	if nil != err {
		return "", err
	}

	headerBytes := b.Bytes()
	blobRange := storage.BlobRange{
		Start: uint64(sizeBytes - uint64(len(headerBytes))),
		End:   uint64(sizeBytes - 1),
	}

	if err = pageBlob.WriteRange(blobRange, bytes.NewBuffer(headerBytes[:vhd.VHD_HEADER_SIZE]), nil); nil != err {
		return "", err
	}

	//	fmt.Fprintf(os.Stderr, "Wrote VHD header for PageBlob in account:%s %s/%s\n", c.storageAccountName, container, pageBlobName)

	if lease {
		leaseId, err := page_blob_lease(pageBlob, false)
		return leaseId, err
	} else {
		return "", err
	}
}

func (c *dyskclient) Mount(d *Dysk, autoLease, breakExistingLease bool) error {
	if err := c.openDeviceFile(); nil != err {
		return err
	}
	defer c.closeDeviceFile()

	err := c.pre_mount(d, autoLease, breakExistingLease)
	if nil != err {
		return err
	}

	as_string := dysk2string(d)
	buffer := bufferize(as_string)

	_, _, e := syscall.Syscall(syscall.SYS_IOCTL, c.f.Fd(), IOCTLMOUNTDYSK, uintptr(unsafe.Pointer(&buffer[0])))
	if e != 0 {
		return e
	}

	res := parseResponse(buffer)
	if res.is_error {
		return fmt.Errorf(res.response)
	}

	newdysk, err := string2dysk(res.response)
	if nil != err {
		return err
	}
	d.Major = newdysk.Major
	d.Minor = newdysk.Minor
	return nil
}

func (c *dyskclient) Unmount(name string, breakleaseflag bool) error {
	var d *Dysk
	var err error
	if err = isValidDeviceName(name); nil != err {
		return err
	}

	if err = c.openDeviceFile(); nil != err {
		return err
	}
	defer c.closeDeviceFile()

	// if break lease is flagged keep a reference to it
	if breakleaseflag {
		d, err = c.get(name)
		if nil != err {
			return err
		}
	}

	newName := fmt.Sprintf("%s\n\x00", name)
	buffer := bufferize(newName)

	_, _, e := syscall.Syscall(syscall.SYS_IOCTL, c.f.Fd(), IOCTLUNMOUNTDYSK, uintptr(unsafe.Pointer(&buffer[0])))
	if e != 0 {
		return e
	}

	res := parseResponse(buffer)
	if res.is_error {
		return fmt.Errorf(res.response)
	}

	if breakleaseflag {
		_ = breakLease(d) // We ignore break leases error
		/*
					if err := breakLease(d); nil != err {
			      fmt.Fprintf(os.Stderr, "Device:%s on %s/$s is unmounted but failed to break lease\n", d.Name, d.AccountName, d.AccountKey)
					} else {
						fmt.Fprintf(os.Stderr, "Broke lease while unmounting device:%s on %s:%s\n", d.Name, d.AccountName, d.Path)
					}
		*/
	}
	return nil
}

func (c *dyskclient) Get(deviceName string) (*Dysk, error) {
	if err := isValidDeviceName(deviceName); nil != err {
		return nil, err
	}

	if err := c.openDeviceFile(); nil != err {
		return nil, err
	}
	defer c.closeDeviceFile()

	d, err := c.get(deviceName)
	if nil != err {
		return nil, err
	}

	c.post_get(d)

	return d, nil
}

func (c *dyskclient) List() ([]*Dysk, error) {
	if err := c.openDeviceFile(); nil != err {
		return nil, err
	}
	defer c.closeDeviceFile()

	var dysks []*Dysk

	buffer := bufferize("-")
	_, _, e := syscall.Syscall(syscall.SYS_IOCTL, c.f.Fd(), IOCTLISTDYYSKS, uintptr(unsafe.Pointer(&buffer[0])))
	if e != 0 {
		return nil, e
	}

	res := parseResponse(buffer)
	if res.is_error {
		return nil, fmt.Errorf(res.response)
	}

	splitNames := strings.Split(res.response, "\n")
	for idx, name := range splitNames {
		if idx == (len(splitNames) - 1) {
			break
		}
		d, err := c.get(name)
		if nil != err {
			return nil, err
		}
		c.post_get(d)
		dysks = append(dysks, d)
	}

	return dysks, nil
}

// --------------------------------
// Utility Funcs
// --------------------------------
// internal get, expects device file to be open prior to execute
func (c *dyskclient) get(deviceName string) (*Dysk, error) {
	newName := fmt.Sprintf("%s\n\x00", deviceName)
	buffer := bufferize(newName)

	_, _, e := syscall.Syscall(syscall.SYS_IOCTL, c.f.Fd(), IOCTGETDYSK, uintptr(unsafe.Pointer(&buffer[0])))
	if e != 0 {
		return nil, e
	}

	res := parseResponse(buffer)
	if res.is_error {
		return nil, fmt.Errorf(res.response)
	}

	d, err := string2dysk(res.response)
	if nil != err {
		return nil, err
	}

	return d, nil
}

//opens dysk device file
func (c *dyskclient) openDeviceFile() error {
	f, err := os.Open(deviceFile)
	c.f = f
	return err
}

//cloes dysk device file
func (c *dyskclient) closeDeviceFile() error {
	if nil == c.f {
		return fmt.Errorf("Device file is not open")
	}
	return c.f.Close()
}

//gets a page blob for a dysk
func (c *dyskclient) pageblob_get(d *Dysk) (*storage.Blob, error) {
	err := c.ensureBlobService()
	if nil != err {
		return nil, err
	}

	blobClient := c.blobClient
	containerPath := path.Dir(d.Path)
	containerPath = containerPath[1:]
	blobContainer := blobClient.GetContainerReference(containerPath)

	exists, err := blobContainer.Exists()
	if nil != err {
		return nil, err
	}
	if !exists {
		return nil, fmt.Errorf("Container at %s does not exist", d.Path)
	}

	pageBlobName := path.Base(d.Path)
	pageBlob := blobContainer.GetBlobReference(pageBlobName)

	exists, err = pageBlob.Exists()
	if nil != err {
		return nil, err
	}
	if !exists {
		return nil, fmt.Errorf("Blob at %s does not exist", d.Path)
	}

	return pageBlob, nil
}

// sets sizeGb on dysk
func (c *dyskclient) set_pageblob_size(d *Dysk) error {
	pageBlob, err := c.pageblob_get(d)
	if nil != err {
		return err
	}

	// Read Properties if read && is page blog then we are cool
	getProps := storage.GetBlobPropertiesOptions{
		LeaseID: d.LeaseId,
	}

	// Failed to read Properties?
	if err := pageBlob.GetProperties(&getProps); nil != err {
		return err
	}

	d.SizeGB = int(pageBlob.Properties.ContentLength / (1024 * 1024 * 1024))
	return nil
}

//lease + validation
func (c *dyskclient) pre_mount(d *Dysk, autoLease, breakExistingLease bool) error {
	d.AccountName = c.storageAccountName
	d.AccountKey = c.storageAccountKey

	c.set_pageblob_size(d) /* TODO: Merge size functions in one place for validation and set_pageblob_size */

	byteSize := d.SizeGB * (1024 * 1024 * 1024)
	if d.Vhd {
		byteSize -= vhd.VHD_HEADER_SIZE
	}
	d.sectorCount = uint64(byteSize / 512)

	pageBlob, err := c.pageblob_get(d)
	if nil != err {
		return err
	}

	if "" == d.LeaseId && autoLease {
		lease, err := page_blob_lease(pageBlob, breakExistingLease)
		if nil != err {
			return err
		}

		d.LeaseId = lease
	}
	return c.validateDysk(d)
}

func (c *dyskclient) post_get(d *Dysk) {
	// Convert sector count to size
	// check if we are VHD by measuring the difference between azure's size and disk size

	byteSize := uint64(d.sectorCount * 512)
	if d.Vhd {
		byteSize += vhd.VHD_HEADER_SIZE
	}

	d.SizeGB = int(byteSize / (1024 * 1024 * 1024))
}

/*Use dysks own storage account and key to break its lease */
func breakLease(d *Dysk) error {
	dyskClient := dyskclient{
		storageAccountName: d.AccountName,
		storageAccountKey:  d.AccountKey,
	}

	pageBlob, err := dyskClient.pageblob_get(d)
	if nil != err {
		return err
	}
	if _, err := pageBlob.BreakLeaseWithBreakPeriod(0, nil); nil != err {
		return err
	}

	return nil
}

/*TODO: should we allow users to break a *provided* lease
users can say mount it with this lease but break it and recreate if lease is invalid..
*/
func page_blob_lease(pageBlob *storage.Blob, breakExistingLease bool) (string, error) {
	leaseId, err := pageBlob.AcquireLease(-1, "", nil)
	if nil != err { /*ding ding ding! if we are here then page blob exists, keys are valid etc etc */
		if breakExistingLease {
			_, err := pageBlob.BreakLeaseWithBreakPeriod(0, nil)
			if nil != err {
				return "", err
			}
			leaseId, err = pageBlob.AcquireLease(-1, "", nil)
			return leaseId, err
		} else {
			return "", err
		}
	} else {
		return leaseId, err
	}
}

func (c *dyskclient) validateLease(d *Dysk) error {
	pageBlob, err := c.pageblob_get(d)
	if nil != err {
		return err
	}
	// Read Properties if read && is page blog then we are cool
	getProps := storage.GetBlobPropertiesOptions{
		LeaseID: d.LeaseId,
	}

	// Failed to read Properties?
	if err = pageBlob.GetProperties(&getProps); nil != err {
		return err
	}

	if storage.BlobTypePage != pageBlob.Properties.BlobType {
		return fmt.Errorf("This blob is not a page blob")
	}

	//if dysk is readonly then we are done now
	if ReadOnly == d.Type {
		return nil
	}

	pageBlob.Metadata["dysk"] = "dysk" //Setting a metadata value to ensure that we have write lease
	setMetaDataProps := storage.SetBlobMetadataOptions{
		LeaseID: d.LeaseId,
	}

	if err = pageBlob.SetMetadata(&setMetaDataProps); nil != err {
		return err
	}

	return nil
}

// set host, ip and static validation
func (c *dyskclient) validateDysk(d *Dysk) error {
	var isValidDeviceName = regexp.MustCompile(`^[a-zA-Z]+[a-zA-Z0-9]*$`).MatchString
	var count_forward_slash = regexp.MustCompile(`/`)
	if 0 == len(d.Type) || (ReadOnly != d.Type && ReadWrite != d.Type) {
		return fmt.Errorf("Invalid type. Must be R or RW")
	}

	// lower the name
	if 0 == len(d.Name) || DEVICE_NAME_LEN < len(d.Name) {
		return fmt.Errorf("Invalid name. Only max of(32) chars")
	}

	if !isValidDeviceName(d.Name) {
		return fmt.Errorf("Invalid device name. alpha+numbers allowed. must start with alpha")
	}

	if 0 == d.sectorCount {
		return fmt.Errorf("Invalid Sector count.")
	}

	if 0 == len(d.AccountName) || ACCOUNT_NAME_LEN < len(d.AccountName) {
		return fmt.Errorf("Invalid Account name. Must be <= than 256")
	}

	if 0 == len(d.AccountKey) || ACCOUNT_KEY_LEN < len(d.AccountKey) {
		return fmt.Errorf("Invalid AccountKey. Must be <= 64")
	}

	_, err := base64.StdEncoding.DecodeString(d.AccountKey)
	if nil != err {
		fmt.Errorf("Invalid account key. Must be a base64 encoded string. Error:%s", err.Error())
	}

	if 0 == len(d.Path) || BLOB_PATH_LEN < len(d.Path) {
		return fmt.Errorf("Invalid path. Must be <= 1024")
	}

	count_slashes := count_forward_slash.FindAllStringIndex(d.Path, -1)
	if 2 != len(count_slashes) {
		return fmt.Errorf("too many forward slashes in dysk path")
	}

	if 0 < len(d.host) && HOST_LEN < len(d.host) {
		return fmt.Errorf("Invalid host. Must be <= 512")
	} else {
		d.host = fmt.Sprintf("%s.blob.core.windows.net", d.AccountName) // Won't support sovereign clouds for now
	}

	if 0 == len(d.LeaseId) || LEASE_ID_LEN < len(d.LeaseId) {
		return fmt.Errorf("Invalid Lease Id. Must be <= 32")
	}

	addr, err := net.LookupIP(d.host)
	if nil != err {
		return fmt.Errorf("Failed to lookup ip for host:%s", d.host)
	}
	d.ip = addr[0].String()

	return c.validateLease(d)
}

// Converts a byte slice to a response object
func parseResponse(bytes []byte) *moduleResponse {
	s := string(bytes)
	firstlinebreak := strings.Index(s, "\n")
	is_error := s[:firstlinebreak] == "ERR"
	response := s[firstlinebreak+1:]

	res := &moduleResponse{
		is_error: is_error,
		response: response,
	}

	return res
}

// Converts a string to a dysk
func string2dysk(asstring string) (*Dysk, error) {
	split := strings.Split(asstring, "\n")

	sectorCount, _ := strconv.ParseUint(split[2], 10, 64)
	major, err := strconv.ParseInt(split[9], 10, 64)
	if nil != err {
		return nil, err
	}

	minor, err := strconv.ParseInt(split[10], 10, 64)
	if nil != err {
		return nil, err
	}
	is_vhd, err := strconv.ParseInt(split[11], 10, 64)

	d := Dysk{
		Type:        DyskType(split[0]),
		Name:        split[1],
		sectorCount: sectorCount,
		AccountName: split[3],
		AccountKey:  split[4],
		Path:        split[5],
		host:        split[6],
		ip:          split[7],
		LeaseId:     split[8],
		Major:       int(major),
		Minor:       int(minor),
	}
	if 1 == is_vhd {
		d.Vhd = true
	}
	return &d, nil
}

// Dysk as string
func dysk2string(d *Dysk) string {
	//type-devicename-sectorcount-accountname-accountkey-path-host-ip-lease-vhd
	const format string = "%s\n%s\n%d\n%s\n%s\n%s\n%s\n%s\n%s\n%d\n"
	is_vhd := 0
	if d.Vhd {
		is_vhd = 1
	}
	out := fmt.Sprintf(format, d.Type, d.Name, d.sectorCount, d.AccountName, d.AccountKey, d.Path, d.host, d.ip, d.LeaseId, is_vhd)
	return out
}

// string as buffer with the correct padding
func bufferize(s string) []byte {
	var b bytes.Buffer
	messageBytes := []byte(s)
	pad := make([]byte, IOCTL_IN_OUT_MAX-len(messageBytes))

	b.Write(messageBytes)
	b.Write(pad)

	return b.Bytes()
}
