#!/bin/sh
# Root-owned; sudoers permits this fixed, argument-free RAM operation only.
set -eu
[ "$#" -eq 0 ] || exit 2
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
/usr/bin/timeout --kill-after=1s 10s /usr/bin/journalctl --sync
/usr/bin/timeout --kill-after=1s 10s /usr/bin/journalctl --rotate
