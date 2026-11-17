#!/bin/sh
set -eu
PATH=/usr/sbin:/usr/bin:/sbin:/bin; export PATH
D=/tmp/s12-pcm-probe
P=/data/s12-followup
TAG=S12_PCM_FOLLOWUP_MANAGED
PNS=/etc/init.d/pns
mounted(){ awk -v p="$1" '$2==p {found=1} END {exit !found}' /proc/mounts; }
hash_is(){ [ "$(md5sum "$1" 2>/dev/null | cut -d ' ' -f 1)" = "$2" ]; }
owned(){ mounted "$PNS" && grep -q "$TAG" "$PNS"; }
stop(){
 [ ! -f "$D/record" ] || rm "$D/record"
 if mounted "$PNS"; then
  owned || { echo 'foreign overlay, not changing'; return 1; }
  umount "$PNS"
  /etc/init.d/pns restart > "$D/restore.log" 2>&1 || return 1
 fi
 [ ! -f "$D/active" ] || rm "$D/active"
 echo STOPPED > "$D/managed.status"
}
start(){
 [ "$(micocfg_model)" = S12 ] || return 1
 [ "$(uci -c /usr/share/mico -q get version.version.ROM)" = 1.74.18 ] || return 1
 [ "$(cat /sys/class/ubi/ubi0/mtd_num)" = 4 ] || return 1
 hash_is /usr/bin/mipns-xiaomi 6cd03f9c1a2f517677898015e49e58b7 || return 1
 hash_is "$P/probe.so" 77b9aeff85c84fa88f34298f4d6274dd || return 1
 hash_is "$P/capture" 869705e7bfcddb90e338bc71d883ffcb || return 1
 if owned; then
  pid=$(pidof mipns-xiaomi); grep -q "$D/probe.so" "/proc/$pid/maps"; return
 fi
 mounted "$PNS" && return 1
 hash_is "$PNS" 9ab0822491964be0a8fc526b9e76bf60 || return 1
 # The route is private configuration and must never be printed.
 config=${CONFIG_FILE:-/data/native_first.env}
 [ -f "$config" ] || return 1
 . "$config"
 curl -fsS -m 3 "${SERVER%/}/" >/dev/null 2>&1 || { echo 'ASR bridge unavailable'; return 1; }
 umask 077; mkdir -p "$D"; chmod 700 "$D"
 [ ! -e "$D/active" ] || return 1
 cp "$P/probe.so" "$D/probe.so.new"; chmod 700 "$D/probe.so.new"; mv "$D/probe.so.new" "$D/probe.so"
 awk -v tag="$TAG" -v so="$D/probe.so" '
 /^_start_mipns_xiaomi\(\)/ {inside=1}
 {print}
 inside && /^[[:space:]]*procd_open_instance[[:space:]]*$/ {
  print "    # " tag; print "    procd_set_param env LD_PRELOAD=" so; inside=0; count++
 }
 END {if(count!=1)exit 1}
 ' "$PNS" > "$D/pns"
 sh -n "$D/pns"; chmod 700 "$D/pns"
 echo PREPARING > "$D/managed.status"; touch "$D/active"
 (trap '' HUP; sleep 90; [ "$(cat "$D/managed.status" 2>/dev/null)" != PREPARING ] || sh "$P/manager.sh" stop >> "$D/watchdog.log" 2>&1) </dev/null >/dev/null 2>&1 &
 trap 'stop >> "$D/restore.log" 2>&1 || :' EXIT
 mount --bind "$D/pns" "$PNS"
 /etc/init.d/pns restart > "$D/start.log" 2>&1
 n=0
 while :; do
  pid=$(pidof mipns-xiaomi 2>/dev/null || :)
  if [ -n "$pid" ] && grep -q "$D/probe.so" "/proc/$pid/maps" 2>/dev/null; then break; fi
  n=$((n+1)); [ "$n" -lt 25 ] || return 1; sleep 1
 done
 echo READY > "$D/managed.status"
 trap - EXIT
}
case "${1:-}" in
 start) start;;
 stop) stop;;
 status) owned && [ "$(cat "$D/managed.status" 2>/dev/null)" = READY ] && echo READY;;
 *) exit 2;;
esac
