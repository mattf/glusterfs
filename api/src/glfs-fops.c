/*
  Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include "glfs-internal.h"
#include "glfs-mem-types.h"
#include "syncop.h"
#include "glfs.h"


struct glfs_fd *
glfs_open (struct glfs *fs, const char *path, int flags)
{
	int              ret = -1;
	struct glfs_fd  *glfd = NULL;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	glfd = GF_CALLOC (1, sizeof (*glfd), glfs_mt_glfs_fd_t);
	if (!glfd)
		goto out;

	ret = glfs_resolve (fs, subvol, path, &loc, &iatt);
	if (ret)
		goto out;

	if (IA_ISDIR (iatt.ia_type)) {
		ret = -1;
		errno = EISDIR;
		goto out;
	}

	if (!IA_ISREG (iatt.ia_type)) {
		ret = -1;
		errno = EINVAL;
		goto out;
	}

	glfd->fd = fd_create (loc.inode, getpid());
	if (!glfd->fd) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = syncop_open (subvol, &loc, flags, glfd->fd);
out:
	loc_wipe (&loc);

	if (ret && glfd) {
		glfs_fd_destroy (glfd);
		glfd = NULL;
	}

	return glfd;
}


int
glfs_close (struct glfs_fd *glfd)
{
	xlator_t  *subvol = NULL;
	int        ret = -1;

	__glfs_entry_fd (glfd);

	subvol = glfs_fd_subvol (glfd);

	ret = syncop_flush (subvol, glfd->fd);

	glfs_fd_destroy (glfd);

	return ret;
}


int
glfs_lstat (struct glfs *fs, const char *path, struct stat *stat)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = glfs_lresolve (fs, subvol, path, &loc, &iatt);

	if (ret == 0 && stat)
		iatt_to_stat (&iatt, stat);
out:
	loc_wipe (&loc);

	return ret;
}


int
glfs_stat (struct glfs *fs, const char *path, struct stat *stat)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = glfs_resolve (fs, subvol, path, &loc, &iatt);

	if (ret == 0 && stat)
		iatt_to_stat (&iatt, stat);
out:
	loc_wipe (&loc);

	return ret;
}


int
glfs_fstat (struct glfs_fd *glfd, struct stat *stat)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	struct iatt      iatt = {0, };

	__glfs_entry_fd (glfd);

	subvol = glfs_fd_subvol (glfd);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = syncop_fstat (subvol, glfd->fd, &iatt);

	if (ret == 0 && stat)
		iatt_to_stat (&iatt, stat);
out:
	return ret;
}


struct glfs_fd *
glfs_creat (struct glfs *fs, const char *path, int flags, mode_t mode)
{
	int              ret = -1;
	struct glfs_fd  *glfd = NULL;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	uuid_t           gfid;
	dict_t          *xattr_req = NULL;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	xattr_req = dict_new ();
	if (!xattr_req) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	uuid_generate (gfid);
	ret = dict_set_static_bin (xattr_req, "gfid-req", gfid, 16);
	if (ret) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	glfd = GF_CALLOC (1, sizeof (*glfd), glfs_mt_glfs_fd_t);
	if (!glfd)
		goto out;

	/* This must be glfs_resolve() and NOT glfs_lresolve().
	   That is because open("name", O_CREAT) where "name"
	   is a danging symlink must create the dangling
	   destinataion.
	*/
	ret = glfs_resolve (fs, subvol, path, &loc, &iatt);
	if (ret == -1 && errno != ENOENT)
		/* Any other type of error is fatal */
		goto out;

	if (ret == -1 && errno == ENOENT && !loc.parent)
		/* The parent directory or an ancestor even
		   higher does not exist
		*/
		goto out;

	if (loc.inode) {
		if (flags & O_EXCL) {
			ret = -1;
			errno = EEXIST;
			goto out;
		}

		if (IA_ISDIR (iatt.ia_type)) {
			ret = -1;
			errno = EISDIR;
			goto out;
		}

		if (!IA_ISREG (iatt.ia_type)) {
			ret = -1;
			errno = EINVAL;
			goto out;
		}
	}

	if (ret == -1 && errno == ENOENT) {
		loc.inode = inode_new (loc.parent->table);
		if (!loc.inode) {
			ret = -1;
			errno = ENOMEM;
			goto out;
		}
	}

	glfd->fd = fd_create (loc.inode, getpid());
	if (!glfd->fd) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = syncop_create (subvol, &loc, flags, mode, glfd->fd, xattr_req);
out:
	loc_wipe (&loc);

	if (xattr_req)
		dict_destroy (xattr_req);

	if (ret && glfd) {
		glfs_fd_destroy (glfd);
		glfd = NULL;
	}

	return glfd;
}


off_t
glfs_lseek (struct glfs_fd *glfd, off_t offset, int whence)
{
	struct stat sb = {0, };
	int         ret = -1;

	__glfs_entry_fd (glfd);

	switch (whence) {
	case SEEK_SET:
		glfd->offset = offset;
		break;
	case SEEK_CUR:
		glfd->offset += offset;
		break;
	case SEEK_END:
		ret = glfs_fstat (glfd, &sb);
		if (ret) {
			/* seek cannot fail :O */
			break;
		}
		glfd->offset = sb.st_size + offset;
		break;
	}

	return glfd->offset;
}


