/* gunzip.c - Alpine Package Keeper (APK)
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
#include <malloc.h>
#include <zlib.h>

#include "apk_defines.h"
#include "apk_io.h"

struct apk_gzip_istream {
	struct apk_istream is;
	struct apk_istream *zis;
	z_stream zs;

	apk_multipart_cb cb;
	void *cbctx;
	void *cbprev;
	apk_blob_t cbarg;
};

static void gzi_get_meta(struct apk_istream *is, struct apk_file_meta *meta)
{
	struct apk_gzip_istream *gis = container_of(is, struct apk_gzip_istream, is);
	apk_istream_get_meta(gis->zis, meta);
}

static int gzi_boundary_change(struct apk_gzip_istream *gis)
{
	int r;

	r = gis->cb(gis->cbctx, gis->is.err ? APK_MPART_END : APK_MPART_BOUNDARY, gis->cbarg);
	if (r > 0) r = -ECANCELED;
	if (r != 0) gis->is.err = r;
	return r;
}

static ssize_t gzi_read(struct apk_istream *is, void *ptr, size_t size)
{
	struct apk_gzip_istream *gis = container_of(is, struct apk_gzip_istream, is);
	int r;

	gis->zs.avail_out = size;
	gis->zs.next_out  = (Bytef*)ptr;

	while (gis->zs.avail_out != 0 && gis->is.err == 0) {
		if (!APK_BLOB_IS_NULL(gis->cbarg)) {
			if (gzi_boundary_change(gis))
				goto ret;
			gis->cbarg = APK_BLOB_NULL;
		}
		if (gis->zs.avail_in == 0) {
			apk_blob_t blob;

			if (gis->cb != NULL && gis->cbprev != NULL &&
			    (Bytef*)gis->cbprev < gis->zs.next_in) {
				gis->cb(gis->cbctx, APK_MPART_DATA,
					APK_BLOB_PTR_LEN(gis->cbprev,
					gis->zs.next_in - (Bytef*)gis->cbprev));
			}
			blob = apk_istream_get_all(gis->zis);
			gis->cbprev = blob.ptr;
			gis->zs.avail_in = blob.len;
			gis->zs.next_in = (Bytef*) gis->cbprev;
			if (blob.len < 0) {
				gis->is.err = blob.len;
				goto ret;
			} else if (gis->zs.avail_in == 0) {
				gis->is.err = 1;
				gis->cbarg = APK_BLOB_NULL;
				gzi_boundary_change(gis);
				goto ret;
			}
		}

		r = inflate(&gis->zs, Z_NO_FLUSH);
		switch (r) {
		case Z_STREAM_END:
			/* Digest the inflated bytes */
			if (gis->zis->err && gis->zs.avail_in == 0)
				gis->is.err = gis->zis->err;
			if (gis->cb != NULL) {
				gis->cbarg = APK_BLOB_PTR_LEN(gis->cbprev, gis->zs.next_in - (Bytef*)gis->cbprev); 
				gis->cbprev = gis->zs.next_in;
			}
			/* If we hit end of the bitstream (not end
			 * of just this gzip), we need to do the
			 * callback here, as we won't be called again.
			 * For boundaries it should be postponed to not
			 * be called until next gzip read is started. */
			if (gis->is.err) {
				gzi_boundary_change(gis);
				goto ret;
			}
			inflateEnd(&gis->zs);
			if (inflateInit2(&gis->zs, 15+32) != Z_OK)
				return -ENOMEM;
			if (gis->cb) goto ret;
			break;
		case Z_OK:
			break;
		default:
			gis->is.err = -EIO;
			break;
		}
	}

ret:
	return size - gis->zs.avail_out;
}

static void gzi_close(struct apk_istream *is)
{
	struct apk_gzip_istream *gis = container_of(is, struct apk_gzip_istream, is);

	inflateEnd(&gis->zs);
	apk_istream_close(gis->zis);
	free(gis);
}

