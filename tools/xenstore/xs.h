/* 
    Xen Store Daemon providing simple tree-like database.
    Copyright (C) 2005 Rusty Russell IBM Corporation

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef _XS_H
#define _XS_H

#include "xs_lib.h"

struct xs_handle;

/* On failure, these routines set errno. */

/* Connect to the xs daemon.
 * Returns a handle or NULL.
 */
struct xs_handle *xs_daemon_open(void);

/* Connect to the xs daemon (readonly for non-root clients).
 * Returns a handle or NULL.
 */
struct xs_handle *xs_daemon_open_readonly(void);

/* Close the connection to the xs daemon. */
void xs_daemon_close(struct xs_handle *);

/* Get contents of a directory.
 * Returns a malloced array: call free() on it after use.
 * Num indicates size.
 */
char **xs_directory(struct xs_handle *h, const char *path, unsigned int *num);

/* Get the value of a single file, nul terminated.
 * Returns a malloced value: call free() on it after use.
 * len indicates length in bytes, not including terminator.
 */
void *xs_read(struct xs_handle *h, const char *path, unsigned int *len);

/* Write the value of a single file.
 * Returns false on failure.  createflags can be 0, O_CREAT, or O_CREAT|O_EXCL.
 */
bool xs_write(struct xs_handle *h, const char *path, const void *data,
	      unsigned int len, int createflags);

/* Create a new directory.
 * Returns false on failure.
 */
bool xs_mkdir(struct xs_handle *h, const char *path);

/* Destroy a file or directory (and children).
 * Returns false on failure.
 */
bool xs_rm(struct xs_handle *h, const char *path);

/* Get permissions of node (first element is owner, first perms is "other").
 * Returns malloced array, or NULL: call free() after use.
 */
struct xs_permissions *xs_get_permissions(struct xs_handle *h,
					  const char *path, unsigned int *num);

/* Set permissions of node (must be owner).
 * Returns false on failure.
 */
bool xs_set_permissions(struct xs_handle *h, const char *path,
			struct xs_permissions *perms, unsigned int num_perms);

/* Watch a node for changes (poll on fd to detect, or call read_watch()).
 * When the node (or any child) changes, fd will become readable.
 * Token is returned when watch is read, to allow matching.
 * Priority indicates order if multiple watchers: higher is first.
 * Returns false on failure.
 */
bool xs_watch(struct xs_handle *h, const char *path, const char *token,
	      unsigned int priority);

/* Return the FD to poll on to see if a watch has fired. */
int xs_fileno(struct xs_handle *h);

/* Find out what node change was on (will block if nothing pending).
 * Returns array of two pointers: path and token, or NULL.
 * Call free() after use.
 */
char **xs_read_watch(struct xs_handle *h);

/* Acknowledge watch on node.  Watches must be acknowledged before
 * any other watches can be read.
 * Returns false on failure.
 */
bool xs_acknowledge_watch(struct xs_handle *h, const char *token);

/* Remove a watch on a node: implicitly acks any outstanding watch.
 * Returns false on failure (no watch on that node).
 */
bool xs_unwatch(struct xs_handle *h, const char *path, const char *token);

/* Start a transaction: changes by others will not be seen during this
 * transaction, and changes will not be visible to others until end.
 * Transaction only applies to the given subtree.
 * You can only have one transaction at any time.
 * Returns false on failure.
 */
bool xs_transaction_start(struct xs_handle *h, const char *subtree);

/* End a transaction.
 * If abandon is true, transaction is discarded instead of committed.
 * Returns false on failure, which indicates an error: transactions will
 * not fail spuriously.
 */
bool xs_transaction_end(struct xs_handle *h, bool abort);

/* Introduce a new domain.
 * This tells the store daemon about a shared memory page, event channel
 * and store path associated with a domain: the domain uses these to communicate.
 */
bool xs_introduce_domain(struct xs_handle *h, domid_t domid, unsigned long mfn,
                         unsigned int eventchn, const char *path);

/* Release a domain.
 * Tells the store domain to release the memory page to the domain.
 */
bool xs_release_domain(struct xs_handle *h, domid_t domid);

/* Only useful for DEBUG versions */
char *xs_debug_command(struct xs_handle *h, const char *cmd,
		       void *data, unsigned int len);

/* Shut down the daemon. */
bool xs_shutdown(struct xs_handle *h);

#endif /* _XS_H */
