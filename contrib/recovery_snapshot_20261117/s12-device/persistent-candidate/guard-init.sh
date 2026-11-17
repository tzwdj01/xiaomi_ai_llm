#!/bin/sh
P=/data/s12-followup
case "$(cat "$P/install.state" 2>/dev/null)" in
 PREPARED|APPLYING)
  sh "$P/recover.sh" >> "$P/recovery.log" 2>&1 || exit 1
  exec sh "$P/original-init.sh" ;;
 COMMITTED) exec sh "$P/new-init.sh" ;;
 *) exec sh "$P/original-init.sh" ;;
esac
