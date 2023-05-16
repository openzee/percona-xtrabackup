/******************************************************
Copyright (c) 2013 Percona LLC and/or its affiliates.

Local datasink implementation for XtraBackup.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA

*******************************************************/

#include <my_base.h>
#include <mysys_err.h>
#include "common.h"
#include "datasink.h"

int xtrabackup_target_socket_fd = -1;
// pthread_mutex_t sockfd_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
	File fd;
} ds_stdout_file_t;

static ds_ctxt_t *stdout_init(const char *root);
static ds_file_t *stdout_open(ds_ctxt_t *ctxt, const char *path,
			     MY_STAT *mystat);
static int stdout_write(ds_file_t *file, const void *buf, size_t len);
static int stdout_close(ds_file_t *file);
static void stdout_deinit(ds_ctxt_t *ctxt);

datasink_t datasink_stdout = {
	&stdout_init,
	&stdout_open,
	&stdout_write,
	&stdout_close,
	&stdout_deinit
};

static
ds_ctxt_t *
stdout_init(const char *root)
{
	ds_ctxt_t *ctxt;

	ctxt = my_malloc(PSI_NOT_INSTRUMENTED, sizeof(ds_ctxt_t), MYF(MY_FAE));

	ctxt->root = my_strdup(PSI_NOT_INSTRUMENTED, root, MYF(MY_FAE));

    msg_ts( "stdout_init\n" );
	return ctxt;
}

static
ds_file_t *
stdout_open(ds_ctxt_t *ctxt __attribute__((unused)),
	    const char *path __attribute__((unused)),
	    MY_STAT *mystat __attribute__((unused)))
{
	ds_stdout_file_t 	*stdout_file;
	ds_file_t		*file;
	size_t			pathlen;
	const char		*fullpath = "<STDOUT>";

	pathlen = strlen(fullpath) + 1;

	file = (ds_file_t *) my_malloc(PSI_NOT_INSTRUMENTED,
				       sizeof(ds_file_t) +
				       sizeof(ds_stdout_file_t) +
				       pathlen,
				       MYF(MY_FAE));
	stdout_file = (ds_stdout_file_t *) (file + 1);


#ifdef __WIN__
	setmode(fileno(stdout), _O_BINARY);
#endif

	stdout_file->fd = fileno(stdout);

	file->path = (char *) stdout_file + sizeof(ds_stdout_file_t);
	memcpy(file->path, fullpath, pathlen);

	file->ptr = stdout_file;

	return file;
}

int send_all( int fd, const char * buf, size_t len ){

	while( len > 0 ){
		ssize_t n = send( fd, buf, len, 0 );
		if( n == -1 ){
			msg( "send fails errno:%d\n", errno );
			exit(1);
			return -1;
		}

		len -= n;
		buf += n;
	}

	return 0;
}

#if 1
// success:0 fails: 1
static
int
stdout_write(ds_file_t *file, const void *buf, size_t len)
{
	// msg( "stdout_write fd: %d thread:%ld\n", fd, pthread_self() );

	if( xtrabackup_target_socket_fd > 0 ){
		char chunked_header[32] = {0};

		// pthread_mutex_lock( &sockfd_mutex );
		// int err_no = pthread_mutex_trylock( &sockfd_mutex );
		// if( err_no != 0 ){
		// 	msg( "trylock fails. err:%d\n", err_no );
		// 	exit(1);
		// }

		snprintf( chunked_header, sizeof(chunked_header), "%lx\r\n", len );
		send_all( xtrabackup_target_socket_fd, chunked_header, strlen(chunked_header) );
		// msg( "header:[%s]\n", chunked_header );
		send_all( xtrabackup_target_socket_fd, buf, len );
		send_all( xtrabackup_target_socket_fd, "\r\n", 2 );
		// pthread_mutex_unlock( &sockfd_mutex );

		return 0;
	}else{
		File fd = ((ds_stdout_file_t *) file->ptr)->fd;

		if (!my_write(fd, buf, len, MYF(MY_WME | MY_NABP))) {
			posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
			return 0;
		}

		return 1;
	}

	return 0;
	// File fd = ((ds_stdout_file_t *) file->ptr)->fd;

	// if( xtrabackup_target_socket_fd > 0 ){
	// 	char buf[ 16 ];
	// 	snprintf( buf, sizeof(buf), "%lx\r\n", len );
	// 	msg( "chunked: len %lx %ld\n", len, len );
	// 	my_write( xtrabackup_target_socket_fd, (uchar*)buf, strlen(buf), MYF(MY_WME | MY_NABP) );
	// }

	// //if (!my_write(fd, buf, len, MYF(MY_WME | MY_NABP))) {
	// if( ! my_write(xtrabackup_target_socket_fd > 0 ? xtrabackup_target_socket_fd: fd , buf, len, MYF(MY_WME | MY_NABP)) ){
	// 	msg( "chunked send fails\n" );
    //     return 0; 
    // }

	// if( ! my_write( xtrabackup_target_socket_fd, (uchar*)"\r\n", 2, MYF(MY_WME | MY_NABP) ) ){
	// 	msg( "chunked end send fails\n" );
	// }

    // // close( xtrabackup_target_socket_fd );

	// return 1;
}
#else
static
int
stdout_write(ds_file_t *file, const void *buf, size_t len)
{
	File fd = ((ds_stdout_file_t *) file->ptr)->fd;

	if (!my_write(fd, buf, len, MYF(MY_WME | MY_NABP))) {
		posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
		return 0;
	}

	return 1;
}
#endif

static
int
stdout_close(ds_file_t *file)
{
	my_free(file);

	return 0;
}

static
void
stdout_deinit(ds_ctxt_t *ctxt)
{
	my_free(ctxt->root);
	my_free(ctxt);
}
