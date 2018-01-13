package cmd

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"

	"github.com/khenidak/dysk/pkg/client"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
)

var (
	storageAccountName string
	storageAccountKey  string

	filePath string

	container      string
	leaseId        string
	deviceName     string
	size           uint
	vhdFlag        bool
	readOnlyFlag   bool
	autoLeaseFlag  bool
	breakLeaseFlag bool

	autoCreate bool

	mountCmd = &cobra.Command{
		Use:   "mount",
		Short: "mounts a page blob as block device",
		Long: `creates and mount a page blob as a block device.
example:

#Mount an existing page blob
dyskctl mount --account {acount-name} --key {key} --device-name d01 --container {container-name}`,
		Run: func(cmd *cobra.Command, args []string) {
			validateOutput()
			mount()
		},
	}

	mountCreateCmd = &cobra.Command{
		Use:   "auto-create",
		Short: "creates then mounts a page blob as block device",
		Long: `creates and mount a page blob as a block device.
example:
# Create a 4 GB page blob and mount it as a dysk
dyskctl auto-create --account {account-name} --key {key} --size 4`,
		Run: func(cmd *cobra.Command, args []string) {
			validateOutput()
			autoCreate = true
			mount()
		},
	}

	mountFileCmd = &cobra.Command{
		Use:   "mount-file",
		Short: "mounts a page blob as block device based on a json file",
		Long: `.
example:
dyskctl mount-file --file {path to file}`,
		Run: func(cmd *cobra.Command, args []string) {
			validateOutput()
			file, err := ioutil.ReadFile(filePath)
			if nil != err {
				printError(err)
				os.Exit(1)
			}

			var d client.Dysk
			err = json.Unmarshal(file, &d)
			if nil != err {
				printError(err)
				os.Exit(1)
			}
			dyskClient := client.CreateClient(d.AccountName, d.AccountKey)
			err = dyskClient.Mount(&d, false, false)
			if nil != err {
				printError(err)
				os.Exit(1)
			}
			printDysk(&d)
		},
	}

	unmountCmd = &cobra.Command{
		Use:   "unmount",
		Short: "unmount a dysk on the local machine",
		Long: `This subcommand unmounts a single dysk of the local host
example:
dyskctl unmount --deviceName dysk01`,
		Run: func(cmd *cobra.Command, args []string) {
			validateOutput()
			dyskClient := client.CreateClient("", "")
			err := dyskClient.Unmount(deviceName, breakLeaseFlag)
			if nil != err {
				printError(err)
				os.Exit(1)
			}
			printStatus(fmt.Sprintf("Device:%s unmounted", deviceName))
		},
	}

	getCmd = &cobra.Command{
		Use:   "get",
		Short: "gets a dysk (mounted on the local host) by name",
		Long: `This subcommand gets a single dysk of the local host
example:
dyskctl get --deviceName dysk01`,
		Run: func(cmd *cobra.Command, args []string) {
			validateOutput()
			dyskClient := client.CreateClient("", "")
			d, err := dyskClient.Get(deviceName)

			if nil != err {
				printError(err)
				os.Exit(1)
			}
			printDysk(d)
		},
	}

	listCmd = &cobra.Command{
		Use:   "list",
		Short: "lists all dysks mounted on local host",
		Long: `This subcommand lists all dysks mounted on local host
example:
dyskctl list`,
		Run: func(cmd *cobra.Command, args []string) {
			validateOutput()
			dyskClient := client.CreateClient("", "")
			dysks, err := dyskClient.List()

			if nil != err {
				printError(err)
				os.Exit(1)
			}
			printDysks(dysks)
		},
	}
)

func init() {
	// MOUNT //
	mountCmd.PersistentFlags().StringVarP(&storageAccountName, "account", "a", "", "Azure storage account name")
	mountCmd.PersistentFlags().StringVarP(&storageAccountKey, "key", "k", "", "Azure storage account key")
	mountCmd.PersistentFlags().StringVarP(&container, "container-name", "c", "dysks", "Azure storage blob container name)")
	mountCmd.PersistentFlags().StringVarP(&deviceName, "device-name", "d", "", "block device name. if empty a random name will be used")
	mountCmd.PersistentFlags().StringVarP(&leaseId, "lease-id", "i", "", "lease id of an existing blob")
	mountCmd.PersistentFlags().BoolVarP(&vhdFlag, "vhd", "v", true, "writes the vhd footer to the blob page")
	mountCmd.PersistentFlags().BoolVarP(&readOnlyFlag, "read-only", "r", false, "mount dysk as read only")
	mountCmd.PersistentFlags().BoolVarP(&autoLeaseFlag, "auto-lease", "l", true, "create lease if not provided")
	mountCmd.PersistentFlags().BoolVarP(&breakLeaseFlag, "break-lease", "b", false, "allow breaking of existing lease while creating")

	// MOUNT CREATE WE NEED SIZE //
	mountCreateCmd.PersistentFlags().UintVarP(&size, "size", "n", 2, "page blob size gb")

	// MOUNT BASED ON FILE //
	mountFileCmd.PersistentFlags().StringVarP(&filePath, "file", "f", "", "json file path location")

	// UNMOUNT //
	unmountCmd.PersistentFlags().StringVarP(&deviceName, "device-name", "d", "", "block device name")
	unmountCmd.PersistentFlags().BoolVarP(&breakLeaseFlag, "break-lease", "b", false, "break existing lease on dysk's page blob")

	// GET //
	getCmd.PersistentFlags().StringVarP(&deviceName, "device-name", "d", "", "block device name")

	// LIST //
	// no args //

	viper.SetEnvPrefix("dysk")
	viper.BindPFlag("account", mountCmd.Flags().Lookup("account"))
	viper.BindPFlag("key", mountCmd.Flags().Lookup("key"))

	mountCmd.AddCommand(mountCreateCmd)
	rootCmd.AddCommand(mountCmd)
	rootCmd.AddCommand(mountFileCmd)
	rootCmd.AddCommand(unmountCmd)
	rootCmd.AddCommand(getCmd)
	rootCmd.AddCommand(listCmd)
}
