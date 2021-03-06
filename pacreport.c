#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <ctype.h>
#include <math.h>

#include <alpm.h>
#include <alpm_list.h>

#define VERSION "1.0"

struct pkg_file_t {
	alpm_pkg_t *pkg;
	alpm_file_t *file;
};

struct pkg_file_t *pkg_file_new(alpm_pkg_t *pkg, alpm_file_t *file)
{
	struct pkg_file_t *mf = calloc(sizeof(struct pkg_file_t), 1);
	if(!mf) {
		return NULL;
	}
	mf->pkg = pkg;
	mf->file = file;
	return mf;
}

/**
 * @brief Convert bytes to a human readable string.
 *
 * @param bytes
 * @param dest will be malloc'd if NULL
 *
 * @return dest
 */
char *hr_size(unsigned int bytes, char *dest) {
	static const char suffixes[] = {'B', 'K', 'M', 'G', 'T', '\0'};
	float size = (float) bytes;
	int suf;

	if(!dest) {
		dest = malloc(10);
	}

	for(suf = 0; fabs(size) >= 1000 && suffixes[suf + 1]; suf++) {
		size = size / 1024;
	}

	sprintf(dest, "%6.2f %c", size, suffixes[suf]);

	return dest;
}

/**
 * @brief Calculates the total size of all unneeded dependencies of a package.
 *
 * @param handle
 * @param pkg
 * @param depchain list of already processed packages
 *
 * @return size in bytes
 */
unsigned int get_pkg_chain_size(alpm_handle_t *handle, alpm_pkg_t *pkg)
{
	alpm_db_t *localdb = alpm_get_localdb(handle);
	alpm_list_t *localpkgs = alpm_db_get_pkgcache(localdb);
	alpm_list_t *depchain = alpm_list_add(NULL, pkg);
	unsigned int size = 0;
	alpm_list_t *d;


	for(d = depchain; d; d = d->next) {
		alpm_pkg_t *p = d->data;
		alpm_list_t *dep, *deps = alpm_pkg_get_depends(p);

		size += alpm_pkg_get_isize(p);

		for(dep = deps; dep; dep = dep->next) {
			char *depstring = alpm_dep_compute_string(dep->data);
			alpm_pkg_t *satisfier = alpm_find_satisfier(localpkgs, depstring);
			free(depstring);

			/* move on if the dependency was installed explicitly or already
			 * processed */
			if(!satisfier
					|| alpm_pkg_get_reason(satisfier) == ALPM_PKG_REASON_EXPLICIT
					|| alpm_list_find_ptr(depchain, satisfier)) {
				continue;
			}

			/* check if the dependency is required outside the chain */
			alpm_list_t *r, *rb = alpm_pkg_compute_requiredby(satisfier);
			int required = 0;
			for(r = rb; r; r = r->next) {
				alpm_pkg_t *p = alpm_db_get_pkg(localdb, r->data);
				if(!alpm_list_find_ptr(depchain, p)) {
					required = 1;
					break;
				}
			}
			FREELIST(rb);

			if(!required) {
				depchain = alpm_list_add(depchain, satisfier);
			}
		}
	}

	return size;
}

void print_pkg_info(alpm_handle_t *handle, alpm_pkg_t *pkg, size_t pkgname_len)
{
	char size[20];
	alpm_list_t *group;

	printf("  %-*s	%s", pkgname_len, alpm_pkg_get_name(pkg),
			hr_size( get_pkg_chain_size(handle, pkg), size));

	if(alpm_pkg_get_groups(pkg)) {
		fputs(" (", stdout);
		for(group = alpm_pkg_get_groups(pkg); group; group = group->next) {
			fputs(group->data, stdout);
			if(group->next) {
				putchar(' ');
			}
		}
		putchar(')');
	}

	putchar('\n');
}

void print_pkglist(alpm_handle_t *handle, alpm_list_t *pkgs)
{
	size_t pkgname_len = 0;
	alpm_list_t *p;
	for(p = pkgs; p; p = p->next) {
		size_t len = strlen(alpm_pkg_get_name(p->data));
		if(len > pkgname_len) {
			pkgname_len = len;
		}
	}
	for(p = pkgs; p; p = p->next) {
		print_pkg_info(handle, p->data, pkgname_len);
	}
}

