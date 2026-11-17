#!/bin/sh
set -eu
PATH=/usr/sbin:/usr/bin:/sbin:/bin; export PATH
P=/data/s12-followup
case "$(cat "$P/install.state" 2>/dev/null)" in PREPARED|APPLYING) ;; *) exit 0;; esac
mkdir /tmp/s12-install-recovery.lock 2>/dev/null || exit 1
trap 'rmdir /tmp/s12-install-recovery.lock 2>/dev/null || :' EXIT
[ "$(md5sum "$P/original-env" | cut -d ' ' -f 1)" = "$(cat "$P/env-before.md5")" ]
[ "$(md5sum "$P/candidate-env" | cut -d ' ' -f 1)" = "$(cat "$P/env-after.md5")" ]
[ "$(md5sum "$P/original-init.sh" | cut -d ' ' -f 1)" = d9f74cbde913afdcd00462f15573aa42 ]
current=$(md5sum /data/native_first.env | cut -d ' ' -f 1)
[ "$current" = "$(cat "$P/env-before.md5")" ] || [ "$current" = "$(cat "$P/env-after.md5")" ]
if [ -s /tmp/native_first_client.pid ]; then sh "$P/native_first_client.sh" stop >> "$P/recovery.log" 2>&1 || :; fi
if [ -d /tmp/s12-pcm-probe ]; then sh "$P/manager.sh" stop >> "$P/recovery.log" 2>&1 || :; fi
cp "$P/original-env" /data/native_first.env.recover; chmod 600 /data/native_first.env.recover; mv /data/native_first.env.recover /data/native_first.env
cp -p "$P/original-init.sh" /data/init.sh.recover; mv /data/init.sh.recover /data/init.sh
[ ! -e "$P/enabled" ] || rm "$P/enabled"
echo RECOVERED > "$P/install.state.new"; mv "$P/install.state.new" "$P/install.state"
sync
echo RECOVERED
