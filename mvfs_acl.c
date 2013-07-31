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

/* This file contains MVFS access related code, including EACL (Effective ACL)
** management and access checking code for both ACLs and modebits.
*/
#include "mvfs_systm.h"
#include "mvfs.h"

/* Globals for constants that could be changed by a debugger. */
int mvfs_eaclhash_size_default = 1024;
int mvfs_eacl_blocks_default = 100;

/* Storage for the EACL hashtable information. */
mvfs_acl_data_t mvfs_acl_data_var;

/* String names for view types, see mvfs_acl.h for more details. */
#define C(a) #a
char *mvfs_view_type_strings[] = MVFS_VIEW_TYPES;
#undef C

/* We should use mvfs_uuid_to_str and mvfs_oid_to_str to print out uuids and
** oids in our log messages.  However, those routines require storage in which
** to put the string, and, unless we want to allocate it each time we need it,
** we will need to use TBS_MAX_UUID_STR (defined to be 50) bytes of stack for
** the tbs_oid_str_t and tbs_uuid_str_t local variables.  To avoid that, we'll
** just define format and argument macros we can use without allocating extra
** space.  Note, in tbs_base.h a tbs_oid_t is the same as a tbs_uuid_t, so the
** cast is OK.  The "correct" way to print a oid is something like (and for a
** uuid it is the same with "oid" replaced by "uuid" in this code):
**
**   tbs_oid_str_t oidstr;
**   mvfs_oid_to_str(&(ep->eah_policy_oid), oidstr);
**
** which then just gets fed to a %s format for logging (since it returns a
** pointer to the string you passed in).
**
** There's some problem with using these macros on Linux 390, presumably because
** they add lots of arguments to the mvfs_log call, which seem to be put in the
** local stack frame, thus making it too big, so handle it specially.
*/
#if defined(__s390__) || defined(__s390x__)
#define MVFS_UUID_FMT "%016llx.%016llx"
#define MVFS_UUID_ARG(objp)                                  \
    ((struct {long long lhi; long long llo;} *)(objp))->lhi, \
    ((struct {long long lhi; long long llo;} *)(objp))->llo
#else
#define MVFS_UUID_FMT "%08x.%04hx%04hx.%02x%02x.%02x:%02x:%02x:%02x:%02x:%02x"
#define MVFS_UUID_ARG(objp)                            \
    ((tbs_uuid_t *)(objp))->time_low,                  \
    ((tbs_uuid_t *)(objp))->time_mid,                  \
    ((tbs_uuid_t *)(objp))->time_hi_and_version,       \
    ((tbs_uuid_t *)(objp))->clock_seq_hi_and_reserved, \
    ((tbs_uuid_t *)(objp))->clock_seq_low,             \
    ((tbs_uuid_t *)(objp))->node[0],                   \
    ((tbs_uuid_t *)(objp))->node[1],                   \
    ((tbs_uuid_t *)(objp))->node[2],                   \
    ((tbs_uuid_t *)(objp))->node[3],                   \
    ((tbs_uuid_t *)(objp))->node[4],                   \
    ((tbs_uuid_t *)(objp))->node[5]
#endif

/* Forward decls for internal routines that deal with hastable entries. */

STATIC void MVFS_NOINLINE
mvfs_eaclhash_validate(
    mvfs_eaclhash_entry_p ep,
    mfs_mnode_t *mnp,
    VFS_T *vfsp,
    CRED_T *cred
);

STATIC tbs_boolean_t MVFS_NOINLINE
mvfs_eaclhash_islocked(
    mvfs_eaclhash_entry_key_p keyp,
    ks_uint32_t hash_val
);

STATIC mvfs_eaclhash_entry_p MVFS_NOINLINE
mvfs_eaclhash_get(
    mvfs_eaclhash_entry_key_p keyp,
    mfs_mnode_t *mnp,
    VFS_T *vfsp,
    CRED_T *cred
);

STATIC void MVFS_NOINLINE
mvfs_eaclhash_release(
    mvfs_eaclhash_entry_p *epp
);

STATIC mvfs_eaclhash_entry_p MVFS_NOINLINE
mvfs_eaclhash_find_entry(
    mvfs_eaclhash_entry_key_p keyp,
    ks_uint32_t hash_val
);

STATIC void MVFS_NOINLINE
mvfs_eaclhash_add_entry(
    mvfs_eaclhash_entry_p ep,
    ks_uint32_t hash_val
);

STATIC void MVFS_NOINLINE
mvfs_eaclhash_remove_entry(
    mvfs_eaclhash_entry_p ep,
    ks_uint32_t hash_val
);

STATIC void MVFS_NOINLINE
mvfs_eaclhash_free_entry(
    mvfs_eaclhash_entry_p ep
);

STATIC void MVFS_NOINLINE
mvfs_eaclhash_update_eaclh(
    mvfs_eaclhash_entry_p ep,
    ks_uint32_t hash_val,
    mfs_mnode_t *mnp,
    VFS_T *vfsp,
    CRED_T *cred
);

STATIC void MVFS_NOINLINE
mvfs_eaclhash_validate_eaclh(
    mvfs_eaclhash_entry_p ep,
    ks_uint32_t hash_val,
    mfs_mnode_t *mnp,
    VFS_T *vfsp,
    CRED_T *cred
);

STATIC void MVFS_NOINLINE
mvfs_eaclhash_dump(void);

/* Externally visible access checking routines for both ACLs and modebits. */