//////////////

ssize_t
glfs_preadv (struct glfs_fd *glfd, const struct iovec *iovec, int iovcnt,
	     off_t offset, int flags)
{
	xlator_t       *subvol = NULL;
	int             ret = -1;
	size_t          size = -1;
	struct iovec   *iov = NULL;
	int             cnt = 0;
	struct iobref  *iobref = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_fd_subvol (glfd);

	size = iov_length (iovec, iovcnt);

	ret = syncop_readv (subvol, glfd->fd, size, offset,
			    0, &iov, &cnt, &iobref);
	if (ret <= 0)
		return ret;

	size = iov_copy (iovec, iovcnt, iov, cnt); /* FIXME!!! */

	glfd->offset = (offset + size);

	if (iov)
		GF_FREE (iov);
	if (iobref)
		iobref_unref (iobref);

	return size;
}


ssize_t
glfs_read (struct glfs_fd *glfd, void *buf, size_t count, int flags)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = buf;
	iov.iov_len = count;

	ret = glfs_preadv (glfd, &iov, 1, glfd->offset, flags);

	return ret;
}


ssize_t
glfs_pread (struct glfs_fd *glfd, void *buf, size_t count, off_t offset,
	    int flags)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = buf;
	iov.iov_len = count;

	ret = glfs_preadv (glfd, &iov, 1, offset, flags);

	return ret;
}


ssize_t
glfs_readv (struct glfs_fd *glfd, const struct iovec *iov, int count,
	    int flags)
{
	ssize_t      ret = 0;

	ret = glfs_preadv (glfd, iov, count, glfd->offset, flags);

	return ret;
}


struct glfs_io {
	struct glfs_fd      *glfd;
	int                  op;
	off_t                offset;
	struct iovec        *iov;
	int                  count;
	int                  flags;
	glfs_io_cbk          fn;
	void                *data;
};


static int
glfs_io_async_cbk (int ret, call_frame_t *frame, void *data)
{
	struct glfs_io  *gio = data;

	gio->fn (gio->glfd, ret, gio->data);

	GF_FREE (gio->iov);
	GF_FREE (gio);

	return 0;
}


static int
glfs_io_async_task (void *data)
{
	struct glfs_io *gio = data;
	ssize_t         ret = 0;

	switch (gio->op) {
	case GF_FOP_READ:
		ret = glfs_preadv (gio->glfd, gio->iov, gio->count,
				   gio->offset, gio->flags);
		break;
	case GF_FOP_WRITE:
		ret = glfs_pwritev (gio->glfd, gio->iov, gio->count,
				    gio->offset, gio->flags);
		break;
	case GF_FOP_FTRUNCATE:
		ret = glfs_ftruncate (gio->glfd, gio->offset);
		break;
	case GF_FOP_FSYNC:
		if (gio->flags)
			ret = glfs_fdatasync (gio->glfd);
		else
			ret = glfs_fsync (gio->glfd);
		break;
	}

	return (int) ret;
}


int
glfs_preadv_async (struct glfs_fd *glfd, const struct iovec *iovec, int count,
		   off_t offset, int flags, glfs_io_cbk fn, void *data)
{
	struct glfs_io *gio = NULL;
	int             ret = 0;

	gio = GF_CALLOC (1, sizeof (*gio), glfs_mt_glfs_io_t);
	if (!gio) {
		errno = ENOMEM;
		return -1;
	}

	gio->iov = iov_dup (iovec, count);
	if (!gio->iov) {
		GF_FREE (gio);
		errno = ENOMEM;
		return -1;
	}

	gio->op     = GF_FOP_READ;
	gio->glfd   = glfd;
	gio->count  = count;
	gio->offset = offset;
	gio->flags  = flags;
	gio->fn     = fn;
	gio->data   = data;

	ret = synctask_new (glfs_from_glfd (glfd)->ctx->env,
			    glfs_io_async_task, glfs_io_async_cbk,
			    NULL, gio);

	if (ret) {
		GF_FREE (gio->iov);
		GF_FREE (gio);
	}

	return ret;
}


int
glfs_read_async (struct glfs_fd *glfd, void *buf, size_t count, int flags,
		 glfs_io_cbk fn, void *data)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = buf;
	iov.iov_len = count;

	ret = glfs_preadv_async (glfd, &iov, 1, glfd->offset, flags, fn, data);

	return ret;
}


int
glfs_pread_async (struct glfs_fd *glfd, void *buf, size_t count, off_t offset,
		  int flags, glfs_io_cbk fn, void *data)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = buf;
	iov.iov_len = count;

	ret = glfs_preadv_async (glfd, &iov, 1, offset, flags, fn, data);

	return ret;
}


int
glfs_readv_async (struct glfs_fd *glfd, const struct iovec *iov, int count,
		  int flags, glfs_io_cbk fn, void *data)
{
	ssize_t      ret = 0;

	ret = glfs_preadv_async (glfd, iov, count, glfd->offset, flags,
				 fn, data);
	return ret;
}

