#!/bin/sh -e

# desc: test trigger script

$APK add --root $ROOT --initdb -U --repository $PWD/repo1 \
	--repository $SYSREPO test-e

test ! -f "$ROOT"/triggered

$APK add --root $ROOT --initdb -U --repository $PWD/repo1 \
	--repository $SYSREPO test-f

test -f "$ROOT"/file-trigger
test -f "$ROOT"/triggered
