#!/bin/sh
set -eu
PATH=/usr/sbin:/usr/bin:/sbin:/bin;export PATH
P=/data/s12-followup
check(){ [ "$(md5sum "$1" | cut -d ' ' -f 1)" = "$2" ]; }
[ "$(cat "$P/install.state")" = COMMITTED ] && [ ! -e /tmp/native_first_busy ]
[ "$(micocfg_model)" = S12 ] && [ "$(uci -c /usr/share/mico -q get version.version.ROM)" = 1.74.18 ]
check "$P/capture" a0f79ab36e87b9eb329de5df8bca7d90
check "$P/manager.sh" f7b30f6d79f4bbac8aca9e7400880afa
check "$P/native_first_client.sh" e2fc1c272f5f8310b93cebfc0ec4735c
check "$P/update-new-capture" 869705e7bfcddb90e338bc71d883ffcb
check "$P/update-new-manager" 5e845de6ea3f3a15335239f983a23993
check "$P/update-new-client" 9ed5fb54a8a627c16a3ad1a709e954a6
for f in update-recover.sh update-guard.sh update-new-manager update-new-client;do sh -n "$P/$f";done
mkdir /tmp/s12-dashboard-config.lock 2>/dev/null || exit 1
trap 'rmdir /tmp/s12-dashboard-config.lock 2>/dev/null || :' EXIT
umask 077
cp "$P/capture" "$P/update-old-capture";cp "$P/manager.sh" "$P/update-old-manager";cp "$P/native_first_client.sh" "$P/update-old-client"
cp "$P/update-guard.sh" /data/init.sh.update;chmod 700 /data/init.sh.update;mv /data/init.sh.update /data/init.sh;sync
echo UPDATING > "$P/install.state.new";mv "$P/install.state.new" "$P/install.state";sync
(trap '' HUP;sleep 100;if [ "$(cat "$P/install.state" 2>/dev/null)" = UPDATING ];then if sh "$P/update-recover.sh" >> "$P/update-recovery.log" 2>&1;then CONFIG_FILE=/data/native_first.env sh "$P/native_first_client.sh" </dev/null >/dev/null 2>&1 & fi;fi) </dev/null >/dev/null 2>&1 &
rollback(){ if sh "$P/update-recover.sh" >> "$P/update-recovery.log" 2>&1;then CONFIG_FILE=/data/native_first.env sh "$P/native_first_client.sh" </dev/null >/dev/null 2>&1 & fi;rmdir /tmp/s12-dashboard-config.lock 2>/dev/null || :; }
trap rollback EXIT
[ ! -e /tmp/native_first_busy ]
sh "$P/native_first_client.sh" stop >> "$P/update.log" 2>&1
sh "$P/manager.sh" stop >> "$P/update.log" 2>&1
for pair in 'capture update-new-capture' 'manager.sh update-new-manager' 'native_first_client.sh update-new-client';do set -- $pair;cp "$P/$2" "$P/$1.update";chmod 700 "$P/$1.update";mv "$P/$1.update" "$P/$1";done
sync
CONFIG_FILE=/data/native_first.env sh "$P/native_first_client.sh" </dev/null >/dev/null 2>&1 &
n=0
until [ -s /tmp/native_first_client.pid ] && kill -0 "$(cat /tmp/native_first_client.pid)" 2>/dev/null && grep -q 'native PCM tap ready' /tmp/native_first_client.log && grep -q '\[IDLE\] 等待原生唤醒词' /tmp/native_first_client.log && sh "$P/manager.sh" status | grep -q READY;do n=$((n+1));[ "$n" -lt 45 ] || exit 1;sleep 1;done
echo COMMITTED > "$P/install.state.new";mv "$P/install.state.new" "$P/install.state";sync
trap - EXIT;rmdir /tmp/s12-dashboard-config.lock 2>/dev/null || :
echo UPDATED > "$P/update.result"
