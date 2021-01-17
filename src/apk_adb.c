#include <errno.h>
#include "adb.h"
#include "apk_adb.h"
#include "apk_print.h"
#include "apk_version.h"

#define APK_VERSION_CONFLICT 16

/* Few helpers to map old database to new one */

int apk_dep_split(apk_blob_t *b, apk_blob_t *bdep)
{
	extern const apk_spn_match_def apk_spn_dependency_separator;

	if (APK_BLOB_IS_NULL(*b)) return 0;
	if (apk_blob_cspn(*b, apk_spn_dependency_separator, bdep, b)) {
		/* found separator - update b to skip over after all separators */
		if (!apk_blob_spn(*b, apk_spn_dependency_separator, NULL, b))
			*b = APK_BLOB_NULL;
	} else {
		/* no separator - return this as the last dependency, signal quit */
		*bdep = *b;
		*b = APK_BLOB_NULL;
	}
	return 1;
}

adb_val_t adb_wo_pkginfo(struct adb_obj *obj, unsigned int f, apk_blob_t val)
{
	struct apk_checksum csum;
	adb_val_t v = ADB_ERROR(EAPKFORMAT);

	/* FIXME: get rid of this function, and handle the conversion via schema? */
	switch (f) {
	case ADBI_PI_UNIQUE_ID:
		if (!val.ptr || val.len < 4) break;
		apk_blob_pull_csum(&val, &csum);
		v = adb_w_int(obj->db, get_unaligned32(csum.data) & ADB_VALUE_MASK);
		break;
	case ADBI_PI_REPO_COMMIT:
		if (val.len < 40) break;
		csum.type = 20;
		apk_blob_pull_hexdump(&val, APK_BLOB_CSUM(csum));
		if (val.ptr) v = adb_w_blob(obj->db, APK_BLOB_CSUM(csum));
		break;
	default:
		return adb_wo_val_fromstring(obj, f, val);
	}
	if (v != ADB_NULL && !ADB_IS_ERROR(v))
		v = adb_wo_val(obj, f, v);
	return v;
}

unsigned int adb_pkg_field_index(char f)
{
#define MAP(ch, ndx) [ch - 'A'] = ndx
	static unsigned char map[] = {
		MAP('C', ADBI_PI_UNIQUE_ID),
		MAP('P', ADBI_PI_NAME),
		MAP('V', ADBI_PI_VERSION),
		MAP('T', ADBI_PI_DESCRIPTION),
		MAP('U', ADBI_PI_URL),
		MAP('I', ADBI_PI_INSTALLED_SIZE),
		MAP('S', ADBI_PI_FILE_SIZE),
		MAP('L', ADBI_PI_LICENSE),
		MAP('A', ADBI_PI_ARCH),
		MAP('D', ADBI_PI_DEPENDS),
		MAP('i', ADBI_PI_INSTALL_IF),
		MAP('p', ADBI_PI_PROVIDES),
		MAP('o', ADBI_PI_ORIGIN),
		MAP('m', ADBI_PI_MAINTAINER),
		MAP('t', ADBI_PI_BUILD_TIME),
		MAP('c', ADBI_PI_REPO_COMMIT),
		MAP('r', ADBI_PI_REPLACES),
		MAP('k', ADBI_PI_PRIORITY),
	};
	if (f < 'A' || f-'A' >= ARRAY_SIZE(map)) return 0;
	return map[(unsigned char)f - 'A'];
}

/* Schema */

static apk_blob_t string_tostring(struct adb *db, adb_val_t val, char *buf, size_t bufsz)
{
	return adb_r_blob(db, val);
}

static adb_val_t string_fromstring(struct adb *db, apk_blob_t val)
{
	return adb_w_blob(db, val);
}

static int string_compare(struct adb *db1, adb_val_t v1, struct adb *db2, adb_val_t v2)
{
	return apk_blob_sort(adb_r_blob(db1, v1), adb_r_blob(db2, v2));
}

static struct adb_scalar_schema scalar_string = {
	.kind = ADB_KIND_BLOB,
	.tostring = string_tostring,
	.fromstring = string_fromstring,
	.compare = string_compare,
};

static struct adb_scalar_schema scalar_mstring = {
	.kind = ADB_KIND_BLOB,
	.multiline = 1,
	.tostring = string_tostring,
	.fromstring = string_fromstring,
	.compare = string_compare,
};

