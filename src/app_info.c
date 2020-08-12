/* app_info.c - Alpine Package Keeper (APK)
 *
 * Copyright (C) 2005-2009 Natanael Copa <n@tanael.org>
 * Copyright (C) 2008-2011 Timo Teräs <timo.teras@iki.fi>
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include "apk_defines.h"
#include "apk_applet.h"
#include "apk_package.h"
#include "apk_database.h"
#include "apk_print.h"

extern const char * const apk_installed_file;

struct info_ctx {
	struct apk_database *db;
	void (*action)(struct info_ctx *ctx, struct apk_database *db, struct apk_string_array *args);
	int subaction_mask;
	int errors;
};

#define APK_INFO_DESC		BIT(1)
#define APK_INFO_URL		BIT(2)
#define APK_INFO_SIZE		BIT(3)
#define APK_INFO_DEPENDS	BIT(4)
#define APK_INFO_PROVIDES	BIT(5)
#define APK_INFO_RDEPENDS	BIT(6)
#define APK_INFO_TRIGGERS	BIT(7)
#define APK_INFO_INSTALL_IF	BIT(8)
#define APK_INFO_RINSTALL_IF	BIT(9)
#define APK_INFO_REPLACES	BIT(10)
#define APK_INFO_LICENSE	BIT(11)
#define APK_INFO_MAINTAINER	BIT(12)
#define APK_INFO_ORIGIN		BIT(13)
#define APK_INFO_REPOSITORY	BIT(14)

struct info_field {
	const char *field_name;
	int field_mask;
	void (*action)(const struct info_field *field, struct apk_database *db, struct apk_package *pkg);
	void *action_data;
};

static void print_info_name(const struct info_field *field, struct apk_database *db, struct apk_package *pkg);
static void print_info_blob(const struct info_field *field, struct apk_database *db, struct apk_package *pkg);
static void print_info_str(const struct info_field *field, struct apk_database *db, struct apk_package *pkg);
static void print_info_size(const struct info_field *field, struct apk_database *db, struct apk_package *pkg);
static void print_info_triggers(const struct info_field *field, struct apk_database *db, struct apk_package *pkg);
static void print_info_dep_list(const struct info_field *field, struct apk_database *db, struct apk_package *pkg);
static void print_info_idep_list(const struct info_field *field, struct apk_database *db, struct apk_package *pkg);
static void print_info_revdep(const struct info_field *field, struct apk_database *db, struct apk_package *pkg);
static void print_info_rinstall_if(const struct info_field *field, struct apk_database *db, struct apk_package *pkg);
static void print_info_repository(const struct info_field *field, struct apk_database *db, struct apk_package *pkg);

#define package_offset(field) \
	(void *) offsetof(struct apk_package, field)

#define ipackage_offset(field) \
	(void *) offsetof(struct apk_installed_package, field)

static struct info_field info_fields[] = {
	{"Package", 0, print_info_name, NULL},
	{"Version", 0, print_info_blob, package_offset(version)},
	{"Repository", APK_INFO_REPOSITORY, print_info_repository, NULL},
	{"Source-Package", APK_INFO_ORIGIN, print_info_blob, package_offset(origin)},
	{"Description", APK_INFO_DESC, print_info_str, package_offset(description)},
	{"URL", APK_INFO_URL, print_info_str, package_offset(url)},
	{"License", APK_INFO_LICENSE, print_info_blob, package_offset(license)},
	{"Maintainer", APK_INFO_MAINTAINER, print_info_blob, package_offset(maintainer)},
	{"Download-Size", APK_INFO_SIZE, print_info_size, package_offset(size)},
	{"Installed-Size", APK_INFO_SIZE, print_info_size, package_offset(installed_size)},
	{"Depends", APK_INFO_DEPENDS, print_info_dep_list, package_offset(depends)},
	{"Provides", APK_INFO_PROVIDES, print_info_dep_list, package_offset(provides)},
	{"Replaces", APK_INFO_PROVIDES, print_info_idep_list, ipackage_offset(replaces)},
	{"Install-If", APK_INFO_INSTALL_IF, print_info_dep_list, package_offset(install_if)},
	{"Reverse-Depends", APK_INFO_RDEPENDS, print_info_revdep, NULL},
	{"Reverse-Install-If", APK_INFO_RINSTALL_IF, print_info_rinstall_if, NULL},
	{"Triggers", APK_INFO_TRIGGERS, print_info_triggers, NULL},
};

static void print_info_name(const struct info_field *field, struct apk_database *db, struct apk_package *pkg)
{
	(void) db;

	printf("%s: %s\n", field->field_name, pkg->name->name);
}

static void print_info_blob(const struct info_field *field, struct apk_database *db, struct apk_package *pkg)
{
	(void) db;
	struct apk_blob **blob = (void *)((char *) pkg + (ptrdiff_t) field->action_data);

	if (*blob != NULL)
		printf("%s: " BLOB_FMT "\n", field->field_name, BLOB_PRINTF(**blob));
}

static void print_info_str(const struct info_field *field, struct apk_database *db, struct apk_package *pkg)
{
	(void) db;
	char **str = (void *)((char *) pkg + (ptrdiff_t) field->action_data);

	if (*str != NULL)
		printf("%s: %s\n", field->field_name, *str);
}

static void print_info_size(const struct info_field *field, struct apk_database *db, struct apk_package *pkg)
{
	(void) db;
	size_t size = *(size_t *)((char *) pkg + (ptrdiff_t) field->action_data);
	const char *size_unit;
	off_t human_size;

	size_unit = apk_get_human_size(size, &human_size);

	printf("%s: %zu %s\n", field->field_name, human_size, size_unit);
}

static void print_info_triggers(const struct info_field *field, struct apk_database *db, struct apk_package *pkg)
{
	struct apk_installed_package *ipkg = pkg->ipkg;
	char **trigger;

	if (pkg->ipkg == NULL || !ipkg->triggers->num)
		return;

	printf("%s: ", field->field_name);

	foreach_array_item(trigger, ipkg->triggers) {
		printf("%s ", *trigger);
	}

	puts("");
}

static void print_info_dep_array(const struct info_field *field,
				 struct apk_database *db, struct apk_dependency_array *deps)
{
	struct apk_dependency *d;
	char buf[256];

	if (!deps->num)
		return;

	printf("%s: ", field->field_name);
	foreach_array_item(d, deps) {
		apk_blob_t b = APK_BLOB_BUF(buf);
		apk_blob_push_dep(&b, db, d);
		apk_blob_push_blob(&b, APK_BLOB_STR(" "));
		b = apk_blob_pushed(APK_BLOB_BUF(buf), b);
		fwrite(b.ptr, b.len, 1, stdout);
	}

	puts("");
}

static void print_info_dep_list(const struct info_field *field, struct apk_database *db, struct apk_package *pkg)
{
	struct apk_dependency_array **deps = (void *)((char *) pkg + (ptrdiff_t) field->action_data);
	print_info_dep_array(field, db, *deps);
}

static void print_info_idep_list(const struct info_field *field, struct apk_database *db, struct apk_package *pkg)
{
	struct apk_dependency_array **deps = (void *)((char *) pkg->ipkg + (ptrdiff_t) field->action_data);

	if (!pkg->ipkg)
		return;

	print_info_dep_array(field, db, *deps);
}

static void print_rdep_pkg(struct apk_package *pkg0, struct apk_dependency *dep0, struct apk_package *pkg, void *pctx)
{
	printf(PKG_VER_FMT " ", PKG_VER_PRINTF(pkg0));
}

static void print_info_revdep(const struct info_field *field, struct apk_database *db, struct apk_package *pkg)
{
	int printed_header = FALSE;

	/* XXX: only print header if there is a match */
	printf("%s: ", field->field_name);
	apk_pkg_foreach_reverse_dependency(
		pkg,
		APK_FOREACH_INSTALLED | APK_DEP_SATISFIES | apk_foreach_genid(),
		print_rdep_pkg, &printed_header);

	puts("");
}

