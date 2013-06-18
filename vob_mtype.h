/* * (C) Copyright IBM Corporation 2002, 2011. */
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
#if !defined(_VOB_MTYPE_H_)
#define _VOB_MTYPE_H_

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------
 * meta-types
 *
 * VOB_NUM_MTYPES	Number of meta-types.
 *
 * vob_mtype_t		Meta-type of an object.
 *			See macros below for categorizing meta-types.
 *  VOB_MTYPE_CG_ACTIVITY	ClearGuide activity object.
 *  VOB_MTYPE_ACTIVITY_TYPE
 *			Activity type.
 *  VOB_MTYPE_ATTR_TYPE	Attribute type.
 *  VOB_MTYPE_BRANCH	Branch of an element.
 *  VOB_MTYPE_BRANCH_TYPE
 *			Branch type.
 *  VOB_MTYPE_CHECKPOINT
 *			Checkpoint object.
 *  VOB_MTYPE_UCM_COMPONENT
 *                      Component object.
 *  VOB_MTYPE_DO	Derived object.
 *  VOB_MTYPE_DO_VERSION
 *			Derived object version of an element.
 *  VOB_MTYPE_DOMAIN	User identity domain object.
 *  VOB_MTYPE_ELEM_TYPE	Element type.
 *  VOB_MTYPE_FILE_ELEM	File element
 *  VOB_MTYPE_UCM_FOLDER
 *                      Folder object.
 *  VOB_MTYPE_HLINK	Hyperlink.
 *  VOB_MTYPE_HLINK_TYPE
 *			Hyperlink type.
 *  VOB_MTYPE_LABEL_TYPE
 *			Label type.
 *  VOB_MTYPE_NSDIR_ELEM
 *			Namespace directory element.
 *  VOB_MTYPE_NSDIR_VERSION
 *			Namespace directory version.
 *  VOB_MTYPE_NULL	Null value for type vob_mtype_t.
 *  VOB_MTYPE_POLICY	Policy.
 *  VOB_MTYPE_POOL 	Storage pool.
 *  VOB_MTYPE_UCM_PROJECT
 *                      Project object.
 *  VOB_MTYPE_REPLICA	Replica object.
 *  VOB_MTYPE_REPLICA_TYPE
 *			Replica type.
 *  VOB_MTYPE_ROLE	Role.
 *  VOB_MTYPE_ROLEMAP	Rolemap.
 *  VOB_MTYPE_SLINK	Symbolic link.
 *  VOB_MTYPE_STATE	Process state.
 *  VOB_MTYPE_STATE_TYPE
 *			Process state type.
 *  VOB_MTYPE_UCM_ACTIVITY
 *                      UCM activity object
 *  VOB_MTYPE_UCM_BASELINE
 *                      UCM baseline object
 *  VOB_MTYPE_UCM_COMPONENT
 *                      UCM component object
 *  VOB_MTYPE_UCM_FOLDER
 *                      UCM folder object
 *  VOB_MTYPE_UCM_PROJECT
 *                      UCM project object
 *  VOB_MTYPE_UCM_STREAM
 *                      UCM stream object
 *  VOB_MTYPE_UCM_TIMELINE
 *                      UCM timeline object (baseline container)
 *  VOB_MTYPE_UCM_LEGACY_OBJ
 *                      Legacy objects needed by UCM 7.1 and below
 *  VOB_MTYPE_TREE_ELEM	Tree element (not supported).
 *  VOB_MTYPE_TRIGGER_TYPE
 *			Trigger type.
 *  VOB_MTYPE_USER	User object.
 *  VOB_MTYPE_VERSION	Version of an element.
 *  VOB_MTYPE_VIEW_DB	View object.  This object exists to point hyperlinks
 *			at for guarding views.
 *  VOB_MTYPE_VIEW_PVT	View-private object.  There are no objects of this
 *			type in the VOB.
 *  VOB_MTYPE_VIEW_ABO	View audited build object.  There are no objects of
 *			this type in the VOB.
 *  VOB_MTYPE_VIEW_ABO_NLC
 *			View audited build object with no DO link counts. 
 * 			There are no objects of	this type in the VOB.
 *  VOB_MTYPE_VIEW_DO	View derived object.  There are no objects of
 *			this type in the VOB.
 *  VOB_MTYPE_NSHAREABLE_DO
 *                      View derived object.  There are no objects of
 *                      this type in the VOB.  (This differs from a
 *                      view DO in that this does not have shopping
 *                      information in the VOB; the other does.)
 *  VOB_MTYPE_VOB	Versioned object base.
 */

typedef enum vob_mtype_t {
   /* These values are stored in the database and must not be modified */
    VOB_MTYPE_NULL		=  0,
    VOB_MTYPE_VOB		=  1,
    VOB_MTYPE_NSDIR_ELEM	=  2,
    VOB_MTYPE_NSDIR_VERSION	=  3,
    VOB_MTYPE_TREE_ELEM	 	=  4,  /* not supported */
    VOB_MTYPE_FILE_ELEM	 	=  5,
    VOB_MTYPE_DO_VERSION	=  6,
    VOB_MTYPE_SLINK		=  7,
    VOB_MTYPE_VERSION	 	=  8,
    VOB_MTYPE_DO		=  9,
    VOB_MTYPE_HLINK		= 10,
    VOB_MTYPE_BRANCH		= 11,
    VOB_MTYPE_POOL		= 12,
    VOB_MTYPE_ELEM_TYPE		= 13,
    VOB_MTYPE_BRANCH_TYPE	= 14,
    VOB_MTYPE_ATTR_TYPE		= 15,
    VOB_MTYPE_HLINK_TYPE	= 16,
    VOB_MTYPE_TRIGGER_TYPE	= 17,
    VOB_MTYPE_REPLICA_TYPE	= 18,
    VOB_MTYPE_VIEW_PVT		= 19,
    VOB_MTYPE_LABEL_TYPE	= 20,
    VOB_MTYPE_VIEW_ABO		= 21,
    VOB_MTYPE_VIEW_DO		= 22,
    VOB_MTYPE_VIEW_ABO_NLC      = 23,
    VOB_MTYPE_REPLICA		= 24,
    VOB_MTYPE_VIEW_DB		= 25,
    VOB_MTYPE_NSHAREABLE_DO     = 26,
    VOB_MTYPE_ACTIVITY_TYPE	= 27,
    VOB_MTYPE_CG_ACTIVITY	= 28,
    VOB_MTYPE_STATE_TYPE	= 29,
    VOB_MTYPE_STATE		= 30,
    VOB_MTYPE_ROLE		= 31,
    VOB_MTYPE_USER		= 32,
    VOB_MTYPE_CHECKPOINT	= 33,
    VOB_MTYPE_DOMAIN		= 34,
    /* FL 7 and above */
    VOB_MTYPE_UCM_COMPONENT     = 35,
    VOB_MTYPE_UCM_FOLDER        = 36,
    VOB_MTYPE_UCM_PROJECT       = 37,
    VOB_MTYPE_UCM_ACTIVITY      = 38,
    VOB_MTYPE_UCM_STREAM        = 39,
    VOB_MTYPE_UCM_TIMELINE      = 40,
    VOB_MTYPE_UCM_BASELINE      = 41,
    VOB_MTYPE_UCM_LEGACY_OBJ    = 42,

#ifdef VOB_ACL
    VOB_MTYPE_POLICY	        = 43,
    VOB_MTYPE_ROLEMAP	        = 44,

    VOB_NUM_MTYPES              = 45
#else
    VOB_NUM_MTYPES		= 43
#endif
} vob_mtype_t;

