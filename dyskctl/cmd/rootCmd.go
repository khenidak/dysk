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
	Use:   "dyskctl",
	Short: "dyskctl allows interactions with the dysk kernel module",
	Long: `This application interacts with the dysk kernel module 
http://github.com/khenidak/dysk/ in order 
to mount/unmount Azure storage as block devices.

Note: default permission on the device file is for root only. You 
will need to sudo while executing dyskctl or change the permission 
on the /dev/dysk device file.
`,
}

// Execute executes the root command.
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
