#!/bin/sh
set -eu
PATH=/usr/sbin:/usr/bin:/sbin:/bin;export PATH
D=/tmp/s12-handoff
[ ! -e /tmp/native_first_busy ] && [ ! -e "$D/active" ]
[ "$(cat /data/s12-followup/install.state)" = COMMITTED ]
sh /data/s12-followup/manager.sh status | grep -q READY
umask 077;cp /data/native_first.env "$D/trial.env";printf '\nPCM_CAPTURE_BIN=/tmp/s12-handoff/capture\n' >> "$D/trial.env";chmod 600 "$D/trial.env";sh -n "$D/trial.env"
touch "$D/active"
(trap '' HUP;sleep 180;sh "$D/restore.sh" >> "$D/watchdog.log" 2>&1) </dev/null >/dev/null 2>&1 &
trap 'sh "$D/restore.sh" >> "$D/watchdog.log" 2>&1 || :' EXIT
sh /data/s12-followup/native_first_client.sh stop >> "$D/activate.log" 2>&1
CONFIG_FILE="$D/trial.env" sh "$D/native_first_client.sh" </dev/null >/dev/null 2>&1 &
sleep 6
[ -s /tmp/native_first_client.pid ] && kill -0 "$(cat /tmp/native_first_client.pid)"
grep -q 'native PCM tap ready' /tmp/native_first_client.log
echo ACTIVE > "$D/status"
trap - EXIT