const struct adb_object_schema schema_string_array = {
	.kind = ADB_KIND_ARRAY,
	.num_fields = APK_MAX_PKG_TRIGGERS,
	.fields = ADB_ARRAY_ITEM(scalar_string),
};

static int version_compare(struct adb *db1, adb_val_t v1, struct adb *db2, adb_val_t v2)
{
	switch (apk_version_compare_blob(adb_r_blob(db1, v1), adb_r_blob(db2, v2))) {
	case APK_VERSION_LESS: return -1;
	case APK_VERSION_GREATER: return 1;
	default: return 0;
	}
}

static struct adb_scalar_schema scalar_version = {
	.kind = ADB_KIND_BLOB,
	.tostring = string_tostring,
	.fromstring = string_fromstring,
	.compare = version_compare,
};


static apk_blob_t hexblob_tostring(struct adb *db, adb_val_t val, char *buf, size_t bufsz)
{
	apk_blob_t b = adb_r_blob(db, val), to = APK_BLOB_PTR_LEN(buf, bufsz);

	if (APK_BLOB_IS_NULL(b)) return b;

	apk_blob_push_hexdump(&to, b);
	if (!APK_BLOB_IS_NULL(to))
		return APK_BLOB_PTR_PTR(buf, to.ptr-1);

	return APK_BLOB_PTR_LEN(buf, snprintf(buf, bufsz, "(%ld bytes)", b.len));
}

static struct adb_scalar_schema scalar_hexblob = {
	.kind = ADB_KIND_BLOB,
	.tostring = hexblob_tostring,
};

static apk_blob_t int_tostring(struct adb *db, adb_val_t val, char *buf, size_t bufsz)
{
	return APK_BLOB_PTR_LEN(buf, snprintf(buf, bufsz, "%u", adb_r_int(db, val)));
}

static adb_val_t int_fromstring(struct adb *db, apk_blob_t val)
{
	uint32_t n = apk_blob_pull_uint(&val, 10);
	if (val.len) return ADB_ERROR(EINVAL);
	return adb_w_int(db, n);
}

static int int_compare(struct adb *db1, adb_val_t v1, struct adb *db2, adb_val_t v2)
{
	uint32_t r1 = adb_r_int(db1, v1);
	uint32_t r2 = adb_r_int(db1, v2);
	if (r1 < r2) return -1;
	if (r1 > r2) return 1;
	return 0;
}

static struct adb_scalar_schema scalar_int = {
	.kind = ADB_KIND_INT,
	.tostring = int_tostring,
	.fromstring = int_fromstring,
	.compare = int_compare,
};

static apk_blob_t oct_tostring(struct adb *db, adb_val_t val, char *buf, size_t bufsz)
{
	return APK_BLOB_PTR_LEN(buf, snprintf(buf, bufsz, "%o", adb_r_int(db, val)));
}

static struct adb_scalar_schema scalar_oct = {
	.kind = ADB_KIND_INT,
	.tostring = oct_tostring,
};

static apk_blob_t hsize_tostring(struct adb *db, adb_val_t val, char *buf, size_t bufsz)
{
	off_t v = adb_r_int(db, val);
	const char *unit = apk_get_human_size(0, v, &v);

	return APK_BLOB_PTR_LEN(buf, snprintf(buf, bufsz, "%jd %s", (intmax_t)v, unit));
}

static struct adb_scalar_schema scalar_hsize = {
	.kind = ADB_KIND_INT,
	.tostring = hsize_tostring,
	.fromstring = int_fromstring,
	.compare = int_compare,
};

static apk_blob_t dependency_tostring(struct adb_obj *obj, char *buf, size_t bufsz)
{
	apk_blob_t name, ver;
	unsigned int mask;

	name = adb_ro_blob(obj, ADBI_DEP_NAME);
	ver  = adb_ro_blob(obj, ADBI_DEP_VERSION);

	if (APK_BLOB_IS_NULL(name)) return APK_BLOB_NULL;
	if (APK_BLOB_IS_NULL(ver)) return name;

	mask = adb_ro_int(obj, ADBI_DEP_MATCH) ?: APK_VERSION_EQUAL;
	return APK_BLOB_PTR_LEN(buf,
		snprintf(buf, bufsz, "%s"BLOB_FMT"%s"BLOB_FMT,
			(mask & APK_VERSION_CONFLICT) ? "!" : "",
			BLOB_PRINTF(name),
			apk_version_op_string(mask & ~APK_VERSION_CONFLICT),
			BLOB_PRINTF(ver)));
}