///// writev /////

ssize_t
glfs_pwritev (struct glfs_fd *glfd, const struct iovec *iovec, int iovcnt,
	      off_t offset, int flags)
{
	xlator_t       *subvol = NULL;
	int             ret = -1;
	size_t          size = -1;
	struct iobref  *iobref = NULL;
	struct iobuf   *iobuf = NULL;
	struct iovec    iov = {0, };

	__glfs_entry_fd (glfd);

	subvol = glfs_fd_subvol (glfd);

	size = iov_length (iovec, iovcnt);

	iobuf = iobuf_get2 (subvol->ctx->iobuf_pool, size);
	if (!iobuf) {
		errno = ENOMEM;
		return -1;
	}

	iobref = iobref_new ();
	if (!iobref) {
		iobuf_unref (iobuf);
		errno = ENOMEM;
		return -1;
	}

	ret = iobref_add (iobref, iobuf);
	if (ret) {
		iobuf_unref (iobuf);
		iobref_unref (iobref);
		errno = ENOMEM;
		return -1;
	}

	iov_unload (iobuf_ptr (iobuf), iovec, iovcnt);  /* FIXME!!! */

	iov.iov_base = iobuf_ptr (iobuf);
	iov.iov_len = size;

	ret = syncop_writev (subvol, glfd->fd, &iov, 1, offset,
			     iobref, flags);

	iobuf_unref (iobuf);
	iobref_unref (iobref);

	if (ret <= 0)
		return ret;

	glfd->offset = (offset + size);

	return ret;
}


ssize_t
glfs_write (struct glfs_fd *glfd, const void *buf, size_t count, int flags)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = (void *) buf;
	iov.iov_len = count;

	ret = glfs_pwritev (glfd, &iov, 1, glfd->offset, flags);

	return ret;
}



ssize_t
glfs_writev (struct glfs_fd *glfd, const struct iovec *iov, int count,
	     int flags)
{
	ssize_t      ret = 0;

	ret = glfs_pwritev (glfd, iov, count, glfd->offset, flags);

	return ret;
}


ssize_t
glfs_pwrite (struct glfs_fd *glfd, const void *buf, size_t count, off_t offset,
	     int flags)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = (void *) buf;
	iov.iov_len = count;

	ret = glfs_pwritev (glfd, &iov, 1, offset, flags);

	return ret;
}


int
glfs_pwritev_async (struct glfs_fd *glfd, const struct iovec *iovec, int count,
		    off_t offset, int flags, glfs_io_cbk fn, void *data)
{
	struct glfs_io *gio = NULL;
	int             ret = 0;

	gio = GF_CALLOC (1, sizeof (*gio), glfs_mt_glfs_io_t);
	if (!gio) {
		errno = ENOMEM;
		return -1;
	}

	gio->iov = iov_dup (iovec, count);
	if (!gio->iov) {
		GF_FREE (gio);
		errno = ENOMEM;
		return -1;
	}

	gio->op     = GF_FOP_WRITE;
	gio->glfd   = glfd;
	gio->count  = count;
	gio->offset = offset;
	gio->flags  = flags;
	gio->fn     = fn;
	gio->data   = data;

	ret = synctask_new (glfs_from_glfd (glfd)->ctx->env,
			    glfs_io_async_task, glfs_io_async_cbk,
			    NULL, gio);

	if (ret) {
		GF_FREE (gio->iov);
		GF_FREE (gio);
	}

	return ret;
}


int
glfs_write_async (struct glfs_fd *glfd, const void *buf, size_t count, int flags,
		  glfs_io_cbk fn, void *data)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = (void *) buf;
	iov.iov_len = count;

	ret = glfs_pwritev_async (glfd, &iov, 1, glfd->offset, flags, fn, data);

	return ret;
}


int
glfs_pwrite_async (struct glfs_fd *glfd, const void *buf, int count,
		   off_t offset, int flags, glfs_io_cbk fn, void *data)
{
	struct iovec iov = {0, };
	ssize_t      ret = 0;

	iov.iov_base = (void *) buf;
	iov.iov_len = count;

	ret = glfs_pwritev_async (glfd, &iov, 1, offset, flags, fn, data);

	return ret;
}


int
glfs_writev_async (struct glfs_fd *glfd, const struct iovec *iov, int count,
		   int flags, glfs_io_cbk fn, void *data)
{
	ssize_t      ret = 0;

	ret = glfs_pwritev_async (glfd, iov, count, glfd->offset, flags,
				  fn, data);
	return ret;
}


int
glfs_fsync (struct glfs_fd *glfd)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_fd_subvol (glfd);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = syncop_fsync (subvol, glfd->fd, 0);
out:
	return ret;
}


