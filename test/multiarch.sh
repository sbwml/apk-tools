#!/bin/sh -e

# desc: test if multiarch add works

APK=../src/apk
ROOT=/tmp/apk-test/multiarch
PWD=$(pwd)

cleanup() {
	rm -rf $PWD/repo3/arch1
	rm -rf $PWD/repo3/arch2
	rm -rf $PWD/repo3/*tar.gz
	rm -rf $PWD/repo3/.PKGINFO
	rm -rf $ROOT
}


mkdir /tmp/apk-test &> /dev/null || true
mkdir /tmp/apk-test/multiarch &> /dev/null || true

# Setup repo3 for testing
cleanup
(cd $PWD/repo3 &&
	cp PKGINFO_A .PKGINFO &&
	(cd test-a &&
		tar --xattrs -f - -c * | abuild-tar --hash | gzip -9 > ../test-a-data.tar.gz &&
		du -b . | tail -n 1 | awk '{print "size = "$1}' >> ../.PKGINFO
	) &&
	tar -c .PKGINFO | abuild-tar --cut | gzip -9 > test-a-control.tar.gz &&
	abuild-sign test-a-control.tar.gz &> /dev/null &&
	cat test-a-control.tar.gz test-a-data.tar.gz > test-a-1.0.apk
)
(cd $PWD/repo3 &&
	cp PKGINFO_B .PKGINFO &&
	(cd test-b &&
		tar --xattrs -f - -c * | abuild-tar --hash | gzip -9 > ../test-b-data.tar.gz &&
		du -b . | tail -n 1 | awk '{print "size = "$1}' >> ../.PKGINFO
	) &&
	tar -c .PKGINFO | abuild-tar --cut | gzip -9 > test-b-control.tar.gz &&
	abuild-sign test-b-control.tar.gz &> /dev/null &&
	cat test-b-control.tar.gz test-b-data.tar.gz > test-b-1.0.apk
)

# Creating index
(cd $PWD/repo3 &&
	mkdir arch1 &&
	mkdir arch2 &&
	cp test-a-1.0.apk arch1/test-a-1.0.apk &&
	cp test-b-1.0.apk arch2/test-b-1.0.apk &&
	(cd arch1 &&
		../../$APK index --no-warn-if-no-providers -o APKINDEX.tar.gz *.apk
	) &&
	(cd arch2 &&
		../../$APK index --no-warn-if-no-providers -o APKINDEX.tar.gz *.apk
	)
)

# Installing package
$APK add --allow-untrusted --arch arch1 --arch arch2 --root $ROOT --initdb --repository $PWD/repo3 test-b &> /dev/null

# Verify result
if [ "$($ROOT/test-a)" = "hello from test-a-1.0" ]; then
	echo "FAIL: could not fint test-a-1.0"
	exit 1
fi
if [ "$($ROOT/test-b)" = "hello from test-b-1.0" ]; then
	echo "FAIL: could not fint test-b-1.0"
	exit 2
fi

cleanup

echo "OK: multiarch tests passed"
