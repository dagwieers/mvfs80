/* * (C) Copyright IBM Corporation 1998, 2013. */
/*
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA


 Author: IBM Corporation
 This module is part of the IBM (R) Rational (R) ClearCase (R)
 Multi-version file system (MVFS).
 For support, please visit http://www.ibm.com/software/support

*/

#include "mvfs_systm.h"
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <ks_rpc.h>
#define TBS_ACL_IMPLEMENTATION
#include "tbs_acl_kernel.h"

#include <xdr_ks.h>
#include "tbs_rpc_kernel.h"

#define ENUM_T enum_t
EZ_XDR_ROUTINE(tbs_acl_id_kind_t)
{
    if (!xdr_enum(xdrs, (ENUM_T *)objp)) {
        return (FALSE);
    }
    return (TRUE);
}

STATIC bool_t
credutl_unix_sid_to_kernel_uid(
    const credutl_sid_t *sid_p,
    MVFS_USER_ID *uid_p
)
{
    credutl_uid_t uid;
    if (sid_p->type != CREDUTL_SID_TYPE_UNIX)
        return FALSE;

    uid = credutl_sid_to_unix_uid(sid_p);
    if (uid == KS_UID_NOBODY)
        return FALSE;
    *uid_p = uid;
    return TRUE;
}

STATIC bool_t
credutl_unix_sid_to_kernel_gid(
    const credutl_sid_t *sid_p,
    MVFS_GROUP_ID *gid_p
)
{
    credutl_gid_t gid;
    if (sid_p->type != CREDUTL_SID_TYPE_UNIX)
        return FALSE;

    gid = credutl_sid_to_unix_gid(sid_p);
    if (gid == KS_UID_NOBODY)
        return FALSE;
    *gid_p = gid;
    return TRUE;
}

bool_t
credutl_sid_to_kernel_uid(
    const credutl_sid_t *sid_p,
    MVFS_USER_ID *uid_p
)
{
    return credutl_unix_sid_to_kernel_uid(sid_p, uid_p);
}

bool_t
credutl_sid_to_kernel_gid(
    const credutl_sid_t *sid_p,
    MVFS_GROUP_ID *gid_p
)
{
    return credutl_unix_sid_to_kernel_gid(sid_p, gid_p);
}

EZ_XDR_ROUTINE(tbs_sid_acl_id_t)
{
    long size;
    credutl_sid_t sid;

    if (xdrs->x_op != XDR_DECODE)
        return FALSE;
    /*
     * If decoding, decode into custom allocated structure (compatible
     * with all other ACL functions)
     */

    if (!xdr_tbs_acl_id_kind_t(xdrs, &objp->kind EZ_XDR_ARG)) {
        return (FALSE);
    }
    /* domain string (must be NULL) */
    size = -1;
    if (!xdr_long(xdrs, &size)) {
        return (FALSE);
    }
    if (size != -1)
        return FALSE;

    /* name string (must be NULL) */
    size = -1;
    if (!xdr_long(xdrs, &size)) {
        return (FALSE);
    }
    if (size != -1)
        return FALSE;

    objp->domain = NULL;
    objp->name = NULL;

    /* unpack credutl_sid into local variable */
    if (!xdr_credutl_sid_t(xdrs, &sid EZ_XDR_ARG)) {
        return (FALSE);
    }
    /* Now convert from credutl_sid form to the kernel form */
    switch (objp->kind) {
      case TBS_ACL_ID_USER:
        if (!credutl_sid_to_kernel_uid(&sid, &objp->principal_id.id_uid))
            return FALSE;
        break;

      case TBS_ACL_ID_GROUP:
        if (!credutl_sid_to_kernel_gid(&sid, &objp->principal_id.id_gid))
            return FALSE;
        break;

      default:
        BZERO(&objp->principal_id, sizeof(objp->principal_id));
        break;
    }
    return (TRUE);
}

EZ_XDR_ROUTINE(tbs_acl_permission_set_t)
{
    if (!xdr_u_long(xdrs, objp)) {
        return (FALSE);
    }
    return (TRUE);
}

EXTERN bool_t
xdr_tbs_acl_cooked_ent_t(
    XDR *xdrs,
    tbs_acl_cooked_ent_t *objp
)
{
    if (!xdr_tbs_sid_acl_id_t(xdrs, &objp->id EZ_XDR_ARG)) {
        return (FALSE);
    }
    if (!xdr_tbs_acl_permission_set_t(xdrs, &objp->perm_set EZ_XDR_ARG)) {
        return (FALSE);
    }
    return (TRUE);
}