static int
glfs_fsync_async_common (struct glfs_fd *glfd, glfs_io_cbk fn, void *data,
			 int dataonly)
{
	struct glfs_io *gio = NULL;
	int             ret = 0;

	gio = GF_CALLOC (1, sizeof (*gio), glfs_mt_glfs_io_t);
	if (!gio) {
		errno = ENOMEM;
		return -1;
	}

	gio->op     = GF_FOP_FSYNC;
	gio->glfd   = glfd;
	gio->flags  = dataonly;
	gio->fn     = fn;
	gio->data   = data;

	ret = synctask_new (glfs_from_glfd (glfd)->ctx->env,
			    glfs_io_async_task, glfs_io_async_cbk,
			    NULL, gio);

	if (ret) {
		GF_FREE (gio->iov);
		GF_FREE (gio);
	}

	return ret;

}


int
glfs_fsync_async (struct glfs_fd *glfd, glfs_io_cbk fn, void *data)
{
	return glfs_fsync_async_common (glfd, fn, data, 0);
}


int
glfs_fdatasync (struct glfs_fd *glfd)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_fd_subvol (glfd);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = syncop_fsync (subvol, glfd->fd, 1);
out:
	return ret;
}


int
glfs_fdatasync_async (struct glfs_fd *glfd, glfs_io_cbk fn, void *data)
{
	return glfs_fsync_async_common (glfd, fn, data, 1);
}


int
glfs_ftruncate (struct glfs_fd *glfd, off_t offset)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_fd_subvol (glfd);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = syncop_ftruncate (subvol, glfd->fd, offset);
out:
	return ret;
}


int
glfs_ftruncate_async (struct glfs_fd *glfd, off_t offset,
		      glfs_io_cbk fn, void *data)
{
	struct glfs_io *gio = NULL;
	int             ret = 0;

	gio = GF_CALLOC (1, sizeof (*gio), glfs_mt_glfs_io_t);
	if (!gio) {
		errno = ENOMEM;
		return -1;
	}

	gio->op     = GF_FOP_FTRUNCATE;
	gio->glfd   = glfd;
	gio->offset = offset;
	gio->fn     = fn;
	gio->data   = data;

	ret = synctask_new (glfs_from_glfd (glfd)->ctx->env,
			    glfs_io_async_task, glfs_io_async_cbk,
			    NULL, gio);

	if (ret) {
		GF_FREE (gio->iov);
		GF_FREE (gio);
	}

	return ret;
}


int
glfs_access (struct glfs *fs, const char *path, int mode)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = glfs_resolve (fs, subvol, path, &loc, &iatt);
	if (ret)
		goto out;

	ret = syncop_access (subvol, &loc, mode);
out:
	loc_wipe (&loc);

	return ret;
}


int
glfs_symlink (struct glfs *fs, const char *data, const char *path)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	uuid_t           gfid;
	dict_t          *xattr_req = NULL;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	xattr_req = dict_new ();
	if (!xattr_req) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	uuid_generate (gfid);
	ret = dict_set_static_bin (xattr_req, "gfid-req", gfid, 16);
	if (ret) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = glfs_lresolve (fs, subvol, path, &loc, &iatt);

	if (loc.inode) {
		errno = EEXIST;
		ret = -1;
		goto out;
	}

	if (ret == -1 && errno != ENOENT)
		/* Any other type of error is fatal */
		goto out;

	if (ret == -1 && errno == ENOENT && !loc.parent)
		/* The parent directory or an ancestor even
		   higher does not exist
		*/
		goto out;

	/* ret == -1 && errno == ENOENT */
	loc.inode = inode_new (loc.parent->table);
	if (!loc.inode) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = syncop_symlink (subvol, &loc, data, xattr_req);
out:
	loc_wipe (&loc);

	if (xattr_req)
		dict_destroy (xattr_req);

	return ret;
}


int
glfs_readlink (struct glfs *fs, const char *path, char *buf, size_t bufsiz)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = glfs_lresolve (fs, subvol, path, &loc, &iatt);
	if (ret)
		goto out;

	if (iatt.ia_type != IA_IFLNK) {
		ret = -1;
		errno = EINVAL;
		goto out;
	}

	ret = syncop_readlink (subvol, &loc, &buf, bufsiz);
out:
	loc_wipe (&loc);

	return ret;
}


int
glfs_mknod (struct glfs *fs, const char *path, mode_t mode, dev_t dev)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	uuid_t           gfid;
	dict_t          *xattr_req = NULL;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	xattr_req = dict_new ();
	if (!xattr_req) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	uuid_generate (gfid);
	ret = dict_set_static_bin (xattr_req, "gfid-req", gfid, 16);
	if (ret) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = glfs_lresolve (fs, subvol, path, &loc, &iatt);

	if (loc.inode) {
		errno = EEXIST;
		ret = -1;
		goto out;
	}

	if (ret == -1 && errno != ENOENT)
		/* Any other type of error is fatal */
		goto out;

	if (ret == -1 && errno == ENOENT && !loc.parent)
		/* The parent directory or an ancestor even
		   higher does not exist
		*/
		goto out;

	/* ret == -1 && errno == ENOENT */
	loc.inode = inode_new (loc.parent->table);
	if (!loc.inode) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = syncop_mknod (subvol, &loc, mode, dev, xattr_req);
out:
	loc_wipe (&loc);

	if (xattr_req)
		dict_destroy (xattr_req);

	return ret;
}


