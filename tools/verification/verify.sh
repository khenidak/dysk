#!/bin/bash

set -eo pipefail

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
V_SETTINGS=${V_SETTINGS:-"${DIR}/settings.json"}

V_ACCOUNT_NAME=""
V_ACCOUNT_KEY=""

V_TESTS=()
V_TEST_FORMAT="{\"name\" : \"%s\", \"script\" : \"%s\" }"

DYSKCTL="${DIR}/../../dyskctl/dyskctl"

function ensure_ready()
{
  if [[ "" == "$(which jq)" ]];then
    echo "jq does not exist, please install jq"
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

  local v_test="$(printf "${V_TEST_FORMAT}" "${test_name}" "${test_script}")"

  V_TESTS+=("${v_test}")
}


function run_tests()
{
  for vt in "${V_TESTS[@]}"  
  do
    local test_name="$(echo -n "${vt}" | jq -r '.name //empty')"
    local test_script="$(echo -n "${vt}" | jq -r '.script //empty')" 
    if [[ -z "${test_name}" || ! -f "${DIR}/${test_script}" ]];then
      echo "skipping test:[${test_name}] script:[${test_script}] because name is empty script file does not exist"
    else
      echo "==>START TEST:${test_name} SCRIPT:${test_script}"
      "${DIR}/${test_script}" "${V_ACCOUNT_NAME}" "${V_ACCOUNT_KEY}" "${DYSKCTL}"
      echo "==>END TEST:${test_name} SCRIPT:${test_script}"
    fi
  done  
}


add_test "Simple add/remove" "v_simple_add_remove.sh"
add_test "Add, format, remove, add another" "v_add_format_remove_add.sh"
add_test "add remove 128 dysks" "v_add_remove_128dysks.sh"
add_test "Add/Format/Remove 10 dysks" "v_add_format_remove_10dysks.sh"

# START HERE
ensure_ready
run_tests

