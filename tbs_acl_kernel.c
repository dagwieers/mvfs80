/* * Copyright IBM Corporation 1998, 2013. */
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
#include "mvfs.h"
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/time.h>
#define TBS_ACL_IMPLEMENTATION
#include "tbs_acl_kernel.h"


/* MACROIZE credutl_sid_eq() */

/****************************************************************************
 * tbs_sid_acl_id_size
 * Calculate size of a tbs_sid_acl_id_t
 * IN    id_p               id entry
 */
STATIC size_t
tbs_sid_acl_id_size(tbs_sid_acl_id_t *id_p)
{
    size_t size = 0;

    size += sizeof(id_p->kind) +
            SAFE_STRLEN(id_p->domain) + 
            SAFE_STRLEN(id_p->name) + 
            sizeof(id_p->principal_id);
    return size;
}

/****************************************************************************
 * tbs_acl_id_size
 * Calculate size of a tbs_acl_id_t
 * IN    id_p               id entry
 */
STATIC size_t
tbs_acl_id_size(tbs_acl_id_t *id_p)
{
    size_t size = 0;

    size += sizeof(id_p->kind) +
            SAFE_STRLEN(id_p->domain) + 
            SAFE_STRLEN(id_p->name);

    return size;
}

/****************************************************************************
 * tbs_acl_cooked_ent_size
 * Calculate size of a tbs_acl_cooked_ent_t
 * IN    cooked_ent_p       cooked entry
 */
STATIC size_t
tbs_acl_cooked_ent_size(tbs_acl_cooked_ent_t *cooked_ent_p)
{
    size_t size = 0;

    size += sizeof(cooked_ent_p->perm_set) +
            tbs_sid_acl_id_size(&cooked_ent_p->id);

    return size;
}

/****************************************************************************
 * tbs_acl_entry_size
 * Calculate size of a tbs_acl_entry_t
 * IN    entry_p            acl entry
 */
STATIC size_t
tbs_acl_entry_size(tbs_acl_entry_t *entry_p)
{
    size_t size = 0;

    size += sizeof(entry_p->perm_set) +
            tbs_acl_id_size(&entry_p->id);

    return size;
}

/****************************************************************************
 * tbs_sid_acl_size
 * Calculate size of a tbs_sid_acl_h_t
 * IN    aclh               ACL
 */
size_t
tbs_sid_acl_size(tbs_sid_acl_h_t aclh)
{
    int i;
    size_t size = 0;
    tbs_acl_cooked_info_t *cook_p = aclh->cooked_p;

    /* calculate size of "cooked" portion */
    for (i = 0; i < cook_p->n_ents; i++)
        size += tbs_acl_cooked_ent_size(&cook_p->cooked_ents_p[i]);

    /* and the rest */
    for (i = 0; i < aclh->n_ents; i++)
        size += tbs_acl_entry_size(&aclh->ents_p[i]);

    return size;
}

/****************************************************************************
 * tbs_acl_free_cooked_p
 * free tbs_acl_cooked_info_t resources.
 *
 * INOUT cooked_pp
 */

STATIC void
tbs_acl_free_cooked_p(
    tbs_acl_cooked_info_t **cooked_pp
)
{
    tbs_acl_cooked_info_t *cip = *cooked_pp;
    int i;

    if (cip != NULL) {
        *cooked_pp = NULL;
        for (i = 0; i < cip->n_ents; i++) {
            SAFE_FREE_STR(cip->cooked_ents_p[i].id.domain);
            SAFE_FREE_STR(cip->cooked_ents_p[i].id.name);
            switch (cip->cooked_ents_p[i].id.kind) {
              case TBS_ACL_ID_USER:
                MVFS_FREE_ID(&cip->cooked_ents_p[i].id.principal_id.id_uid);
                break;
              case TBS_ACL_ID_GROUP:
                MVFS_FREE_ID(&cip->cooked_ents_p[i].id.principal_id.id_gid);
                break;
              default:
                break;
            }
        }
        if (cip->cooked_ents_p != NULL) {
            ACL_FLEX_L_DESTROY(cip->cooked_ents_p,
                               cip->n_ents * sizeof(*cip->cooked_ents_p));
        }
        ACL_SAFE_FREE(cip, sizeof *cip);
    }
}

/****************************************************************************
 * tbs_sid_acl_free
 * Free ACL's resources.
 * IO    aclh_p          ACL
 */

void
tbs_sid_acl_free(tbs_sid_acl_h_t *aclh_p)
{
    tbs_sid_acl_h_t aclh;

    if (aclh_p == NULL || *aclh_p == NULL)
        return;

    aclh = *aclh_p;
    *aclh_p = NULL;
    tbs_acl_free_cooked_p(&aclh->cooked_p);
    aclh->cooked_p = NULL;

    STG_FREE(aclh, sizeof *aclh);

    return;
}