int
glfs_mkdir (struct glfs *fs, const char *path, mode_t mode)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	uuid_t           gfid;
	dict_t          *xattr_req = NULL;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	xattr_req = dict_new ();
	if (!xattr_req) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	uuid_generate (gfid);
	ret = dict_set_static_bin (xattr_req, "gfid-req", gfid, 16);
	if (ret) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = glfs_lresolve (fs, subvol, path, &loc, &iatt);

	if (loc.inode) {
		errno = EEXIST;
		ret = -1;
		goto out;
	}

	if (ret == -1 && errno != ENOENT)
		/* Any other type of error is fatal */
		goto out;

	if (ret == -1 && errno == ENOENT && !loc.parent)
		/* The parent directory or an ancestor even
		   higher does not exist
		*/
		goto out;

	/* ret == -1 && errno == ENOENT */
	loc.inode = inode_new (loc.parent->table);
	if (!loc.inode) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = syncop_mkdir (subvol, &loc, mode, xattr_req);
out:
	loc_wipe (&loc);

	if (xattr_req)
		dict_destroy (xattr_req);

	return ret;
}


int
glfs_unlink (struct glfs *fs, const char *path)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = glfs_lresolve (fs, subvol, path, &loc, &iatt);
	if (ret)
		goto out;

	if (iatt.ia_type == IA_IFDIR) {
		ret = -1;
		errno = EISDIR;
		goto out;
	}

	ret = syncop_unlink (subvol, &loc);
out:
	loc_wipe (&loc);

	return ret;
}


int
glfs_rmdir (struct glfs *fs, const char *path)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = glfs_lresolve (fs, subvol, path, &loc, &iatt);
	if (ret)
		goto out;

	if (iatt.ia_type != IA_IFDIR) {
		ret = -1;
		errno = ENOTDIR;
		goto out;
	}

	ret = syncop_rmdir (subvol, &loc);
out:
	loc_wipe (&loc);

	return ret;
}


int
glfs_rename (struct glfs *fs, const char *oldpath, const char *newpath)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            oldloc = {0, };
	loc_t            newloc = {0, };
	struct iatt      oldiatt = {0, };
	struct iatt      newiatt = {0, };

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = glfs_lresolve (fs, subvol, oldpath, &oldloc, &oldiatt);
	if (ret)
		goto out;

	ret = glfs_lresolve (fs, subvol, newpath, &newloc, &newiatt);
	if (ret && errno != ENOENT && newloc.parent)
		goto out;

	if ((oldiatt.ia_type == IA_IFDIR) != (newiatt.ia_type == IA_IFDIR)) {
		/* Either both old and new must be dirs, or both must be
		   non-dirs. Else, fail.
		*/
		ret = -1;
		errno = EISDIR;
		goto out;
	}

	/* TODO: check if new or old is a prefix of the other, and fail EINVAL */

	ret = syncop_rename (subvol, &oldloc, &newloc);
out:
	loc_wipe (&oldloc);
	loc_wipe (&newloc);

	return ret;
}


int
glfs_link (struct glfs *fs, const char *oldpath, const char *newpath)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            oldloc = {0, };
	loc_t            newloc = {0, };
	struct iatt      oldiatt = {0, };
	struct iatt      newiatt = {0, };

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = glfs_lresolve (fs, subvol, oldpath, &oldloc, &oldiatt);
	if (ret)
		goto out;

	ret = glfs_lresolve (fs, subvol, newpath, &newloc, &newiatt);
	if (ret == 0) {
		ret = -1;
		errno = EEXIST;
		goto out;
	}

	if (oldiatt.ia_type == IA_IFDIR) {
		ret = -1;
		errno = EISDIR;
		goto out;
	}

	ret = syncop_link (subvol, &oldloc, &newloc);
out:
	loc_wipe (&oldloc);
	loc_wipe (&newloc);

	return ret;
}


struct glfs_fd *
glfs_opendir (struct glfs *fs, const char *path)
{
	int              ret = -1;
	struct glfs_fd  *glfd = NULL;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	glfd = GF_CALLOC (1, sizeof (*glfd), glfs_mt_glfs_fd_t);
	if (!glfd)
		goto out;
	INIT_LIST_HEAD (&glfd->entries);

	ret = glfs_resolve (fs, subvol, path, &loc, &iatt);
	if (ret)
		goto out;

	if (!IA_ISDIR (iatt.ia_type)) {
		ret = -1;
		errno = ENOTDIR;
		goto out;
	}

	glfd->fd = fd_create (loc.inode, getpid());
	if (!glfd->fd) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = syncop_opendir (subvol, &loc, glfd->fd);
out:
	loc_wipe (&loc);

	if (ret && glfd) {
		glfs_fd_destroy (glfd);
		glfd = NULL;
	}

	return glfd;
}


int
glfs_closedir (struct glfs_fd *glfd)
{
	__glfs_entry_fd (glfd);

	gf_dirent_free (list_entry (&glfd->entries, gf_dirent_t, list));

	glfs_fd_destroy (glfd);

	return 0;
}


long
glfs_telldir (struct glfs_fd *fd)
{
	return fd->offset;
}