static int dependency_fromstring(struct adb_obj *obj, apk_blob_t bdep)
{
	extern const apk_spn_match_def apk_spn_dependency_comparer;
	extern const apk_spn_match_def apk_spn_repotag_separator;
	apk_blob_t bname, bop, bver = APK_BLOB_NULL, btag;
	int mask = APK_DEPMASK_ANY;

	/* [!]name[<,<=,<~,=,~,>~,>=,>,><]ver */

	/* parse the version */
	if (bdep.ptr[0] == '!') {
		bdep.ptr++;
		bdep.len--;
		mask |= APK_VERSION_CONFLICT;
	}

	if (apk_blob_cspn(bdep, apk_spn_dependency_comparer, &bname, &bop)) {
		int i;

		if (mask == 0)
			goto fail;
		if (!apk_blob_spn(bop, apk_spn_dependency_comparer, &bop, &bver))
			goto fail;

		mask = 0;
		for (i = 0; i < bop.len; i++) {
			switch (bop.ptr[i]) {
			case '<':
				mask |= APK_VERSION_LESS;
				break;
			case '>':
				mask |= APK_VERSION_GREATER;
				break;
			case '~':
				mask |= APK_VERSION_FUZZY|APK_VERSION_EQUAL;
				break;
			case '=':
				mask |= APK_VERSION_EQUAL;
				break;
			}
		}
		if ((mask & APK_DEPMASK_CHECKSUM) != APK_DEPMASK_CHECKSUM &&
		    !apk_version_validate(bver))
			goto fail;
	} else {
		bname = bdep;
		bop = APK_BLOB_NULL;
		bver = APK_BLOB_NULL;
	}

	if (apk_blob_cspn(bname, apk_spn_repotag_separator, &bname, &btag))
		; /* tag = repository tag */

	adb_wo_blob(obj, ADBI_DEP_NAME, bname);
	if (mask != APK_DEPMASK_ANY) {
		adb_wo_blob(obj, ADBI_DEP_VERSION, bver);
		if (mask != APK_VERSION_EQUAL)
			adb_wo_int(obj, ADBI_DEP_MATCH, mask);
	}
	return 0;

fail:
	return -EAPKDEPFORMAT;
}

static int dependency_cmp(const struct adb_obj *o1, const struct adb_obj *o2)
{
	return adb_ro_cmp(o1, o2, ADBI_DEP_NAME);
}

const struct adb_object_schema schema_dependency = {
	.kind = ADB_KIND_OBJECT,
	.num_fields = ADBI_DEP_MAX,
	.tostring = dependency_tostring,
	.fromstring = dependency_fromstring,
	.compare = dependency_cmp,
	.fields = {
		ADB_FIELD(ADBI_DEP_NAME,	"name",		scalar_string),
		ADB_FIELD(ADBI_DEP_VERSION,	"version",	scalar_version),
		ADB_FIELD(ADBI_DEP_MATCH,	"match",	scalar_int),
	},
};

static int dependencies_fromstring(struct adb_obj *obj, apk_blob_t b)
{
	struct adb_obj dep;
	apk_blob_t bdep;

	adb_wo_alloca(&dep, &schema_dependency, obj->db);

	while (apk_dep_split(&b, &bdep)) {
		adb_wo_fromstring(&dep, bdep);
		adb_wa_append_obj(obj, &dep);
	}

	return 0;
}

const struct adb_object_schema schema_dependency_array = {
	.kind = ADB_KIND_ARRAY,
	.fromstring = dependencies_fromstring,
	.num_fields = APK_MAX_PKG_DEPENDENCIES,
	.pre_commit = adb_wa_sort_unique,
	.fields = ADB_ARRAY_ITEM(schema_dependency),
};

