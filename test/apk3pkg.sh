#!/bin/sh -e

# desc: test if apk3 packages can contain files starting with a dot

mkdir -p "$ROOT"
$APK add --root "$ROOT" --initdb --force-non-repository \
	--allow-untrusted --no-interactive --quiet apk3.apk

[ -f "$ROOT"/.dotfile ]