void
glfs_seekdir (struct glfs_fd *fd, long offset)
{
	gf_dirent_t *entry = NULL;
	gf_dirent_t *tmp = NULL;

	if (fd->offset == offset)
		return;

	fd->offset = offset;
	fd->next = NULL;

	list_for_each_entry_safe (entry, tmp, &fd->entries, list) {
		if (entry->d_off != offset)
			continue;

		if (&tmp->list != &fd->entries) {
			/* found! */
			fd->next = tmp;
			return;
		}
	}
	/* could not find entry at requested offset in the cache.
	   next readdir_r() will result in glfd_entry_refresh()
	*/
}


void
gf_dirent_to_dirent (gf_dirent_t *gf_dirent, struct dirent *dirent)
{
	dirent->d_ino = gf_dirent->d_ino;

#ifdef _DIRENT_HAVE_D_OFF
	dirent->d_off = gf_dirent->d_off;
#endif

#ifdef _DIRENT_HAVE_D_TYPE
	dirent->d_type = gf_dirent->d_type;
#endif

#ifdef _DIRENT_HAVE_D_NAMLEN
	dirent->d_namlen = strlen (gf_dirent->d_name);
#endif

	strncpy (dirent->d_name, gf_dirent->d_name, 256);
}


int
glfd_entry_refresh (struct glfs_fd *glfd)
{
	xlator_t        *subvol = NULL;
	gf_dirent_t      entries;
	gf_dirent_t      old;
	int              ret = -1;

	subvol = glfs_fd_subvol (glfd);
	if (!subvol) {
		errno = EIO;
		return -1;
	}

	INIT_LIST_HEAD (&entries.list);
	INIT_LIST_HEAD (&old.list);

	ret = syncop_readdir (subvol, glfd->fd, 131072, glfd->offset,
			      &entries);
	if (ret >= 0) {
		/* spurious errno is dangerous for glfd_entry_next() */
		errno = 0;

		list_splice_init (&glfd->entries, &old.list);
		list_splice_init (&entries.list, &glfd->entries);
	}

	if (ret > 0)
		glfd->next = list_entry (glfd->entries.next, gf_dirent_t, list);

	gf_dirent_free (&old);

	return ret;
}


gf_dirent_t *
glfd_entry_next (struct glfs_fd *glfd)
{
	gf_dirent_t     *entry = NULL;
	int              ret = -1;

	if (!glfd->offset || !glfd->next) {
		ret = glfd_entry_refresh (glfd);
		if (ret < 0)
			return NULL;
	}

	entry = glfd->next;
	if (!entry)
		return NULL;

	if (&entry->next->list == &glfd->entries)
		glfd->next = NULL;
	else
		glfd->next = entry->next;

	glfd->offset = entry->d_off;

	return entry;
}


int
glfs_readdir_r (struct glfs_fd *glfd, struct dirent *buf, struct dirent **res)
{
	int              ret = 0;
	gf_dirent_t     *entry = NULL;

	__glfs_entry_fd (glfd);

	if (glfd->fd->inode->ia_type != IA_IFDIR) {
		ret = -1;
		errno = EBADF;
		goto out;
	}

	errno = 0;
	entry = glfd_entry_next (glfd);
	if (errno)
		ret = -1;

	if (res) {
		if (entry)
			*res = buf;
		else
			*res = NULL;
	}

	if (entry)
		gf_dirent_to_dirent (entry, buf);
out:
	return ret;
}


int
glfs_statvfs (struct glfs *fs, const char *path, struct statvfs *buf)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = glfs_resolve (fs, subvol, path, &loc, &iatt);
	if (ret)
		goto out;

	ret = syncop_statfs (subvol, &loc, buf);
out:
	loc_wipe (&loc);

	return ret;
}


int
glfs_setattr (struct glfs *fs, const char *path, struct iatt *iatt,
	      int valid, int follow)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      riatt = {0, };

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	if (follow)
		ret = glfs_resolve (fs, subvol, path, &loc, &riatt);
	else
		ret = glfs_lresolve (fs, subvol, path, &loc, &riatt);

	if (ret)
		goto out;

	ret = syncop_setattr (subvol, &loc, iatt, valid, 0, 0);
out:
	loc_wipe (&loc);

	return ret;
}


int
glfs_fsetattr (struct glfs_fd *glfd, struct iatt *iatt, int valid)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_fd_subvol (glfd);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = syncop_fsetattr (subvol, glfd->fd, iatt, valid, 0, 0);
out:
	return ret;
}


int
glfs_chmod (struct glfs *fs, const char *path, mode_t mode)
{
	int              ret = -1;
	struct iatt      iatt = {0, };
	int              valid = 0;

	iatt.ia_prot = ia_prot_from_st_mode (mode);
	valid = GF_SET_ATTR_MODE;

	ret = glfs_setattr (fs, path, &iatt, valid, 1);

	return ret;
}


int
glfs_fchmod (struct glfs_fd *glfd, mode_t mode)
{
	int              ret = -1;
	struct iatt      iatt = {0, };
	int              valid = 0;

	iatt.ia_prot = ia_prot_from_st_mode (mode);
	valid = GF_SET_ATTR_MODE;

	ret = glfs_fsetattr (glfd, &iatt, valid);

	return ret;
}