#define VOB_NUM_MTYPES_FL6 (VOB_MTYPE_DOMAIN + 1)
#define VOB_NUM_MTYPES_FL7 (VOB_MTYPE_UCM_LEGACY_OBJ + 1)


/****************************************************************************
 * VOB_MTYPE_IS_ELEM
 * Decide whether a meta-type represents an element.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */

#define VOB_MTYPE_IS_ELEM(mtype) \
    ((mtype) == VOB_MTYPE_FILE_ELEM || \
     (mtype) == VOB_MTYPE_TREE_ELEM || \
     (mtype) == VOB_MTYPE_NSDIR_ELEM)

/****************************************************************************
 * VOB_MTYPE_CASE_ELEM
 * Switch statement case labels for element meta-types.
 */

#define VOB_MTYPE_CASE_ELEM \
    case VOB_MTYPE_FILE_ELEM: \
    case VOB_MTYPE_TREE_ELEM: \
    case VOB_MTYPE_NSDIR_ELEM

/****************************************************************************
 * VOB_MTYPE_IS_VERSION
 * Decide whether a meta-type represents a version.
 * IN	mtype		meta-type
 */

#define VOB_MTYPE_IS_VERSION(mtype) \
    ((mtype) == VOB_MTYPE_VERSION || \
     (mtype) == VOB_MTYPE_DO_VERSION || \
     (mtype) == VOB_MTYPE_NSDIR_VERSION)

/****************************************************************************
 * VOB_MTYPE_CASE_VERSION
 * Switch statement case labels for version meta-types.
 */

#define VOB_MTYPE_CASE_VERSION \
    case VOB_MTYPE_VERSION: \
    case VOB_MTYPE_DO_VERSION: \
    case VOB_MTYPE_NSDIR_VERSION

/****************************************************************************
 * VOB_MTYPE_IS_FILE_VERSION
 * Decide whether a meta-type represents a file version.
 * IN	mtype		meta-type
 */

#define VOB_MTYPE_IS_FILE_VERSION(mtype) \
    ((mtype) == VOB_MTYPE_VERSION || \
     (mtype) == VOB_MTYPE_DO_VERSION)

/****************************************************************************
 * VOB_MTYPE_CASE_FILE_VERSION
 * Switch statement case labels for version meta-types.
 */

#define VOB_MTYPE_CASE_FILE_VERSION \
    case VOB_MTYPE_VERSION: \
    case VOB_MTYPE_DO_VERSION

/****************************************************************************
 * VOB_MTYPE_IS_DO
 * Decide whether a meta-type represents a derived object.
 * IN	mtype		meta-type
 */

#define VOB_MTYPE_IS_DO(mtype) \
    ((mtype) == VOB_MTYPE_DO || \
     (mtype) == VOB_MTYPE_DO_VERSION || \
     (mtype) == VOB_MTYPE_VIEW_DO || \
     (mtype) == VOB_MTYPE_NSHAREABLE_DO)

/****************************************************************************
 * VOB_MTYPE_CASE_DO
 * Switch statement case labels for derived object meta-types.
 */

#define VOB_MTYPE_CASE_DO \
  case VOB_MTYPE_DO: \
  case VOB_MTYPE_DO_VERSION: \
  case VOB_MTYPE_VIEW_DO: \
  case VOB_MTYPE_NSHAREABLE_DO

/****************************************************************************
 * VOB_MTYPE_IS_TYPE
 * Decide whether a meta-type represents a type object.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */
#ifdef VOB_ACL
#define VOB_MTYPE_IS_TYPE(mtype) \
    ((mtype) == VOB_MTYPE_ELEM_TYPE || \
     (mtype) == VOB_MTYPE_BRANCH_TYPE || \
     (mtype) == VOB_MTYPE_LABEL_TYPE || \
     (mtype) == VOB_MTYPE_ATTR_TYPE || \
     (mtype) == VOB_MTYPE_TRIGGER_TYPE || \
     (mtype) == VOB_MTYPE_HLINK_TYPE || \
     (mtype) == VOB_MTYPE_REPLICA_TYPE || \
     (mtype) == VOB_MTYPE_ACTIVITY_TYPE || \
     (mtype) == VOB_MTYPE_STATE_TYPE || \
     (mtype) == VOB_MTYPE_ROLEMAP || \
     (mtype) == VOB_MTYPE_POLICY)
#else
#define VOB_MTYPE_IS_TYPE(mtype) \
    ((mtype) == VOB_MTYPE_ELEM_TYPE || \
     (mtype) == VOB_MTYPE_BRANCH_TYPE || \
     (mtype) == VOB_MTYPE_LABEL_TYPE || \
     (mtype) == VOB_MTYPE_ATTR_TYPE || \
     (mtype) == VOB_MTYPE_TRIGGER_TYPE || \
     (mtype) == VOB_MTYPE_HLINK_TYPE || \
     (mtype) == VOB_MTYPE_REPLICA_TYPE || \
     (mtype) == VOB_MTYPE_ACTIVITY_TYPE || \
     (mtype) == VOB_MTYPE_STATE_TYPE)    