/* MVFS_CHKACCESS_ACL - Function used by the default mvfs_chkaccess() below and
** also by machine dependent replacements for it to check the EACL pointed to by
** the mnode (which is pointed to by the vnode argument) against the access
** requested by the mode argument.
**
** On return, if *check_modebits_p == TRUE then the caller can ignore the error
** returned and is expected to check the mode bits to determine if access is
** allowed.  If *check_modebits_p == FALSE, then error 0 means access granted
** and EACCES means access denied.
*/
int
mvfs_chkaccess_acl(
    const VNODE_T *vp,
    int mode,
    const tbs_sid_acl_id_principal_t *owner_usid_p,
    const tbs_sid_acl_id_principal_t *owner_gsid_p,
    int vmode,
    CRED_T *cred,
    tbs_boolean_t caller_locked_mnode,
    tbs_boolean_t *check_modebits_p
)
{
    int error = 0;
    tbs_status_t status = TBS_ST_OK;
    tbs_boolean_t granted = FALSE;
    tbs_acl_permission_set_t requested_perms = TBS_ACL_PERMS_NONE;
    mfs_mnode_t *mnp = VTOM(vp);
    mvfs_eaclhash_entry_p ep = NULL;

    /* Superuser always gets access, no modebit or ACL checking needed. */
    if (MDKI_CR_IS_ROOT(cred)) {
        MDB_VLOG((MFS_VACCESS, "mvfs_chkaccess_acl:"
                  " vp=%p superuser match\n", vp));
        *check_modebits_p = FALSE;
        error = 0;
        goto done;
    }
    /* Only a VOB object can have an EACL, and it might not have one, so check
    ** for both cases.  For the ACL access check we need to convert some args
    ** (for the tbs_sid_acl_check_permission_creds() call that does the actual
    ** check).  For the creds arg, in the kernel a SID_ACL_CRED_T is a CRED_T
    ** (see tbs_acl_kernel.h) so no conversion is necessary.  For the
    ** requested_perms, the TBS_ACL permissions for elements don't map directly
    ** to mode bits.  They also map differently for file and directory elements.
    ** The main idea comes from comments in tbs_sid_acl_convert_to_sysutl_acl(),
    ** which does the mapping the other way (from TBS_ACL permissions to mode
    ** bits).  The following code attempts to "invert" that mapping.  For the
    ** owner args, a SID_ACL_PRINCIPAL_ID_T is a tbs_sid_acl_id_principal_t in
    ** the kernel and that's what our input args are, so no conversion is
    ** necessary.
    **
    ** It is safe (and a bit more efficient) to check the mclass via MFS_ISVOB
    ** before locking the mnode (if our caller hasn't already locked it) because
    ** mclass is only set for an mnode when it is created (so it never changes),
    ** and our caller must have a hold on the mnode (since it is checking access
    ** on the mnode object) so it can't go away while we're looking at it.
    */
    if (MFS_ISVOB(mnp)) {
        /* We need the mnode lock while we access the mnode fields. */
        if (caller_locked_mnode) {
            ASSERT(MISLOCKED(mnp));
        } else {
            MLOCK(mnp);
        }
        if (MFS_OIDNULL(mnp->mn_vob.attr.rolemap_oid)) {
            /* We don't have a rolemap (for whatever reason), so no ACL checking
            ** is needed and modebits rule.  This also means we shouldn't have
            ** an EACL entry pointer.
            */
            ASSERT(mnp->mn_vob.mn_aclh == NULL);
            *check_modebits_p = TRUE;
            MDB_VLOG((MFS_VACCESS, "mvfs_chkaccess_acl: "
                     "NULL rolemap mnp=%p vp=%p err=%d mode=%o "
                     "requested perms=%#lx\n",
                      mnp, vp, error, mode, requested_perms));
            goto unlock;
        }
        /* Cast the ACL handle for our internal use (could be NULL). */
        ep = (mvfs_eaclhash_entry_p)(mnp->mn_vob.mn_aclh);

        /* Validate the ACL entry before we try to use it. */
        mvfs_eaclhash_validate(ep, mnp, vp->v_vfsp, cred);

        if ((ep == NULL) || (ep->eah_eaclh == TBS_SID_ACL_H_NULL)) {
            /* We have a rolemap so we're supposed to have an EACL, but we
            ** don't, so deny access.
            */
            *check_modebits_p = FALSE;
            error = EACCES;
            mvfs_log(MFS_LOG_ERR, "mvfs_chkaccess_acl:"
                     " bad EACL mnp=%p vp=%p err=%d mode=%o"
                     " requested perms=%#lx has rolemap="MVFS_UUID_FMT
                     " - denying access.\n",
                     mnp, vp, error, mode, requested_perms,
                     MVFS_UUID_ARG(&(mnp->mn_vob.attr.rolemap_oid)));
            goto unlock;
        }
        /* We have a rolemap and an EACL so we're never going to check the
        ** modebits.
        */
        *check_modebits_p = FALSE;

        /* Files and directories map "r" access the same way. */
        if (mode & VREAD) requested_perms |= ELEMENT_RIGHTS_READ_INFO;

        /* The rest of the mappings depend on the object type. */
        if (MVFS_ISVTYPE(vp, VDIR)) {
            /* Directories are easy. */
            if (mode & VWRITE) requested_perms |= ELEMENT_RIGHTS_WRITE_DIR;
            if (mode & VEXEC) requested_perms |= ELEMENT_RIGHTS_LOOKUP_DIR;
        } else {
            /* File "w" access is only possible for view-private files, so
            ** there is no ACL, so we wouldn't be in this case (since we
            ** have an EACL), so no "w" access is allowed.
            */
            if (mode & VWRITE) {
                MDB_VLOG((MFS_VACCESS, "mvfs_chkaccess_acl:"
                          " vp=%p VWRITE requested on VOB object\n", vp));
                error = EACCES;
                goto unlock;
            }
            if (mode & VEXEC) {
                /* File "x" access checking requires checking both the ACL
                ** and the mode bits for the file (note, IXOTH is ignored).
                ** We check the mode bits first and if they fail, we're
                ** done, otherwise we continue on and check the ACL.
                */
                if (!(((vmode & S_IXUSR) &&
                       (MDKI_CR_GET_UID(cred) ==
                        MVFS_USER_ID_GET_UID(owner_usid_p->id_uid)))
                      ||
                      (vmode & S_IXGRP)))
                {
                    MDB_VLOG((MFS_VACCESS, "mvfs_chkaccess_acl:"
                              " vp=%p vmode=%o vuid=%ld uid=%ld x access\n",
                              vp, vmode,
                              MVFS_USER_ID_GET_UID(owner_usid_p->id_uid),
                              MDKI_CR_GET_UID(cred)));
                    error = EACCES;
                    goto unlock;
                }
                requested_perms |= ELEMENT_RIGHTS_READ_INFO;
            }
        }
        status =
            tbs_sid_acl_check_permission_creds(ep->eah_eaclh,
                                               requested_perms, cred,
                                               owner_usid_p, owner_gsid_p,
                                               &granted);

        if (status == TBS_ST_OK) {
            error = (granted ? 0 : EACCES);
        } else {
            /* This never actually happens (the access checking routine
            ** always returns OK), but be conservative when granting access.
            */
            error = EACCES;
        }
      unlock:
        if (!caller_locked_mnode) {
            /* If the caller didn't lock the mnode then we locked it above so
            ** unlock it here.
            */
            MUNLOCK(mnp);
        }
    } else {
        /* Not a VOB object so no ACL checking is possible and modebits rule.
        ** Also, the lock is not held by us.
        */
        *check_modebits_p = TRUE;
    }
  done:
    MDB_VLOG((MFS_VACCESS, "mvfs_chkaccess_acl:"
              " vp=%p mnp=%p err=%d mode=%o eaclh=%p(%s)"
              " requested perms=%#lx chk mode=%s\n",
              vp, mnp, error, mode,
              (ep != NULL) ? ep->eah_eaclh : NULL,
              MFS_ISVOB(mnp) ? "vob obj" : "not vob obj",
              requested_perms, *check_modebits_p ? "TRUE" : "FALSE"));
    return(error);
}

/* MVFS_CHKACCESS_MODEBITS - Function used by the default mvfs_chkaccess() below
** and also by machine dependent replacements for it (if they want to use it) to
** check the modebits against the access requested by the mode argument.
*/
int
mvfs_chkaccess_modebits(
    VNODE_T *vp,
    int mode,
    u_long vuid,
    u_long vgid,
    int vmode,
    CRED_T *cred
)
{
    int error = 0;

    /* Mode bit access check.  Based on only one of owner, group, public
    ** (checked in that order).
    */
    if (MDKI_CR_GET_UID(cred) != vuid) {
        /* Not owner - try group */
        mode >>= 3;
        if (MDKI_CR_GET_GID(cred) != vgid) {
            /* Not group, try group list */
            if (!MVFS_GROUPMEMBER(vgid, cred))
                /* No groups, try public */
                mode >>= 3;
        }
    }
    if ((vmode & mode) == mode) {
        error = 0;
    } else {
        error = EACCES;
    }
    MDB_VLOG((MFS_VACCESS, "mvfs_chkaccess_modebits:"
              " vp=%"KS_FMT_PTR_T" err=%d rqst=%o, mode=%o\n",
              vp, error, mode, vmode));
    return(error);
}

