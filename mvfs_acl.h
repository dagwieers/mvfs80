/* * (C) Copyright IBM Corporation 2013. */
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
#ifndef MVFS_ACL_H_
#define MVFS_ACL_H_

#include "mvfs_base.h"

/* The EACL hash table (entries are "pointed" to by mnodes using "handles").
** This will really be an array of struct mvfs_eaclhash_slot's allocated by
** mvfs_acl_init().
*/
typedef struct mvfs_acl_data {
    struct mvfs_eaclhash_slot *mvfs_eaclhash;
    size_t mvfs_eaclhash_size;
    mvfs_lock_pool_t mvfs_eaclhash_mlp;
} mvfs_acl_data_t;

/* Declarations for the EACL hash table (pointed to by mvfs_eaclhash in
** mvfs_common_data_t.  We use a fancy declaration to get an enumerated type and
** string names for it (for logging) from one "definition".  See mvfs_acl.c for
** the string name definitions.
*/
#define MVFS_VIEW_TYPES { C(Unix), C(Windows) }
#define C(a) ENUM_##a
enum mvfs_view_type_enums MVFS_VIEW_TYPES;
#undef C

struct mvfs_eaclhash_entry_hdr {
    struct mvfs_eaclhash_entry *next;
    struct mvfs_eaclhash_entry *prev;
};

struct mvfs_eaclhash_entry_key {
    tbs_oid_t eah_rolemap_oid;
    tbs_uuid_t eah_replica_uuid;
    enum mvfs_view_type_enums eah_view_type;
};

/* eah_hdr must have the same name and location (first) here as in
** mvfs_eaclhash_entry because we cast this mvfs_eaclhash_slot to a
** mvfs_eaclhash_entry for hash table operations.
*/
struct mvfs_eaclhash_slot {
    struct mvfs_eaclhash_entry_hdr eah_hdr;
    ks_uint32_t eah_entry_cnt;
};

/* External callers use a handle to reference this hashtable entry. */
struct mvfs_eaclhash_entry {
    struct mvfs_eaclhash_entry_hdr eah_hdr; /* Must be first in this structure. */
    struct mvfs_eaclhash_entry_key eah_key;
    ks_uint32_t eah_refcnt;
    struct timeval eah_eacl_lut;
    tbs_oid_t eah_policy_oid;
    tbs_sid_acl_h_t eah_eaclh;
};

typedef struct mvfs_eaclhash_entry_hdr * mvfs_eaclhash_entry_hdr_p;
typedef struct mvfs_eaclhash_entry_key * mvfs_eaclhash_entry_key_p;
typedef struct mvfs_eaclhash_slot * mvfs_eaclhash_slot_p;
typedef struct mvfs_eaclhash_entry * mvfs_eaclhash_entry_p;

#define MVFS_EACLHASH_LOCK(dp, hv, lpp)                \
    MVFS_LOCK_SELECT(&((dp)->mvfs_eaclhash_mlp), (hv), \
                     HASH_MVFS_LOCK_MAP, (lpp));       \
    MVFS_LOCK(*(lpp))

#define MVFS_EACLHASH_UNLOCK(lpp) \
    MVFS_UNLOCK(*(lpp))

#define MVFS_EACLHASH(dp, keyp)                                           \
    (ks_uint32_t)(                                                        \
        (mfs_uuid_to_hash32((tbs_uuid_t *)(&((keyp)->eah_rolemap_oid))) + \
         mfs_uuid_to_hash32(&((keyp)->eah_replica_uuid)) +                \
         (ks_uint32_t)((keyp)->eah_view_type)) % (dp)->mvfs_eaclhash_size)

/* Access checking functions. */

extern int
mvfs_chkaccess_acl(
    const VNODE_T *vp,
    int mode,
    const tbs_sid_acl_id_principal_t *owner_usid_p,
    const tbs_sid_acl_id_principal_t *owner_gsid_p,
    int vmode,
    CRED_T *cred,
    tbs_boolean_t caller_locked_mnode,
    tbs_boolean_t *check_modebits_p
);

extern int
mvfs_chkaccess_modebits(
    VNODE_T *vp,
    int mode,
    u_long vuid,
    u_long vgid,
    int vmode,
    CRED_T *cred
);

#ifdef MVFS_CHKACCESS_DEFAULT
extern int
mvfs_chkaccess(
    VNODE_T *vp,
    int mode,
    u_long vuid,
    u_long vgid,
    int vmode,
    CRED_T *cred,
    tbs_boolean_t mn_islocked
);
#endif /* MVFS_CHKACCESS_DEFAULT */

#ifdef MVFS_GROUPMEMBER_DEFAULT
extern int
mvfs_groupmember(
    u_long vgid,
    CRED_T *cred
);
#endif /* MVFS_GROUPMEMBER_DEFAULT */

/* ACL management functions. */

extern int
mvfs_acl_init(void);

extern void
mvfs_acl_free(void);

extern void
mvfs_acl_cache(
    mfs_mnode_t *mnp,
    VFS_T *vfsp,
    CRED_T *cred
);

extern void
mvfs_acl_uncache(
    mfs_mnode_t *mnp
);

extern void
mvfs_acl_inval_oid(
    vob_mtype_t oid_type, /* In: which type of OID it is. */
    tbs_oid_t *obj_oid_p  /* In: rolemap or policy OID. */
);

#endif /* MVFS_ACL_H_ */
/* $Id: b7a9794c.5b6211e2.8064.00:01:83:9c:f6:11 $ */
