#!/bin/sh
# Dependency-free smoke: unit tests + CLI convert/decide + doctor.
set -e
ctl=$1
unit=$2
data=$3
if [ -n "$data" ]; then
  KEYBOOP_DATA_DIR=$data
  export KEYBOOP_DATA_DIR
fi
if [ -n "$unit" ]; then
  "$unit"
fi
out=$("$ctl" convert ghbdtn)
[ "$out" = "привет" ]
dec=$("$ctl" decide ghbdtn)
[ "$dec" = "to-ru" ]
"$ctl" doctor | grep -q '^session:'
"$ctl" doctor | grep -q '^backend:'
"$ctl" doctor | grep -q '^im:'
"$ctl" doctor | grep -q '^auto:'
"$ctl" auto | grep -q '^auto='
"$ctl" hotkey | grep -q '^hotkey='
echo "smoke ok"