static int pkginfo_cmp(const struct adb_obj *o1, const struct adb_obj *o2)
{
	int r;
	r = adb_ro_cmp(o1, o2, ADBI_PI_NAME);
	if (r) return r;
	r = adb_ro_cmp(o1, o2, ADBI_PI_VERSION);
	if (r) return r;
	return adb_ro_cmp(o1, o2, ADBI_PI_UNIQUE_ID);
}

const struct adb_object_schema schema_pkginfo = {
	.kind = ADB_KIND_OBJECT,
	.num_fields = ADBI_PI_MAX,
	.compare = pkginfo_cmp,
	.fields = {
		ADB_FIELD(ADBI_PI_NAME,		"name",		scalar_string),
		ADB_FIELD(ADBI_PI_VERSION,	"version",	scalar_version),
		ADB_FIELD(ADBI_PI_UNIQUE_ID,	"unique-id",	scalar_int),
		ADB_FIELD(ADBI_PI_DESCRIPTION,	"description",	scalar_string),
		ADB_FIELD(ADBI_PI_ARCH,		"arch",		scalar_string),
		ADB_FIELD(ADBI_PI_LICENSE,	"license",	scalar_string),
		ADB_FIELD(ADBI_PI_ORIGIN,	"origin",	scalar_string),
		ADB_FIELD(ADBI_PI_MAINTAINER,	"maintainer",	scalar_string),
		ADB_FIELD(ADBI_PI_URL,		"url",		scalar_string),
		ADB_FIELD(ADBI_PI_REPO_COMMIT,	"repo-commit",	scalar_hexblob),
		ADB_FIELD(ADBI_PI_BUILD_TIME,	"build-time",	scalar_int),
		ADB_FIELD(ADBI_PI_INSTALLED_SIZE,"installed-size",scalar_hsize),
		ADB_FIELD(ADBI_PI_FILE_SIZE,	"file-size",	scalar_hsize),
		ADB_FIELD(ADBI_PI_PRIORITY,	"priority",	scalar_int),
		ADB_FIELD(ADBI_PI_DEPENDS,	"depends",	schema_dependency_array),
		ADB_FIELD(ADBI_PI_PROVIDES,	"provides",	schema_dependency_array),
		ADB_FIELD(ADBI_PI_REPLACES,	"replaces",	schema_dependency_array),
		ADB_FIELD(ADBI_PI_INSTALL_IF,	"install-if",	schema_dependency_array),
		ADB_FIELD(ADBI_PI_RECOMMENDS,	"recommends",	schema_dependency_array),
	},
};

const struct adb_object_schema schema_pkginfo_array = {
	.kind = ADB_KIND_ARRAY,
	.num_fields = APK_MAX_INDEX_PACKAGES,
	.pre_commit = adb_wa_sort,
	.fields = ADB_ARRAY_ITEM(schema_pkginfo),
};

const struct adb_object_schema schema_index = {
	.kind = ADB_KIND_OBJECT,
	.num_fields = ADBI_NDX_MAX,
	.fields = {
		ADB_FIELD(ADBI_NDX_DESCRIPTION,	"description",	scalar_string),
		ADB_FIELD(ADBI_NDX_PACKAGES,	"packages",	schema_pkginfo_array),
	},
};

static uint32_t file_get_default_int(unsigned i)
{
	switch (i) {
	case ADBI_FI_UID:
	case ADBI_FI_GID:
		return 0;
	case ADBI_FI_MODE:
		return 0644;
	}
	return -1;
}

static int file_cmp(const struct adb_obj *o1, const struct adb_obj *o2)
{
	return adb_ro_cmp(o1, o2, ADBI_FI_NAME);
}

const struct adb_object_schema schema_file = {
	.kind = ADB_KIND_OBJECT,
	.num_fields = ADBI_FI_MAX,
	.get_default_int = file_get_default_int,
	.compare = file_cmp,
	.fields = {
		ADB_FIELD(ADBI_FI_NAME,		"name",		scalar_string),
		ADB_FIELD(ADBI_FI_HASHES,	"hash",		scalar_hexblob),
		ADB_FIELD(ADBI_FI_UID,		"uid",		scalar_int),
		ADB_FIELD(ADBI_FI_GID,		"gid",		scalar_int),
		ADB_FIELD(ADBI_FI_MODE,		"mode",		scalar_oct),
		ADB_FIELD(ADBI_FI_XATTRS,	"xattr",	scalar_hexblob),
	},
};

