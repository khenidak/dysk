package cmd

import (
	"encoding/json"
	"fmt"
	"math/rand"
	"os"
	"text/tabwriter"

	"github.com/khenidak/dysk/pkg/client"
)

func mount_create() {
	var err error
	dyskClient := client.CreateClient(storageAccountName, storageAccountKey)

	if "" == deviceName {
		deviceName = getRandomDyskName()
	}

	if 0 == size {
		size = 2
	}

	if autoCreate {
		if "" == pageBlobName {
			pageBlobName = deviceName
		}

		if vhdFlag && pageBlobName[len(pageBlobName)-4:] != ".vhd" {
			pageBlobName += ".vhd"
		}

		leaseId, err = dyskClient.CreatePageBlob(size, container, pageBlobName, vhdFlag, autoLeaseFlag)
		if nil != err {
			printError(err)
			os.Exit(1)
		}
	} else {
		if "" == pageBlobName {
			printError(fmt.Errorf("Mounting existing page blob requires a page blob name"))
			os.Exit(1)
		}
	}

	d := client.Dysk{}
	d.Type = client.ReadWrite
	if readOnlyFlag {
		d.Type = client.ReadOnly
	}
	d.AccountName = storageAccountName
	d.Sas = storageAccountKey
	d.Name = deviceName
	d.SizeGB = int(size)
	d.Path = "/" + container + "/" + pageBlobName
	d.LeaseId = leaseId
	d.Vhd = vhdFlag

	if mount {
		err = dyskClient.Mount(&d, autoLeaseFlag, breakLeaseFlag)
		if nil != err {
			printError(err)
			os.Exit(1)
		}
	}
	printDysk(&d)
}

func getRandomDyskName() string {
	var of = []rune("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")
	out := make([]rune, 8)
	for i := range out {
		out[i] = of[rand.Intn(len(of))]
	}
	return "dysk" + string(out)
}

func validateOutput() {
	if "json" != output_format && "table" != output_format {
		printError(fmt.Errorf("Invalid output format"))
		os.Exit(1)
	}
}
func printError(err error) {
	fmt.Fprintf(os.Stderr, "Err:\n%s\n", err.Error())
}

func printStatus(message string) {
	fmt.Fprintf(os.Stderr, "Status:\n%s\n", message)
}

func printDyskLine(d *client.Dysk, w *tabwriter.Writer) {
	isVhd := "No"
	if d.Vhd {
		isVhd = "Yes"
	}
	format := "%s\t%s\t%s\t%d\t%s\t%s"
	toPrint := fmt.Sprintf(format, string(d.Type), d.Name, isVhd, d.SizeGB, d.AccountName, d.Path)

	fmt.Fprintln(w, toPrint)
}

func printDyskHeader(w *tabwriter.Writer) {
	fmt.Fprintln(w, "Type\tName\tVHD\tSizeGB\tAccountName\tPath")
}

func printDysk(d *client.Dysk) {
	if "table" == output_format {
		w := new(tabwriter.Writer)
		w.Init(os.Stdout, 32, 2, 0, ' ', 0)
		printDyskHeader(w)
		printDyskLine(d, w)
		w.Flush()
	} else {
		enc := json.NewEncoder(os.Stdout)
		enc.SetIndent("", "    ")
		err := enc.Encode(d)
		if nil != err {
			printError(err)
		}
	}
}
func printDysks(dysks []*client.Dysk) {
	if "table" == output_format {
		w := new(tabwriter.Writer)
		w.Init(os.Stdout, 32, 2, 0, ' ', 0)
		printDyskHeader(w)

		for _, d := range dysks {
			printDyskLine(d, w)
		}
		w.Flush()
	} else {
		enc := json.NewEncoder(os.Stdout)
		enc.SetIndent("", "    ")
		err := enc.Encode(dysks)
		if nil != err {
			printError(err)
		}
	}
}