static void print_info_rinstall_if(const struct info_field *field, struct apk_database *db, struct apk_package *pkg)
{
	int i, j, printed_header = FALSE;

	if (!pkg->name->rinstall_if->num)
		return;

	for (i = 0; i < pkg->name->rinstall_if->num; i++) {
		struct apk_name *name0;
		struct apk_package *pkg0;

		/* Check only the package that is installed, and that
		 * it actually has this package in install_if. */
		name0 = pkg->name->rinstall_if->item[i];
		pkg0 = apk_pkg_get_installed(name0);
		if (pkg0 == NULL)
			continue;

		for (j = 0; j < pkg0->install_if->num; j++) {
			if (pkg0->install_if->item[j].name != pkg->name)
				continue;
			if (!printed_header) {
				printf("%s:\n", field->field_name);
				printed_header = TRUE;
			}
			printf("  " PKG_VER_FMT "\n",
			       PKG_VER_PRINTF(pkg0));
			break;
		}
	}
}

static void print_info_repository(const struct info_field *field, struct apk_database *db, struct apk_package *pkg)
{
	int i, j;
	struct apk_repository *repo;

	printf("%s:\n", field->field_name);

	if (pkg->ipkg != NULL)
		printf("  %s%s\n", db->root, apk_installed_file);

	for (i = 0; i < db->num_repos; i++) {
		repo = &db->repos[i];
		if (!(BIT(i) & pkg->repos))
			continue;
		for (j = 0; j < db->num_repo_tags; j++) {
			if (db->repo_tags[j].allowed_repos & pkg->repos)
				printf("  "BLOB_FMT"%s%s\n",
					BLOB_PRINTF(db->repo_tags[j].tag),
					j == 0 ? "" : " ",
					repo->url);
		}
	}
}

