/* verify.c - Alpine Package Keeper (APK)
 *
 * Copyright (C) 2008-2011 Timo Teräs <timo.teras@iki.fi>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation. See http://www.gnu.org/ for details.
 */

#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include "apk_applet.h"
#include "apk_database.h"
#include "apk_print.h"

static int verify_main(void *ctx, struct apk_database *db, struct apk_string_array *args)
{
	struct apk_sign_ctx sctx;
	char **parg;
	int r, ok, rc = 0;

	foreach_array_item(parg, args) {
		apk_sign_ctx_init(&sctx, APK_SIGN_VERIFY, NULL, db->keys_fd);
		r = apk_tar_parse(
			apk_istream_gunzip_mpart(apk_istream_from_file(AT_FDCWD, *parg),
						 apk_sign_ctx_mpart_cb, &sctx),
			apk_sign_ctx_verify_tar, &sctx, &db->id_cache);
		ok = sctx.control_verified && sctx.data_verified;
		if (apk_verbosity >= 1)
			apk_message("%s: %d - %s", *parg, r,
				r < 0 ? apk_error_str(r) :
				ok ? "OK" :
				!sctx.control_verified ? "UNTRUSTED" : "FAILED");
		else if (!ok)
			printf("%s\n", *parg);
		if (!ok)
			rc++;

		apk_sign_ctx_free(&sctx);
	}

	return rc;
}

static struct apk_applet apk_verify = {
	.name = "verify",
	.arguments = "FILE...",
	.help = "Verify package integrity and signature",
	.open_flags = APK_OPENF_READ | APK_OPENF_NO_STATE,
	.forced_flags = APK_ALLOW_UNTRUSTED,
	.command_groups = APK_COMMAND_GROUP_REPO,
	.main = verify_main,
};

APK_DEFINE_APPLET(apk_verify);

