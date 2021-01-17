/* apk_print.h - Alpine Package Keeper (APK)
 *
 * Copyright (C) 2005-2008 Natanael Copa <n@tanael.org>
 * Copyright (C) 2008-2011 Timo Teräs <timo.teras@iki.fi>
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef APK_PRINT_H
#define APK_PRINT_H

#include "apk_blob.h"

const char *apk_error_str(int error);
const char *apk_get_human_size(unsigned int byte_size, off_t size, off_t *dest);

struct apk_url_print {
	const char *url;
	const char *pwmask;
	const char *url_or_host;
	size_t len_before_pw;
};

void apk_url_parse(struct apk_url_print *, const char *);

#define URL_FMT			"%.*s%s%s"
#define URL_PRINTF(u)		u.len_before_pw, u.url, u.pwmask, u.url_or_host

struct apk_out {
	int verbosity;
	unsigned int width, last_change;
	FILE *out, *err;
};

static inline int apk_out_verbosity(struct apk_out *out) { return out->verbosity; }

#define apk_err(out, args...)	do { apk_out_fmt(out, "ERROR: ", args); } while (0)
#define apk_out(out, args...)	do { apk_out_fmt(out, NULL, args); } while (0)
#define apk_warn(out, args...)	do { if (apk_out_verbosity(out) >= 0) { apk_out_fmt(out, "WARNING: ", args); } } while (0)
#define apk_msg(out, args...)	do { if (apk_out_verbosity(out) >= 1) { apk_out_fmt(out, NULL, args); } } while (0)
#define apk_dbg(out, args...)	do { if (apk_out_verbosity(out) >= 2) { apk_out_fmt(out, NULL, args); } } while (0)
#define apk_dbg2(out, args...)	do { if (apk_out_verbosity(out) >= 3) { apk_out_fmt(out, NULL, args); } } while (0)

void apk_out_reset(struct apk_out *);
void apk_out_fmt(struct apk_out *, const char *prefix, const char *format, ...);

struct apk_progress {
	struct apk_out *out;
	int fd, last_bar, last_percent;
	unsigned int last_out_change;
	size_t last_done;
	const char *progress_char;
};

void apk_print_progress(struct apk_progress *p, size_t done, size_t total);

struct apk_indent {
	struct apk_out *out;
	int x, indent;
};

int  apk_print_indented(struct apk_indent *i, apk_blob_t blob);
void apk_print_indented_words(struct apk_indent *i, const char *text);
void apk_print_indented_fmt(struct apk_indent *i, const char *fmt, ...);

#endif