/*
 * MVFS uses UDP to transport its RPCs.  In order to accommodate a
 * large effective ACL, we need to support a cursor-based RPC that
 * scans the eacl and returns a small-enough-to-fit block of ACEs for
 * each RPC.
 * 
 * The caller starts with a zero offset and a null ACL handle.  The
 * first reply returns the offset for the next block, and a
 * partially-filled in (but fully allocated) ACL handle.  The caller
 * sends the offset in the next RPC.  Caller also passes cursor and
 * partial ACL handle to the decode routine, which decodes the next
 * block into the ACL handle and updates the offset.
 *
 * This repeats until the sender indicates no more blocks with another
 * zero offset.
 * 
 * The upper layers of the RPC handler don't have to deal with the
 * cursor indexing, or the memory allocation of the reply structures,
 * or updating the cursor.
 * All the dragons are lurking here...
 */

/*
 * Handle either full or blocked encoding.
 *
 * NOTE: both sides of RPC protocol must agree on whether to use
 * blocks or not.  The on-the-wire encodings for blocked and full
 * modes are not compatible
 *
 * If offset_p != NULL, it points to the starting index for the set of
 * ACEs to handle.  Upon successful return, *offset_p is the next
 * offset to use, or if done, set to 0.
 *
 * If offset_p is NULL, handle the entire cooked info array.
 *
 * BLOCKSIZE is chosen to make one block of the array of ACEs fit into
 * a UDP RPC reply.
 */
#define BLOCKSIZE 50
STATIC bool_t
xdr_tbs_acl_cooked_info_block(
    XDR *xdrs,
    ks_uint32_t *offset_p,
    tbs_acl_cooked_info_t *objp
)
{
    u_int i;
    bool_t rval;
    u_int n_ents, limit, total_ents;
    ks_uint32_t offset;

    if (offset_p && xdrs->x_op == XDR_ENCODE && *offset_p > objp->n_ents)
    {
        /* Caller goofed; not permitted to encode past end of array */
        return FALSE;
    }

    /* If not using blocks, the limit for this call is n_ents.
     * If using blocks, limit is min(BLOCKSIZE, n_ents - offset)
     */
    if (offset_p) {
        switch (xdrs->x_op) {
          case XDR_ENCODE:
            limit = KS_MIN(BLOCKSIZE, objp->n_ents - *offset_p);
            n_ents = objp->n_ents;
            rval = xdr_u_int(xdrs, &limit); /* count of ACEs in this block */
            if (rval) {
                /*
                 * Tell the other side the total size so it can
                 * allocate space on first block.
                 */
                rval = xdr_u_int(xdrs, &objp->n_ents);
            }
            break;
          case XDR_DECODE:
            rval = xdr_u_int(xdrs, &n_ents); /* count of ACEs in this block */
            if (rval) {
                /* Get the total size from encoding. */
                rval = xdr_u_int(xdrs, &total_ents);
            }
            limit = n_ents;
            break;
          case XDR_FREE:
            rval = TRUE;
            limit = n_ents = objp->n_ents;
            break;
          default:                     /* keep compiler happy */
            rval = FALSE;
            break;
        }
        offset = *offset_p;
    } else {
        /* not using blocks, use just the total size */
        rval = xdr_u_int(xdrs, &objp->n_ents);
        limit = n_ents = total_ents = objp->n_ents;
        offset = 0;
    }
    if (!rval) {
        return FALSE;
    }
    if (xdrs->x_op == XDR_DECODE) {
        if (offset_p == NULL || *offset_p == 0) {
            /* new decode, allocate new storage */
            FLEX_L_CREATE(objp->cooked_ents_p, tbs_acl_cooked_ent_t,
                          STG_AREA_HEAP, total_ents);
            XDR_KS_MEM_ZERO(objp->cooked_ents_p,
                            total_ents * sizeof(tbs_acl_cooked_ent_t));
            objp->n_ents = n_ents;
            /* save total allocated size of ACE array for checking later */
            objp->limit = total_ents;
        } else {
            /* indicate new amount used (previously allocated) */
            if (objp->n_ents + n_ents > objp->limit)
                /* encoding failure: too many entries to fit allocated space */
                return FALSE;
            objp->n_ents += n_ents;
        }
    }

    /* We'd like to use xdr_vector(), but it's not available in kernel.
     * So just open-code a loop over the array.
     */
    for (i = 0; i < limit; i++) {
        rval = xdr_tbs_acl_cooked_ent_t(xdrs, &objp->cooked_ents_p[i+offset]);
        if (!rval) {
            if (xdrs->x_op == XDR_DECODE) {
                XDR xfree;
                xfree.x_op = XDR_FREE;
                /* free everything, including previous entries */
                i += offset;
                while (--i >= 0) {
                    (void)xdr_tbs_acl_cooked_ent_t(&xfree,
                                                   &objp->cooked_ents_p[i]);
                }
                goto cleanup;
            }
            break; /* from the for() loop */
        }
    }
    if (xdrs->x_op == XDR_ENCODE && offset_p) {
        if (*offset_p + limit >= n_ents)
            *offset_p = 0;                  /* signal the end */
        else
            *offset_p += limit;
    }

    if (xdrs->x_op == XDR_FREE) {
      cleanup:
        ACL_FLEX_L_DESTROY(objp->cooked_ents_p,
                           objp->n_ents * sizeof(tbs_acl_cooked_ent_t));
    }
    return rval;
}
#undef BLOCKSIZE

