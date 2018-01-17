#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
account="$1"
key="$2"
DYSKCTL="$3"

"${DIR}"/__add_remove_format_dysk.sh "${account}" "${key}" "${DYSKCTL}" "128" "NO FORMAT"
