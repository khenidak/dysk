package client

type DyskType string

const (
	ReadOnly  DyskType = "R"
	ReadWrite DyskType = "RW"
)

type Dysk struct {
	Type        DyskType
	Name        string
	sectorCount uint64
	AccountName string
	Sas         string
	Path        string
	host        string
	ip          string
	LeaseId     string
	Major       int
	Minor       int
	Vhd         bool
	SizeGB      int
}