#endif /* VOB_ACL */

/****************************************************************************
 * VOB_MTYPE_CASE_TYPE
 * Switch statement case labels for type meta-types.
 */
#ifdef VOB_ACL
#define VOB_MTYPE_CASE_TYPE \
    case VOB_MTYPE_ELEM_TYPE: \
    case VOB_MTYPE_BRANCH_TYPE: \
    case VOB_MTYPE_LABEL_TYPE: \
    case VOB_MTYPE_ATTR_TYPE: \
    case VOB_MTYPE_TRIGGER_TYPE: \
    case VOB_MTYPE_HLINK_TYPE: \
    case VOB_MTYPE_REPLICA_TYPE: \
    case VOB_MTYPE_ACTIVITY_TYPE: \
    case VOB_MTYPE_STATE_TYPE: \
    case VOB_MTYPE_ROLEMAP: \
    case VOB_MTYPE_POLICY
#else
#define VOB_MTYPE_CASE_TYPE \
    case VOB_MTYPE_ELEM_TYPE: \
    case VOB_MTYPE_BRANCH_TYPE: \
    case VOB_MTYPE_LABEL_TYPE: \
    case VOB_MTYPE_ATTR_TYPE: \
    case VOB_MTYPE_TRIGGER_TYPE: \
    case VOB_MTYPE_HLINK_TYPE: \
    case VOB_MTYPE_REPLICA_TYPE: \
    case VOB_MTYPE_ACTIVITY_TYPE: \
    case VOB_MTYPE_STATE_TYPE
#endif /* VOB_ACL */

/****************************************************************************
 * VOB_MTYPE_IS_VOB_OBJ
 * Decide whether a meta-type represents a VOB object.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */

#define VOB_MTYPE_IS_VOB_OBJ(mtype) \
    ((mtype) == VOB_MTYPE_VOB || \
     (mtype) == VOB_MTYPE_REPLICA || \
     VOB_MTYPE_IS_ELEM (mtype) || \
     (mtype) == VOB_MTYPE_BRANCH || \
     VOB_MTYPE_IS_VERSION (mtype) || \
     (mtype) == VOB_MTYPE_SLINK || \
     (mtype) == VOB_MTYPE_DO || \
     (mtype) == VOB_MTYPE_HLINK || \
     (mtype) == VOB_MTYPE_POOL || \
     VOB_MTYPE_IS_TYPE (mtype) || \
     (mtype) == VOB_MTYPE_VIEW_DB || \
     VOB_MTYPE_IS_PVOB_OBJ (mtype))

/****************************************************************************
 * VOB_MTYPE_IS_PVOB_OBJ
 * Decide whether a meta-type represents a process VOB only object.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */

#define VOB_MTYPE_IS_PVOB_OBJ(mtype) \
    ((mtype) == VOB_MTYPE_CG_ACTIVITY || \
     (mtype) == VOB_MTYPE_ACTIVITY_TYPE || \
     (mtype) == VOB_MTYPE_STATE || \
     (mtype) == VOB_MTYPE_STATE_TYPE || \
     (mtype) == VOB_MTYPE_ROLE || \
     (mtype) == VOB_MTYPE_USER || \
     (mtype) == VOB_MTYPE_CHECKPOINT || \
     (mtype) == VOB_MTYPE_DOMAIN || \
     (mtype) == VOB_MTYPE_UCM_COMPONENT || \
     (mtype) == VOB_MTYPE_UCM_FOLDER || \
     (mtype) == VOB_MTYPE_UCM_PROJECT || \
     (mtype) == VOB_MTYPE_UCM_STREAM || \
     (mtype) == VOB_MTYPE_UCM_ACTIVITY || \
     (mtype) == VOB_MTYPE_UCM_TIMELINE || \
     (mtype) == VOB_MTYPE_UCM_BASELINE || \
     (mtype) == VOB_MTYPE_UCM_LEGACY_OBJ)

/*
 * FL7 switches CHECKPOINT to UCM_BASELINE.  Match either style with
 * these macros.
 */

/****************************************************************************
 * VOB_MTYPE_CASE_UCM_BASELINE
 * Switch statement case labels for UCM baseline meta-types.
 */
#define VOB_MTYPE_CASE_UCM_BASELINE \
    case VOB_MTYPE_UCM_BASELINE:    \
    case VOB_MTYPE_CHECKPOINT /* no : */

/****************************************************************************
 * VOB_MTYPE_IS_UCM_BASELINE
 * Decide whether a meta-type represents a UCM baseline
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */
#define VOB_MTYPE_IS_UCM_BASELINE(mtype) \
    ((mtype) == VOB_MTYPE_UCM_BASELINE || \
     (mtype) == VOB_MTYPE_CHECKPOINT)

/*
 * FL7 introduces new mtypes for formerly-CG_ACTIVITY objects.  Match
 * old or new style with these macros.
 */

/****************************************************************************
 * VOB_MTYPE_CASE_UCM_FC_AND_CGACT_OBJ
 * Switch statement case labels for UCM FC and legacy CG_ACTIVITY mtypes.
 */
#define VOB_MTYPE_CASE_UCM_FC_AND_CGACT_OBJ \
     case VOB_MTYPE_CG_ACTIVITY: \
     case VOB_MTYPE_UCM_FOLDER: \
     case VOB_MTYPE_UCM_PROJECT: \
     case VOB_MTYPE_UCM_STREAM: \
     case VOB_MTYPE_UCM_COMPONENT: \
     case VOB_MTYPE_UCM_ACTIVITY: \
     case VOB_MTYPE_UCM_TIMELINE /* no : */

/****************************************************************************
 * VOB_MTYPE_IS_UCM_FC_AND_CGACT_OBJ
 * Decide whether a mtype is a UCM FC or CG_ACTIVITY object.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */
#define VOB_MTYPE_IS_UCM_FC_AND_CGACT_OBJ(mtype) \
    ((mtype) == VOB_MTYPE_CG_ACTIVITY || \
     (mtype) == VOB_MTYPE_UCM_FOLDER || \
     (mtype) == VOB_MTYPE_UCM_PROJECT || \
     (mtype) == VOB_MTYPE_UCM_STREAM || \
     (mtype) == VOB_MTYPE_UCM_COMPONENT || \
     (mtype) == VOB_MTYPE_UCM_ACTIVITY || \
     (mtype) == VOB_MTYPE_UCM_TIMELINE)

