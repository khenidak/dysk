package cmd

import (
	"fmt"
	"math/rand"
	"os"
	"time"

	"github.com/spf13/cobra"
)

var output_format = ""
var rootCmd = &cobra.Command{
	Use:   "dyskctl mount ",
	Short: "An example of cobra",
	Long: `This application interacts with dysk kernel module 
http://github.com/khenidak/dysk/ in order 
to mount/unmount Azure storage as block devices

Note: default permission on the device file is for root only . you 
will need to sudo while executing dyskctl or change the permission 
on /dev/dysk device file
`,
}

func Execute() {
	if err := rootCmd.Execute(); err != nil {
		fmt.Println(err)
		os.Exit(1)
	}
}

func init() {
	rand.Seed(time.Now().UnixNano())
	rootCmd.PersistentFlags().StringVarP(&output_format, "output", "o", "table", "output format")
}