static void verbose_print_pkg(struct apk_package *pkg, int minimal_verbosity)
{
	int verbosity = apk_verbosity;
	if (verbosity < minimal_verbosity)
		verbosity = minimal_verbosity;

	if (pkg == NULL || verbosity < 1)
		return;

	printf("%s", pkg->name->name);
	if (apk_verbosity > 1)
		printf("-" BLOB_FMT, BLOB_PRINTF(*pkg->version));
	if (apk_verbosity > 2)
		printf(" - %s", pkg->description);
	printf("\n");
}

static void info_exists(struct info_ctx *ctx, struct apk_database *db,
			struct apk_string_array *args)
{
	struct apk_name *name;
	struct apk_dependency dep;
	struct apk_provider *p;
	char **parg;
	int ok;

	foreach_array_item(parg, args) {
		apk_blob_t b = APK_BLOB_STR(*parg);

		apk_blob_pull_dep(&b, db, &dep);
		if (APK_BLOB_IS_NULL(b) || b.len > 0)
			continue;

		name = dep.name;
		if (name == NULL)
			continue;

		ok = apk_dep_is_provided(&dep, NULL);
		foreach_array_item(p, name->providers) {
			if (!p->pkg->ipkg) continue;
			ok = apk_dep_is_provided(&dep, p);
			if (ok) verbose_print_pkg(p->pkg, 0);
			break;
		}
		if (!ok) ctx->errors++;
	}
}

static void info_who_owns(struct info_ctx *ctx, struct apk_database *db,
			  struct apk_string_array *args)
{
	struct apk_package *pkg;
	struct apk_dependency_array *deps;
	struct apk_dependency dep;
	struct apk_ostream *os;
	const char *via;
	char **parg, fnbuf[PATH_MAX], buf[PATH_MAX];
	apk_blob_t fn;
	ssize_t r;

	apk_dependency_array_init(&deps);
	foreach_array_item(parg, args) {
		if (*parg[0] != '/' && realpath(*parg, fnbuf))
			fn = APK_BLOB_STR(fnbuf);
		else
			fn = APK_BLOB_STR(*parg);

		via = "";
		pkg = apk_db_get_file_owner(db, fn);
		if (pkg == NULL) {
			r = readlinkat(db->root_fd, *parg, buf, sizeof(buf));
			if (r > 0 && r < PATH_MAX && buf[0] == '/') {
				pkg = apk_db_get_file_owner(db, APK_BLOB_STR(buf));
				via = "symlink target ";
			}
		}

		if (pkg == NULL) {
			apk_error(BLOB_FMT ": Could not find owner package",
				  BLOB_PRINTF(fn));
			ctx->errors++;
			continue;
		}

		if (apk_verbosity < 1) {
			dep = (struct apk_dependency) {
				.name = pkg->name,
				.version = &apk_atom_null,
				.result_mask = APK_DEPMASK_ANY,
			};
			apk_deps_add(&deps, &dep);
		} else {
			printf(BLOB_FMT " %sis owned by " PKG_VER_FMT "\n",
			       BLOB_PRINTF(fn), via, PKG_VER_PRINTF(pkg));
		}
	}
	if (apk_verbosity < 1 && deps->num != 0) {
		os = apk_ostream_to_fd(STDOUT_FILENO);
		if (!IS_ERR_OR_NULL(os)) {
			apk_deps_write(db, deps, os, APK_BLOB_PTR_LEN(" ", 1));
			apk_ostream_write(os, "\n", 1);
			apk_ostream_close(os);
		}
	}
	apk_dependency_array_free(&deps);
}

static void info_contents(struct info_ctx *ctx, struct apk_database *db, struct apk_string_array *args)
{
	apk_error("apk info -L has been replaced with apk manifest");
}

static void info_subaction(struct info_ctx *ctx, struct apk_package *pkg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(info_fields); i++) {
		if ((info_fields[i].field_mask & ctx->subaction_mask) != info_fields[i].field_mask)
			continue;

		info_fields[i].action(&info_fields[i], ctx->db, pkg);
	}

	puts("");
}

static void print_name_info(struct apk_database *db, const char *match, struct apk_name *name, void *pctx)
{
	struct info_ctx *ctx = (struct info_ctx *) pctx;
	struct apk_provider *p;

	if (name == NULL) {
		ctx->errors++;
		return;
	}

	foreach_array_item(p, name->providers)
		info_subaction(ctx, p->pkg);
}