/****************************************************************************
 * VOB_MTYPE_CASE_UCM_FC_CGACT_OBJ
 * Switch statement case labels for FL7-only representation of UCM
 * objects that used to be CG_ACTIVITY meta-types.  (omits CG_ACTIVITY)
 */
#define VOB_MTYPE_CASE_UCM_FC_CGACT_OBJ \
     case VOB_MTYPE_UCM_FOLDER: \
     case VOB_MTYPE_UCM_PROJECT: \
     case VOB_MTYPE_UCM_STREAM: \
     case VOB_MTYPE_UCM_COMPONENT: \
     case VOB_MTYPE_UCM_ACTIVITY: \
     case VOB_MTYPE_UCM_TIMELINE /* no : */

/****************************************************************************
 * VOB_MTYPE_IS_UCM_FC_CGACT_OBJ
 * Decide whether a meta-type represents a UCM VOB object in FL7 that
 * used to be CG_ACTIVITY.  (omits CG_ACTIVITY)
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */
#define VOB_MTYPE_IS_UCM_FC_CGACT_OBJ(mtype) \
    ((mtype) == VOB_MTYPE_UCM_FOLDER || \
     (mtype) == VOB_MTYPE_UCM_PROJECT || \
     (mtype) == VOB_MTYPE_UCM_STREAM || \
     (mtype) == VOB_MTYPE_UCM_COMPONENT || \
     (mtype) == VOB_MTYPE_UCM_ACTIVITY || \
     (mtype) == VOB_MTYPE_UCM_TIMELINE)

/****************************************************************************
 * VOB_MTYPE_CASE_UCM_FC_VOB_OBJ
 * Switch statement case labels for UCM VOB object meta-types.
 */
#define VOB_MTYPE_CASE_UCM_FC_VOB_OBJ \
     case VOB_MTYPE_UCM_FOLDER: \
     case VOB_MTYPE_UCM_PROJECT: \
     case VOB_MTYPE_UCM_STREAM: \
     case VOB_MTYPE_UCM_COMPONENT: \
     case VOB_MTYPE_UCM_ACTIVITY: \
     case VOB_MTYPE_UCM_BASELINE: \
     case VOB_MTYPE_UCM_TIMELINE: \
     case VOB_MTYPE_UCM_LEGACY_OBJ /* no : */

/****************************************************************************
 * VOB_MTYPE_IS_UCM_FC_VOB_OBJ
 * Decide whether a meta-type represents a UCM VOB object.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */

#define VOB_MTYPE_IS_UCM_FC_VOB_OBJ(mtype) \
    ((mtype) == VOB_MTYPE_UCM_FOLDER || \
     (mtype) == VOB_MTYPE_UCM_PROJECT || \
     (mtype) == VOB_MTYPE_UCM_STREAM || \
     (mtype) == VOB_MTYPE_UCM_COMPONENT || \
     (mtype) == VOB_MTYPE_UCM_ACTIVITY || \
     (mtype) == VOB_MTYPE_UCM_BASELINE || \
     (mtype) == VOB_MTYPE_UCM_TIMELINE || \
     (mtype) == VOB_MTYPE_UCM_LEGACY_OBJ)

/****************************************************************************
 * VOB_MTYPE_IS_UCM_VOB_OBJ_AT_FLEVEL
 * Decide whether a meta-type represents a UCM VOB object specific to feature level
 * IN   flevel          feature level
 * IN	mtype		meta-type
 */
#define VOB_MTYPE_IS_UCM_VOB_OBJ_AT_FLEVEL(flevel, mtype) \
    ((flevel >= VOB_FEATURE_LEVEL_UCM_FC_OBJS) ? \
     VOB_MTYPE_IS_UCM_FC_VOB_OBJ(mtype) : mtype == VOB_MTYPE_CG_ACTIVITY)

/****************************************************************************
 * VOB_MTYPE_CASE_VOB_OBJ
 * Switch statement case labels for VOB object meta-types.
 */

#define VOB_MTYPE_CASE_VOB_OBJ \
     case VOB_MTYPE_VOB: \
     case VOB_MTYPE_REPLICA: \
     VOB_MTYPE_CASE_ELEM: \
     case VOB_MTYPE_BRANCH: \
     VOB_MTYPE_CASE_VERSION: \
     case VOB_MTYPE_SLINK: \
     case VOB_MTYPE_DO: \
     case VOB_MTYPE_HLINK: \
     case VOB_MTYPE_POOL: \
     case VOB_MTYPE_CG_ACTIVITY: \
     case VOB_MTYPE_STATE: \
     case VOB_MTYPE_ROLE: \
     case VOB_MTYPE_USER: \
     case VOB_MTYPE_CHECKPOINT: \
     case VOB_MTYPE_DOMAIN: \
     case VOB_MTYPE_VIEW_DB: \
     VOB_MTYPE_CASE_UCM_FC_VOB_OBJ: \
     VOB_MTYPE_CASE_TYPE

/****************************************************************************
 * VOB_MTYPE_IS_REPLICATED
 * Decide whether objects of a given meta-type are propagated to VOB replicas.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */
#ifdef VOB_ACL
#define VOB_MTYPE_IS_REPLICATED(mtype) \
    ((mtype) == VOB_MTYPE_VOB || \
     (mtype) == VOB_MTYPE_REPLICA || \
     VOB_MTYPE_IS_ELEM (mtype) || \
     (mtype) == VOB_MTYPE_BRANCH || \
     VOB_MTYPE_IS_VERSION (mtype) || \
     (mtype) == VOB_MTYPE_SLINK || \
     (mtype) == VOB_MTYPE_HLINK || \
     (mtype) == VOB_MTYPE_STATE || \
     (mtype) == VOB_MTYPE_ROLE || \
     (mtype) == VOB_MTYPE_USER || \
     (mtype) == VOB_MTYPE_CHECKPOINT || \
     (mtype) == VOB_MTYPE_DOMAIN || \
     (mtype) == VOB_MTYPE_CG_ACTIVITY || \
     (mtype) == VOB_MTYPE_VIEW_DB || \
     (mtype) == VOB_MTYPE_ELEM_TYPE || \
     (mtype) == VOB_MTYPE_BRANCH_TYPE || \
     (mtype) == VOB_MTYPE_LABEL_TYPE || \
     (mtype) == VOB_MTYPE_ATTR_TYPE || \
     (mtype) == VOB_MTYPE_HLINK_TYPE || \
     (mtype) == VOB_MTYPE_REPLICA_TYPE || \
     (mtype) == VOB_MTYPE_ACTIVITY_TYPE || \
     (mtype) == VOB_MTYPE_STATE_TYPE || \
     (mtype) == VOB_MTYPE_ROLEMAP || \
     (mtype) == VOB_MTYPE_POLICY || \
     (mtype) == VOB_MTYPE_UCM_COMPONENT || \
     (mtype) == VOB_MTYPE_UCM_FOLDER || \
     (mtype) == VOB_MTYPE_UCM_PROJECT || \
     (mtype) == VOB_MTYPE_UCM_STREAM || \
     (mtype) == VOB_MTYPE_UCM_ACTIVITY || \
     (mtype) == VOB_MTYPE_UCM_TIMELINE || \
     (mtype) == VOB_MTYPE_UCM_BASELINE || \
     (mtype) == VOB_MTYPE_UCM_LEGACY_OBJ)
#else
#define VOB_MTYPE_IS_REPLICATED(mtype) \
    ((mtype) == VOB_MTYPE_VOB || \
     (mtype) == VOB_MTYPE_REPLICA || \
     VOB_MTYPE_IS_ELEM (mtype) || \
     (mtype) == VOB_MTYPE_BRANCH || \
     VOB_MTYPE_IS_VERSION (mtype) || \
     (mtype) == VOB_MTYPE_SLINK || \
     (mtype) == VOB_MTYPE_HLINK || \
     (mtype) == VOB_MTYPE_STATE || \
     (mtype) == VOB_MTYPE_ROLE || \
     (mtype) == VOB_MTYPE_USER || \
     (mtype) == VOB_MTYPE_CHECKPOINT || \
     (mtype) == VOB_MTYPE_DOMAIN || \
     (mtype) == VOB_MTYPE_CG_ACTIVITY || \
     (mtype) == VOB_MTYPE_VIEW_DB || \
     (mtype) == VOB_MTYPE_ELEM_TYPE || \
     (mtype) == VOB_MTYPE_BRANCH_TYPE || \
     (mtype) == VOB_MTYPE_LABEL_TYPE || \
     (mtype) == VOB_MTYPE_ATTR_TYPE || \
     (mtype) == VOB_MTYPE_HLINK_TYPE || \
     (mtype) == VOB_MTYPE_REPLICA_TYPE || \
     (mtype) == VOB_MTYPE_ACTIVITY_TYPE || \
     (mtype) == VOB_MTYPE_STATE_TYPE || \
     (mtype) == VOB_MTYPE_UCM_COMPONENT || \
     (mtype) == VOB_MTYPE_UCM_FOLDER || \
     (mtype) == VOB_MTYPE_UCM_PROJECT || \
     (mtype) == VOB_MTYPE_UCM_STREAM || \
     (mtype) == VOB_MTYPE_UCM_ACTIVITY || \
     (mtype) == VOB_MTYPE_UCM_TIMELINE || \
     (mtype) == VOB_MTYPE_UCM_BASELINE || \
     (mtype) == VOB_MTYPE_UCM_LEGACY_OBJ)
#endif

/****************************************************************************
 * VOB_MTYPE_HAS_NAME
 * Decide whether a meta-type represents an object that has a name.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */
#ifdef VOB_ACL
#define VOB_MTYPE_HAS_NAME(mtype) \
    (VOB_MTYPE_IS_TYPE (mtype) || \
     (mtype) == VOB_MTYPE_POOL || \
     (mtype) == VOB_MTYPE_REPLICA || \
     (mtype) == VOB_MTYPE_CG_ACTIVITY || \
     (mtype) == VOB_MTYPE_STATE || \
     (mtype) == VOB_MTYPE_ROLE || \
     (mtype) == VOB_MTYPE_USER || \
     (mtype) == VOB_MTYPE_CHECKPOINT || \
     (mtype) == VOB_MTYPE_DOMAIN || \
     (mtype) == VOB_MTYPE_UCM_COMPONENT || \
     (mtype) == VOB_MTYPE_UCM_FOLDER || \
     (mtype) == VOB_MTYPE_UCM_PROJECT || \
     (mtype) == VOB_MTYPE_UCM_STREAM || \
     (mtype) == VOB_MTYPE_UCM_ACTIVITY || \
     (mtype) == VOB_MTYPE_UCM_TIMELINE || \
     (mtype) == VOB_MTYPE_POLICY || \
     (mtype) == VOB_MTYPE_ROLEMAP || \
     (mtype) == VOB_MTYPE_UCM_BASELINE || \
     (mtype) == VOB_MTYPE_UCM_LEGACY_OBJ)
#else
#define VOB_MTYPE_HAS_NAME(mtype) \
    (VOB_MTYPE_IS_TYPE (mtype) || \
     (mtype) == VOB_MTYPE_POOL || \
     (mtype) == VOB_MTYPE_REPLICA || \
     (mtype) == VOB_MTYPE_CG_ACTIVITY || \
     (mtype) == VOB_MTYPE_STATE || \
     (mtype) == VOB_MTYPE_ROLE || \
     (mtype) == VOB_MTYPE_USER || \
     (mtype) == VOB_MTYPE_CHECKPOINT || \
     (mtype) == VOB_MTYPE_DOMAIN || \
     (mtype) == VOB_MTYPE_UCM_COMPONENT || \
     (mtype) == VOB_MTYPE_UCM_FOLDER || \
     (mtype) == VOB_MTYPE_UCM_PROJECT || \
     (mtype) == VOB_MTYPE_UCM_STREAM || \
     (mtype) == VOB_MTYPE_UCM_ACTIVITY || \
     (mtype) == VOB_MTYPE_UCM_TIMELINE || \
     (mtype) == VOB_MTYPE_UCM_BASELINE || \
     (mtype) == VOB_MTYPE_UCM_LEGACY_OBJ)
#endif /* VOB_ACL */

/****************************************************************************
 * VOB_MTYPE_HAS_GEND_NAME
 * Decide whether a meta-type can be an object with a generated name.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */

#define VOB_MTYPE_HAS_GEND_NAME(mtype) \
      ((mtype) == VOB_MTYPE_CG_ACTIVITY || \
      (mtype) == VOB_MTYPE_CHECKPOINT || \
      (mtype) == VOB_MTYPE_UCM_ACTIVITY || \
      (mtype) == VOB_MTYPE_UCM_STREAM || \
      (mtype) == VOB_MTYPE_UCM_PROJECT || \
      (mtype) == VOB_MTYPE_UCM_FOLDER || \
      (mtype) == VOB_MTYPE_UCM_TIMELINE || \
      (mtype) == VOB_MTYPE_UCM_COMPONENT || \
      (mtype) == VOB_MTYPE_UCM_BASELINE || \
      (mtype) == VOB_MTYPE_UCM_LEGACY_OBJ)

/****************************************************************************
 * VOB_MTYPE_HAS_SELECTOR
 * Decide whether a meta-type represents an object that has an object selector
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */

#define VOB_MTYPE_HAS_SELECTOR(mtype) \
    (VOB_MTYPE_HAS_NAME (mtype) || \
     (mtype) == VOB_MTYPE_VOB || \
     (mtype) == VOB_MTYPE_HLINK)

/****************************************************************************
 * VOB_MTYPE_HAS_TYPE
 * Decide whether a meta-type represents an object that has a type.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */

#define VOB_MTYPE_HAS_TYPE(mtype) \
    (VOB_MTYPE_IS_ELEM (mtype) || \
     (mtype) == VOB_MTYPE_BRANCH || \
     (mtype) == VOB_MTYPE_HLINK || \
     (mtype) == VOB_MTYPE_REPLICA || \
     (mtype) == VOB_MTYPE_CG_ACTIVITY || \
     (mtype) == VOB_MTYPE_STATE)

/****************************************************************************
 * VOB_MTYPE_HAS_FS_NAME
 * Decide whether a meta-type represents an object that has a file system name.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */

#define VOB_MTYPE_HAS_FS_NAME(mtype) \
    (VOB_MTYPE_IS_ELEM (mtype) || \
     VOB_MTYPE_IS_VERSION (mtype) || \
     (mtype) == VOB_MTYPE_DO || \
     (mtype) == VOB_MTYPE_NSHAREABLE_DO || \
     (mtype) == VOB_MTYPE_BRANCH || \
     (mtype) == VOB_MTYPE_SLINK)

/****************************************************************************
 * VOB_MTYPE_HAS_OWNER_GROUP
 * Decide whether a meta-type represents an object that has an owner and group.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */
#ifdef VOB_ACL
#define VOB_MTYPE_HAS_OWNER_GROUP(mtype) \
    (VOB_MTYPE_HAS_FS_NAME (mtype) || \
     VOB_MTYPE_IS_TYPE (mtype) || \
     (mtype) == VOB_MTYPE_HLINK || \
     (mtype) == VOB_MTYPE_REPLICA || \
     (mtype) == VOB_MTYPE_POOL || \
     (mtype) == VOB_MTYPE_CG_ACTIVITY || \
     (mtype) == VOB_MTYPE_STATE || \
     (mtype) == VOB_MTYPE_ROLE || \
     (mtype) == VOB_MTYPE_USER || \
     (mtype) == VOB_MTYPE_CHECKPOINT || \
     (mtype) == VOB_MTYPE_DOMAIN || \
     (mtype) == VOB_MTYPE_UCM_COMPONENT || \
     (mtype) == VOB_MTYPE_UCM_FOLDER || \
     (mtype) == VOB_MTYPE_UCM_PROJECT || \
     (mtype) == VOB_MTYPE_UCM_STREAM || \
     (mtype) == VOB_MTYPE_UCM_ACTIVITY || \
     (mtype) == VOB_MTYPE_UCM_TIMELINE || \
     (mtype) == VOB_MTYPE_POLICY || \
     (mtype) == VOB_MTYPE_ROLEMAP || \
     (mtype) == VOB_MTYPE_UCM_BASELINE || \
     (mtype) == VOB_MTYPE_UCM_LEGACY_OBJ)
#else
#define VOB_MTYPE_HAS_OWNER_GROUP(mtype) \
    (VOB_MTYPE_HAS_FS_NAME (mtype) || \
     VOB_MTYPE_IS_TYPE (mtype) || \
     (mtype) == VOB_MTYPE_HLINK || \
     (mtype) == VOB_MTYPE_REPLICA || \
     (mtype) == VOB_MTYPE_POOL || \
     (mtype) == VOB_MTYPE_CG_ACTIVITY || \
     (mtype) == VOB_MTYPE_STATE || \
     (mtype) == VOB_MTYPE_ROLE || \
     (mtype) == VOB_MTYPE_USER || \
     (mtype) == VOB_MTYPE_CHECKPOINT || \
     (mtype) == VOB_MTYPE_DOMAIN || \
     (mtype) == VOB_MTYPE_UCM_COMPONENT || \
     (mtype) == VOB_MTYPE_UCM_FOLDER || \
     (mtype) == VOB_MTYPE_UCM_PROJECT || \
     (mtype) == VOB_MTYPE_UCM_STREAM || \
     (mtype) == VOB_MTYPE_UCM_ACTIVITY || \
     (mtype) == VOB_MTYPE_UCM_TIMELINE || \
     (mtype) == VOB_MTYPE_UCM_BASELINE || \
     (mtype) == VOB_MTYPE_UCM_LEGACY_OBJ)
#endif /* VOB_ACL */

/****************************************************************************
 * VOB_MTYPE_HAS_LABELS
 * Decide whether a meta-type represents an object that can have labels.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */

#define VOB_MTYPE_HAS_LABELS(mtype) \
     VOB_MTYPE_IS_VERSION (mtype)

/****************************************************************************
 * VOB_MTYPE_HAS_ATTRS
 * Decide whether a meta-type represents an object that can have attributes.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */

#define VOB_MTYPE_HAS_ATTRS(mtype) \
    (VOB_MTYPE_IS_VOB_OBJ (mtype) && !((mtype) == VOB_MTYPE_VIEW_DB))

/****************************************************************************
 * VOB_MTYPE_HAS_HLINKS
 * Decide whether a meta-type represents an object that can have hyperlinks.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */

#define VOB_MTYPE_HAS_HLINKS(mtype) \
    (VOB_MTYPE_IS_VOB_OBJ (mtype) && !((mtype) == VOB_MTYPE_HLINK))

/****************************************************************************
 * VOB_MTYPE_HAS_TRIGGERS
 * Decide whether a meta-type represents an object that can have triggers.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */

#define VOB_MTYPE_HAS_TRIGGERS(mtype) \
     (VOB_MTYPE_IS_ELEM (mtype) || \
     (mtype) == VOB_MTYPE_CG_ACTIVITY || \
     (mtype) == VOB_MTYPE_UCM_COMPONENT || \
     (mtype) == VOB_MTYPE_UCM_FOLDER || \
     (mtype) == VOB_MTYPE_UCM_PROJECT || \
     (mtype) == VOB_MTYPE_UCM_STREAM || \
     (mtype) == VOB_MTYPE_UCM_ACTIVITY || \
     (mtype) == VOB_MTYPE_CHECKPOINT || \
     (mtype) == VOB_MTYPE_UCM_LEGACY_OBJ || \
     (mtype) == VOB_MTYPE_UCM_BASELINE)

/****************************************************************************
 * VOB_MTYPE_HAS_MASTER
 * Decide whether a meta-type represents an object that has a master replica.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */

#ifdef VOB_ACL
#define VOB_MTYPE_HAS_MASTER(mtype) \
    (VOB_MTYPE_IS_ELEM (mtype) || \
     (mtype) == VOB_MTYPE_BRANCH || \
     (VOB_MTYPE_IS_TYPE (mtype) && \
      !((mtype) == VOB_MTYPE_TRIGGER_TYPE)) || \
     (mtype) == VOB_MTYPE_HLINK || \
     (mtype) == VOB_MTYPE_REPLICA || \
     (mtype) == VOB_MTYPE_VOB || \
     (mtype) == VOB_MTYPE_SLINK || \
     (mtype) == VOB_MTYPE_CG_ACTIVITY || \
     (mtype) == VOB_MTYPE_STATE || \
     (mtype) == VOB_MTYPE_ROLE || \
     (mtype) == VOB_MTYPE_USER || \
     (mtype) == VOB_MTYPE_CHECKPOINT || \
     (mtype) == VOB_MTYPE_DOMAIN || \
     (mtype) == VOB_MTYPE_POLICY || \
     (mtype) == VOB_MTYPE_ROLEMAP  || \
     (mtype) == VOB_MTYPE_UCM_STREAM || \
     (mtype) == VOB_MTYPE_UCM_COMPONENT || \
     (mtype) == VOB_MTYPE_UCM_FOLDER || \
     (mtype) == VOB_MTYPE_UCM_PROJECT || \
     (mtype) == VOB_MTYPE_UCM_ACTIVITY || \
     (mtype) == VOB_MTYPE_UCM_TIMELINE || \
     (mtype) == VOB_MTYPE_UCM_BASELINE || \
     (mtype) == VOB_MTYPE_UCM_LEGACY_OBJ)
#else
#define VOB_MTYPE_HAS_MASTER(mtype) \
    (VOB_MTYPE_IS_ELEM (mtype) || \
     (mtype) == VOB_MTYPE_BRANCH || \
     (VOB_MTYPE_IS_TYPE (mtype) && \
      !((mtype) == VOB_MTYPE_TRIGGER_TYPE)) || \
     (mtype) == VOB_MTYPE_HLINK || \
     (mtype) == VOB_MTYPE_REPLICA || \
     (mtype) == VOB_MTYPE_VOB || \
     (mtype) == VOB_MTYPE_SLINK || \
     (mtype) == VOB_MTYPE_CG_ACTIVITY || \
     (mtype) == VOB_MTYPE_STATE || \
     (mtype) == VOB_MTYPE_ROLE || \
     (mtype) == VOB_MTYPE_USER || \
     (mtype) == VOB_MTYPE_CHECKPOINT || \
     (mtype) == VOB_MTYPE_DOMAIN || \
     (mtype) == VOB_MTYPE_UCM_STREAM || \
     (mtype) == VOB_MTYPE_UCM_COMPONENT || \
     (mtype) == VOB_MTYPE_UCM_FOLDER || \
     (mtype) == VOB_MTYPE_UCM_PROJECT || \
     (mtype) == VOB_MTYPE_UCM_ACTIVITY || \
     (mtype) == VOB_MTYPE_UCM_TIMELINE || \
     (mtype) == VOB_MTYPE_UCM_BASELINE || \
     (mtype) == VOB_MTYPE_UCM_LEGACY_OBJ)
#endif

/****************************************************************************
 * VOB_MTYPE_HAS_MASTER_KIND
 * Decide whether a meta-type represents an object with a master kind.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */

#define VOB_MTYPE_HAS_MASTER_KIND(mtype) \
    ((mtype) == VOB_MTYPE_ATTR_TYPE || \
     (mtype) == VOB_MTYPE_LABEL_TYPE || \
     (mtype) == VOB_MTYPE_HLINK_TYPE || \
     (mtype) == VOB_MTYPE_ACTIVITY_TYPE || \
     (mtype) == VOB_MTYPE_STATE_TYPE)

/****************************************************************************
 * VOB_MTYPE_HAS_MOD_MASTER
 * Decide whether a meta-type represents an object that can be marked 
 * as 'modifiable at master replica only'
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */

#define VOB_MTYPE_HAS_MOD_MASTER(mtype) \
    ((mtype) == VOB_MTYPE_LABEL_TYPE)

/****************************************************************************
 * VOB_MTYPE_HAS_GLOBALNESS
 * Decide whether a meta-type represents an object with a global definitions.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */

#define VOB_MTYPE_HAS_GLOBALNESS(mtype) \
    ((mtype) == VOB_MTYPE_ELEM_TYPE || \
     (mtype) == VOB_MTYPE_BRANCH_TYPE || \
     (mtype) == VOB_MTYPE_LABEL_TYPE || \
     (mtype) == VOB_MTYPE_ATTR_TYPE || \
     (mtype) == VOB_MTYPE_HLINK_TYPE) /* || \
     (mtype) == VOB_MTYPE_ROLEMAP || \
     (mtype) == VOB_MTYPE_POLICY) */