void print_toplevel_explicit(alpm_handle_t *handle)
{
	alpm_db_t *localdb = alpm_get_localdb(handle);
	alpm_list_t *matches = NULL, *p, *pkgs = alpm_db_get_pkgcache(localdb);

	for(p = pkgs; p; p = p->next) {
		alpm_list_t *rb = alpm_pkg_compute_requiredby(p->data);
		if(!rb && alpm_pkg_get_reason(p->data) == ALPM_PKG_REASON_EXPLICIT) {
			matches = alpm_list_add(matches, p->data);
		}
		FREELIST(rb);
	}
	printf("Unneeded Packages Installed Explicitly:\n");
	print_pkglist(handle, matches);
	alpm_list_free(matches);
}

void print_toplevel_depends(alpm_handle_t *handle)
{
	alpm_db_t *localdb = alpm_get_localdb(handle);
	alpm_list_t *matches = NULL, *p, *pkgs = alpm_db_get_pkgcache(localdb);

	for(p = pkgs; p; p = p->next) {
		alpm_list_t *rb = alpm_pkg_compute_requiredby(p->data);
		if(!rb && alpm_pkg_get_reason(p->data) == ALPM_PKG_REASON_DEPEND) {
			matches = alpm_list_add(matches, p->data);
		}
		FREELIST(rb);
	}
	printf("Unneeded Packages Installed As Dependencies:\n");
	print_pkglist(handle, matches);
	alpm_list_free(matches);
}

int pkg_is_foreign(alpm_handle_t *handle, alpm_pkg_t *pkg)
{
	alpm_list_t *s;
	const char *pkgname = alpm_pkg_get_name(pkg);
	for(s = alpm_get_syncdbs(handle); s; s = s->next) {
		if(alpm_db_get_pkg(s->data, pkgname)) {
			return 0;
		}
	}
	return 1;
}

void print_foreign(alpm_handle_t *handle)
{
	alpm_db_t *localdb = alpm_get_localdb(handle);
	alpm_list_t *matches = NULL, *p, *pkgs = alpm_db_get_pkgcache(localdb);

	for(p = pkgs; p; p = p->next) {
		if(pkg_is_foreign(handle, p->data)) {
			matches = alpm_list_add(matches, p->data);
		}
	}
	printf("Installed Packages Not In A Repository:\n");
	print_pkglist(handle, matches);
	alpm_list_free(matches);
}

void print_group_missing(alpm_handle_t *handle, alpm_list_t *groups)
{
	alpm_db_t *localdb = alpm_get_localdb(handle);
	alpm_list_t *localpkgs = alpm_db_get_pkgcache(localdb);
	alpm_list_t *matches = NULL;
	alpm_list_t *i;

	for(i = groups; i; i = i->next) {
		const char *group = i->data;
		alpm_list_t *p, *pkgs;
		pkgs = alpm_find_group_pkgs(alpm_get_syncdbs(handle), group);
		for(p = pkgs; p; p = p->next) {
			const char *pkgname = alpm_pkg_get_name(p->data);
			if(!alpm_list_find_ptr(matches, p->data)
					&& !alpm_find_satisfier(localpkgs, pkgname)) {
				matches = alpm_list_add(matches, p->data);
			}
		}
		alpm_list_free(pkgs);
	}

	puts("Missing Group Packages:");
	print_pkglist(handle, matches);
	alpm_list_free(matches);
}

void print_filelist(alpm_handle_t *handle, alpm_list_t *files)
{
	size_t pkgname_len = 0;
	alpm_list_t *f;
	const char *root = alpm_option_get_root(handle);
	for(f = files; f; f = f->next) {
		struct pkg_file_t *mf = f->data;
		size_t len = strlen(alpm_pkg_get_name(mf->pkg));
		if(len > pkgname_len) {
			pkgname_len = len;
		}
	}
	for(f = files; f; f = f->next) {
		struct pkg_file_t *mf = f->data;
		printf("  %-*s	%s%s\n", pkgname_len, alpm_pkg_get_name(mf->pkg),
				root, mf->file->name);
	}
}

