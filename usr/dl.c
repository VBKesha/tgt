/*
 * Dynamic library
 *
 * (C) 2005 FUJITA Tomonori <tomof@acm.org>
 * (C) 2005 Mike Christie <michaelc@cs.wisc.edu>
 *
 * This code is licenced under the GPL.
 */

/* TODO : better handling of dynamic library. */

#include <string.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/poll.h>

#include "log.h"
#include "dl.h"
#include "tgtd.h"

struct driver_info {
	char *name;
	char *proto;
	void *dl;
	void *pdl;
};

static struct driver_info dinfo[MAX_DL_HANDLES];

static int driver_find_by_name(char *name)
{
	int i;

	for (i = 0; i < MAX_DL_HANDLES; i++) {
		if (dinfo[i].dl &&
		    !strncmp(dinfo[i].name, name, strlen(dinfo[i].name)))
			return i;
	}

	return -ENOENT;
}

static char *dlname(char *d_name, char *entry)
{
	int fd, err;
	char *p, path[PATH_MAX], buf[PATH_MAX];

	snprintf(path, sizeof(path),
		 TGT_TYPE_SYSFSDIR "/%s/%s", d_name, entry);
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		eprintf("%s\n", path);
		return NULL;
	}
	memset(buf, 0, sizeof(buf));
	err = read(fd, buf, sizeof(buf));
	close(fd);
	if (err < 0) {
		eprintf("%s %d\n", path, errno);
		return NULL;
	}

	p = strchr(buf, '\n');
	if (p)
		*p = '\0';

	return strdup(buf);
}

static int filter(const struct dirent *dir)
{
	return strcmp(dir->d_name, ".") && strcmp(dir->d_name, "..");
}

int dl_init(void)
{
	int i, nr, idx;
	char path[PATH_MAX], *p;
	struct dirent **namelist;
	struct driver_info *di;

	nr = scandir(TGT_TYPE_SYSFSDIR, &namelist, filter, alphasort);
	for (i = 0; i < nr; i++) {
		for (p = namelist[i]->d_name; !isdigit((int) *p); p++)
			;
		idx = atoi(p);
		if (idx > MAX_DL_HANDLES) {
			eprintf("Cannot load %s %d\n",
				namelist[i]->d_name, idx);
			continue;
		}

		p = dlname(namelist[i]->d_name, "name");
		if (!p)
			continue;

		di = &dinfo[idx];

		di->name = p;
		snprintf(path, sizeof(path), "%s.so", p);
		di->dl = dlopen(path, RTLD_LAZY);
		if (!di->dl) {
			eprintf("%s %s\n", path, dlerror());
			continue;
		}

		p = dlname(namelist[i]->d_name, "protocol");
		if (!p)
			continue;
		di->proto = p;
		snprintf(path, sizeof(path), "%s.so", p);
		di->pdl = dlopen(path, RTLD_LAZY);
		if (!di->pdl) {
			eprintf("%s %s\n", path, dlerror());
			continue;
		}
	}

	return 0;
}

void dl_config_load(void)
{
	void (* fn)(void);
	int i;

	for (i = 0; i < MAX_DL_HANDLES; i++) {
		if (!dinfo[i].dl)
			continue;

		fn = dlsym(dinfo[i].dl, "initial_config_load");
		if (!fn)
			eprintf("%s\n", dlerror());
		else
			fn();
	}
}

void *dl_poll_init_fn(int idx)
{
	if (dinfo[idx].dl)
		return dlsym(dinfo[idx].dl, "poll_init");
	return NULL;
}

void *dl_poll_fn(int idx)
{
	if (dinfo[idx].dl)
		return dlsym(dinfo[idx].dl, "poll_event");
	return NULL;
}

void *dl_ipc_fn(char *name)
{
	int idx = driver_find_by_name(name);

	if (idx < 0) {
		eprintf("%d %s\n", idx, name);
		return NULL;
	}

	if (dinfo[idx].dl)
		return dlsym(dinfo[idx].dl, "ipc_mgmt");

	return NULL;
}

void *dl_proto_cmd_process(int tid, int typeid)
{
	if (dinfo[typeid].pdl)
		return dlsym(dinfo[typeid].pdl, "cmd_process");

	return NULL;
}

void *dl_event_fn(int tid, int typeid)
{
	if (dinfo[typeid].dl)
		return dlsym(dinfo[typeid].dl, "async_event");

	return NULL;
}