/****************************************************************************
 * VOB_MTYPE_HAS_ROLEMAP
 * Decide whether a meta-type represents an object that has a rolemap.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */
#ifdef VOB_ACL
#define VOB_MTYPE_HAS_ROLEMAP(mtype)            \
    (VOB_MTYPE_IS_ELEM(mtype) ||                \
     (mtype) == VOB_MTYPE_POLICY ||             \
     (mtype) == VOB_MTYPE_ROLEMAP ||            \
     (mtype) == VOB_MTYPE_VOB                   \
    )
#endif

/****************************************************************************
 * VOB_MTYPE_CASE_ROLEMAP
 * Switch statement case labels for objects with rolemaps.  Should match
 * VOB_MTYPE_HAS_ROLEMAP() above.  Final ":" comes from the calling site.
 */
#ifdef VOB_ACL
#define VOB_MTYPE_CASE_ROLEMAP                  \
     VOB_MTYPE_CASE_ELEM:                       \
     case VOB_MTYPE_POLICY:                     \
     case VOB_MTYPE_ROLEMAP:                    \
     case VOB_MTYPE_VOB
#endif

/****************************************************************************
 * VOB_MTYPE_HAS_ACL_ENFORCEMENT
 * Decide whether a meta-type represents an object that has ACLs enforced.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */
/* No VOB_MTYPE_CG_ACTIVITY, since those only represent UCM objects in
 * FL6 or lower, while ACLs only show up in FL7 or higher */
#ifdef VOB_ACL
#define VOB_MTYPE_HAS_ACL_ENFORCEMENT(mtype)    \
    (VOB_MTYPE_IS_ELEM(mtype) ||                \
     VOB_MTYPE_IS_VERSION(mtype) ||             \
     (mtype) == VOB_MTYPE_POLICY ||             \
     (mtype) == VOB_MTYPE_ROLEMAP ||            \
     (mtype) == VOB_MTYPE_VOB                   \
    )
#endif

/****************************************************************************
 * VOB_MTYPE_CASE_ACL_ENFORCEMENT Switch statement case labels for
 * objects with ACLs enforced.  Should match
 * VOB_MTYPE_HAS_ACL_ENFORCEMENT() above.  Final ":" comes from the
 * calling site.
 */
#ifdef VOB_ACL
#define VOB_MTYPE_CASE_ACL_ENFORCEMENT          \
     VOB_MTYPE_CASE_ELEM:                       \
     case VOB_MTYPE_POLICY:                     \
     case VOB_MTYPE_ROLEMAP:                    \
     case VOB_MTYPE_VOB
#endif

/****************************************************************************
 * VOB_MTYPE_CAN_LOCK
 * Decide whether a meta-type represents an object that can be locked.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */

#define VOB_MTYPE_CAN_LOCK(mtype) \
     (VOB_MTYPE_IS_ELEM (mtype) || \
      (mtype) == VOB_MTYPE_BRANCH || \
      VOB_MTYPE_IS_TYPE (mtype) || \
      VOB_MTYPE_IS_VERSION (mtype) || \
      (mtype) == VOB_MTYPE_POOL || \
      (mtype) == VOB_MTYPE_VOB || \
      (mtype) == VOB_MTYPE_CG_ACTIVITY || \
      (mtype) == VOB_MTYPE_STATE || \
      (mtype) == VOB_MTYPE_ROLE || \
      (mtype) == VOB_MTYPE_USER || \
      (mtype) == VOB_MTYPE_CHECKPOINT || \
      (mtype) == VOB_MTYPE_DOMAIN || \
      (mtype) == VOB_MTYPE_UCM_COMPONENT || \
      (mtype) == VOB_MTYPE_UCM_FOLDER || \
      (mtype) == VOB_MTYPE_UCM_PROJECT || \
      (mtype) == VOB_MTYPE_UCM_STREAM || \
      (mtype) == VOB_MTYPE_UCM_ACTIVITY || \
      (mtype) == VOB_MTYPE_UCM_LEGACY_OBJ || \
      (mtype) == VOB_MTYPE_UCM_BASELINE)

/****************************************************************************
 * VOB_MTYPE_SVR_REQMASTER
 * Decide whether a meta-type represents an object that can be reqmastered
 * at the server site. This macro can only be used in the server code
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */

#define VOB_MTYPE_SVR_REQMASTER(mtype) \
      ((mtype) == VOB_MTYPE_BRANCH || \
      (mtype) == VOB_MTYPE_BRANCH_TYPE || \
      (mtype) == VOB_MTYPE_VERSION || \
      (mtype) == VOB_MTYPE_DO_VERSION || \
      (mtype) == VOB_MTYPE_NSDIR_VERSION)

/****************************************************************************
 * VOB_MTYPE_CAN_REQMASTER
 * Decide whether a meta-type represents an object that can be reqmastered.
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */

#define VOB_MTYPE_CAN_REQMASTER(mtype) \
      ((mtype) == VOB_MTYPE_BRANCH || \
      (mtype) == VOB_MTYPE_BRANCH_TYPE)

/****************************************************************************
 * VOB_MTYPE_HAS_VISIBLE_ACCESS_CHECK
 * Decide whether a meta-type represents an object with visible access checks
 * IN	mtype		meta-type
 * RESULT:		TRUE if yes; FALSE if no
 */
#ifdef VOB_ACL
#define VOB_MTYPE_HAS_VISIBLE_ACCESS_CHECK(mtype)       \
     ((mtype) == VOB_MTYPE_POLICY ||                    \
      (mtype) == VOB_MTYPE_ROLEMAP)
#endif

/****************************************************************************
 * VOB_NUM_MTYPES_BY_FL
 * Returns the maximum supported mtype for a given feature level
 * IN	flevel		feature level
 * RESULT:		maximum mtype supported at that feature level
 */

/* Up through FL6 we only know about mtypes up through VOB_MTYPE_DOMAIN */

#define VOB_NUM_MTYPES_BY_FL(flevel)			\
      ((flevel <= VOB_FEATURE_LEVEL_6) ? VOB_NUM_MTYPES_FL6 : \
        VOB_NUM_MTYPES_FL7)
#ifdef VOB_ACL
    /* Check above macro when ACLs */
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _VOB_MTYPE_H_ */
/* $Id: 7a063bf4.45b611e0.96c7.00:01:80:c4:d5:44 $ */