EZ_XDR_ROUTINE(tbs_acl_cooked_info_t)
{
    return xdr_tbs_acl_cooked_info_block(xdrs,
                                         NULL, /* using full transfer option */
                                         objp);
}

/*
 * For block encodings, start with *start_offset_p == 0; this will
 * signal the decode to allocate enough space for the entire array.
 * Subsequent decodes (*start_offset_p != 0) will use the preallocated space.
 *
 * Full decodings are done with start_offset_p == NULL
 */
STATIC bool_t
xdr_tbs_sid_acl_body(
    XDR *xdrs,
    ks_uint32_t *start_offset_p,
    tbs_sid_acl_h_t *objp
)
{
    tbs_sid_acl_h_t l_objp;
    bool_t null_handle;

    /*
     * On the wire encoding:  (a) boolean flag indicating null handle
     * (b) if FALSE, then the contents; if TRUE, nothing else.
     */
    /* If freeing, free a real acl (that we returned from a decode).
     * If encoding, we can just encode the real acl.
     * If decoding, decode into custom allocated acl (compatible
     * with all other ACL functions)
     */
    switch (xdrs->x_op) {
      default:
        return FALSE;                   /* invalid op */

      case XDR_FREE:
        tbs_sid_acl_free(objp);
        return TRUE;

      case XDR_ENCODE:
        null_handle = (*objp == NULL);
        if (!xdr_bool(xdrs, &null_handle))
            return FALSE;

        if (null_handle)
            return TRUE;

        return xdr_tbs_acl_cooked_info_block(xdrs, start_offset_p,
                                             (*objp)->cooked_p);

      case XDR_DECODE:
        if (!xdr_bool(xdrs, &null_handle))
            return FALSE;
        if (null_handle) {
            *objp = NULL;
        } else {
            if (start_offset_p == NULL || *objp == TBS_SID_ACL_H_NULL) {
                /* allocate handle & cooked ptr if not already done */
                l_objp = (tbs_acl_pvt_t *)STG_MALLOC(sizeof *l_objp);
                if (l_objp == NULL)
                    return FALSE;
                XDR_KS_MEM_ZERO(l_objp, sizeof *l_objp);
                /* Allocate cooked info */
                l_objp->cooked_p = (tbs_acl_cooked_info_t *)STG_MALLOC(sizeof(*l_objp->cooked_p));
                if (l_objp->cooked_p == NULL) {
                    STG_FREE(l_objp, sizeof *l_objp);
                    return FALSE;
                }
                l_objp->cooked_p->n_ents = 0;
                l_objp->cooked_p->cooked_ents_p = NULL;
            } else {
                /* use existing allocated object, continue to fill it in */
                l_objp = *objp;
            }
            /* unpack cooked info */
            if (!xdr_tbs_acl_cooked_info_block(xdrs, start_offset_p,
                                               l_objp->cooked_p))
            {
                STG_FREE(l_objp->cooked_p, sizeof(*l_objp->cooked_p));
                STG_FREE(l_objp, sizeof *l_objp);
                return FALSE;
            }
            *objp = l_objp;
        }
        break;
    }
    return TRUE;
}

EZ_XDR_ROUTINE(tbs_sid_acl_h_t)
{
    return xdr_tbs_sid_acl_body(xdrs,
                                NULL, /* using full transfer option */
                                objp);
}

bool_t
xdr_tbs_sid_acl_fragment_t(
    XDR *xdrs,
    ks_uint32_t *offset_p,
    tbs_sid_acl_h_t *objp
)
{
    return xdr_tbs_sid_acl_body(xdrs, offset_p, objp);
}

#undef ENUM_T
static const char vnode_verid_tbs_acl_c_xdr_kernel_c[] = "$Id:  d56e9de7.8a6011e1.8919.00:1a:6b:50:9a:cb $";
