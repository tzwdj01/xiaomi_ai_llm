#!/bin/sh
set -eu
PATH=/usr/sbin:/usr/bin:/sbin:/bin; export PATH
P=/data/s12-followup
[ "$(cat "$P/install.state" 2>/dev/null)" = UPDATING ] || exit 0
mkdir /tmp/s12-update-recovery.lock 2>/dev/null || exit 1
trap 'rmdir /tmp/s12-update-recovery.lock 2>/dev/null || :' EXIT
check(){ [ "$(md5sum "$1" | cut -d ' ' -f 1)" = "$2" ]; }
check "$P/update-old-capture" a0f79ab36e87b9eb329de5df8bca7d90
check "$P/update-old-manager" f7b30f6d79f4bbac8aca9e7400880afa
check "$P/update-old-client" e2fc1c272f5f8310b93cebfc0ec4735c
known(){ v=$(md5sum "$1" | cut -d ' ' -f 1); [ "$v" = "$2" ] || [ "$v" = "$3" ]; }
known "$P/capture" a0f79ab36e87b9eb329de5df8bca7d90 869705e7bfcddb90e338bc71d883ffcb
known "$P/manager.sh" f7b30f6d79f4bbac8aca9e7400880afa 5e845de6ea3f3a15335239f983a23993
known "$P/native_first_client.sh" e2fc1c272f5f8310b93cebfc0ec4735c 9ed5fb54a8a627c16a3ad1a709e954a6
if [ -s /tmp/native_first_client.pid ];then sh "$P/native_first_client.sh" stop >> "$P/update-recovery.log" 2>&1 || :;fi
if [ -d /tmp/s12-pcm-probe ];then sh "$P/manager.sh" stop >> "$P/update-recovery.log" 2>&1 || :;fi
for pair in 'capture update-old-capture' 'manager.sh update-old-manager' 'native_first_client.sh update-old-client';do set -- $pair;cp "$P/$2" "$P/$1.recover";chmod 700 "$P/$1.recover";mv "$P/$1.recover" "$P/$1";done
sync
echo COMMITTED > "$P/install.state.new";mv "$P/install.state.new" "$P/install.state";sync
echo UPDATE_RECOVERED
