#!/bin/sh
# Exercises the client's LED guard with a temporary regular file and mocked
# native LED commands. Does not access the device's real LED sysfs.
set -eu
CLIENT=${1:?usage: test_led_guard.sh /path/to/native_first_client.sh}
D=$(mktemp -d /tmp/s12-led-unit.XXXXXX)
trap 'rm -f "$D/functions" "$D/functions.tmp" "$D/led" "$D/calls"; rmdir "$D"' EXIT HUP INT TERM
awk '
 /^led_set_all\(\) \{/ {on=1}
 /^led_write_one_sync\(\) \{/ {on=1}
 /^led_native_shut_all\(\) \{/ {on=1}
 on {print}
 on && /^\}/ {on=0}
' "$CLIENT" > "$D/functions.tmp"
sed 's@/bin/show_led@test_show_led@g' "$D/functions.tmp" > "$D/functions"
. "$D/functions"
ubus() { echo ubus >> "$D/calls"; }
test_show_led() { echo show_led >> "$D/calls"; }
LED_RGB="$D/led"
LED_IDS="0 1"
printf 'unchanged\n' > "$LED_RGB"
LED_FEEDBACK_ENABLED=0
led_set_all 123
led_write_one_sync 5 456
led_native_shut_all
[ "$(cat "$LED_RGB")" = unchanged ]
[ ! -e "$D/calls" ]
LED_FEEDBACK_ENABLED=1
led_set_all 123
[ "$(cat "$LED_RGB")" = '1 123' ]
led_write_one_sync 5 456
[ "$(cat "$LED_RGB")" = '5 456' ]
led_native_shut_all
[ "$(wc -l < "$D/calls")" -eq 2 ]
echo S12_LED_GUARD_TEST_OK