static const struct apk_istream_ops gunzip_istream_ops = {
	.get_meta = gzi_get_meta,
	.read = gzi_read,
	.close = gzi_close,
};

struct apk_istream *apk_istream_gunzip_mpart(struct apk_istream *is, apk_multipart_cb cb, void *ctx)
{
	struct apk_gzip_istream *gis;

	if (IS_ERR_OR_NULL(is)) return (struct apk_istream*)ERR_CAST(is);

	gis = (struct apk_gzip_istream*)malloc(sizeof(*gis) + apk_io_bufsize);
	if (!gis) goto err;

	*gis = (struct apk_gzip_istream) {
		.is.ops = &gunzip_istream_ops,
		.is.buf = (uint8_t*)(gis + 1),
		.is.buf_size = apk_io_bufsize,
		.zis = is,
		.cb = cb,
		.cbctx = ctx,
	};

	if (inflateInit2(&gis->zs, 15+32) != Z_OK) {
		free(gis);
		goto err;
	}

	return &gis->is;
err:
	apk_istream_close(is);
	return (struct apk_istream*)ERR_PTR(-ENOMEM);
}

struct apk_gzip_ostream {
	struct apk_ostream os;
	struct apk_ostream *output;
	z_stream zs;
};

static ssize_t gzo_write(struct apk_ostream *os, const void *ptr, size_t size)
{
	struct apk_gzip_ostream *gos = container_of(os, struct apk_gzip_ostream, os);
	unsigned char buffer[1024];
	ssize_t have, r;

	gos->zs.avail_in = size;
	gos->zs.next_in = (Bytef*) ptr;
	while (gos->zs.avail_in) {
		gos->zs.avail_out = sizeof(buffer);
		gos->zs.next_out = buffer;
		r = deflate(&gos->zs, Z_NO_FLUSH);
		if (r == Z_STREAM_ERROR)
			return -EIO;
		have = sizeof(buffer) - gos->zs.avail_out;
		if (have != 0) {
			r = apk_ostream_write(gos->output, buffer, have);
			if (r != have)
				return -EIO;
		}
	}

	return size;
}

static int gzo_close(struct apk_ostream *os)
{
	struct apk_gzip_ostream *gos = container_of(os, struct apk_gzip_ostream, os);
	unsigned char buffer[1024];
	ssize_t have;
	int r, rc = 0;

	do {
		gos->zs.avail_out = sizeof(buffer);
		gos->zs.next_out = buffer;
		r = deflate(&gos->zs, Z_FINISH);
		have = sizeof(buffer) - gos->zs.avail_out;
		if (apk_ostream_write(gos->output, buffer, have) != have)
			rc = -EIO;
	} while (r == Z_OK);
	r = apk_ostream_close(gos->output);
	if (r != 0)
		rc = r;

	deflateEnd(&gos->zs);
	free(gos);

	return rc;
}

static const struct apk_ostream_ops gzip_ostream_ops = {
	.write = gzo_write,
	.close = gzo_close,
};

struct apk_ostream *apk_ostream_gzip(struct apk_ostream *output)
{
	struct apk_gzip_ostream *gos;

	if (IS_ERR_OR_NULL(output)) return (struct apk_ostream*)ERR_CAST(output);

	gos = (struct apk_gzip_ostream*)malloc(sizeof(struct apk_gzip_ostream));
	if (gos == NULL) goto err;

	*gos = (struct apk_gzip_ostream) {
		.os.ops = &gzip_ostream_ops,
		.output = output,
	};

	if (deflateInit2(&gos->zs, 9, Z_DEFLATED, 15 | 16, 8,
			 Z_DEFAULT_STRATEGY) != Z_OK) {
		free(gos);
		goto err;
	}

	return &gos->os;
err:
	apk_ostream_close(output);
	return (struct apk_ostream*)ERR_PTR(-ENOMEM);
}

