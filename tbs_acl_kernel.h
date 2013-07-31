/* *  (C) Copyright IBM Corporation 1991, 2013. */
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
#if !defined(_TBS_ACL_KERNEL_H_)
#define _TBS_ACL_KERNEL_H_

#include <tbs_base.h>
#include <credutl_kernel.h>

#define TBS_ACL_PERMS_NONE               0x00000000
#define TBS_ACL_PERMS_SPECIFIC_MASK      0x0780ffff
#define TBS_ACL_PERMS_STANDARD_MASK      0x007f0000
#define TBS_ACL_PERM_DELETE              0x00010000
#define TBS_ACL_PERM_READ_ACL            0x00020000
#define TBS_ACL_PERM_WRITE_ACL           0x00040000
#define TBS_ACL_PERMS_RESERVED_MASK      0x08000000
#define TBS_ACL_PERMS_GENERIC_MASK       0xf0000000
#define TBS_ACL_PERM_GENERIC_ALL         0x10000000
#define TBS_ACL_PERM_GENERIC_EXECUTE     0x20000000
#define TBS_ACL_PERM_GENERIC_WRITE       0x40000000
#define TBS_ACL_PERM_GENERIC_READ        0x80000000

typedef enum tbs_acl_id_kind {
    TBS_ACL_ID_NONE,
    TBS_ACL_ID_USER,
    TBS_ACL_ID_GROUP,
    TBS_ACL_ID_DOMAIN_ALL,
    TBS_ACL_ID_ALL,
    TBS_ACL_ID_OWNER_USER,
    TBS_ACL_ID_OWNER_GROUP,
    TBS_ACL_ID_ROLE,
    TBS_ACL_ID_LAST   /* Must be last */
} tbs_acl_id_kind_t;

typedef u_long tbs_acl_permission_set_t;

typedef union {
    /*
     * The type to use is determined by the kind of the enclosing
     * tbs_sid_acl_id_t.
     */
    MVFS_USER_ID id_uid;
    MVFS_GROUP_ID id_gid;
} tbs_sid_acl_id_principal_t;

typedef struct tbs_sid_acl_id {
    tbs_acl_id_kind_t kind;
    KS_CHAR *domain;
    KS_CHAR *name;
    tbs_sid_acl_id_principal_t principal_id;
} tbs_sid_acl_id_t;

typedef struct tbs_acl_cooked_ent_t {
    tbs_sid_acl_id_t id;
    tbs_acl_permission_set_t perm_set;
} tbs_acl_cooked_ent_t;

typedef struct tbs_acl_cooked_info_t {
    u_int n_ents;                       /* number of entries */
    u_int limit;                        /* allocated length of array */
    tbs_acl_cooked_ent_t *cooked_ents_p;
} tbs_acl_cooked_info_t;

typedef struct tbs_acl_id {
    tbs_acl_id_kind_t kind;
    KS_CHAR *domain;
    KS_CHAR *name;
} tbs_acl_id_t;

typedef struct tbs_acl_entry {
    tbs_acl_id_t id;
    tbs_acl_permission_set_t perm_set;
} tbs_acl_entry_t;

#include "tbs_acl_pvt_kernel.h"
#define TBS_SID_ACL_H_NULL ((tbs_sid_acl_h_t) NULL)

/* Remainder is local only stuff */

/****************************************************************************
 * tbs_sid_acl_size
 * Calculate size of a tbs_sid_acl_h_t
 * IN    aclh               ACL
 */
EXTERN size_t
tbs_sid_acl_size(tbs_sid_acl_h_t aclh);

/****************************************************************************
 * tbs_sid_acl_free
 * Free "sid" ACL's resources.
 * IO    aclh_p          ACL
 */

EXTERN void
tbs_sid_acl_free(
    tbs_sid_acl_h_t *aclh_p
);

#define SID_ACL_CRED_T CRED_T
#define SID_ACL_PRINCIPAL_ID_T tbs_sid_acl_id_principal_t
#define ACL_SAFE_FREE(x,sz) if ((x) != NULL) { KMEM_FREE((x),(sz));}
#define SAFE_FREE_STR(x) if ((x) != NULL) { STRFREE(x); }
#define SAFE_STRLEN(s) ((s == NULL ? 0 : strlen(s)))
#define ACL_FLEX_L_DESTROY(x,sz) KMEM_FREE(x,sz)
#define FLEX_L_CREATE(ptr,type,area,count) (ptr) = KMEM_ALLOC(count*sizeof(type),KM_SLEEP)
#define STG_MALLOC(sz) KMEM_ALLOC(sz,KM_SLEEP)
#define STG_FREE(ptr,sz) KMEM_FREE(ptr,sz)

/****************************************************************************
 * tbs_sid_acl_check_permission_creds
 * Handle "credentials" forms of client ids in a "sid" ACL.
 * See tbs_sid_acl_check_permission.
 */
EXTERN tbs_status_t
tbs_sid_acl_check_permission_creds(
    tbs_sid_acl_h_t aclh,
    tbs_acl_permission_set_t requested_perms,
    SID_ACL_CRED_T *creds,
    const SID_ACL_PRINCIPAL_ID_T *owner_usid_p,
    const SID_ACL_PRINCIPAL_ID_T *owner_gsid_p,
    tbs_boolean_t *permission_granted_p
);

#endif /* _TBS_ACL_KERNEL_H_ */
/* $Id: b7197904.5b6211e2.8064.00:01:83:9c:f6:11 $ */
