package cmd

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"strings"

	"github.com/khenidak/dysk/pkg/client"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
)

var (
	storageAccountName string
	storageAccountKey  string

	filePath string

	pageBlobName   string
	container      string
	leaseId        string
	deviceName     string
	size           uint
	vhdFlag        bool
	readOnlyFlag   bool
	autoLeaseFlag  bool
	breakLeaseFlag bool

	autoCreate bool // set when sub command autocreate is used
	mount      bool // set when mount commands are called

	// Convert args
	namespace        string
	reclaimPolicy    string
	accessMode       string
	secretName       string
	secretNamespace  string
	fsType           string
	storageClassName string
	arlabels         string

	mountCmd = &cobra.Command{
		Use:   "mount",
		Short: "mounts a page blob as block device",
		Long: `creates and mount a page blob as a block device.
example:

#Mount an existing page blob
dyskctl mount --account {acount-name} --key {key} --device-name d01 --container {container-name}`,
		Run: func(cmd *cobra.Command, args []string) {
			validateOutput()
			mount = true
			mount_create()
		},
	}
	createCmd = &cobra.Command{
		Use:   "create",
		Short: "creates a page blob but does not mount it",
		Long: `creates a page blob but does not mount it.
example:
#create 1TB page blob
dyskctl create --account {acount-name} --key {key} --device-name d01 --container {container-name} --size 1024`,
		Run: func(cmd *cobra.Command, args []string) {
			validateOutput()
			autoCreate = true
			mount = false
			mount_create()
		},
	}
	deleteCmd = &cobra.Command{
		Use:   "delete",
		Short: "deletes a page blob from azure storage",
		Long: `deletes a page blob from azure storage, if the pageblob is mounted it will go into catastrophe mode.
example:
#Delete if not leased
dyskctl delete --account {acount-name} --key {key} --pageblob-name {pageblobname} --container {container-name}
#Delete (if leased, lease will be broken first)
dyskctl delete --account {acount-name} --key {key} --pageblob-name {pageblobname} --container {container-name} --break-lease
#Delete with lease
dyskctl delete --account {acount-name} --key {key} --pageblob-name {pageblobname} --container {container-name} --lease-id
`,
		Run: func(cmd *cobra.Command, args []string) {
			dyskClient := client.CreateClient(storageAccountName, storageAccountKey)
			if err := dyskClient.DeletePageBlob(container, pageBlobName, leaseId, breakLeaseFlag); nil != err {
				printError(err)
				os.Exit(1)
			}
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
			autoLeaseFlag = true
			mount = true
			mount_create()
		},
	}

	convertPvCmd = &cobra.Command{
		Use:   "convert-pv",
		Short: "convert a dysk to kuberentes PV",
		Long: `.
example:
dyskctl convert-pv --file {path to file} --secretName {kubernetes secret name} 
#using stdin
cat {dysk-file} | dyskctl convert-pv --secretName {kubernetes secret name}
`,
		Run: func(cmd *cobra.Command, args []string) {
			// validate

			if "" == secretName {
				printError(fmt.Errorf("secretName is required"))
				os.Exit(1)
			}

			labels := make(map[string]string)
			splitLabels := strings.Split(arlabels, ",")
			for _, val := range splitLabels {
				split := strings.Split(val, "=")
				labels[split[0]] = split[1]
			}

			var d client.Dysk
			var file []byte
			var err error
			if "" != filePath {
				file, err = ioutil.ReadFile(filePath)
				if nil != err {
					printError(err)
					os.Exit(1)
				}
			} else {
				file, err = ioutil.ReadAll(os.Stdin)
				if nil != err {
					printError(err)
					os.Exit(1)
				}
			}

			err = json.Unmarshal(file, &d)
			if nil != err {
				printError(err)
				os.Exit(1)
			}

			pv := dysk2Pv(namespace,
				reclaimPolicy,
				accessMode,
				secretName,
				secretNamespace,
				fsType,
				d.Type == client.ReadOnly || readOnlyFlag,
				storageClassName,
				labels,
				&d)

			enc := json.NewEncoder(os.Stdout)
			enc.SetIndent("", "    ")
			err = enc.Encode(pv)
			if nil != err {
				printError(err)
			}

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
			dyskClient := client.CreateClientWithSas(d.AccountName, d.Sas)
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
	mountCmd.PersistentFlags().StringVarP(&pageBlobName, "pageblob-name", "p", "", "Azure storage page blob name")
	mountCmd.PersistentFlags().StringVarP(&container, "container-name", "c", "dysks", "Azure storage blob container name)")
	mountCmd.PersistentFlags().StringVarP(&deviceName, "device-name", "d", "", "block device name. if empty a random name will be used")
	mountCmd.PersistentFlags().StringVarP(&leaseId, "lease-id", "i", "", "an existing valid lease id of the page blob (not needed for r/o dysks)")
	mountCmd.PersistentFlags().BoolVarP(&vhdFlag, "vhd", "v", true, "writes the vhd footer to the blob page")
	mountCmd.PersistentFlags().BoolVarP(&readOnlyFlag, "read-only", "r", false, "mount dysk as read only")
	mountCmd.PersistentFlags().BoolVarP(&autoLeaseFlag, "auto-lease", "l", true, "create lease if not provided")
	mountCmd.PersistentFlags().BoolVarP(&breakLeaseFlag, "break-lease", "b", false, "allow breaking of existing lease while creating")

	// CREATE //
	createCmd.PersistentFlags().StringVarP(&storageAccountName, "account", "a", "", "Azure storage account name")
	createCmd.PersistentFlags().StringVarP(&storageAccountKey, "key", "k", "", "Azure storage account key")
	createCmd.PersistentFlags().StringVarP(&pageBlobName, "pageblob-name", "p", "", "Azure storage page blob name. (if empty a random name will be used)")
	createCmd.PersistentFlags().StringVarP(&container, "container-name", "c", "dysks", "Azure storage blob container name")
	createCmd.PersistentFlags().StringVarP(&deviceName, "device-name", "d", "", "block device name. (if empty a random name will be used)")
	createCmd.PersistentFlags().BoolVarP(&vhdFlag, "vhd", "v", true, "writes the vhd footer to the blob page")
	createCmd.PersistentFlags().BoolVarP(&readOnlyFlag, "read-only", "r", false, "mount dysk as read only")
	createCmd.PersistentFlags().BoolVarP(&autoLeaseFlag, "auto-lease", "l", false, "lease the new page blob")
	createCmd.PersistentFlags().UintVarP(&size, "size", "n", 2, "page blob size gb")

	// DELETE //
	deleteCmd.PersistentFlags().StringVarP(&storageAccountName, "account", "a", "", "Azure storage account name")
	deleteCmd.PersistentFlags().StringVarP(&storageAccountKey, "key", "k", "", "Azure storage account key")
	deleteCmd.PersistentFlags().StringVarP(&pageBlobName, "pageblob-name", "p", "", "Azure storage page blob name")
	deleteCmd.PersistentFlags().StringVarP(&container, "container-name", "c", "dysks", "Azure storage blob container name")
	deleteCmd.PersistentFlags().StringVarP(&leaseId, "lease-id", "i", "", "dysk is already leased, use this lease while deleting")
	deleteCmd.PersistentFlags().BoolVarP(&breakLeaseFlag, "break-lease", "b", false, "force delete even page blob is leased")

	// MOUNT CREATE - WE NEED SIZE //
	mountCreateCmd.PersistentFlags().UintVarP(&size, "size", "n", 2, "page blob size gb")

	// CONVERT PV //
	convertPvCmd.PersistentFlags().StringVarP(&filePath, "file", "f", "", "json file path")
	convertPvCmd.PersistentFlags().StringVar(&storageClassName, "storageclass-name", "", "volume storage class")

	convertPvCmd.PersistentFlags().StringVar(&namespace, "namespace", "", "volume namespace")
	convertPvCmd.PersistentFlags().StringVar(&reclaimPolicy, "reclaim-policy", "Retain", "volume reclaim policy")
	convertPvCmd.PersistentFlags().StringVar(&accessMode, "access-mode", "ReadWriteOnce", "volume access mode")
	convertPvCmd.PersistentFlags().StringVar(&secretName, "secret-name", "", "Flex vol secret ref (name) ")
	convertPvCmd.PersistentFlags().StringVar(&secretNamespace, "secret-namespace", "", "Flex vol secret ref (namespace) only kubernetes-v10+ ")
	convertPvCmd.PersistentFlags().StringVar(&fsType, "fsType", "ext4", "volume filesystem type - if dysk is row it will be formatted with fstype")
	convertPvCmd.PersistentFlags().StringVar(&arlabels, "labels", "", "pv labels key1=val1,key2=val2")
	convertPvCmd.PersistentFlags().BoolVar(&readOnlyFlag, "read-only", false, "override dysk readonly value")
	//MOUNT BASED ON FILE //
	mountFileCmd.PersistentFlags().StringVarP(&filePath, "file", "f", "", "json file path")

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
	rootCmd.AddCommand(createCmd)
	rootCmd.AddCommand(deleteCmd)
	rootCmd.AddCommand(convertPvCmd)
	rootCmd.AddCommand(mountFileCmd)
	rootCmd.AddCommand(unmountCmd)
	rootCmd.AddCommand(getCmd)
	rootCmd.AddCommand(listCmd)
}
