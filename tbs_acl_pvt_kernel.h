/* *  (C) Copyright IBM Corporation 1998, 2012. */
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
#if !defined(_TBS_ACL_PVT_KERNEL_H_)
#define _TBS_ACL_PVT_KERNEL_H_

#define TBS_ACL_IMPLEMENTATION

/****************************************************************************
 * tbs_sid_acl_h_t    Pointer to tbs_acl_pvt_t.
 *
 * tbs_acl_pvt_t      Access Control List
 * .n_ents        Number of Access Control Entries.
 * .ents_p        Array of Access Control Entries.
 * .cooked_p      A "cooked" (compiled) representation of the ACL.
 */

typedef struct tbs_acl_pvt_t {
    size_t n_ents;
    KS_DYNARR(n_ents, tbs_acl_entry_t, *ents_p);
    tbs_acl_cooked_info_t *cooked_p;
} tbs_acl_pvt_t;

typedef tbs_acl_pvt_t *tbs_sid_acl_h_t;

#define TBS_ACL_PVT_KERNEL_H_DEFINED

#endif /* _TBS_ACL_PVT_KERNEL_H_ */
/* $Id: 0419ef27.0c1f11e2.93ec.00:01:83:9c:f6:11 $ */