#ifdef MVFS_CHKACCESS_DEFAULT
int
mvfs_chkaccess(
    VNODE_T *vp,
    int mode,
    u_long vuid,
    u_long vgid,
    int vmode,
    CRED_T *cred,
    tbs_boolean_t mn_islocked
)
{
    int error = 0;
    tbs_boolean_t check_modebits;
    tbs_sid_acl_id_principal_t owner_usid;
    tbs_sid_acl_id_principal_t owner_gsid;
    mfs_mnode_t *mnp = VTOM(vp);

    owner_usid.id_uid = vuid;
    owner_gsid.id_gid = vgid;
    error = mvfs_chkaccess_acl(vp, mode, &owner_usid, &owner_gsid, vmode, cred,
                               mn_islocked, &check_modebits);
    if (check_modebits) {
        error = mvfs_chkaccess_modebits(vp, mode, vuid, vgid, vmode, cred);
    }
    MDB_VLOG((MFS_VACCESS, "mvfs_chkaccess:"
              " vp=%"KS_FMT_PTR_T" err=%d uid=%ld gid=%ld rqst=%o, mode=%o\n",
              vp, error, vuid, vgid, mode, vmode));
    return(error);
}
#endif /* MVFS_CHKACCESS_DEFAULT */

#ifdef MVFS_GROUPMEMBER_DEFAULT
/* MVFS_GROUPMEMBER - Routine to check if a gid is a member of the group list.
** The prototype is declared in mvfs_systm.h because it might be machine
** dependent.
*/
int
mvfs_groupmember(
    u_long vgid,
    CRED_T *cred
)
{
    CRED_GID_T *gp;
    CRED_GID_T *gpe;

    gp = MDKI_CR_GET_GRPLIST(cred);
    gpe = MDKI_CR_END_GRPLIST(cred);
#ifdef MFS_NO_GROUPCOUNT_IN_CRED
    /* List terminated by special null value */
    for (; gp < gpe && *gp != CRED_EOGROUPS; gp++) {
#else
    for (; gp < gpe; gp++) {
#endif
        if (vgid == *gp) {
            return(TRUE);
        }
    }
    return(FALSE);
}
#endif  /* MVFS_GROUPMEMBER_DEFAULT */

/* Externally visible ACL management code. */

/* Thoughts...the vstat for each mnode has a rolemap_oid (for the mnode) and an
** eacl_mtime (supposed to say if the eacl for the rolemap_oid could have
** changed).  So, when any mnode finds the eacl_mtime changed (by doing a
** getattr) it will update the eacl cache and everyone will see the change
** "immediately".
*/

/* MVFS_EACLHASH_INIT - Allocate and initialize the eaclhash table.  */
int
mvfs_acl_init(void)
{
    int i;
    int error;
    int mlp_poolsize;
    mvfs_acl_data_t *macldp;

    MDKI_ACL_ALLOC_DATA();
    macldp = MDKI_ACL_GET_DATAP();

    /* Dynamically allocate the eaclhash table using a fixed size since we don't
    ** have a good idea of how many distinct EACLs there might be.  This seems
    ** like a reasonable number, since, if the hash is good and evenly
    ** distributes entries, we could have about 10K EACLs with a hash chain
    ** length of 10, which is probably OK for the linear hash chain search.  We
    ** can revisit this to make it tuneable, or something, if it gets to be a
    ** problem.
    */
    macldp->mvfs_eaclhash_size = mvfs_eaclhash_size_default;

    macldp->mvfs_eaclhash = (mvfs_eaclhash_slot_p)KMEM_ALLOC(
        macldp->mvfs_eaclhash_size * sizeof(struct mvfs_eaclhash_slot),
        KM_SLEEP);

    if (macldp->mvfs_eaclhash == NULL) {
        mvfs_log(MFS_LOG_ERR,
                 "mvfs_acl_init: Could not allocate %ld bytes for eaclhash.\n",
                 macldp->mvfs_eaclhash_size *
                 sizeof(struct mvfs_eaclhash_slot));
        error = ENOMEM;
        goto cleanup;
    }
    for (i = 0; i < macldp->mvfs_eaclhash_size; i++) {
        macldp->mvfs_eaclhash[i].eah_hdr.next =
            macldp->mvfs_eaclhash[i].eah_hdr.prev =
            (mvfs_eaclhash_entry_p)&(macldp->mvfs_eaclhash[i]);
        macldp->mvfs_eaclhash[i].eah_entry_cnt = 0;
    }
    /* Now initialize the lock pool for this hash table.  The actual size is
    ** dependent on the HASH_MVFS_LOCK_RATIO for the platform (which is the same
    ** for all MVFS_LOCK hashtables on the platform).
    **
    ** Also, we currently don't lock individual entries, just this lock for the
    ** chain, and since the table is a fixed size, we don't need a global lock
    ** to do anything.
    */
    HASH_MVFS_LOCK_SET_POOLSIZE(mlp_poolsize, macldp->mvfs_eaclhash_size);
    if (mvfs_lock_pool_init(&(macldp->mvfs_eaclhash_mlp), mlp_poolsize, NULL,
                            "mvfs_eaclhash_mlp") != 0)
    {
        mvfs_log(MFS_LOG_ERR,
                 "mvfs_acl_init: Could not alloc eaclhash_mlp.\n");
        error = ENOMEM;
        goto cleanup;
    }
    error = 0;

  done:
    MDB_VLOG((MFS_VACCESS, "mvfs_acl_init:"
              " macldp=%p size=%"KS_FMT_SIZE_T_D" eaclhash=%p\n",
              macldp, macldp->mvfs_eaclhash_size, macldp->mvfs_eaclhash));
    return(error);

  cleanup:
    if (macldp->mvfs_eaclhash != NULL) {
        KMEM_FREE(macldp->mvfs_eaclhash,
                  macldp->mvfs_eaclhash_size *
                  sizeof(struct mvfs_eaclhash_slot));
        macldp->mvfs_eaclhash = NULL;
    }
    goto done;
}

/* MVFS_ACL_FREE - Free what we initialized in mvfs_acl_init(). */
void
mvfs_acl_free(void)
{
    ks_uint32_t hash_val;
    mvfs_eaclhash_entry_p hp;
    mvfs_eaclhash_entry_p ep;
    LOCK_T *mlplockp;
    mvfs_acl_data_t *macldp = MDKI_ACL_GET_DATAP();

    /* Since we're unloading the MVFS, it should be the case that all the EACL
    ** hashtable entries should be gone since all their refcnts should have gone
    ** to zero when the mnodes that referenced them were freed.  Check that here
    ** and cleanup and log errors if there's a problem.  We shouldn't need to
    ** lock since nobody else should be running at this point, but lock anyway
    ** for consistency (and to make some asserts happy).
    */
    for (hash_val = 0; hash_val < macldp->mvfs_eaclhash_size; hash_val++) {
        MVFS_EACLHASH_LOCK(macldp, hash_val, &mlplockp);

        /* Run down this chain (cast the slot to an entry for ease of use).  Of
        ** course, it should be empty.  We would like to use a for loop (like
        ** mvfs_eaclhash_dump):
        **
        **  for (ep = hp->eah_hdr.next; ep != hp; ep = ep->eah_hdr.next) {}
        **
        ** but we're going to free the entry inside the loop, so we have to get
        ** the next ptr before freeing, so use a while loop.
        */
        hp = (mvfs_eaclhash_entry_p)&(macldp->mvfs_eaclhash[hash_val]);
        ep = hp->eah_hdr.next;
        while (ep != hp) {
            mvfs_eaclhash_entry_p next;

            /* Oops, there's an entry on the chain.  Split up the log message
           ** because the native linux_390 overflows the stack with all the args
           ** (although the cross-compiled linux_390 is OK).
           */
            mvfs_log(MFS_LOG_ERR, "mvfs_acl_free:"
                     " entry should be gone:"
                     " ep=%p next=%p prev=%p"
                     " refcnt=%d size=%ld vwtp=%s eaclh=%p\n",
                     ep,
                     ep->eah_hdr.next,
                     ep->eah_hdr.prev,
                     ep->eah_refcnt,
                     (ep->eah_eaclh != NULL) ?
                         tbs_sid_acl_size(ep->eah_eaclh) : 0,
                     mvfs_view_type_strings[ep->eah_key.eah_view_type],
                     ep->eah_eaclh);
            mvfs_log(MFS_LOG_ERR, "mvfs_acl_free:"
                     " role="MVFS_UUID_FMT
                     " repl="MVFS_UUID_FMT
                     " policy="MVFS_UUID_FMT
                     " lut=%"KS_FMT_TV_SEC_T_D".%"KS_FMT_TV_USEC_T_D"\n",
                     MVFS_UUID_ARG(&(ep->eah_key.eah_rolemap_oid)),
                     MVFS_UUID_ARG(&(ep->eah_key.eah_replica_uuid)),
                     MVFS_UUID_ARG(&(ep->eah_policy_oid)),
                     ep->eah_eacl_lut.tv_sec, ep->eah_eacl_lut.tv_usec);

            /* Cleanup the entry to avoid leaks, even in the error case. */
            next = ep->eah_hdr.next;
            mvfs_eaclhash_remove_entry(ep, hash_val);
            mvfs_eaclhash_free_entry(ep);
            ep = next;
        }
        MVFS_EACLHASH_UNLOCK(&mlplockp);
    }
    /* Free the structure internals. */
    if (macldp->mvfs_eaclhash != NULL) {
        KMEM_FREE(macldp->mvfs_eaclhash,
                  macldp->mvfs_eaclhash_size *
                  sizeof(struct mvfs_eaclhash_slot));
        macldp->mvfs_eaclhash = NULL;
    }
    /* Free the lock table. */
    mvfs_lock_pool_free(&(macldp->mvfs_eaclhash_mlp));

    /* Free the structure. */
    MDKI_ACL_FREE_DATA();

    MDB_VLOG((MFS_VACCESS, "mvfs_acl_free: macldp=%p\n", macldp));
}

/* MVFS_ACL_CACHE - Convert a rolemap_oid to an ACL handle and cache the result
** in the mnode.  The caller is assumed to have the mnode locked and to only
** call us for VOB objects (which are the only objects with ACLs).
**
** A note on errors and assumptions...If we're calling this routine, an RPC
** containing a vstat structure must have returned without any errors because
** that's the only time calls to mvfs_attrcache or mvfs_ac_set_stat (which is
** who calls here) are made.  So, the vstat information in the mnode is valid
** and trustworthy.
*/
void
mvfs_acl_cache(
    mfs_mnode_t *mnp,
    VFS_T *vfsp,
    CRED_T *cred
)
{
    int error = 0;
    struct mvfs_eaclhash_entry_key key;
    mvfs_eaclhash_entry_p ep;

    ASSERT(MISLOCKED(mnp));
    ASSERT(MFS_ISVOB(mnp)); /* Only MFS_VOBCLAS objects have ACLs. */

    if (MFS_OIDNULL(mnp->mn_vob.attr.rolemap_oid)) {
        /* We have a null rolemap oid so we don't need to check ACLs (and we
        ** don't need to cache anything, either).  The view/vob may not support
        ** ACLs, or the vob feature level may not support ACLs.  In any case,
        ** the view has told us what we need to know by setting the rolemap oid
        ** to NULL or not.  Release any reference to the existing handle (which
        ** might be NULL).  The routine handles a NULL pointer and sets the arg
        ** to NULL in any case.
        */
        mvfs_eaclhash_release((mvfs_eaclhash_entry_p *)(&(mnp->mn_vob.mn_aclh)));
    } else {
        /* We have a rolemap_oid so find the EACL entry (ACL handle) in the EACL
        ** hashtable that goes with it.  This may turn out to be the same as the
        ** ACL handle that is already cached in the mnode, in which case the
        ** hashtable entry will be validated.  If we find a different entry, we
        ** release the one we have and replace it with the new one.  If there's
        ** some error, then we might get back NULL or the EACL handle in the
        ** entry might be NULL (and any error logging will have already been
        ** done).  In this case (an error getting the EACL),
        ** mvfs_chkaccess_acl() will see the non-NULL rolemap and the NULL ACL
        ** handle or EACL handle and deny access.  We pass in the EACL mtime
        ** from the vstat so the hash routine can tell if the EACL for the entry
        ** might be out of date.
        */
        key.eah_rolemap_oid = mnp->mn_vob.attr.rolemap_oid;
        key.eah_replica_uuid = VFS_TO_MMI(mnp->mn_hdr.vfsp)->mmi_vobuuid;
        key.eah_view_type = MVFS_VIEW_IS_WINDOWS_VIEW(mnp->mn_hdr.viewvp) ?
            ENUM_Windows : ENUM_Unix;

        ep = mvfs_eaclhash_get(&key, mnp, vfsp, cred);
        if ((mvfs_eaclhash_entry_p)(mnp->mn_vob.mn_aclh) != ep) {
            /* We got a new held EACL entry pointer (or NULL if there was an
            ** error), so release the old one (could be NULL) and set the new
            ** one.
            */
            mvfs_eaclhash_release(
                (mvfs_eaclhash_entry_p *)(&(mnp->mn_vob.mn_aclh)));
            mnp->mn_vob.mn_aclh = (mvfs_acl_h)ep;
        } else {
            /* We got the same entry as we had (but now it's been validated),
            ** and we already have a hold on it (or it was NULL), so release the
            ** hold we just got.
            */
            mvfs_eaclhash_release(&ep);
        }
    }
    MDB_VLOG((MFS_VACCESS, "mvfs_acl_cache: mnp=%p vfsp=%p\n", mnp, vfsp));
    return;
}

/* MVFS_ACL_UNCACHE - Decrement the refcnt on an ACL handle and free it if the
** refcnt is zero.  Always NULL the handle.  This is called when cleaning up an
** mnode, so the mnode is not locked, but the cleanup code guarantees it is the
** only referencer before it calls here.  Normally, mvfs_acl_cache() keeps the
** ACL handle up to date and releases the old one when replacing it.
*/
void
mvfs_acl_uncache(
    mfs_mnode_t *mnp
)
{
    ASSERT(mnp != NULL);
    ASSERT(MFS_ISVOB(mnp));

    mvfs_eaclhash_release((mvfs_eaclhash_entry_p *)(&(mnp->mn_vob.mn_aclh)));
    MDB_VLOG((MFS_VACCESS, "mvfs_acl_uncache: mnp=%p\n", mnp));
}

/* MVFS_ACL_INVAL_OID - Invalidate the EACL hashtable entries matching the
** rolemap or policy OID.
*/
void
mvfs_acl_inval_oid(
    vob_mtype_t oid_type, /* In: which type of OID it is. */
    tbs_oid_t *obj_oid_p  /* In: rolemap or policy OID. */
)
{
    ks_uint32_t hash_val;
    mvfs_eaclhash_entry_p hp;
    mvfs_eaclhash_entry_p ep;
    LOCK_T *mlplockp;
    mvfs_acl_data_t *macldp = MDKI_ACL_GET_DATAP();

     /* Go through the whole EACL hashtable looking for entries that match the
     ** provided OID and invalidate them (by setting their lut to 0) so the EACL
     ** will be re-fetched the next time it is validated.
     */
    for (hash_val = 0; hash_val < macldp->mvfs_eaclhash_size; hash_val++) {
        /* Lock for this chain and point to the slot (as an entry). */
        MVFS_EACLHASH_LOCK(macldp, hash_val, &mlplockp);
        hp = (mvfs_eaclhash_entry_p)&(macldp->mvfs_eaclhash[hash_val]);

        /* Run down this chain looking for matches. */
        for (ep = hp->eah_hdr.next; ep != hp; ep = ep->eah_hdr.next) {
            if ((oid_type == VOB_MTYPE_ROLEMAP &&
                 MFS_OIDEQ(ep->eah_key.eah_rolemap_oid, *obj_oid_p))
                ||
                (oid_type == VOB_MTYPE_POLICY &&
                 MFS_OIDEQ(ep->eah_policy_oid, *obj_oid_p)))
            {
                /* Setting the time to zero will cause the EACL to be re-fetched
                ** the next time someone validates this entry.
                */
                MVFS_INIT_TIMEVAL(ep->eah_eacl_lut);
            }
        }
        MVFS_EACLHASH_UNLOCK(&mlplockp);
    }
    MDB_VLOG((MFS_VACCESS, "mvfs_acl_inval_oid:"
              " type=%d oid="MVFS_UUID_FMT"\n",
              oid_type, MVFS_UUID_ARG(obj_oid_p)));
}

/* Internal hashtable routines for the external routines above. */

/* MVFS_EACLHASH_VALIDATE - Validate an EACL hashtable entry by locking and
** validating the EACL handle it contains.
*/
STATIC void MVFS_NOINLINE
mvfs_eaclhash_validate(
    mvfs_eaclhash_entry_p ep,
    mfs_mnode_t *mnp,
    VFS_T *vfsp,
    CRED_T *cred
)
{
    ks_uint32_t hash_val;
    LOCK_T *mlplockp;
    mvfs_acl_data_t *macldp = MDKI_ACL_GET_DATAP();

    DEBUG_ASSERT(mnp != NULL);
    DEBUG_ASSERT(MISLOCKED(mnp));
    DEBUG_ASSERT(MFS_ISVOB(mnp));

    if (ep == NULL) {
        /* Well, that was easy. */
        return;
    }
    hash_val = MVFS_EACLHASH(macldp, &(ep->eah_key));
    MVFS_EACLHASH_LOCK(macldp, hash_val, &mlplockp);

    mvfs_eaclhash_validate_eaclh(ep, hash_val, mnp, vfsp, cred);

    MVFS_EACLHASH_UNLOCK(&mlplockp);

    MDB_VLOG((MFS_VACCESS, "mvfs_eaclhash_validate:"
              " mnp=%p vfsp=%p\n",
              mnp, vfsp));
#ifdef MVFS_DEBUG
    mvfs_eaclhash_dump();
#endif
}

/* MVFS_EACLHASH_ISLOCKED - Return TRUE if the hashtable chain lock for this key
** is held, FALSE otherwise.
*/
STATIC tbs_boolean_t MVFS_NOINLINE
mvfs_eaclhash_islocked(
    mvfs_eaclhash_entry_key_p keyp,
    ks_uint32_t hash_val
)
{
    ks_uint32_t my_hash_val;
    LOCK_T *mlplockp;
    mvfs_acl_data_t *macldp = MDKI_ACL_GET_DATAP();

    my_hash_val = MVFS_EACLHASH(macldp, keyp);

    /* Verify the hash_val's we got and computed. */
    if (hash_val != 0 && hash_val != my_hash_val) {
        mvfs_log(MFS_LOG_ERR,
                 "mvfs_eacl_islocked: unmatched hash=%d (key hash=%d)\n",
                 hash_val, my_hash_val);
        return(FALSE);
    }
    MVFS_LOCK_SELECT(&(macldp->mvfs_eaclhash_mlp), my_hash_val,
                     HASH_MVFS_LOCK_MAP, &mlplockp);
    return(ISLOCKED(mlplockp));
}

/* MVFS_EACLHASH_GET - Get an existing EACL hashtable entry or create a new one.
** Always increment the reference count on the returned entry, except in the
** error case when NULL is returned.  The caller is responsible for calling
** mvfs_eaclhash_release to decrement the reference count.
*/
STATIC mvfs_eaclhash_entry_p MVFS_NOINLINE
mvfs_eaclhash_get(
    mvfs_eaclhash_entry_key_p keyp,
    mfs_mnode_t *mnp,
    VFS_T *vfsp,
    CRED_T *cred
)
{
    int error;
    tbs_status_t status;
    ks_uint32_t hash_val;
    mvfs_eaclhash_entry_p ep;
    mvfs_eaclhash_entry_p new_ep;
    LOCK_T *mlplockp;
    mvfs_acl_data_t *macldp = MDKI_ACL_GET_DATAP();

    /* We should be called only with "good" key values. */
    ASSERT(!MFS_OIDEQ(keyp->eah_rolemap_oid, TBS_OID_NULL));
    ASSERT(!MFS_UUIDEQ(keyp->eah_replica_uuid, TBS_UUID_NULL));
    ASSERT(keyp->eah_view_type == ENUM_Windows ||
           keyp->eah_view_type == ENUM_Unix);

    /* We only need to compute this once, and then pass it around. */
    hash_val = MVFS_EACLHASH(macldp, keyp);

    /* We need to hold this lock while we use the entry fields, even for the
    ** duration of the EACL update RPC.  We may want to consider a per-entry
    ** lock if this gets to be too much.
    */
    MVFS_EACLHASH_LOCK(macldp, hash_val, &mlplockp);

    ep = mvfs_eaclhash_find_entry(keyp, hash_val);
    if (ep != NULL) {
        /* We found an entry, which is now held.  Validate the EACL handle. */
        mvfs_eaclhash_validate_eaclh(ep, hash_val, mnp, vfsp, cred);
    } else {
        /* No existing entry, create a new one. */
        new_ep = KMEM_ALLOC(sizeof(struct mvfs_eaclhash_entry), KM_SLEEP);
        if (new_ep == NULL) {
            /* Our caller can handle a NULL entry return pointer. */
            mvfs_log(MFS_LOG_ERR, "mvfs_eaclhash_get: KMEM_ALLOC failed\n");
        } else {
            /* Fill in the entry, including the EACL handle (which might fail, but
            ** that's OK).
            */
            new_ep->eah_hdr.next = new_ep->eah_hdr.prev = NULL;
            new_ep->eah_refcnt = 1; /* Hold the entry. */
            new_ep->eah_key.eah_rolemap_oid = keyp->eah_rolemap_oid;
            new_ep->eah_key.eah_replica_uuid = keyp->eah_replica_uuid;
            new_ep->eah_key.eah_view_type = keyp->eah_view_type;
            MVFS_INIT_TIMEVAL(new_ep->eah_eacl_lut);
            new_ep->eah_policy_oid = TBS_OID_NULL;

            /* Set the eah_eaclh field to NULL since the
            ** mvfs_eaclhash_update_eacl assumes it is overwriting a previous
            ** one if it is non-NULL.  Note, this call also fills in the real
            ** values for the policy_oid and lut that we initialized above
            ** (since it does an RPC to get all that information).
            */
            new_ep->eah_eaclh = NULL;
            mvfs_eaclhash_update_eaclh(new_ep, hash_val, mnp, vfsp, cred);

            mvfs_eaclhash_add_entry(new_ep, hash_val);
            ep = new_ep;
        }
    }
    MVFS_EACLHASH_UNLOCK(&mlplockp);

    MDB_VLOG((MFS_VACCESS, "mvfs_eaclhash_get:"
              " ep=%p mnp=%p vfsp=%p refcnt=%u eaclh=%p"
              " rolemap="MVFS_UUID_FMT"\n",
              ep, mnp, vfsp,
              (ep != NULL) ? ep->eah_refcnt : 0,
              (ep != NULL) ? ep->eah_eaclh : NULL,
              MVFS_UUID_ARG(&(keyp->eah_rolemap_oid))));
#ifdef MVFS_DEBUG
    mvfs_eaclhash_dump();
#endif
    return(ep);
}

/* MVFS_EACLHASH_RELEASE - Decrement the reference count on a EACL hastable
** entry.  If it goes to zero, then remove the entry and free it.  The caller is
** holding the mnode lock (unless the mnode is being freed and the call comes
** through mvfs_acl_uncache()).  This guarantees that there is no race when we
** free the entry because a racing caller won't get the mnode lock until the
** handle (entry pointer) has been set to NULL.
*/
STATIC void MVFS_NOINLINE
mvfs_eaclhash_release(
    mvfs_eaclhash_entry_p *epp
)
{
    ks_uint32_t hash_val;
    LOCK_T *mlplockp;
    mvfs_acl_data_t *macldp = MDKI_ACL_GET_DATAP();
    mvfs_eaclhash_entry_p ep = *epp;
    ks_uint32_t refcnt = 0;     /* For logging purposes. */

    if (ep == NULL) {
        goto done;              /* Nothing to do. */
    }
    hash_val = MVFS_EACLHASH(macldp, &(ep->eah_key));
    MVFS_EACLHASH_LOCK(macldp, hash_val, &mlplockp);

    refcnt = ep->eah_refcnt;
    ASSERT(ep->eah_refcnt >= 1);
    ep->eah_refcnt--;
    if (ep->eah_refcnt == 0) {
        /* No more references...remove it from the hashtable and unlock. */
        mvfs_eaclhash_remove_entry(ep, hash_val);
        MVFS_EACLHASH_UNLOCK(&mlplockp);

        /* The entry is out of the hashtable, no lock is needed to free it. */
        mvfs_eaclhash_free_entry(ep);
    } else {
        MVFS_EACLHASH_UNLOCK(&mlplockp);
    }
    /* The caller no longer has a refcnt, so NULL their pointer (handle). */
    *epp = NULL;

  done:
    /* ep is the one we copied above (might be NULL), but we only print it out,
    ** we don't reference through it.
    */
    MDB_VLOG((MFS_VACCESS, "mvfs_eaclhash_release:"
              " ep=%p old refcnt=%u\n",
              ep, refcnt));
}

/* MVFS_EACLHASH_FIND_ENTRY - Find an entry in the EACL hashtable matching the
** key.  If one is found, increment its reference count.  Otherwise, return
** NULL.
*/
STATIC mvfs_eaclhash_entry_p MVFS_NOINLINE
mvfs_eaclhash_find_entry(
    mvfs_eaclhash_entry_key_p keyp,
    ks_uint32_t hash_val
)
{
    mvfs_eaclhash_entry_p hp;
    mvfs_eaclhash_entry_p ep;
    mvfs_acl_data_t *macldp = MDKI_ACL_GET_DATAP();

    DEBUG_ASSERT(mvfs_eaclhash_islocked(keyp, hash_val));

    hp = (mvfs_eaclhash_entry_p)&(macldp->mvfs_eaclhash[hash_val]);
    for (ep = hp->eah_hdr.next; ep != hp; ep = ep->eah_hdr.next) {
        if (MFS_OIDEQ(ep->eah_key.eah_rolemap_oid, keyp->eah_rolemap_oid) &&
            MFS_UUIDEQ(ep->eah_key.eah_replica_uuid, keyp->eah_replica_uuid) &&
            ep->eah_key.eah_view_type == keyp->eah_view_type)
        {
            ep->eah_refcnt++;
            BUMPSTAT(mvfs_eacstat.eac_hits);
            goto done;
        }
    }
    /* We didn't find it if we fall out of the loop. */
    ep = NULL;
    BUMPSTAT(mvfs_eacstat.eac_misses);
  done:
    MDB_VLOG((MFS_VACCESS, "mvfs_eaclhash_find_entry: ep=%p refcnt=%u\n",
              ep, (ep != NULL) ? ep->eah_refcnt : 0));
    return(ep);
}

/* MVFS_EACLHASH_ADD_ENTRY - Add an entry to the EACL hashtable using the given
** hash value.  All relevant locks are assumed to be held.
*/
STATIC void MVFS_NOINLINE
mvfs_eaclhash_add_entry(
    mvfs_eaclhash_entry_p ep,
    ks_uint32_t hash_val
)
{
    mvfs_eaclhash_entry_p hp;
    mvfs_acl_data_t *macldp = MDKI_ACL_GET_DATAP();

    DEBUG_ASSERT(ep != NULL);
    DEBUG_ASSERT(mvfs_eaclhash_islocked(&(ep->eah_key), hash_val));
    DEBUG_ASSERT(ep->eah_hdr.next == NULL);
    DEBUG_ASSERT(ep->eah_hdr.prev == NULL);

    /* We can cast between slot and entry, see mvfs_acl.h for details. */
    hp = (mvfs_eaclhash_entry_p)(&(macldp->mvfs_eaclhash[hash_val]));
    ep->eah_hdr.next = hp->eah_hdr.next;
    ep->eah_hdr.prev = hp;
    hp->eah_hdr.next->eah_hdr.prev = ep;
    hp->eah_hdr.next = ep;

    macldp->mvfs_eaclhash[hash_val].eah_entry_cnt++;
    BUMPSTAT(mvfs_eacstat.eac_hashcnt);
    MDB_VLOG((MFS_VACCESS, "mvfs_eaclhash_add_entry: ep=%p\n", ep));
}

/* MVFS_EACLHASH_REMOVE_ENTRY - Remove an entry from the EACL hashtable using
** the given hash value.  All relevant locks are assumed to be held.
*/
STATIC void MVFS_NOINLINE
mvfs_eaclhash_remove_entry(
    mvfs_eaclhash_entry_p ep,
    ks_uint32_t hash_val
)
{
    mvfs_acl_data_t *macldp = MDKI_ACL_GET_DATAP();

    DEBUG_ASSERT(ep != NULL);
    DEBUG_ASSERT(mvfs_eaclhash_islocked(&(ep->eah_key), hash_val));
    DEBUG_ASSERT(ep->eah_hdr.next != NULL);
    DEBUG_ASSERT(ep->eah_hdr.prev != NULL);

    ep->eah_hdr.next->eah_hdr.prev = ep->eah_hdr.prev;
    ep->eah_hdr.prev->eah_hdr.next = ep->eah_hdr.next;
    ep->eah_hdr.next = ep->eah_hdr.prev = NULL;

    macldp->mvfs_eaclhash[hash_val].eah_entry_cnt--;
    DECSTAT(mvfs_eacstat.eac_hashcnt);
    MDB_VLOG((MFS_VACCESS, "mvfs_eaclhash_remove_entry: ep=%p\n", ep));
}

/* MVFS_EACLHASH_FREE_ENTRY - Free an EACL hashtable entry.  It is assumed to be
** unlinked from the hashtable, so we just free the EACL handle it contains and
** then free the entry itself.
*/
STATIC void MVFS_NOINLINE
mvfs_eaclhash_free_entry(
    mvfs_eaclhash_entry_p ep
)
{
    DEBUG_ASSERT(ep != NULL);
    DEBUG_ASSERT(ep->eah_hdr.next == NULL);
    DEBUG_ASSERT(ep->eah_hdr.prev == NULL);
    DEBUG_ASSERT(ep->eah_refcnt == 0);

    if (ep->eah_eaclh != TBS_SID_ACL_H_NULL) {
        tbs_sid_acl_free(&(ep->eah_eaclh));
    }
    MDB_VLOG((MFS_VACCESS, "mvfs_eaclhash_free_entry: ep=%p\n", ep));
    KMEM_FREE(ep, sizeof(struct mvfs_eaclhash_entry));
}

/* MVFS_EACLHASH_UPDATE_EACLH - Update the EACL handle in an EACL hashtable
** entry.  Free the current one first, if necessary, and then make the RPC to
** the view server to get the updated EACL handle.
*/
STATIC void MVFS_NOINLINE
mvfs_eaclhash_update_eaclh(
    mvfs_eaclhash_entry_p ep,
    ks_uint32_t hash_val,
    mfs_mnode_t *mnp,
    VFS_T *vfsp,
    CRED_T *cred
)
{
    int error;
    int count;
    tbs_boolean_t restarted;
    view_eacl_cursor_t cursor;
    tbs_status_t status;

    DEBUG_ASSERT(ep != NULL);
    DEBUG_ASSERT(mvfs_eaclhash_islocked(&(ep->eah_key), hash_val));

    /* We're going to replace the existing EACL handle, so free it first. */
    if (ep->eah_eaclh != TBS_SID_ACL_H_NULL) {
        tbs_sid_acl_free(&(ep->eah_eaclh));
    }
    /* Call the RPC repeatedly, using a cursor, to accumulate and reassemble all
    ** the parts of the EACL.  The first call will allocate space for the entire
    ** EACL and return a non-null cursor offset if more data must be retrieved
    ** in another RPC.  Loop until we get a null cursor offset or we believe
    ** it's looped too many times.  Only check the cursor offset if there are no
    ** errors from the RPC.
    */
    restarted = FALSE;
    VIEW_EACL_CURSOR_RESET(&cursor); /* Like cursor = VIEW_EACL_CURSOR_NULL */

  restart:
    /* XXX how do we choose the right loop limit here?  Some platforms could
    ** have 2000 ACEs, at about 250 per block that's 8 blocks.  But we don't
    ** have a specific upper limit in VOB code so just pick something big here.
    */
    for (count = 0; count < mvfs_eacl_blocks_default; count++) {
        error = mvfs_clnt_get_eacl_mnp(mnp, vfsp, cred,
                                       &(ep->eah_key.eah_rolemap_oid),
                                       &cursor,
                                       &(ep->eah_eaclh),
                                       &(ep->eah_eacl_lut),
                                       &(ep->eah_policy_oid),
                                       &status);

        if ((error == 0) && (status == TBS_ST_ESTALE) && !restarted) {
            /* The EACL changed during the cursor scan.  We must restart the
            ** scan (after freeing any partially accumulated EACL).  Assume the
            ** next scan will succeed so don't restart more than once.
            */
            mvfs_log(MFS_LOG_DEBUG,
                     "mvfs_eaclhash_update_eaclh: restarting on stale eacl at"
                     " block=%d offset=%d\n",
                     count, (int)(cursor.next_offset));

            if (ep->eah_eaclh != TBS_SID_ACL_H_NULL) {
                tbs_sid_acl_free(&(ep->eah_eaclh));
            }
            VIEW_EACL_CURSOR_RESET(&cursor);
            restarted = TRUE;
            goto restart;
        }
        if ((error == 0) && (status == TBS_ST_OK)) {
            /* We're either done (null cursor offset) or there's more to fetch.
            ** If there's more, call again, sending in the same cursor and eaclh
            ** to accumulate the next section's contents.
            */
            if (cursor.next_offset == 0) {
                break;          /* We're done (and it's good). */
            } else {
                continue;       /* There's more. */
            }
        } else {
            break;              /* We're done (and it's bad). */
        }
    }
    /* We could be done (for good or bad) or we could have finished the for loop
    ** (in which case the cursor will still be non-null).
    */
    if ((error == 0) && (status == TBS_ST_OK) && (cursor.next_offset != 0)) {
        /* Oops, we finished the for loop, which shouldn't happen. */
        error = ERANGE;
    }
    if ((error != 0) || (status != TBS_ST_OK)) {
        /* We're supposed to be checking ACLs (we're only called with a non-NULL
        ** rolemap), but there's some error getting the ACL to check against.
        ** The RPC has set the EACL handle to NULL (or we will below) and the
        ** mtime to the current time (and left the policy_oid alone), so we will
        ** try again in mvfs_eaclhash_validate after a timeout.  The
        ** mvfs_chkaccess_acl routine understands a NULL EACL handle and will
        ** "fail safe" by denying access.
        **
        ** We could have used tbs_sid_acl_create to create an empty EACL that
        ** would deny access for everybody and then cached that to use for later
        ** access checks.  However, leaving the EACL handle NULL achieves the
        ** same result, and avoids allocating memory for the empty EACL (which
        ** could fail, too).
        */
        mvfs_log(MFS_LOG_ERR, "mvfs_eaclhash_update_eaclh:"
                 " view=%s (%s) error=%d mnp=%p"
                 " tbs_status=%d block=%d offset=%d rolemap="MVFS_UUID_FMT
                 " - leaving/setting eaclh NULL.\n",
                 mnp->mn_hdr.viewvp == NULL ?
                     "noview" : VTOM(mnp->mn_hdr.viewvp)->mn_view.viewname,
                 mvfs_view_type_strings[ep->eah_key.eah_view_type],
                 error, mnp, status, count, (int)(cursor.next_offset),
                 MVFS_UUID_ARG(&(ep->eah_key.eah_rolemap_oid)));

        /* We could have accumulated a partial EACL, so free it. */
        if (ep->eah_eaclh != TBS_SID_ACL_H_NULL) {
            tbs_sid_acl_free(&(ep->eah_eaclh));
        }
    }
    MDB_VLOG((MFS_VACCESS, "mvfs_eaclhash_update_eaclh: ep=%p\n", ep));
}

/* MVFS_EACLHASH_VALIDATE_EACLH - Validate the EACL handle in a hashtable entry
** by checking the times.  If the handle has "expired", fetch the EACL from the
** view server.  All necessary locks are assumed to be held.
**
** This routine needs a timeout to check for transient errors.  For now, just
** make this a global with an arbitrary value that seems long enough to make
** retries for permanent errors not too annoying, but clears transient errors in
** some "reasonable" time.  We might use MVFS_MAXTIME (the default RPC timeout),
** but it's currently only defined in mvfs_rpcutl.c.  We might make this
** tuneable if it gets to be a problem.
*/
time_t mvfs_eacl_err_timeout = 4 * 60; /* 4 minutes (in seconds). */

STATIC void MVFS_NOINLINE
mvfs_eaclhash_validate_eaclh(
    mvfs_eaclhash_entry_p ep,
    ks_uint32_t hash_val,
    mfs_mnode_t *mnp,
    VFS_T *vfsp,
    CRED_T *cred
)
{
    DEBUG_ASSERT(mnp != NULL);
    DEBUG_ASSERT(MISLOCKED(mnp));
    DEBUG_ASSERT(MFS_ISVOB(mnp));

    if (ep == NULL) {
        /* Well, that was easy. */
        return;
    }
    DEBUG_ASSERT(mvfs_eaclhash_islocked(&(ep->eah_key), hash_val));

    /* Check the EACL mtime to see if the EACL might need updating.  If the EACL
    ** handle is NULL, we had an error when we last checked, so only check again
    ** after a timeout.
    */
    if (MVFS_TIMEVAL_NEWER(&(mnp->mn_vob.attr.eacl_mtime),
                           &(ep->eah_eacl_lut)) ||
        ((ep->eah_eaclh == TBS_SID_ACL_H_NULL) &&
         (ep->eah_eacl_lut.tv_sec + mvfs_eacl_err_timeout < MDKI_CTIME())))
    {
        /* Try to update the EACL handle.  Any failure will result in a NULL
        ** handle, which will be handled by the code that uses it.
        */
        mvfs_eaclhash_update_eaclh(ep, hash_val, mnp, vfsp, cred);
    }
    MDB_VLOG((MFS_VACCESS, "mvfs_eaclhash_validate_eaclh: ep=%p\n", ep));
}

/* MVFS_EACLHASH_DUMP - Dump the contents of the (non-NULL) entries in the EACL
** hashtable to the mvfs_log.  Callers use this when MVFS_DEBUG is set, and the
** logging is only done if the MFS_VACCESS bit is set, as well.
*/
STATIC void MVFS_NOINLINE
mvfs_eaclhash_dump(void)
{
    ks_uint32_t hash_val;
    mvfs_eaclhash_entry_p hp;
    mvfs_eaclhash_entry_p ep;
    LOCK_T *mlplockp;
    mvfs_acl_data_t *macldp = MDKI_ACL_GET_DATAP();
    ks_uint32_t total_entries = 0;
    ks_uint32_t max_refcnt = 0;
    ks_uint32_t max_chain_len = 0;

    /* Print table information. */
    MDB_VLOG((MFS_VACCESS,
              "EACL Hash Table: %p size=%ld lock=%p\n",
              macldp->mvfs_eaclhash,
              macldp->mvfs_eaclhash_size,
              macldp->mvfs_eaclhash_mlp));

    /* Go through the whole EACL hash table and print non-empty entries. */
    for (hash_val = 0; hash_val < macldp->mvfs_eaclhash_size; hash_val++) {
        /* Lock for this chain (slot). */
        MVFS_EACLHASH_LOCK(macldp, hash_val, &mlplockp);

        /* Just adds zero if this is an empty chain. */
        total_entries += macldp->mvfs_eaclhash[hash_val].eah_entry_cnt;
        max_chain_len = KS_MAX(max_chain_len,
                               macldp->mvfs_eaclhash[hash_val].eah_entry_cnt);

        /* Run down this chain (cast the slot to an entry for ease of use). */
        hp = (mvfs_eaclhash_entry_p)&(macldp->mvfs_eaclhash[hash_val]);
        for (ep = hp->eah_hdr.next; ep != hp; ep = ep->eah_hdr.next) {

            /* Header line for this (non-empty) chain. */
            if (ep == hp->eah_hdr.next) {
                MDB_VLOG((MFS_VACCESS,
                          "EACL Hash Chain: hash_val=%u cnt=%u"
                          " slot=%p next=%p prev=%p\n",
                          hash_val,
                          ((mvfs_eaclhash_slot_p)hp)->eah_entry_cnt,
                          hp,
                          hp->eah_hdr.next,
                          hp->eah_hdr.prev));
            }
            /* Contents of each entry in this chain.  Break up the logging because
            ** otherwise there are too many arguments, which causes a stack overflow
            ** on linux_390.
            */
            MDB_VLOG((MFS_VACCESS,
                      "EACL Hash Entry: refcnt=%d size=%ld vwtp=%s eaclh=%p\n",
                      ep->eah_refcnt,
                      (ep->eah_eaclh != NULL)
                      ? tbs_sid_acl_size(ep->eah_eaclh)
                      : 0,
                      mvfs_view_type_strings[ep->eah_key.eah_view_type],
                      ep->eah_eaclh));
            MDB_VLOG((MFS_VACCESS,
                      "ep=%p next=%p prev=%p\n",
                      ep,
                      ep->eah_hdr.next,
                      ep->eah_hdr.prev));
            MDB_VLOG((MFS_VACCESS,
                      "role="MVFS_UUID_FMT
                      " repl="MVFS_UUID_FMT
                      " policy="MVFS_UUID_FMT
                      " lut=%"KS_FMT_TV_SEC_T_D".%"KS_FMT_TV_USEC_T_D"\n",
                      MVFS_UUID_ARG(&(ep->eah_key.eah_rolemap_oid)),
                      MVFS_UUID_ARG(&(ep->eah_key.eah_replica_uuid)),
                      MVFS_UUID_ARG(&(ep->eah_policy_oid)),
                      ep->eah_eacl_lut.tv_sec, ep->eah_eacl_lut.tv_usec));

            max_refcnt = KS_MAX(max_refcnt, ep->eah_refcnt);
        }
        MVFS_EACLHASH_UNLOCK(&mlplockp);
    }
    MDB_VLOG((MFS_VACCESS,
              "EACL Hash Table: tot ents=%u max chain len=%u  max refcnt=%u\n",
              total_entries, max_chain_len, max_refcnt));
}

/* Clean up these defines since this .c file is included in mvfs-src.c */
#undef MVFS_UUID_FMT
#undef MVFS_UUID_ARG
static const char vnode_verid_mvfs_acl_c[] = "$Id:  b7797934.5b6211e2.8064.00:01:83:9c:f6:11 $";
