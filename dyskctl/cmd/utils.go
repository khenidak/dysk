package cmd

import (
	"encoding/json"
	"fmt"
	"math/rand"
	"os"
	"text/tabwriter"

	"github.com/khenidak/dysk/pkg/client"
)

func mount() {
	var err error
	dyskClient := client.CreateClient(storageAccountName, storageAccountKey)
	pageBlobName := ""

	if "" == deviceName {
		deviceName = getRandomDyskName()
	}

	pageBlobName = deviceName

	if vhdFlag {
		pageBlobName += ".vhd"
	}

	if 0 == size {
		size = 2
	}

	if autoCreate {
		leaseId, err = dyskClient.CreatePageBlob(size, container, pageBlobName, vhdFlag)
		if nil != err {
			printError(err)
			os.Exit(1)
		}
	}

	d := client.Dysk{}
	d.Type = client.ReadWrite
	if readOnlyFlag {
		d.Type = client.ReadOnly
	}
	d.Name = deviceName
	d.SizeGB = int(size)
	d.Path = "/" + container + "/" + pageBlobName
	d.LeaseId = leaseId
	d.Vhd = vhdFlag

	err = dyskClient.Mount(&d)
	if nil != err {
		printError(err)
		os.Exit(1)
	}

	dysks := []*client.Dysk{&d}
	printDysks(dysks)

}

func getRandomDyskName() string {
	var of = []rune("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")
	out := make([]rune, 20)
	for i := range out {
		out[i] = of[rand.Intn(len(of))]
	}
	return "d" + string(out)
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

func printDysks(dysks []*client.Dysk) {
	if "table" == output_format {
		w := new(tabwriter.Writer)
		w.Init(os.Stdout, 32, 2, 0, ' ', 0)

		fmt.Fprintln(w, "Type\tName\tVHD\tSizeGB\tAccountName\tPath")
		for _, d := range dysks {
			isVhd := "No"
			if d.Vhd {
				isVhd = "Yes"
			}
			format := "%s\t%s\t%s\t%d\t%s\t%s"
			toPrint := fmt.Sprintf(format, string(d.Type), d.Name, isVhd, d.SizeGB, d.AccountName, d.Path)

			fmt.Fprintln(w, toPrint)
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