const struct adb_object_schema schema_file_array = {
	.kind = ADB_KIND_ARRAY,
	.pre_commit = adb_wa_sort,
	.num_fields = APK_MAX_MANIFEST_FILES,
	.fields = ADB_ARRAY_ITEM(schema_file),
};

static uint32_t path_get_default_int(unsigned i)
{
	switch (i) {
	case ADBI_FI_UID:
	case ADBI_FI_GID:
		return 0;
	case ADBI_FI_MODE:
		return 0755;
	}
	return -1;
}

const struct adb_object_schema schema_path = {
	.kind = ADB_KIND_OBJECT,
	.num_fields = ADBI_FI_MAX,
	.get_default_int = path_get_default_int,
	.compare = file_cmp,
	.fields = {
		ADB_FIELD(ADBI_FI_NAME,		"name",		scalar_string),
		ADB_FIELD(ADBI_FI_FILES,	"files",	schema_file_array),
		ADB_FIELD(ADBI_FI_UID,		"uid",		scalar_int),
		ADB_FIELD(ADBI_FI_GID,		"gid",		scalar_int),
		ADB_FIELD(ADBI_FI_MODE,		"mode",		scalar_oct),
		ADB_FIELD(ADBI_FI_XATTRS,	"xattr",	scalar_hexblob),
	},
};

const struct adb_object_schema schema_path_array = {
	.kind = ADB_KIND_ARRAY,
	.pre_commit = adb_wa_sort,
	.num_fields = APK_MAX_MANIFEST_PATHS,
	.fields = ADB_ARRAY_ITEM(schema_path),
};

const struct adb_object_schema schema_scripts = {
	.kind = ADB_KIND_OBJECT,
	.num_fields = ADBI_SCRPT_MAX,
	.fields = {
		ADB_FIELD(ADBI_SCRPT_TRIGGER,	"trigger",	scalar_mstring),
		ADB_FIELD(ADBI_SCRPT_PREINST,	"pre-install",	scalar_mstring),
		ADB_FIELD(ADBI_SCRPT_POSTINST,	"post-install",	scalar_mstring),
		ADB_FIELD(ADBI_SCRPT_PREDEINST,	"pre-deinstall",scalar_mstring),
		ADB_FIELD(ADBI_SCRPT_POSTDEINST,"post-deinstall",scalar_mstring),
		ADB_FIELD(ADBI_SCRPT_PREUPGRADE,"pre-upgrade",	scalar_mstring),
		ADB_FIELD(ADBI_SCRPT_POSTUPGRADE,"post-upgrade",scalar_mstring),
	},
};

static int package_cmp(const struct adb_obj *o1, const struct adb_obj *o2)
{
	return adb_ro_cmp(o1, o2, ADBI_PKG_PKGINFO);
}

const struct adb_object_schema schema_package = {
	.kind = ADB_KIND_OBJECT,
	.num_fields = ADBI_PKG_MAX,
	.compare = package_cmp,
	.fields = {
		ADB_FIELD(ADBI_PKG_PKGINFO,	"info",		schema_pkginfo),
		ADB_FIELD(ADBI_PKG_PATHS,	"paths",	schema_path_array),
		ADB_FIELD(ADBI_PKG_SCRIPTS,	"scripts",	schema_scripts),
		ADB_FIELD(ADBI_PKG_TRIGGERS,	"triggers",	schema_string_array),
		//ADB_FIELD(ADBI_PKG_PASSWD,	"passwd",	schema_string_array),
	},
};

const struct adb_adb_schema schema_package_adb = {
	.kind = ADB_KIND_ADB,
	.schema_id = ADB_SCHEMA_PACKAGE,
	.schema = &schema_package,
};

const struct adb_object_schema schema_package_adb_array = {
	.kind = ADB_KIND_ARRAY,
	.pre_commit = adb_wa_sort,
	.num_fields = APK_MAX_INDEX_PACKAGES,
	.fields = ADB_ARRAY_ITEM(schema_package_adb),
};

const struct adb_object_schema schema_idb = {
	.kind = ADB_KIND_OBJECT,
	.num_fields = ADBI_IDB_MAX,
	.fields = {
		ADB_FIELD(ADBI_IDB_PACKAGES,	"packages",	schema_package_adb_array),
	},
};
