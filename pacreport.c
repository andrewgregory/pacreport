#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <ctype.h>
#include <math.h>

#include <alpm.h>
#include <alpm_list.h>

struct pu_missing_file_t {
	alpm_pkg_t *pkg;
	alpm_file_t *file;
};

struct pu_repo_t {
	char *name;
};

struct pu_config_t {
	char *rootdir, *dbpath;
	alpm_list_t *repos, *cachedirs;
};

struct pu_missing_file_t *pu_missing_file_new(alpm_pkg_t *pkg, alpm_file_t *file)
{
	struct pu_missing_file_t *mf = calloc(sizeof(struct pu_missing_file_t), 1);
	if(!mf) {
		return NULL;
	}
	mf->pkg = pkg;
	mf->file = file;
	return mf;
}

struct pu_config_t *pu_config_new(void)
{
	return calloc(sizeof(struct pu_config_t), 1);
}

struct pu_repo_t *pu_repo_new(void)
{
	return calloc(sizeof(struct pu_repo_t), 1);
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
unsigned int get_pkg_chain_size(alpm_handle_t *handle,
		alpm_pkg_t *pkg, alpm_list_t *depchain)
{
	alpm_list_t *d;
	unsigned int size = 0;

	if(!alpm_list_find_ptr(depchain, pkg)) {
		depchain = alpm_list_add(depchain, pkg);
	}

	for(d = depchain; d; d = d->next) {
		alpm_pkg_t *p = d->data;
		alpm_list_t *dep, *deps = alpm_pkg_get_depends(p);

		size += alpm_pkg_get_isize(p);

		for(dep = deps; dep; dep = dep->next) {
			char *depstring = alpm_dep_compute_string(dep->data);
			alpm_pkg_t *satisfier = alpm_find_dbs_satisfier(handle,
					alpm_get_syncdbs(handle), depstring);
			free(depstring);

			/* move on if the dependency was installed explicitly or already
			 * processed */
			if(alpm_pkg_get_reason(satisfier) == ALPM_PKG_REASON_EXPLICIT ||
					alpm_list_find_ptr(depchain, satisfier)) {
				continue;
			}

			/* check if the dependency is required outside the chain */
			alpm_list_t *r, *rb = alpm_pkg_compute_requiredby(satisfier);
			int required = 0;
			alpm_db_t *localdb = alpm_get_localdb(handle);
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
			hr_size( get_pkg_chain_size(handle, pkg, NULL), size));

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

void print_group_missing(alpm_handle_t *handle)
{
	char **group, *groups[] = {"base", "base-devel", NULL};
	alpm_db_t *localdb = alpm_get_localdb(handle);
	alpm_list_t *localpkgs = alpm_db_get_pkgcache(localdb);
	alpm_list_t *matches = NULL;

	for(group = groups; *group; group++) {
		alpm_list_t *p, *pkgs;
		pkgs = alpm_find_group_pkgs(alpm_get_syncdbs(handle), *group);
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
		struct pu_missing_file_t *mf = f->data;
		size_t len = strlen(alpm_pkg_get_name(mf->pkg));
		if(len > pkgname_len) {
			pkgname_len = len;
		}
	}
	for(f = files; f; f = f->next) {
		struct pu_missing_file_t *mf = f->data;
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
				struct pu_missing_file_t *mf = pu_missing_file_new(p->data,
						&files->files[i]);
				matches = alpm_list_add(matches, mf);
			}
		}
	}
	puts("Missing Package Files:");
	print_filelist(handle, matches);
	FREELIST(matches);
}

unsigned int get_dir_size(const char *path)
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
		if(strcmp(".", de->d_name) == 0 || strcmp("..", de->d_name) == 0) {
			continue;
		}
		strncpy(tail, de->d_name, max);
		struct stat buf;
		if(stat(de_path, &buf)) {
			continue;
		}

		if(S_ISDIR(buf.st_mode)) {
			bytes += get_dir_size(de_path);
		} else {
			bytes += buf.st_size;
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
		char *size = hr_size(get_dir_size(c->data), NULL);
		printf("  %*s %s\n", pathlen, c->data, size);
		free(size);
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


struct pu_config_t *initialize_config_from_file(const char *filename)
{
	struct pu_config_t *config = pu_config_new();
	config->rootdir = strdup("/");
	config->dbpath = strdup("/var/lib/pacman/");

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
				struct pu_repo_t *r = pu_repo_new();
				r->name = strdup(state);
				config->repos = alpm_list_add(config->repos, r);
			}
		} else if(strcmp(key, "RootDir") == 0) {
			free(config->rootdir);
			config->rootdir = strdup(val);
		} else if(strcmp(key, "DBPath") == 0) {
			free(config->dbpath);
			config->dbpath = strdup(val);
		}
	}
	fclose(infile);

	if(!config->cachedirs) {
		config->cachedirs = alpm_list_add(config->cachedirs, "/var/cache/pacman/pkg");
	}

	return config;
}

alpm_handle_t *initialize_handle_from_config(struct pu_config_t *config)
{
	alpm_list_t *c;
	alpm_handle_t *handle = alpm_initialize(config->rootdir, config->dbpath, NULL);

	if(!handle) {
		return NULL;
	}

	for(c = config->cachedirs; c; c = c->next) {
		alpm_option_add_cachedir(handle, c->data);
	}

	return handle;
}

alpm_db_t *register_syncdb(alpm_handle_t *handle, struct pu_repo_t *repo)
{
	return alpm_register_syncdb(handle, repo->name, ALPM_SIG_USE_DEFAULT);
}

alpm_list_t *register_syncdbs(alpm_handle_t *handle, alpm_list_t *repos)
{
	alpm_list_t *r, *registered = NULL;
	for(r = repos; r; r = r->next) {
		registered = alpm_list_add(registered, register_syncdb(handle, r->data));
	}
	return registered;
}

int main(int argc, char **argv)
{
	struct pu_config_t *config = initialize_config_from_file("/etc/pacman.conf");
	alpm_handle_t *handle = initialize_handle_from_config(config);

	if(!handle) {
		exit(-1);
	}

	register_syncdbs(handle, config->repos);

	print_toplevel_explicit(handle);
	print_toplevel_depends(handle);
	print_foreign(handle);

	print_group_missing(handle);
	print_missing_files(handle);
	print_cache_sizes(handle);

	return 0;
}

/* vim: set ts=2 sw=2 noet: */
