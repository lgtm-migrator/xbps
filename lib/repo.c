/*-
 * Copyright (c) 2012-2013 Juan Romero Pardines.
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
 * @file lib/repo.c
 * @brief Repository functions
 * @defgroup repo Repository functions
 */
char *
xbps_repo_path(struct xbps_handle *xhp, const char *url)
{
	assert(xhp);
	assert(url);

	return xbps_xasprintf("%s/%s-repodata",
	    url, xhp->target_arch ? xhp->target_arch : xhp->native_arch);
}

struct xbps_repo *
xbps_repo_open(struct xbps_handle *xhp, const char *url)
{
	struct xbps_repo *repo;
	const char *arch;
	char *repofile;

	assert(xhp);
	assert(url);

	if (xhp->target_arch)
		arch = xhp->target_arch;
	else
		arch = xhp->native_arch;

	if (xbps_repository_is_remote(url)) {
		/* remote repository */
		char *rpath;

		if ((rpath = xbps_get_remote_repo_string(url)) == NULL)
			return NULL;
		repofile = xbps_xasprintf("%s/%s/%s-repodata", xhp->metadir, rpath, arch);
		free(rpath);
	} else {
		/* local repository */
		repofile = xbps_repo_path(xhp, url);
	}

	repo = calloc(1, sizeof(struct xbps_repo));
	assert(repo);

	repo->xhp = xhp;
	repo->uri = url;
	repo->ar = archive_read_new();
	archive_read_support_filter_gzip(repo->ar);
	archive_read_support_format_tar(repo->ar);

	if (archive_read_open_filename(repo->ar, repofile, ARCHIVE_READ_BLOCKSIZE)) {
		xbps_dbg_printf(xhp, "cannot open repository file %s: %s\n",
				repofile, strerror(archive_errno(repo->ar)));
		archive_read_free(repo->ar);
		free(repo);
		repo = NULL;
	}
	free(repofile);
	return repo;
}

prop_dictionary_t
xbps_repo_get_plist(struct xbps_repo *repo, const char *file)
{
	prop_dictionary_t d;
	struct archive_entry *entry;
	void *buf;
	size_t buflen;
	ssize_t nbytes = -1;
	int rv;

	assert(repo);
	assert(repo->ar);
	assert(file);

	for (;;) {
		rv = archive_read_next_header(repo->ar, &entry);
		if (rv == ARCHIVE_EOF || rv == ARCHIVE_FATAL)
			break;
		else if (rv == ARCHIVE_RETRY)
			continue;
		if (strcmp(archive_entry_pathname(entry), file) == 0) {
			buflen = (size_t)archive_entry_size(entry);
			buf = malloc(buflen);
			assert(buf);
			nbytes = archive_read_data(repo->ar, buf, buflen);
			if ((size_t)nbytes != buflen) {
				free(buf);
				return NULL;
			}
			d = prop_dictionary_internalize(buf);
			free(buf);
			return d;
		}
		archive_read_data_skip(repo->ar);
	}
	return NULL;
}

void
xbps_repo_close(struct xbps_repo *repo)
{
	assert(repo);

	archive_read_free(repo->ar);
	if (prop_object_type(repo->idx) == PROP_TYPE_DICTIONARY)
		prop_object_release(repo->idx);
	if (prop_object_type(repo->idxfiles) == PROP_TYPE_DICTIONARY)
		prop_object_release(repo->idxfiles);
	free(repo);
}

prop_dictionary_t
xbps_repo_get_virtualpkg(struct xbps_repo *repo, const char *pkg)
{
	prop_dictionary_t pkgd;

	assert(repo);
	assert(repo->ar);
	assert(pkg);

	if (prop_object_type(repo->idx) != PROP_TYPE_DICTIONARY) {
		repo->idx = xbps_repo_get_plist(repo, XBPS_PKGINDEX);
		assert(repo->idx);
	}
	pkgd = xbps_find_virtualpkg_in_dict(repo->xhp, repo->idx, pkg);
	if (pkgd) {
		prop_dictionary_set_cstring_nocopy(pkgd,
				"repository", repo->uri);
		return pkgd;
	}
	return NULL;
}

prop_dictionary_t
xbps_repo_get_pkg(struct xbps_repo *repo, const char *pkg)
{
	prop_dictionary_t pkgd;

	assert(repo);
	assert(repo->ar);
	assert(pkg);

	if (prop_object_type(repo->idx) != PROP_TYPE_DICTIONARY) {
		repo->idx = xbps_repo_get_plist(repo, XBPS_PKGINDEX);
		assert(repo->idx);
	}
	pkgd = xbps_find_pkg_in_dict(repo->idx, pkg);
	if (pkgd) {
		prop_dictionary_set_cstring_nocopy(pkgd,
				"repository", repo->uri);
		return pkgd;
	}

	return NULL;
}
