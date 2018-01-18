#!/bin/bash

set -eo pipefail
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

account="$1"
key="$2"
DYSKCTL="$3"


"${DIR}"/__run_perf_test.sh "${account}" "${key}" ${DYSKCTL} "4096"  "${DIR}/basic.fio"