void print_missing_files(alpm_handle_t *handle)
{
	alpm_db_t *localdb = alpm_get_localdb(handle);
	alpm_list_t *matches = NULL, *p, *pkgs = alpm_db_get_pkgcache(localdb);
	char path[PATH_MAX], *tail;
	strncpy(path, alpm_option_get_root(handle), PATH_MAX);
	size_t len = strlen(path);
	size_t max = PATH_MAX - len;
	tail = path + len;

	for(p = pkgs; p; p = p->next) {
		alpm_filelist_t *files = alpm_pkg_get_files(p->data);
		int i;
		for(i = 0; i < files->count; ++i) {
			strncpy(tail, files->files[i].name, max);
			if(access(path, F_OK) != 0) {
				struct pkg_file_t *mf = pkg_file_new(p->data, &files->files[i]);
				matches = alpm_list_add(matches, mf);
			}
		}
	}
	puts("Missing Package Files:");
	print_filelist(handle, matches);
	FREELIST(matches);
}

int is_cache_file_installed(alpm_handle_t *handle, const char *path)
{
	char *pkgver, *pkgname = strdup(path);
	int ret = 0;

	if(!(pkgver = strrchr(pkgname, '-'))) {
		goto cleanup;
	}
	*pkgver = '\0';
	for(pkgver--; *pkgver != '-' && pkgver > pkgname; pkgver--);
	for(pkgver--; *pkgver != '-' && pkgver > pkgname; pkgver--);

	if(pkgver > pkgname) {
		*pkgver = '\0';
		pkgver++;
		alpm_pkg_t *lp = alpm_db_get_pkg(alpm_get_localdb(handle), pkgname);
		if(lp && alpm_pkg_vercmp(alpm_pkg_get_version(lp), pkgver) == 0) {
			ret = 1;
		}
	}

cleanup:
	free(pkgname);
	return ret;
}

size_t get_cache_size(alpm_handle_t *handle, const char *path,
		size_t *uninstalled)
{
	unsigned int bytes = 0;
	DIR *d = opendir(path);
	struct dirent *de;
	char de_path[PATH_MAX];

	strncpy(de_path, path, PATH_MAX);
	size_t plen = strlen(path);
	char *tail = de_path + plen;
	size_t max = PATH_MAX - plen;

	for(de = readdir(d); de; de = readdir(d)) {
		if(de->d_name[0] == '.'
				&& (de->d_name[1] == '\0' || strcmp("..", de->d_name) == 0)) {
			continue;
		}
		strncpy(tail, de->d_name, max);
		struct stat buf;
		if(stat(de_path, &buf) != 0) {
			continue;
		}

		if(S_ISDIR(buf.st_mode)) {
			bytes += get_cache_size(handle, de_path, uninstalled);
		} else {
			bytes += buf.st_size;
			if(uninstalled && !is_cache_file_installed(handle, de->d_name)) {
				*uninstalled += buf.st_size;
			}
		}
	}
	return bytes;
}

void print_cache_sizes(alpm_handle_t *handle)
{
	alpm_list_t *c, *cache_dirs = alpm_option_get_cachedirs(handle);
	size_t pathlen = 0;

	for(c = cache_dirs; c; c = c->next) {
		size_t len = strlen(c->data);
		if(len > pathlen) {
			pathlen = len;
		}
	}

	puts("Package Cache Size:");
	for(c = cache_dirs; c; c = c->next) {
		size_t uninstalled = 0;
		char *size = hr_size(get_cache_size(handle, c->data, &uninstalled), NULL);
		char *usize = hr_size(uninstalled, NULL);
		printf("  %*s %s (%s not installed)\n", pathlen, c->data, size, usize);
		free(size);
		free(usize);
	}
}

size_t strtrim(char *str) {
	char *start = str, *end;

	if(!(str && *str)) {
		return 0;
	}

	end = str + strlen(str);

	for(; isspace(*start) && start < end; start++);
	for(; end > start && isspace(*(end - 1)); end--);

	memmove(str, start, end - start);
	*(end) = '\0';

	return end - start;
}


