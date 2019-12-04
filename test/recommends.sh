#!/bin/sh -e

# desc: test if recommends add works

APK=../src/apk
ROOT=/tmp/apk-test/recommends
PWD=$(pwd)

cleanup() {
	echo "Cleanup"
	rm -rf $PWD/repo-recommends/arch1
	rm -rf $PWD/repo-recommends/*tar.gz
	rm -rf $PWD/repo-recommends/.PKGINFO
	rm -rf $ROOT
}


mkdir /tmp/apk-test &> /dev/null || true
mkdir /tmp/apk-test/recommends &> /dev/null || true

# Setup repo-recommends for testing
cleanup
(cd $PWD/repo-recommends &&
	cp PKGINFO_A .PKGINFO &&
	(cd test-a &&
		tar --xattrs -f - -c * | abuild-tar --hash | gzip -9 > ../test-a-data.tar.gz &&
		du -b . | tail -n 1 | awk '{print "size = "$1}' >> ../.PKGINFO
	) &&
	tar -c .PKGINFO | abuild-tar --cut | gzip -9 > test-a-control.tar.gz &&
	cat test-a-control.tar.gz test-a-data.tar.gz > test-a-1.0.apk
)
(cd $PWD/repo-recommends &&
	cp PKGINFO_B .PKGINFO &&
	(cd test-b &&
		tar --xattrs -f - -c * | abuild-tar --hash | gzip -9 > ../test-b-data.tar.gz &&
		du -b . | tail -n 1 | awk '{print "size = "$1}' >> ../.PKGINFO
	) &&
	tar -c .PKGINFO | abuild-tar --cut | gzip -9 > test-b-control.tar.gz &&
	cat test-b-control.tar.gz test-b-data.tar.gz > test-b-1.0.apk
)

# Creating index
(cd $PWD/repo-recommends &&
	mkdir arch1 &&
	cp test-a-1.0.apk arch1/test-a-1.0.apk &&
	cp test-b-1.0.apk arch1/test-b-1.0.apk &&
	(cd arch1 &&
		../../$APK index -o APKINDEX.tar.gz *.apk
	)
)

# Installing package
$APK add --allow-untrusted --arch arch1 --root $ROOT --initdb --repository $PWD/repo-recommends test-b

# Verify result
if [ -f "$ROOT/test-a" ]; then
	if [ "$ROOT/test-a" = "hello from test-a-1.0" ]; then
		echo "FAIL: could not fint test-a-1.0"
		exit 1
	fi
else
		echo "EXIT 2: $ROOT/test-a"
		exit 2
fi

if [ -f "$ROOT/test-b" ]; then
	if [ "$ROOT/test-b" = "hello from test-b-1.0" ]; then
		echo "FAIL: could not fint test-b-1.0"
		exit 3
	fi
else
		echo "EXIT 4: $ROOT/test-b"
		exit 4
fi

cleanup

echo "OK: recommends tests passed"