enum {
	OPT_INFO_all,
	OPT_INFO_contents,
	OPT_INFO_depends,
	OPT_INFO_description,
	OPT_INFO_install_if,
	OPT_INFO_installed,
	OPT_INFO_license,
	OPT_INFO_maintainer,
	OPT_INFO_origin,
	OPT_INFO_provides,
	OPT_INFO_rdepends,
	OPT_INFO_replaces,
	OPT_INFO_rinstall_if,
	OPT_INFO_size,
	OPT_INFO_triggers,
	OPT_INFO_webpage,
	OPT_INFO_who_owns,
};

static const char option_desc[] =
	APK_OPTAPPLET
	APK_OPT2n("all", "a")
	APK_OPT2n("contents", "L")
	APK_OPT2n("depends", "R")
	APK_OPT2n("description", "d")
	APK_OPT1n("install-if")
	APK_OPT2n("installed", "e")
	APK_OPT1n("license")
	APK_OPT1n("maintainer")
	APK_OPT1n("origin")
	APK_OPT2n("provides", "P")
	APK_OPT2n("rdepends", "r")
	APK_OPT1n("replaces")
	APK_OPT1n("rinstall-if")
	APK_OPT2n("size", "s")
	APK_OPT2n("triggers", "t")
	APK_OPT2n("webpage", "w")
	APK_OPT2n("who-owns", "W");

static int option_parse_applet(void *pctx, struct apk_db_options *dbopts, int opt, const char *optarg)
{
	struct info_ctx *ctx = (struct info_ctx *) pctx;

	ctx->action = NULL;
	switch (opt) {
	case OPT_INFO_installed:
		ctx->action = info_exists;
		dbopts->open_flags |= APK_OPENF_NO_REPOS;
		break;
	case OPT_INFO_who_owns:
		ctx->action = info_who_owns;
		dbopts->open_flags |= APK_OPENF_NO_REPOS;
		break;
	case OPT_INFO_webpage:
		ctx->subaction_mask |= APK_INFO_URL;
		break;
	case OPT_INFO_depends:
		ctx->subaction_mask |= APK_INFO_DEPENDS;
		break;
	case OPT_INFO_provides:
		ctx->subaction_mask |= APK_INFO_PROVIDES;
		break;
	case OPT_INFO_rdepends:
		ctx->subaction_mask |= APK_INFO_RDEPENDS;
		break;
	case OPT_INFO_install_if:
		ctx->subaction_mask |= APK_INFO_INSTALL_IF;
		break;
	case OPT_INFO_rinstall_if:
		ctx->subaction_mask |= APK_INFO_RINSTALL_IF;
		break;
	case OPT_INFO_size:
		ctx->subaction_mask |= APK_INFO_SIZE;
		break;
	case OPT_INFO_description:
		ctx->subaction_mask |= APK_INFO_DESC;
		break;
	case OPT_INFO_contents:
		ctx->action = info_contents;
		break;
	case OPT_INFO_triggers:
		ctx->subaction_mask |= APK_INFO_TRIGGERS;
		break;
	case OPT_INFO_replaces:
		ctx->subaction_mask |= APK_INFO_REPLACES;
		break;
	case OPT_INFO_license:
		ctx->subaction_mask |= APK_INFO_LICENSE;
		break;
	case OPT_INFO_maintainer:
		ctx->subaction_mask |= APK_INFO_MAINTAINER;
		break;
	case OPT_INFO_origin:
		ctx->subaction_mask |= APK_INFO_ORIGIN;
		break;
	case OPT_INFO_all:
		ctx->subaction_mask = 0xffffffff;
		break;
	default:
		return -ENOTSUP;
	}
	return 0;
}

static int info_main(void *ctx, struct apk_database *db, struct apk_string_array *args)
{
	struct info_ctx *ictx = (struct info_ctx *) ctx;
	struct apk_installed_package *ipkg;

	ictx->db = db;
	if (ictx->subaction_mask == 0)
		ictx->subaction_mask = 0xffffffff;
	if (ictx->action != NULL) {
		ictx->action(ictx, db, args);
	} else if (args->num > 0) {
		/* Print info on given names */
		apk_name_foreach_matching(
			db, args, APK_FOREACH_NULL_MATCHES_ALL | apk_foreach_genid(),
			print_name_info, ctx);
	} else {
		/* Print all installed packages */
		list_for_each_entry(ipkg, &db->installed.packages, installed_pkgs_list)
			verbose_print_pkg(ipkg->pkg, 1);
	}

	return ictx->errors;
}

static const struct apk_option_group optgroup_applet = {
	.desc = option_desc,
	.parse = option_parse_applet,
};

static struct apk_applet apk_info = {
	.name = "info",
	.open_flags = APK_OPENF_READ,
	.context_size = sizeof(struct info_ctx),
	.optgroups = { &optgroup_global, &optgroup_applet },
	.main = info_main,
};

APK_DEFINE_APPLET(apk_info);