alpm_handle_t *initialize_from_file(const char *filename)
{
	alpm_handle_t *handle;
	alpm_list_t *repos = NULL, *caches = NULL, *i;
	char *rootdir = strdup("/");
	char *dbpath = strdup("/var/lib/pacman/");

	FILE *infile = fopen(filename, "r");
	char buf[BUFSIZ];

	while(fgets(buf, BUFSIZ, infile)) {
		char *key, *val, *state;
		char *line = buf;
		size_t linelen;

		/* remove comments */
		if((state = strchr(line, '#'))) {
			*state = '\0';
		}

		/* strip surrounding whitespace */
		linelen = strtrim(line);

		/* skip empty lines */
		if(line[0] == '\0') {
			continue;
		}

		key = strtok_r(line, " =", &state);
		val = strtok_r(NULL, " =", &state);

		if(line[0] == '[' && line[linelen - 1] == ']') {
			line[linelen - 1] = '\0';
			state = line + 1;

			if(strcmp(state, "options") != 0) {
				repos = alpm_list_add(repos, strdup(state));
			}
		} else if(strcmp(key, "RootDir") == 0) {
			free(rootdir);
			rootdir = strdup(val);
		} else if(strcmp(key, "DBPath") == 0) {
			free(dbpath);
			dbpath = strdup(val);
		}
	}
	fclose(infile);

	if(!caches) {
		caches = alpm_list_add(caches, strdup("/var/cache/pacman/pkg"));
	}

	handle = alpm_initialize(rootdir, dbpath, NULL);

	if(!handle) {
		return NULL;
	}

	for(i = repos; i; i = i->next) {
		alpm_register_syncdb(handle, i->data, ALPM_SIG_USE_DEFAULT);
	}
	FREELIST(repos);

	for(i = caches; i; i = i->next) {
		alpm_option_add_cachedir(handle, i->data);
	}
	FREELIST(caches);

	return handle;
}

void version(void) {
	printf("pacreport v" VERSION " - libalpm v%s\n", alpm_version());
	exit(0);
}

void usage(int ret) {
	FILE *out = (ret ? stderr : stdout);
	fputs("Usage: pacreport [options]\n", out);
	fputs("       pacreport [--help|--version]\n", out);
	fputs("\n", out);
	fputs("Options:\n", out);
	fputs("  --backups                list .pac{save,orig,new} files\n", out);
	fputs("                           (pass twice for extended search)\n", out);
	fputs("  --groups=GROUP           list missing group packages\n", out);
	fputs("  --missing-files          list missing package files\n", out);
	fputs("  --unowned-files          list unowned files\n", out);
	exit(ret);
}

int file_is_unowned(alpm_handle_t *handle, const char *path) {
	alpm_list_t *p, *pkgs = alpm_db_get_pkgcache(alpm_get_localdb(handle));
	for(p = pkgs; p; p = p->next) {
		if(alpm_filelist_contains(alpm_pkg_get_files(p->data), path + 1)) {
			return 0;
		}
	}
	return 1;
}

void _scan_filesystem(alpm_handle_t *handle, const char *dir, int backups,
		int orphans, alpm_list_t **backups_found, alpm_list_t **orphans_found) {
	static char *skip[] = {
		"/etc/ssl/certs",
		"/dev",
		"/home",
		"/media",
		"/mnt",
		"/proc",
		"/root",
		"/run",
		"/sys",
		"/tmp",
		"/usr/share/mime",
		"/var/cache",
		"/var/log",
		"/var/run",
		"/var/tmp",
		NULL
	};

	char path[PATH_MAX];
	char *filename = path + strlen(dir);
	strcpy(path, dir);

	DIR *dirp = opendir(dir);
	if(!dirp) {
		fprintf(stderr, "Error opening '%s' (%s).\n", dir, strerror(errno));
		return;
	}

	struct dirent *entry;
	while((entry = readdir(dirp))) {
		struct stat buf;
		char **s;
		int need_skip = 0;

		if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ) {
			continue;
		}

		strcpy(filename, entry->d_name);

		for(s = skip; *s && !need_skip; s++) {
			if(strcmp(path, *s) == 0) {
				need_skip = 1;
			}
		}
		if(need_skip) {
			continue;
		}

		if(lstat(path, &buf) != 0) {
			fprintf(stderr, "Error reading '%s' (%s).\n", path, strerror(errno));
			continue;
		}

		if(S_ISDIR(buf.st_mode)) {
			strcat(filename, "/");
			if(orphans && file_is_unowned(handle, path)) {
				*orphans_found = alpm_list_add(*orphans_found, strdup(path));
				if(backups) {
					_scan_filesystem(handle, path, backups, 0, backups_found, orphans_found);
				}
			} else {
				_scan_filesystem(handle, path, backups, orphans, backups_found, orphans_found);
			}
		} else {
			if(orphans && file_is_unowned(handle, path)) {
				*orphans_found = alpm_list_add(*orphans_found, strdup(path));
			}

			if(backups) {
				if(strstr(filename, ".pacnew")
						|| strstr(filename, ".pacsave")
						|| strstr(filename, ".pacorig")) {
					*backups_found = alpm_list_add(*backups_found, strdup(path));
				}
			}
		}
	}
	closedir(dirp);
}

