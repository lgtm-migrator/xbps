/*-
 * Copyright (c) 2009-2013 Juan Romero Pardines.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "xbps_api_impl.h"

/**
 * @file lib/rpool_get.c
 * @brief Repository pool functions
 * @defgroup repopool Repository pool functions
 */
struct rpool_fpkg {
	prop_dictionary_t pkgd;
	const char *pattern;
	const char *bestpkgver;
	bool best;
};

static int
find_virtualpkg_cb(struct xbps_repo *repo, void *arg, bool *done)
{
	struct rpool_fpkg *rpf = arg;

	rpf->pkgd = xbps_repo_get_virtualpkg(repo, rpf->pattern);
	if (rpf->pkgd) {
		/* found */
		*done = true;
		return 0;
	}
	/* not found */
	return 0;
}

static int
find_pkg_cb(struct xbps_repo *repo, void *arg, bool *done)
{
	struct rpool_fpkg *rpf = arg;

	rpf->pkgd = xbps_repo_get_pkg(repo, rpf->pattern);
	if (rpf->pkgd) {
		/* found */
		*done = true;
		return 0;
	}
	/* Not found */
	return 0;
}

static int
find_best_pkg_cb(struct xbps_repo *repo, void *arg, bool *done)
{
	struct rpool_fpkg *rpf = arg;
	prop_dictionary_t pkgd;
	const char *repopkgver;

	(void)done;

	pkgd = xbps_repo_get_pkg(repo, rpf->pattern);
	if (pkgd == NULL) {
		if (errno && errno != ENOENT)
			return errno;

		xbps_dbg_printf(repo->xhp,
		    "[rpool] Package '%s' not found in repository "
		    "'%s'.\n", rpf->pattern, repo->uri);
		return 0;
	}
	prop_dictionary_get_cstring_nocopy(pkgd,
	    "pkgver", &repopkgver);
	if (rpf->bestpkgver == NULL) {
		xbps_dbg_printf(repo->xhp,
		    "[rpool] Found best match '%s' (%s).\n",
		    repopkgver, repo->uri);
		rpf->pkgd = pkgd;
		prop_dictionary_set_cstring_nocopy(rpf->pkgd,
		    "repository", repo->uri);
		rpf->bestpkgver = repopkgver;
		return 0;
	}
	/*
	 * Compare current stored version against new
	 * version from current package in repository.
	 */
	if (xbps_cmpver(repopkgver, rpf->bestpkgver) == 1) {
		xbps_dbg_printf(repo->xhp,
		    "[rpool] Found best match '%s' (%s).\n",
		    repopkgver, repo->uri);
		rpf->pkgd = pkgd;
		prop_dictionary_set_cstring_nocopy(rpf->pkgd,
		    "repository", repo->uri);
		rpf->bestpkgver = repopkgver;
	}
	return 0;
}

typedef enum {
	BEST_PKG = 1,
	VIRTUAL_PKG,
	REAL_PKG
} pkg_repo_type_t;

static prop_dictionary_t
repo_find_pkg(struct xbps_handle *xhp,
	      const char *pkg,
	      pkg_repo_type_t type)
{
	struct rpool_fpkg rpf;
	int rv = 0;

	rpf.pattern = pkg;
	rpf.pkgd = NULL;
	rpf.bestpkgver = NULL;

	switch (type) {
	case BEST_PKG:
		/*
		 * Find best pkg version.
		 */
		rv = xbps_rpool_foreach(xhp, find_best_pkg_cb, &rpf);
		break;
	case VIRTUAL_PKG:
		/*
		 * Find virtual pkg.
		 */
		rv = xbps_rpool_foreach(xhp, find_virtualpkg_cb, &rpf);
		break;
	case REAL_PKG:
		/*
		 * Find real pkg.
		 */
		rv = xbps_rpool_foreach(xhp, find_pkg_cb, &rpf);
		break;
	}
	if (rv != 0) {
		errno = rv;
		return NULL;
	}

	return rpf.pkgd;
}

prop_dictionary_t
xbps_rpool_get_virtualpkg(struct xbps_handle *xhp, const char *pkg)
{
	assert(xhp);
	assert(pkg);

	return repo_find_pkg(xhp, pkg, VIRTUAL_PKG);
}

prop_dictionary_t
xbps_rpool_get_pkg(struct xbps_handle *xhp, const char *pkg)
{
	assert(xhp);
	assert(pkg);

	if (!xbps_pkgpattern_version(pkg) && !xbps_pkg_version(pkg))
		return repo_find_pkg(xhp, pkg, BEST_PKG);

	return repo_find_pkg(xhp, pkg, REAL_PKG);
}

prop_dictionary_t
xbps_rpool_get_pkg_plist(struct xbps_handle *xhp,
			 const char *pkg,
			 const char *plistf)
{
	prop_dictionary_t pkgd = NULL, plistd = NULL;
	const char *repo;
	char *url;

	assert(pkg != NULL);
	assert(plistf != NULL);
	/*
	 * Iterate over the the repository pool and search for a plist file
	 * in the binary package matching `pattern'. The plist file will be
	 * internalized to a proplib dictionary.
	 *
	 * The first repository that has it wins and the loop is stopped.
	 * This will work locally and remotely, thanks to libarchive and
	 * libfetch!
	 */
	if (((pkgd = xbps_rpool_get_pkg(xhp, pkg)) == NULL) &&
	    ((pkgd = xbps_rpool_get_virtualpkg(xhp, pkg)) == NULL))
		goto out;

	url = xbps_repository_pkg_path(xhp, pkgd);
	if (url == NULL) {
		errno = EINVAL;
		goto out;
	}
	plistd = xbps_get_pkg_plist_from_binpkg(url, plistf);
	if (plistd) {
		prop_dictionary_get_cstring_nocopy(pkgd, "repository", &repo);
		prop_dictionary_set_cstring_nocopy(plistd, "repository", repo);
	}
	free(url);

out:
	if (plistd == NULL)
		errno = ENOENT;

	return plistd;
}