int
glfs_chown (struct glfs *fs, const char *path, uid_t uid, gid_t gid)
{
	int              ret = -1;
	int              valid = 0;
	struct iatt      iatt = {0, };

	iatt.ia_uid = uid;
	iatt.ia_gid = gid;
	valid = GF_SET_ATTR_UID|GF_SET_ATTR_GID;

	ret = glfs_setattr (fs, path, &iatt, valid, 1);

	return ret;
}


int
glfs_lchown (struct glfs *fs, const char *path, uid_t uid, gid_t gid)
{
	int              ret = -1;
	int              valid = 0;
	struct iatt      iatt = {0, };

	iatt.ia_uid = uid;
	iatt.ia_gid = gid;
	valid = GF_SET_ATTR_UID|GF_SET_ATTR_GID;

	ret = glfs_setattr (fs, path, &iatt, valid, 0);

	return ret;
}


int
glfs_fchown (struct glfs_fd *glfd, uid_t uid, gid_t gid)
{
	int              ret = -1;
	int              valid = 0;
	struct iatt      iatt = {0, };

	iatt.ia_uid = uid;
	iatt.ia_gid = gid;
	valid = GF_SET_ATTR_UID|GF_SET_ATTR_GID;

	ret = glfs_fsetattr (glfd, &iatt, valid);

	return ret;
}


int
glfs_utimens (struct glfs *fs, const char *path, struct timespec times[2])
{
	int              ret = -1;
	int              valid = 0;
	struct iatt      iatt = {0, };

	iatt.ia_atime = times[0].tv_sec;
	iatt.ia_atime_nsec = times[0].tv_nsec;
	iatt.ia_mtime = times[1].tv_sec;
	iatt.ia_mtime_nsec = times[1].tv_nsec;

	valid = GF_SET_ATTR_ATIME|GF_SET_ATTR_MTIME;

	ret = glfs_setattr (fs, path, &iatt, valid, 1);

	return ret;
}


int
glfs_lutimens (struct glfs *fs, const char *path, struct timespec times[2])
{
	int              ret = -1;
	int              valid = 0;
	struct iatt      iatt = {0, };

	iatt.ia_atime = times[0].tv_sec;
	iatt.ia_atime_nsec = times[0].tv_nsec;
	iatt.ia_mtime = times[1].tv_sec;
	iatt.ia_mtime_nsec = times[1].tv_nsec;

	valid = GF_SET_ATTR_ATIME|GF_SET_ATTR_MTIME;

	ret = glfs_setattr (fs, path, &iatt, valid, 0);

	return ret;
}


int
glfs_futimens (struct glfs_fd *glfd, struct timespec times[2])
{
	int              ret = -1;
	int              valid = 0;
	struct iatt      iatt = {0, };

	iatt.ia_atime = times[0].tv_sec;
	iatt.ia_atime_nsec = times[0].tv_nsec;
	iatt.ia_mtime = times[1].tv_sec;
	iatt.ia_mtime_nsec = times[1].tv_nsec;

	valid = GF_SET_ATTR_ATIME|GF_SET_ATTR_MTIME;

	ret = glfs_fsetattr (glfd, &iatt, valid);

	return ret;
}


int
glfs_getxattr_process (void *value, size_t size, dict_t *xattr,
		       const char *name)
{
	data_t *data = NULL;
	int     ret = -1;

	data = dict_get (xattr, (char *)name);
	if (!data) {
		errno = ENODATA;
		ret = -1;
		goto out;
	}

	ret = data->len;
	if (!value || !size)
		goto out;

	if (size < ret) {
		ret = -1;
		errno = ERANGE;
		goto out;
	}

	memcpy (value, data->data, ret);
out:
	if (xattr)
		dict_unref (xattr);
	return ret;
}


ssize_t
glfs_getxattr_common (struct glfs *fs, const char *path, const char *name,
		      void *value, size_t size, int follow)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	dict_t          *xattr = NULL;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	if (follow)
		ret = glfs_resolve (fs, subvol, path, &loc, &iatt);
	else
		ret = glfs_lresolve (fs, subvol, path, &loc, &iatt);
	if (ret)
		goto out;

	ret = syncop_getxattr (subvol, &loc, &xattr, name);
	if (ret)
		goto out;

	ret = glfs_getxattr_process (value, size, xattr, name);
out:
	loc_wipe (&loc);

	return ret;
}


ssize_t
glfs_getxattr (struct glfs *fs, const char *path, const char *name,
	       void *value, size_t size)
{
	return glfs_getxattr_common (fs, path, name, value, size, 1);
}


ssize_t
glfs_lgetxattr (struct glfs *fs, const char *path, const char *name,
		void *value, size_t size)
{
	return glfs_getxattr_common (fs, path, name, value, size, 0);
}


ssize_t
glfs_fgetxattr (struct glfs_fd *glfd, const char *name, void *value,
		size_t size)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	dict_t          *xattr = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_fd_subvol (glfd);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = syncop_fgetxattr (subvol, glfd->fd, &xattr, name);
	if (ret)
		goto out;

	ret = glfs_getxattr_process (value, size, xattr, name);