void scan_filesystem(alpm_handle_t *handle, int backups, int orphans) {
	char *base_dir = "/etc/";
	alpm_list_t *orphans_found = NULL, *backups_found = NULL;
	if(backups > 1 || orphans) {
		base_dir = "/";
	}
	_scan_filesystem(handle, base_dir, backups, orphans, &backups_found, &orphans_found);

	if(orphans) {
		puts("Unowned Files:");
		if(!orphans_found) {
			puts("  None");
		} else {
			alpm_list_t *i;
			orphans_found = alpm_list_msort(orphans_found,
					alpm_list_count(orphans_found), (alpm_list_fn_cmp) strcmp);
			for(i = orphans_found; i; i = i->next) {
				printf("  %s\n", i->data);
			}
			FREELIST(orphans_found);
		}
	}

	if(backups) {
		puts("Pacman Backup Files:");
		if(!backups_found) {
			puts("  None");
		} else {
			alpm_list_t *i;
			backups_found = alpm_list_msort(backups_found,
					alpm_list_count(backups_found), (alpm_list_fn_cmp) strcmp);
			for(i = backups_found; i; i = i->next) {
				printf("  %s\n", i->data);
			}
			FREELIST(backups_found);
		}
	}
}

int main(int argc, char **argv) {
	alpm_handle_t *handle;
	alpm_list_t *groups = NULL;
	int missing_files = 0, backup_files = 0, orphan_files = 0;

	if(argc > 1) {
		int c;

		struct option long_options[] = {
			{"backups",       no_argument,       &backup_files,   1 },
			{"groups",        required_argument, NULL,           'g'},
			{"help",          no_argument,       NULL,           'h'},
			{"missing-files", no_argument,       &missing_files,  1 },
			{"unowned-files", no_argument,       &orphan_files,   1 },
			{"version",       no_argument,       NULL,           'V'},
			{0, 0, 0, 0 }
		};

		while(( c = getopt_long(argc, argv, "hV", long_options, NULL)) != -1) {
			switch(c) {
				case 'g':
					{
						char *state, *group;
						group = strtok_r(optarg, ",", &state);
						while(group) {
							groups = alpm_list_add(groups, strdup(group));
							group = strtok_r(NULL, ",", &state);
						}
					}
					break;
				case 'h': usage(0); break;
				case 'V': version(); break;
				case '?': usage(1); break;
			}
		}
	}

	handle = initialize_from_file("/etc/pacman.conf");

	if(!handle) {
		fprintf(stderr, "Could not initialize alpm handle.\n");
		exit(-1);
	}

	if(backup_files || orphan_files) {
		scan_filesystem(handle, backup_files, orphan_files);
	}

	print_toplevel_explicit(handle);
	print_toplevel_depends(handle);
	print_foreign(handle);

	if(!groups) {
		groups = alpm_list_add(groups, strdup("base"));
		groups = alpm_list_add(groups, strdup("base-devel"));
	}
	print_group_missing(handle, groups);
	FREELIST(groups);

	if(missing_files) {
		print_missing_files(handle);
	}
	print_cache_sizes(handle);

	return 0;
}

/* vim: set ts=2 sw=2 noet: */
