#!/bin/sh
set -u
PATH=/usr/sbin:/usr/bin:/sbin:/bin;export PATH
D=/tmp/s12-handoff
[ -f "$D/active" ] || exit 0
mkdir "$D/restore.lock" 2>/dev/null || exit 0
trap 'rmdir "$D/restore.lock" 2>/dev/null || :' EXIT
n=0;while [ -e /tmp/native_first_busy ] && [ "$n" -lt 60 ];do sleep 1;n=$((n+1));done
CONFIG_FILE="$D/trial.env" sh "$D/native_first_client.sh" stop >> "$D/restore.log" 2>&1 || :
CONFIG_FILE=/data/native_first.env sh /data/s12-followup/native_first_client.sh </dev/null >/dev/null 2>&1 &
sleep 5
if [ -s /tmp/native_first_client.pid ] && kill -0 "$(cat /tmp/native_first_client.pid)" 2>/dev/null;then
 rm -f "$D/active" /tmp/s12-pcm-probe/record /tmp/s12-pcm-probe/capture.raw /tmp/voice.wav
 echo RESTORED > "$D/status"
else echo RESTORE_FAILED > "$D/status";exit 1;fi