out:
	return ret;
}


int
glfs_listxattr_process (void *value, size_t size, dict_t *xattr)
{
	int     ret = -1;

	ret = dict_keys_join (NULL, 0, xattr, NULL);

	if (!value || !size)
		goto out;

	if (size < ret) {
		ret = -1;
		errno = ERANGE;
		goto out;
	}

	dict_keys_join (value, size, xattr, NULL);
out:
	if (xattr)
		dict_unref (xattr);
	return ret;
}


ssize_t
glfs_listxattr_common (struct glfs *fs, const char *path, void *value,
		       size_t size, int follow)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	dict_t          *xattr = NULL;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	if (follow)
		ret = glfs_resolve (fs, subvol, path, &loc, &iatt);
	else
		ret = glfs_lresolve (fs, subvol, path, &loc, &iatt);
	if (ret)
		goto out;

	ret = syncop_getxattr (subvol, &loc, &xattr, NULL);
	if (ret)
		goto out;

	ret = glfs_listxattr_process (value, size, xattr);
out:
	loc_wipe (&loc);

	return ret;
}


ssize_t
glfs_listxattr (struct glfs *fs, const char *path, void *value, size_t size)
{
	return glfs_listxattr_common (fs, path, value, size, 1);
}


ssize_t
glfs_llistxattr (struct glfs *fs, const char *path, void *value, size_t size)
{
	return glfs_listxattr_common (fs, path, value, size, 0);
}


ssize_t
glfs_flistxattr (struct glfs_fd *glfd, void *value, size_t size)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	dict_t          *xattr = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_fd_subvol (glfd);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = syncop_fgetxattr (subvol, glfd->fd, &xattr, NULL);
	if (ret)
		goto out;

	ret = glfs_listxattr_process (value, size, xattr);
out:
	return ret;
}


dict_t *
dict_for_key_value (const char *name, const char *value, size_t size)
{
	dict_t *xattr = NULL;
	int     ret = 0;

	xattr = dict_new ();
	if (!xattr)
		return NULL;

	ret = dict_set_static_bin (xattr, (char *)name, (void *)value, size);
	if (ret) {
		dict_destroy (xattr);
		xattr = NULL;
	}

	return xattr;
}


int
glfs_setxattr_common (struct glfs *fs, const char *path, const char *name,
		      const void *value, size_t size, int flags, int follow)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };
	dict_t          *xattr = NULL;

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	if (follow)
		ret = glfs_resolve (fs, subvol, path, &loc, &iatt);
	else
		ret = glfs_lresolve (fs, subvol, path, &loc, &iatt);
	if (ret)
		goto out;

	xattr = dict_for_key_value (name, value, size);
	if (!xattr) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = syncop_setxattr (subvol, &loc, xattr, flags);
out:
	loc_wipe (&loc);
	if (xattr)
		dict_unref (xattr);

	return ret;
}


int
glfs_setxattr (struct glfs *fs, const char *path, const char *name,
	       const void *value, size_t size, int flags)
{
	return glfs_setxattr_common (fs, path, name, value, size, flags, 1);
}


int
glfs_lsetxattr (struct glfs *fs, const char *path, const char *name,
		const void *value, size_t size, int flags)
{
	return glfs_setxattr_common (fs, path, name, value, size, flags, 0);
}


int
glfs_fsetxattr (struct glfs_fd *glfd, const char *name, const void *value,
		size_t size, int flags)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	dict_t          *xattr = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_fd_subvol (glfd);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	xattr = dict_for_key_value (name, value, size);
	if (!xattr) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	ret = syncop_fsetxattr (subvol, glfd->fd, xattr, flags);
out:
	if (xattr)
		dict_unref (xattr);

	return ret;
}


int
glfs_removexattr_common (struct glfs *fs, const char *path, const char *name,
			 int follow)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;
	loc_t            loc = {0, };
	struct iatt      iatt = {0, };

	__glfs_entry_fs (fs);

	subvol = glfs_active_subvol (fs);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	if (follow)
		ret = glfs_resolve (fs, subvol, path, &loc, &iatt);
	else
		ret = glfs_lresolve (fs, subvol, path, &loc, &iatt);
	if (ret)
		goto out;

	ret = syncop_removexattr (subvol, &loc, name);
out:
	loc_wipe (&loc);

	return ret;
}


int
glfs_removexattr (struct glfs *fs, const char *path, const char *name)
{
	return glfs_removexattr_common (fs, path, name, 1);
}


int
glfs_lremovexattr (struct glfs *fs, const char *path, const char *name)
{
	return glfs_removexattr_common (fs, path, name, 0);
}


int
glfs_fremovexattr (struct glfs_fd *glfd, const char *name)
{
	int              ret = -1;
	xlator_t        *subvol = NULL;

	__glfs_entry_fd (glfd);

	subvol = glfs_fd_subvol (glfd);
	if (!subvol) {
		ret = -1;
		errno = EIO;
		goto out;
	}

	ret = syncop_fremovexattr (subvol, glfd->fd, name);
out:
	return ret;
}
