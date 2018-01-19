#!/bin/bash

set -eo pipefail

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
V_SETTINGS=${V_SETTINGS:-"${DIR}/settings.json"}
V_RUN_TYPE="$1"

V_ACCOUNT_NAME=""
V_ACCOUNT_KEY=""

V_TESTS=()
V_TEST_FORMAT="{\"name\" : \"%s\", \"script\" : \"%s\" , \"type\" : \"%s\" }"

DYSKCTL="${DIR}/../../dyskctl/dyskctl"

function ensure_ready()
{
  if [[ "" == "$(which jq)" ]];then
    echo "jq does not exist, please install jq"
    exit 1
  fi

  if [[ "" == "$(which fio)" ]];then
    echo "fio does not exist, please install fio"
    exit 1
  fi

  if [[ ! -f "${V_SETTINGS}" ]]; then
    echo "settings file at ${V_SETTINGS} does not exist"
    exit 1
  fi

  echo "using settings file:${V_SETTINGS}"  
  V_ACCOUNT_NAME="$(cat "${V_SETTINGS}" | jq -r '.account //empty')"
  V_ACCOUNT_KEY="$(cat "${V_SETTINGS}" | jq -r '.key //empty')"

  if [[ -z "$V_ACCOUNT_NAME}" || -z "$V_ACCOUNT_KEY" ]]; then
    echo "invalid account name or key"
    exit 1
  fi
}

function add_test()
{
  local test_name="$1"
  local test_script="$2"
  local test_type="$3"

  local v_test="$(printf "${V_TEST_FORMAT}" "${test_name}" "${test_script}" "${test_type}")"

  V_TESTS+=("${v_test}")
}


function run_tests()
{
  for vt in "${V_TESTS[@]}"  
  do
    local test_name="$(echo -n "${vt}" | jq -r '.name //empty')"
    local test_script="$(echo -n "${vt}" | jq -r '.script //empty')" 
    local test_type="$(echo -n "${vt}" | jq -r '.type //empty')"
    if [[ -z "${test_name}" || ! -f "${DIR}/${test_script}" ]];then
      echo "skipping test:[${test_name}] script:[${test_script}] because name is empty script file does not exist"
    else
      if [[ "${V_RUN_TYPE}" ==  "${test_type}" ]];then
        echo "==>START TEST - ${test_type}: ${test_name} SCRIPT:${test_script}"
        "${DIR}/${test_script}" "${V_ACCOUNT_NAME}" "${V_ACCOUNT_KEY}" "${DYSKCTL}"
        echo "==>END TEST - ${test_type}: ${test_name} SCRIPT:${test_script}"
      fi
    fi
  done  
}


#verify tests
add_test "Simple add/remove" "v_simple_add_remove.sh" "VERIFY"
add_test "mount read only" "v_mount_ro.sh" "VERIFY"
add_test "Add, format, remove, add another" "v_add_format_remove_add.sh" "VERIFY"
add_test "add remove 128 dysks" "v_add_remove_128dysks.sh" "VERIFY"
add_test "Add/Format/Remove 10 dysks" "v_add_format_remove_10dysks.sh" "VERIFY"

#perf tests
#add_test "Basic perf tests" "p_basic_io.sh" "PERF"


# START HERE
ensure_ready

if [[ -z "${V_RUN_TYPE}" ]]; then
  echo "No run type, setting it to VERIFY"
  V_RUN_TYPE="VERIFY"
fi

run_tests