/****************************************************************************
 * tbs_acl_add_and_check_perms
 * IN    required_perms  permissions necessary to return TRUE
 * IN    current_perms   permissions for current principal (will be OR'd
 *                       with existing set)
 * INOUT accumulated_perms_p existing set of perms accumulated for this
                             set of principals
 * RETURNS    TRUE iff all requested perms are satisified.
 */

tbs_boolean_t
tbs_acl_add_and_check_perms(
    tbs_acl_permission_set_t required_perms,
    tbs_acl_permission_set_t current_perms,
    tbs_acl_permission_set_t *accumulated_perms_p
)
{
    tbs_boolean_t permission_granted;
    tbs_acl_permission_set_t accumulated_perms = *accumulated_perms_p;

    accumulated_perms |= current_perms;

    permission_granted =
        ((required_perms & accumulated_perms) == required_perms);

    *accumulated_perms_p = accumulated_perms;

    return permission_granted;
}

STATIC tbs_boolean_t
tbs_sid_acl_creds_is_user(
    /* const */ CRED_T *creds,
    const tbs_sid_acl_id_principal_t *id_p
)
{
    const MVFS_USER_ID *user_p = &id_p->id_uid;
    return (MDKI_CR_GET_UID(creds) == *user_p);
}

STATIC tbs_boolean_t
tbs_sid_acl_creds_has_group(
    /* const */ CRED_T *creds,
    const tbs_sid_acl_id_principal_t *id_p
)
{
    const MVFS_GROUP_ID *group_p = &id_p->id_gid;
    return MVFS_GROUPMEMBER(*group_p, creds);
}

/****************************************************************************
 * tbs_sid_acl_check_permission_creds
 * Handle "credentials" forms of client ids in a "sid" ACL.
 */
tbs_status_t
tbs_sid_acl_check_permission_creds(
    tbs_sid_acl_h_t aclh,
    tbs_acl_permission_set_t requested_perms,
    SID_ACL_CRED_T *creds,
    const SID_ACL_PRINCIPAL_ID_T *owner_usid_p,
    const SID_ACL_PRINCIPAL_ID_T *owner_gsid_p,
    tbs_boolean_t *permission_granted_p
)
{
    int i;
    tbs_acl_permission_set_t allowed_perms;
    tbs_sid_acl_id_t id;
    tbs_acl_cooked_info_t *cip = aclh->cooked_p;

/* define accessor macros to compare creds passed in */
#define CREDS_IS_USER(idp,creds) \
    tbs_sid_acl_creds_is_user((creds),&(idp)->principal_id)
#define CREDS_HAS_GROUP(idp,creds) \
    tbs_sid_acl_creds_has_group((creds),&(idp)->principal_id)
#define CREDS_IS_OWNER_USER(idp,creds) \
    tbs_sid_acl_creds_is_user((creds),(idp))
#define CREDS_HAS_OWNER_GROUP(idp,creds) \
    tbs_sid_acl_creds_has_group((creds),(idp))

    /* Gather up (OR together) all matching entries' granted permission masks */
    allowed_perms = TBS_ACL_PERMS_NONE;

    for (i = 0; i < cip->n_ents; i++) {
        switch (cip->cooked_ents_p[i].id.kind) {
          case TBS_ACL_ID_DOMAIN_ALL:
          case TBS_ACL_ID_ROLE:
          default:
            continue;                   /* not supported */

          case TBS_ACL_ID_ALL:
            break;                      /* this applies */
                
          case TBS_ACL_ID_USER:
            /* check if the user matches */
            if (CREDS_IS_USER(&cip->cooked_ents_p[i].id, creds)) {
                break;
            }
            continue;

          case TBS_ACL_ID_GROUP:
            /* check if the group matches any of the user's groups */
            if (CREDS_HAS_GROUP(&cip->cooked_ents_p[i].id, creds)) {
                break;
            }

            continue;

          case TBS_ACL_ID_OWNER_USER:
            /* Is this user the owner-user? */
            if (owner_usid_p != NULL &&
                CREDS_IS_OWNER_USER(owner_usid_p, creds))
            {
                break;
            }
            continue;

          case TBS_ACL_ID_OWNER_GROUP:
            /* Does this user belong to the owner-group? */
            if (owner_gsid_p != NULL &&
                CREDS_HAS_OWNER_GROUP(owner_gsid_p, creds))
            {
                break;
            }
            continue;
        }
        /* OK, this ACE applies.  Accumulate its perms */
        if ((*permission_granted_p =
             tbs_acl_add_and_check_perms(requested_perms,
                                        cip->cooked_ents_p[i].perm_set,
                                         &allowed_perms))
            == TRUE)
        {
            goto done;
        }
    }

    *permission_granted_p = FALSE;

  done:
    return TBS_ST_OK;

}
#undef CREDS_IS_USER
#undef CREDS_HAS_GROUP
static const char vnode_verid_tbs_acl_kernel_c[] = "$Id:  d192d0e1.8a6011e1.8919.00:1a:6b:50:9a:cb $";
