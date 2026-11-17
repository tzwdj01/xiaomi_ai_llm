#!/bin/sh
set -eu
PATH=/usr/sbin:/usr/bin:/sbin:/bin; export PATH
P=/data/s12-followup
[ ! -e /tmp/native_first_busy ] && [ ! -e "$P/install.state" ]
[ "$(micocfg_model)" = S12 ] && [ "$(uci -c /usr/share/mico -q get version.version.ROM)" = 1.74.18 ]
[ "$(cat /sys/class/ubi/ubi0/mtd_num)" = 4 ]
[ "$(md5sum /data/init.sh | cut -d ' ' -f 1)" = d9f74cbde913afdcd00462f15573aa42 ]
[ "$(md5sum /data/native_first_client.sh | cut -d ' ' -f 1)" = f8f2d8fc3d43a2914c21e5e814afd6e2 ]
[ "$(md5sum "$P/native_first_client.sh" | cut -d ' ' -f 1)" = e2fc1c272f5f8310b93cebfc0ec4735c ]
[ "$(md5sum /data/native_first.env | cut -d ' ' -f 1)" = "$(cat "$P/env-before.md5")" ]
[ "$(md5sum "$P/candidate-env" | cut -d ' ' -f 1)" = "$(cat "$P/env-after.md5")" ]
[ "$(md5sum "$P/original-init.sh" | cut -d ' ' -f 1)" = d9f74cbde913afdcd00462f15573aa42 ]
for f in guard-init.sh recover.sh new-init.sh manager.sh native_first_client.sh; do sh -n "$P/$f"; done
mkdir /tmp/s12-dashboard-config.lock 2>/dev/null || { echo SETTINGS_BUSY; exit 1; }
trap 'rmdir /tmp/s12-dashboard-config.lock 2>/dev/null || :' EXIT
umask 077
echo PREPARED > "$P/install.state.new"; mv "$P/install.state.new" "$P/install.state"; sync
cp "$P/guard-init.sh" /data/init.sh.new; chmod 700 /data/init.sh.new; mv /data/init.sh.new /data/init.sh; sync
echo APPLYING > "$P/install.state.new"; mv "$P/install.state.new" "$P/install.state"; sync
(trap '' HUP; sleep 100; if [ "$(cat "$P/install.state" 2>/dev/null)" = APPLYING ]; then if sh "$P/recover.sh" >> "$P/recovery.log" 2>&1; then CONFIG_FILE=/data/native_first.env sh /data/native_first_client.sh </dev/null >/dev/null 2>&1 & fi; fi) </dev/null >/dev/null 2>&1 &
rollback(){ if sh "$P/recover.sh" >> "$P/recovery.log" 2>&1; then CONFIG_FILE=/data/native_first.env sh /data/native_first_client.sh </dev/null >/dev/null 2>&1 & fi; rmdir /tmp/s12-dashboard-config.lock 2>/dev/null || :; }
trap rollback EXIT
[ ! -e /tmp/native_first_busy ]
sh /data/native_first_client.sh stop >> "$P/install.log" 2>&1
[ "$(md5sum /data/native_first.env | cut -d ' ' -f 1)" = "$(cat "$P/env-before.md5")" ]
cp "$P/candidate-env" /data/native_first.env.new; chmod 600 /data/native_first.env.new; mv /data/native_first.env.new /data/native_first.env; sync
CONFIG_FILE=/data/native_first.env sh "$P/native_first_client.sh" </dev/null >/dev/null 2>&1 &
n=0
until [ -s /tmp/native_first_client.pid ] && kill -0 "$(cat /tmp/native_first_client.pid)" 2>/dev/null && grep -q 'native PCM tap ready' /tmp/native_first_client.log && grep -q '\[IDLE\] 等待原生唤醒词' /tmp/native_first_client.log && sh "$P/manager.sh" status | grep -q READY; do
 n=$((n+1)); [ "$n" -lt 45 ] || exit 1; sleep 1
done
touch "$P/enabled"
echo COMMITTED > "$P/install.state.new"; mv "$P/install.state.new" "$P/install.state"; sync
trap - EXIT
rmdir /tmp/s12-dashboard-config.lock 2>/dev/null || :
echo COMMITTED > "$P/install.result"
echo INSTALL_COMMITTED
