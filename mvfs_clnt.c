/* * (C) Copyright IBM Corporation 1991, 2014. */
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
/* mvfs_clnt.c */

/*
 * This is the MFS's client interface to the view server.  It encapsulates
 * the core of the calls in the view RPC trait.  You will find
 * "bindroot" in mfs_vnodeops.c and "get cleartext" in mfs_clearops.c
 *
 * WARNING:  when you add new view server calls in here, you must make
 * 	     sure the trait descriptions (operation names and timeout 
 *	     shifts) are correct in mfs_utils.c or crashes may result.
 *
 * NOTE ON NAME CACHE:
 *	To promote consistency and avoid all those nice race conditions 
 *	between an update to the view and the name cache contents, 
 *	all operations that add/remove names
 *	from a dir should be single threaded with the name cache updates
 *      by an MLOCK/MUNLOCK pair on the dir vnode around both the mfscall
 *	to the view server, and the name cache add/remove op.  The
 *	name cache add/remove ops REQUIRE (via assert) that the dir 
 *      vnode is locked.  This is not required for the name cache
 *	flushing ops. 
 */

#include "mvfs_systm.h"
#include "mvfs.h"
#include <tbs_errno.h>
/* for dnc op prototypes */
#include "mvfs_dnc.h"

/*
 * Some platforms have limited stack space and we need to save what we
 * can.  The RPC request/reply structures can be quite big on a 64-bit
 * system, so we allocate them from the heap.
 */
#define HEAP_DEFINE(type, var) type *var
#define HEAP_ALLOC1(type, var) var = (type *)KMEM_ALLOC(sizeof(type), KM_SLEEP)
#define HEAP_ALLOC(type, var)   \
    HEAP_DEFINE(type, var);     \
    HEAP_ALLOC1(type, var);
#define HEAP_FREE(var) KMEM_FREE(var, sizeof(*var))
#define HEAP_ALLOC2(type1, var1, type2, var2)   \
    HEAP_DEFINE(type1, var1);                   \
    HEAP_DEFINE(type2, var2);                   \
    HEAP_ALLOC1(type1, var1);                   \
    if (var1 == NULL)                           \
        return ENOMEM;                          \
    HEAP_ALLOC1(type2, var2);                   \
    if (var2 == NULL) {                         \
        HEAP_FREE(var1);                        \
        return ENOMEM;                          \
    }
#define HEAP_ALLOC3(type1, var1, type2, var2, type3, var3) \
    HEAP_DEFINE(type1, var1);			\
    HEAP_DEFINE(type2, var2);			\
    HEAP_DEFINE(type3, var3);			\
    HEAP_ALLOC1(type1, var1);			\
    if (var1 == NULL)				\
        return ENOMEM;				\
    HEAP_ALLOC1(type2, var2);			\
    if (var2 == NULL) {				\
        HEAP_FREE(var1);			\
        return ENOMEM;				\
    }                                           \
    HEAP_ALLOC1(type3, var3);                    \
    if (var3 == NULL) {                         \
        HEAP_FREE(var2);                        \
        HEAP_FREE(var1);                        \
        return ENOMEM;                          \
    }

#define PROVIDE_V8_COMPAT

/* Macros to manage our downrev server compatibility story.  We now say that a
** client has to handle a server one release back.  This avoids the previous
** requirement that you upgrade all servers before any clients.  These macros
** actually implement support for one version older RPCs (rather than one
** release version back).  The key is to invoke the new RPC, as usual, but then
** check the (new, see below) rpc_status return code for RPC_PROCUNAVAIL.  That
** indicates that the server doesn't implement that RPC, so then fall back and
** try the previous RPC.  That's done by creating an old type reply structure,
** doing the old RPC, and then converting the old reply to a new reply so the
** rest of the code remains the same.  We also set a flag saying the server is
** downrev to save doing the wrong RPC the next time.
**
** Note, mvfs_vwcall() (renamed from mfs_vwcall() because of this change) calls
** mfscall_int() that can get RPC_PROCUNAVAIL from the actual RPC.  It then
** turns that into ECOMM (if it is defined, or into rpcerr.re_errno on some
** platforms, e.g. EIO or EINVAL on AIX), thus losing the actual reason for the
** RPC failure.  If the kernel RPC returns RPC_PROCUNAVAIL that means the server
** side didn't even get a chance to respond (that is, our code isn't even called
** since the kernel RPC handler returns that error).  So, TBS_ST_XREV_COMPAT
** can't be set by our code.  In any case, the only view RPC that can return
** TBS_ST_XREV_COMPAT is VIEW_CONTACT, which it can do because it can compute it
** (and it is always the same RPC number, hence can't be RPC_PROCUNAVAIL).  We
** don't currently use that RPC, although we could consider using it instead of
** the VIEW_NULL RPC at some point.  So, mvfs_vwcall() has been changed to
** return the actual RPC status (as well as its normal error code) so we can
** figure out what's going on and implement the xrev server checking described
** above.
**
** Add comments here explaining each upgrade:
**
** For the upgrade from V8, the only change was in the view_vstat_t used in a
** number of RPC reply structures. None of the request structures are different.
*/

/* Allocate the request and reply structures.  They must be named "rap" and
** "rrp" so the other macros can use them.  The other macros also use
** "rpc_status" and "error", so declare them here, too.
*/
#define HEAP_ALLOC_RPC_ARGS(arg_type)         \
    int error = 0;                            \
    enum clnt_stat rpc_status = RPC_SUCCESS;  \
    HEAP_ALLOC2(arg_type ## _req_t, rap, arg_type ## _reply_t, rrp)

/* Allocate request and reply structures and a structure containing what
** were stack variables if stack overflow is a problem.  The restrictions
** on request and reply structures are as documented above.
*/
#define HEAP_ALLOC_RPC_ARGS_AND_VARS(arg_type, type3, var3)         \
    int error = 0;                            \
    enum clnt_stat rpc_status = RPC_SUCCESS;  \
    HEAP_ALLOC3(arg_type ## _req_t, rap, arg_type ## _reply_t, rrp, type3, var3)

/* This helper macro gets the types right based on the downrev name suffix. */
#define _MVFS_VWCALL(vwvp, vfsp, op, arg_type, opx, argx, cred)   \
    mvfs_vwcall(vwvp, vfsp, op ## opx,                            \
                (xdrproc_t)xdr_ ## arg_type ## _req_t,            \
                (caddr_t)rap,                                     \
                (xdrproc_t)xdr_ ## arg_type ## argx ## _reply_t,  \
                (caddr_t)rrp ## argx,                             \
                (cred), &rpc_status)

#ifdef PROVIDE_V8_COMPAT
/* This macro encapsulates the downrev checking and the conversion of a downrev
** reply structure to the current reply structure if necessary.  Some callers
** have set fields in the reply structure, which means we have to do something
** to get that information into the downrev reply structure before the call.
** Currently, this only happens for mfs_clnt_create (all the others that set
** reply fields actually use MVFS_VWCALL_NO_XREV, so there isn't a problem).
** So, just handle the one case in an ugly way by passing in an assignment
** statement as a macro argument.
*/
#define MVFS_VWCALL(vwvp, vfsp, op, arg_type, rrpv8_cp)                        \
    if (!VTOM(vwvp)->mn_view.downrev_view) {                                   \
        error = _MVFS_VWCALL(vwvp, vfsp, op, arg_type, , ,                     \
                             MVFS_CD2CRED(cd));                                \
        if (rpc_status == RPC_PROCUNAVAIL) {                                   \
            VTOM(vwvp)->mn_view.downrev_view = TRUE;                           \
            mvfs_log(MFS_LOG_DEBUG, "Mark downrev view %s\n", mfs_vw2nm(vwvp));\
        }                                                                      \
    }                                                                          \
    if (VTOM(vwvp)->mn_view.downrev_view) {                                    \
        HEAP_ALLOC(arg_type ## _v8 ## _reply_t, rrp_v8);                       \
        if (rrp_v8 != NULL) {                                                  \
            rrpv8_cp; /* Normally an empty statement. */                       \
            error = _MVFS_VWCALL(vwvp, vfsp, op, arg_type, _V8, _v8,           \
                                 MVFS_CD2CRED(cd));                            \
            op ## _V8_TO_ ## op(rrp_v8, rrp);                                  \
            HEAP_FREE(rrp_v8);                                                 \
        } else {                                                               \
            error = ENOMEM;                                                    \
        }                                                                      \
    }
#else /* not PROVIDE_V8_COMPAT */
#define MVFS_VWCALL(vwvp, vfsp, op, arg_type, rrpv8_cp) \
    error = _MVFS_VWCALL(vwvp, vfsp, op, arg_type, , , MVFS_CD2CRED(cd))
#endif /* PROVIDE_V8_COMPAT */

/* Macros used by routines that don't have a new RPC version to worry about.
** Most of them have call data, so make a simpler macro for that case.
*/
#define MVFS_VWCALL_NO_XREV(vwvp, vfsp, op, arg_type) \
    error = _MVFS_VWCALL(vwvp, vfsp, op, arg_type, , , MVFS_CD2CRED(cd))

#define MVFS_VWCALL_NO_XREV_CR(vwvp, vfsp, op, arg_type, cred) \
    error = _MVFS_VWCALL(vwvp, vfsp, op, arg_type, , , cred)

/*
 * When the directory modification time stamp changes due to a view RPC, the
 * rddir cache would normally get flushed as a side effect of the
 * mvfs_attrcache() call, via mfs_ac_modevents() calling
 * mvfs_rddir_cache_flush().  As the calling vnodeop renders the parent's rddir
 * cache stale it is flushed by this macro explicitly if mvfs_attrcache() has
 * not taken care of it.  If the rddir cache for this directory mnode has
 * already been flushed due to the side effect mentioned above, then all the
 * blocks, including the first one, are marked invalid and the call to
 * mvfs_rddir_cache_flush() is unnecessary.
 */
#define MVFS_CLNT_RDDIR_CACHE_CHECK(mnptr, dvptr, name, dtm, function)  \
    if ((mnptr)->mn_vob.rddir_cache &&                                  \
        ((mnptr)->mn_vob.rddir_cache->entries[0]).valid)                \
    {                                                                   \
        mvfs_log(MFS_LOG_DEBUG,                                         \
                 function ": flush rddir explicitly: vp=%"KS_FMT_PTR_T  \
                 ", nm=%s mtime "                                       \
                 "before RPC=%"KS_FMT_TV_SEC_T_X".%"KS_FMT_TV_USEC_T_X  \
                 " after RPC=%"KS_FMT_TV_SEC_T_X".%"KS_FMT_TV_USEC_T_X  \
                 "\n",                                                  \
                 (dvptr),                                               \
                 (name),                                                \
                 (dtm).tv_sec,                                          \
                 (dtm).tv_usec,                                         \
                 rrp->dir_mod.dvstat.fstat.mtime.tv_sec,                \
                 rrp->dir_mod.dvstat.fstat.mtime.tv_usec);              \
        mvfs_rddir_cache_flush(mnptr);                                  \
    }

/* MFS_CLNT_GETATTR_MNP - RPC to get attrs into mnode 
 * MNODE MUST BE LOCKED!
 */
int
mfs_clnt_getattr_mnp(
    register mfs_mnode_t *mnp,
    VFS_T *vfsp,
    CALL_DATA_T *cd
)
{
    VNODE_T *vw;
    HEAP_ALLOC_RPC_ARGS(view_getattr);

    ASSERT(MFS_ISVOB(mnp));
    ASSERT(MISLOCKED(mnp));

    vw = mnp->mn_hdr.viewvp;
    if (vw == NULL) {
        error = ESRCH;
        goto done;
    }
    rap->hdr.view = VTOM(vw)->mn_view.vh;
    rap->hdr.build_handle = MFS_BH(cd);
    rap->fhandle = mnp->mn_vob.vfh;

    MVFS_VWCALL(vw, vfsp, VIEW_GETATTR, view_getattr, );

    if (!error) {
        error = mfs_geterrno(rrp->hdr.status);
    }
    if (!error) {
        /* Set the lvut before mvfs_attrcache() since it will call
        ** mvfs_set_ac_timeout() that needs it.
        */
        mnp->mn_vob.lvut = rrp->lvut;
        mvfs_attrcache(mnp, vfsp, &(rrp->vstat), FALSE, cd);

        /* Fix up stats for a 'history mode' symlink.  The size returned does
        ** not include the hm suffix we will add, because the view_server
        ** doesn't know it.  So... we have to fix that up here.
        */
        if (MFS_HMVFH(&rrp->fhandle) &&
            mfs_ftype_to_vtype(mnp->mn_vob.attr.fstat.type) == VLNK)
        {
            mnp->mn_vob.attr.fstat.size += mfs_hmsuffix_len();
        }
        /* To help out the FSS, fill in potentially misssing elem_dbid and
        ** generation number.  See comments in mfs_iread()
        */
        MFS_UPDATE_PARTIAL_VFH(mnp->mn_vob.vfh, rrp->fhandle);
    } else {
        MVFS_CHK_STALE_MNP(error, mnp, vfsp, cd);
    }
  done:
    HEAP_FREE(rap);
    HEAP_FREE(rrp);
    return(error);
}

/* MFS_CLNT_GETATTR - RPC to the view to get attrs */

int
mfs_clnt_getattr(
    register VNODE_T *vp,
    CALL_DATA_T *cd
)
{
    int error;

    /* 
     * Must lock around getattr so that no races on attributes with dir
     * ops. e.g. don't want go fetch attrs, then dir op gets and changes
     * them/caches them, then get the lock to update the attributes and put in
     * old ones.
     */
    MLOCK(VTOM(vp));
    error = mfs_clnt_getattr_mnp(VTOM(vp), vp->v_vfsp, cd);
    MUNLOCK(VTOM(vp));
    return(error);
}

/*
 * MFS_CLNT_SETATTR
 * Change only the view attributes (ignoring the cleartext).
 * Used from mfs_inactive() to update info in the view only.
 *
 * Automatically merges in cleartext info and clears
 * dirty/accessed bits if attributes successfully set.
 * 
 * WARNING: vnode should already be locked for this call.
 *	the procedure name has the _locked at the end to remind
 *	you of this!
 */

int
mvfs_clnt_setattr_locked(
    register VNODE_T *vp,
    register VATTR_T *vap, /* WATCHOUT: may be null for just sync */
    u_long xmode,          /* extra (special) fmode bits */
    int bhflag,            /* Which build handle to use */
    int wcred,             /* We think cred is a "writer's" cred */
    CALL_DATA_T *cd,
    u_int saflag           /* Set Attr Flag */
)
{
    register mfs_mnode_t *mnp;
    VNODE_T *vw;
    u_long mask;
    HEAP_ALLOC_RPC_ARGS(view_setattr);

    vw = MFS_VIEW(vp);
    if (vw == NULL) {
        error = ESRCH;
        goto done;
    }

    mnp = VTOM(vp);
    ASSERT(MISLOCKED(mnp));
    ASSERT(MFS_ISVOB(mnp));

    /* Change attributes in the view database (if view object) */


    /* Set up request */

    rap->hdr.view = VTOM(vw)->mn_view.vh;
    if (bhflag == MFS_USE_NULLBH) {
	rap->hdr.build_handle = mfs_null_bh;
    } else {
        rap->hdr.build_handle = MFS_BH(cd);
    }
    rap->fhandle = MFS_VFH(vp);

    /* 
     * If non-null attributes to set, then pre-process them and
     * get mask bites telling us what fields are set.
     */
    if (vap) {
        mfs_vattr_to_sattr(vap, &rap->sattr);
        mask = VATTR_GET_MASK(vap);
    } else {
	mfs_sattr_null(&rap->sattr);
	mask = 0;
    }
	
    /* Merge in "audited" bit to setattr call */

    if ((xmode == TBS_FMODE_AUDITED_OBJ)  &&
		(mnp->mn_vob.attr.fstat.mode & TBS_FMODE_AUDITED_OBJ) == 0) {
	rap->sattr.mask |= VIEW_ATTR_AUDITED;
	rap->sattr.mode |= TBS_FMODE_AUDITED_OBJ;
    }
	
    /* Merge in any modifications from cleartext with this
       setattr call.  Don't try to set mod time/size unless
       we have writer credentials. */

    if (mnp->mn_hdr.realvp) {
	ASSERT(MVFS_ISVTYPE(vp, VREG));
        if ( wcred || 
             (MVFS_CD2CRED(cd) &&
             (MVFS_COMPARE_MNODE_UID(MVFS_CD2CRED(cd), mnp->mn_vob.user_id))) ) 
        {
	    if ((mask & AT_SIZE) == 0 &&
		  VATTR_GET_SIZE(&mnp->mn_vob.cleartext.va) != 
				mnp->mn_vob.attr.fstat.size) {
	        rap->sattr.size = VATTR_GET_SIZE(&mnp->mn_vob.cleartext.va);
		rap->sattr.mask |= VIEW_ATTR_SIZE;
	    }
	    /* 
	     * When merging mtime, do this if the seconds are
	     * out of sync, or if we have made a modification
	     * that requires sync-ing the mtime since the last
	     * sync point (ctime counts too!), but not if the
	     * fstat is already correct to the tv_usec!
	     */
	    if ((mask & AT_MTIME) == 0 &&
			(VATTR_GET_MTIME(&mnp->mn_vob.cleartext.va) !=
				mnp->mn_vob.attr.fstat.mtime.tv_sec ||
		 	mnp->mn_vob.sync_mtime || mnp->mn_vob.sync_ctime)) {
		VATTR_GET_MTIME_TV(&mnp->mn_vob.cleartext.va, &rap->sattr.mtime);
		/* Only set mtime if it would be different */
	        if (!MFS_TVEQ(mnp->mn_vob.attr.fstat.mtime, rap->sattr.mtime))
			rap->sattr.mask |= VIEW_ATTR_MTIME;
		else mnp->mn_vob.sync_mtime = 0;	/* False alarm */
	    }
	}
	/* 
	 * Only update atime with lo-res check,
	 * but set hi-res time if available from cleartext.
         *
         * Some FS's (notably FAT on NT) do not support the Atime
         * parameter.  The definition for wrappers on these systems
         * is that they should return a 0 as the atime if the underlying
         * system does not keep atime.  Here in this code, we then ignore
         * trying to 'sync' the atime to the view-server (leaving a useless
	 * atime in the view, but better than 0 would be).
         */
	if ((mask & AT_ATIME) == 0 &&
            VATTR_GET_ATIME(&mnp->mn_vob.cleartext.va) != 
                mnp->mn_vob.attr.fstat.atime.tv_sec &&
            VATTR_GET_ATIME(&mnp->mn_vob.cleartext.va) != 0)
        {
	    VATTR_GET_ATIME_TV(&mnp->mn_vob.cleartext.va, &rap->sattr.atime);
	    rap->sattr.mask |= VIEW_ATTR_ATIME;
	}
    }

    /* Don't do call if nothing to update. */

    if (mfs_sattr_is_null(&rap->sattr)) {
	MDB_XLOG((MDB_CTIME, "clnt_setattr no work wcred=%d\n", wcred));
        error = 0;
        goto done;
    } else
	MDB_XLOG((MDB_CTIME, "clnt_setattr mask=%x\n", rap->sattr.mask));

#ifdef MVFS_DEBUG
    /* Print warning if either time is 0 */

    if (rap->sattr.mtime.tv_sec == 0 || rap->sattr.atime.tv_sec == 0) {
	mvfs_log(MFS_LOG_DEBUG, 
                 "setattr: setting a/mtime of 0: vw=%s vob=%s dbid=0x%x\n",
                 mfs_vp2vw(vp), mfs_vp2dev(vp), mfs_vp2dbid(vp));
    }

    /* Print warning if setting size of non-reg file */

    if ((rap->sattr.mask & VIEW_ATTR_SIZE) && !MVFS_ISVTYPE(vp, VREG)) {
	mvfs_log(MFS_LOG_DEBUG, 
	    "setattr: setting size of non-VREG: vw=%s vob=%s dbid=0x%x\n",
		mfs_vp2vw(vp), mfs_vp2dev(vp), mfs_vp2dbid(vp));
    }
#endif

    MVFS_VWCALL(vw, vp->v_vfsp, VIEW_SETATTR, view_setattr, );

    if (!error) {
        /*
         * If the flag bit MVFS_SATTR_ATIME_EROFS_OK is set, we don't want to
         * see EROFS when we fail to sync last-access-time from cleartext to
         * viewdb on a ReadOnly view.
         *
         * RATLC01011420: When the view server returns TBS_ST_EROFS then
         *                rrp->vstat is not populated. Thereby there is nothing
         *                to update the attribute cache with. If we still go
         *                ahead with the mvfs_attrcache call, we fail on the
         *                debug assertion, sid_p->type == CREDUTL_SID_TYPE_UNIX,
         *                in credutl_sid_to_unix_uid() and
         *                credutl_sid_to_unix_gid() which get called down the
         *                order.  See the CR for details.
         */
        if ((saflag & MVFS_SATTR_ATIME_EROFS_OK) != 0
            && (rap->sattr.mask == VIEW_ATTR_ATIME)
            && (rrp->hdr.status == TBS_ST_EROFS))
        {
            MDB_XLOG((MDB_CTIME,
                      "clnt_setattr: read-only view: nothing to do "
                      "(mcred=%"KS_FMT_PTR_T")\n",
                      MCRED(mnp)));
            return(0);
        }
        error = mfs_geterrno(rrp->hdr.status);
    }
    
    if (!error) {
#ifdef MVFS_DEBUG
	if (!MFS_TVEQ(mnp->mn_vob.attr.fstat.ctime,
		      rrp->vstat.fstat.ctime))
        {
	    MDB_XLOG((MDB_CTIME,
		      "clnt_setattr updating ctime to 0x%"KS_FMT_TV_SEC_T_D
                      ".%"KS_FMT_TV_USEC_T_D" wcred=%d\n",
		      rrp->vstat.fstat.ctime.tv_sec,
		      rrp->vstat.fstat.ctime.tv_usec, wcred));
	} else {
	    MDB_XLOG((MDB_CTIME,
		      "clnt_setattr nodelta ctime 0x%"KS_FMT_TV_SEC_T_D
                      ".%"KS_FMT_TV_USEC_T_D" wcred=%d\n",
		      rrp->vstat.fstat.ctime.tv_sec,
		      rrp->vstat.fstat.ctime.tv_usec, wcred));
        }
#endif
        mvfs_attrcache(mnp, vp->v_vfsp, &(rrp->vstat), 0, cd);

	/* Clear bits if we have sync'd (or set) the mtime */
	if (rap->sattr.mask != 0)
	    /* any change will have set the ctime in the reply */
	    mnp->mn_vob.sync_ctime = 0;
	if (rap->sattr.mask & VIEW_ATTR_MTIME) mnp->mn_vob.sync_mtime = 0;
    } else {
	MFS_CHK_STALE(error, vp);
	MDB_XLOG((MDB_CTIME, "clnt_setattr error %d", error));
    }
  done:
    HEAP_FREE(rap);
    HEAP_FREE(rrp);
    return(error);
}

/*
 * MFS_CLNT_READLINK - enter with link text buffer allocated.
 * Must be MAXPATHLEN in size.
 */

int
mfs_clnt_readlink(
    register VNODE_T *vp,
    mfs_pn_char_t *lnkbuf,
    int *lnklenp,
    CALL_DATA_T *cd
)
{
    VNODE_T *vw;
    HEAP_ALLOC_RPC_ARGS(view_readlink);

    ASSERT(MFS_ISVOB(VTOM(vp)));
    ASSERT(lnkbuf);

    /* Check for view.  If have none, then bug out. */

    vw = MFS_VIEW(vp);
    if (vw == NULL) {
        error = ESRCH;
        goto done;
    }

    rap->hdr.view = VTOM(vw)->mn_view.vh;
    rap->hdr.build_handle = MFS_BH(cd);
    rap->fhandle = MFS_VFH(vp);
    rap->max_text_size = MAXPATHLEN;
    rrp->text = lnkbuf;

    MVFS_VWCALL_NO_XREV(vw, vp->v_vfsp, VIEW_READLINK, view_readlink);
    if (!error) error = mfs_geterrno(rrp->hdr.status);
    if (!error) {
	*lnklenp = (int)rrp->text_size;
        /* To follow symlinks when warping in history mode,
           append a HM suffix to the text of any history mode symlink. */

        if (MFS_HMVFH(&rap->fhandle)) {
	    *lnklenp = *lnklenp + mfs_hmstrcat(lnkbuf);
        }
    } else {
	MFS_CHK_STALE(error, vp);
    }
  done:
    HEAP_FREE(rap);
    HEAP_FREE(rrp);
    return(error);
}

/*
 * MFS_CLNT_REMOVE
 */

int
mfs_clnt_remove(
    register VNODE_T *dvp,
    mfs_pn_char_t *nm,
    int bhflag,
    int sleep,
    CALL_DATA_T *cd
)
{
    VNODE_T *vw;
    mfs_mnode_t *mnp;
    struct timeval dtm_save;
    HEAP_ALLOC_RPC_ARGS(view_remove);

    vw = MFS_VIEW(dvp);
    if (vw == NULL) {
        error = ESRCH;
        goto done;
    }

    if (mfs_hmname(nm, NULL)) {
        error = EROFS;
        goto done;
    }

    rap->hdr.view = VTOM(vw)->mn_view.vh;
    if (bhflag == MFS_USE_NULLBH) {
	rap->hdr.build_handle = mfs_null_bh;
    } else {
        rap->hdr.build_handle = MFS_BH(cd);
    }
    rap->file.d_fhandle = MFS_VFH(dvp);
    rap->file.name = nm;

    mnp = VTOM(dvp);
    if (sleep) {
	MLOCK(mnp);
    } else {
	if (!MLOCK_NOWAIT(mnp)) {
            error = EAGAIN;
            goto done;
        }
    }
    dtm_save = rap->dir_dtm = mnp->mn_vob.attr.fstat.mtime;

    /* Remove NC ent before call to view */
    mfs_dncremove(dvp, nm, cd);

    MVFS_VWCALL(vw, dvp->v_vfsp, VIEW_REMOVE, view_remove, );

    if (!error) error = mfs_geterrno(rrp->hdr.status);
    if (!error) {
        mvfs_attrcache(mnp, dvp->v_vfsp, &(rrp->dir_mod.dvstat),
                       rrp->dir_mod.dir_dtm_valid, cd);
        MVFS_CLNT_RDDIR_CACHE_CHECK(mnp, dvp, nm, dtm_save, "mvfs_clnt_remove");
    } else {
        MFS_CHK_STALE(error, dvp);
    }
    MUNLOCK(mnp);
  done:
    HEAP_FREE(rap);
    HEAP_FREE(rrp);
    return(error);
}

/*
 * MFS_CLNT_LOOKUP - call view for lookup op
 */

/* Define structure to contain variables used in clnt_lookup to reduce stack
 * usage for platforms short on stack space.
 */
typedef struct mvfs_clnt_lookup_vars {
    VNODE_T *vw;
    mfs_mnode_t *mnp;
    view_hm_warp_opt_t hm_warp_opt;
    int hm;
    int hmwarp;
    struct timeval mtime;
    int isdotdot;
    int pri;
    u_int dncflags;
    int view_op;
} mvfs_clnt_lookup_vars_t;

int
mfs_clnt_lookup(
    VNODE_T *dvp,
    mfs_pn_char_t *nm,
    VNODE_T **vpp,
    CALL_DATA_T *cd
)
{
    HEAP_ALLOC_RPC_ARGS_AND_VARS(view_lookup, mvfs_clnt_lookup_vars_t, lvp);

    lvp->hm_warp_opt = VIEW_HM_WARP_OPT_NONE;
    lvp->isdotdot = 0;
    lvp->view_op = VIEW_LOOKUP;
    lvp->vw = MFS_VIEW(dvp);
    if (lvp->vw == NULL) {
        error = ESRCH;
        goto done;
    }
    MVFS_INIT_TIMEVAL(lvp->mtime);
    /* 
     * Look for history mode warp names and handle as follows:
     * Cases are:
     *
     *     View 	Name (hm suffix?)	Action
     *     ------	----------------	------------------------
     *	   Not HM	Not HM suffix		Normal lookup on name
     *	   		HM suffix		Warp into history mode and
     *						lookup	name with suffix 
     *						stripped
     *	   HM	        Not HM suffix	        Normal lookup on name
     *			HM suffix		Lookup name with suffix
     *						stripped (stay in HM)
     * Also: look out for special cases:
     *
     * Case "." or ".@@":  If lookup same as parent, mvfs_makevobnode would
     * try to lock an already locked vnode and panic on a recursive lock.
     *
     *     View 	Name (hm suffix?)	Action
     *     ------	----------------	------------------------
     *	   Not HM	"."			Return same vnode
     *	   		".@@"			Real lookup required for warp.
     *						Return vnode is in a
     *						in a different view/tree.
     *	   HM	        "."	        	Return same vnode
     *			".@@"			Return same vnode
     *
     * Case ".." or "..@@":  Lookup is going up the tree.  If kept dir
     * vnode locked while in mvfs_makevobnode (and lock result of lookup),
     * then lock order would be child->parent and can cause a deadlock.
     * So... must unlock the dir before the mvfs_makevobnode below.
     *
     * Case: "^@@" (hmvers_nm) is really a lookup of ".@@" but
     * it warps to the version level instead of the element level
     * of the current directory.  (Generally this really means a lookup
     * of "."!!)
     */
    lvp->hm = mfs_hmname(nm, &rap->name);	/* Get "stripped" name in rap->name */
    if (lvp->hm) {
	lvp->hm_warp_opt = VIEW_HM_WARP_OPT_ELEMENT_WARP;	/* Warp to element */
	/* rap->name already has stripped hm name */
    } else {
	lvp->hm_warp_opt = VIEW_HM_WARP_OPT_NONE;	/* No warp */
	rap->name = nm;
    }
    /*
     * Check for HM warp to version e.g. handling of "^@@"
     */
    if (lvp->hm && rap->name[0] == MVFS_VX_VERS_CHAR && rap->name[1] == '\0') {
  	/* Have found ^@@ in non-hm mode.  Rewrite the name lookup
     	 * to be ".", and the warp mode to be to the VERSION instead
      	 * of the element.  Ok to rewrite the string because
	 * for HM we know that "rap->name" points to a locally
	 * allocated copy, not the passed in argument.
	 */
	ASSERT(rap->name != nm);
	rap->name[0] = '.';
	lvp->hm_warp_opt = VIEW_HM_WARP_OPT_VERSION_WARP;
    }
    /* 
     * Check for various cases.  
     * Optimize by only doing this if first char is "." 
     */
    if (rap->name[0] == '.') {

	/* Check for "." cases */

        if (rap->name[1] == '\0') {
	    if (!lvp->hm) {		/* Leafname not History Mode i.e. just "." */
    	        *vpp = dvp;
                VN_HOLD(dvp);
                error = 0;
                goto done;
	    } else if (VTOM(lvp->vw)->mn_view.hm) {  /* ".@@" when already in HM */
		*vpp = dvp;
		VN_HOLD(dvp);
		STRFREE(rap->name);
                error = 0;
                goto done;
	    } 
	}
	/* 
	 * Set flag if ".." - this is used later to
	 * control locking order.
	 */
        lvp->isdotdot = (rap->name[1] == '.' && rap->name[2] == '\0');
    }

    rap->hdr.view = VTOM(lvp->vw)->mn_view.vh;
    rap->hdr.build_handle = MFS_BH(cd);
    rap->d_fhandle = MFS_VFH(dvp);
    rap->hm_warp_opt = lvp->hm_warp_opt;
    rap->residual_pname = "";	/* FIXME: Residual pathname, none for now ... */

    lvp->mnp=VTOM(dvp);
retry:
    MLOCK(lvp->mnp);

    MVFS_VWCALL(lvp->vw, dvp->v_vfsp, VIEW_LOOKUP, view_lookup, );

    if (lvp->hm) {
	ASSERT(rap->name != nm);
	STRFREE(rap->name);	/* Free allocated stripped name */
    }

    if (!error) error = mfs_geterrno(rrp->hdr.status);
    if (!error) {
	/*
	 * Check for accidental lookup of "." by another name 
	 * If so, handle this and print a debug warning to the
         * console.  The idea is to be very ROBUST, cause anything
         * could get cataloged in the namespace.
	 */
	
	if (lvp->mnp->mn_vob.vfh.ver_dbid == rrp->fhandle.ver_dbid &&
	    lvp->mnp->mn_vob.vfh.gen == rrp->fhandle.gen &&
	    ((lvp->mnp->mn_vob.vfh.flags ^ rrp->fhandle.flags) & 
             VIEW_FHANDLE_FLAGS_HISTORY_MODE) == 0 &&
	    BCMP(&lvp->mnp->mn_vob.vfh.vob_uuid, 
                 &rrp->fhandle.vob_uuid, sizeof(tbs_uuid_t)) == 0)
            {
		*vpp = dvp;
		VN_HOLD(dvp);
	        mfs_dncadd(dvp, 
                           rrp->bh_invariant ? MFS_DNC_BHINVARIANT : 0, 
                           nm, *vpp, cd);
		MUNLOCK(lvp->mnp);
		mvfs_log(MFS_LOG_INFO, 
                         "vw lookup: vw=%s vob=%s dbid=0x%x nm=%s hardlink "
                         "to '.'!\n",
                         mfs_vp2vw(dvp),
                         mfs_vp2dev(dvp), mfs_vp2dbid(dvp), nm);
                error = 0;
                goto done;
	}
		
	/* 
         * Lookup is the only clnt op which can do a makevobnode
         * on its parent (when going "up" the tree).  This can
         * cause a deadlock when:
         *	Proc A				Proc B
	 *	MLOCK dir <parent>		MLOCK dir <child>
	 *	RPC lookup <child>		RPC lookup ".."
	 *	Makevobnode <child>		Makevobnode ".."
	 *	    MLOCK <child> waits!	    MLOCK <parent> waits!
	 *
	 * (No other op e.g. create, mkdir etc. can makevobnode a parent.)
	 *
	 * SO, when going up the tree, use the mod time of the dir to see if 
         * anything might have changed between when we did the lookup RPC, and
 	 * when we try to add the name cache entry.  If anything might
         * have changed, then don't add the name cache entry (i.e.
         * if all attributes invalidated on the dir and not refetched
         * yet, or the mtime changed).
	 *
	 * I don't do this for all lookups, because I would lose too
	 * many name cache entries that way during normal operation of
         * builds etc.  (Note that a dir mod is not required, just anything
         * that invalidates the attributes e.g. like and open!).  Also,
         * holding the dir locked across the RPC is not a big issue because
         * it really doesn't matter all that much whether we are single
         * threaded in the MFS, or at the view_server (which only handles 1
         * RPC at a time!), and the value of the name cache to performance
         * far outweighs anything else.
         */
	if (lvp->isdotdot) {
	    lvp->mtime = lvp->mnp->mn_vob.attr.fstat.mtime;
	    MUNLOCK(lvp->mnp);
	}

        /*
         * Fix up stats for a 'history mode' symlink.
         * The size returned does not include the hm suffix
         * we will add, because the view_server doesn't know
         * it.  So... we have to fix that up here.
         */
        if (MFS_HMVFH(&(rrp->fhandle)) &&
            mfs_ftype_to_vtype(rrp->vstat.fstat.type) == VLNK)
        {
            rrp->vstat.fstat.size += mfs_hmsuffix_len();
        }

	error = mvfs_makevobnode(&rrp->vstat, &rrp->lvut, lvp->vw,
                                 &rrp->fhandle, dvp->v_vfsp, cd, vpp, FALSE);

        if (error == ENOENT) {
            /* This happens on Linux if VNGET finds that the vnode is in 
             * the process of being deleted.  There is a potential race with
             * the invalidate code, so we have to unlock the parent to give
             * the invalidate a chance to finish and then we will try again.
             */
            if (!lvp->isdotdot)
                MUNLOCK(lvp->mnp);
            goto retry;
        }
        if (lvp->isdotdot) {	
	    MLOCK(lvp->mnp);	/* Must relock the dir */
	    if (!error) {
		/* Only add to name cache if noone has invalidated the
                 * attributes and mtime has not changed.  For this
                 * check I don't care about timed-out attributes, only
                 * if a process on this node explicitly invalidated them
                 * (e.g. due to a dir op).
                 */
	        if (MFS_ATTRISVALID(dvp) && 
		    MFS_TVEQ(lvp->mtime, lvp->mnp->mn_vob.attr.fstat.mtime))
                {
                    mfs_dncadd(dvp, rrp->bh_invariant ? MFS_DNC_BHINVARIANT : 0,
                               nm, *vpp, cd);
	        } else {
		    /* Count this as an add race. */
		    BUMPSTAT(mfs_dncstat.dnc_addunlock);
                    BUMP_VDNCSTATV(lvp->vw, dncstat.dnc_addunlock);
	        }
	    }
	} else {	/* Normal lookups */
            if (!error) {
	        mfs_dncadd(dvp, rrp->bh_invariant ? MFS_DNC_BHINVARIANT : 0,
                           nm, *vpp, cd);
		switch (MVFS_GETVTYPE(*vpp)) {
		case VDIR:
		    BUMPSTAT(mfs_dncstat.dnc_missdir);
                    BUMP_VDNCSTATV(lvp->vw, dncstat.dnc_missdir);
		    break;
		case VREG:
		    BUMPSTAT(mfs_dncstat.dnc_missreg);
                    BUMP_VDNCSTATV(lvp->vw, dncstat.dnc_missreg);
		    break;
		  default:
		    break;
		}
	    }
	}
    } else if (error == ENOENT) {
	/* 
	 * Cache "name not found" 
	 * FIXME: For now (for safety) these are NEVER bh invariant
         * for VOB dirs (view didn't send rrp->bh_invariant in reply
         * because status was not OK).  However, we can always make
         * a translation BH_INVARIANT for a view-pvt dir, since ref
         * time can't affect the result of view-pvt dir translations.
         */
	if (VIEW_ISA_VIEW_OBJ(&lvp->mnp->mn_vob.vfh))
	    lvp->dncflags = MFS_DNC_BHINVARIANT;
	else
	    lvp->dncflags = 0;
	/*
	 * If the reason this is not visible is because it's not there,
	 * then we can cache the result specially and avoid some inefficient
	 * invalidations.  See mfs_dncops.c.
	 */
	if ((rrp->name_state & VIEW_NAME_STATE_NOT_VISIBLE) ==
	    VIEW_NAME_STATE_ENOTENT)
        {
	    lvp->dncflags |= MFS_DNC_NOTINDIR;
        }
	mfs_dncadd(dvp, lvp->dncflags, nm, NULL, cd);
	BUMPSTAT(mfs_dncstat.dnc_missnoent);
        BUMP_VDNCSTATV(lvp->vw, dncstat.dnc_missnoent);
	/* 
         * Log ENOENT lookup if debug, or at DEBUG level if
	 * the underlying error is "no version selected"
	 */
	if (rrp->hdr.status != TBS_ST_VIEW_NO_VER) {
	    mvfs_logperr(MFS_LOG_ENOENT, ENOENT,
                         "vw lookup: vw=%s vob=%s dbid=0x%x nm=%s",
                         mfs_vp2vw(dvp), mfs_vp2dev(dvp), mfs_vp2dbid(dvp), nm);
	} else {
	    mvfs_log(MFS_LOG_ENOENT,
                     "lookup: vw=%s vob=%s dbid=0x%x nm=%s - "
                     "no version selected\n",
                     mfs_vp2vw(dvp), mfs_vp2dev(dvp), mfs_vp2dbid(dvp), nm);
	}
    } else {
	MFS_CHK_STALE(error, dvp);
    }
    MUNLOCK(lvp->mnp);
  done:
    HEAP_FREE(rap);
    HEAP_FREE(rrp);
    HEAP_FREE(lvp);
    return(error);
}

/*
 * MFS_CLNT_CREATE - do op to view server to create
 */

int
mfs_clnt_create(
    VNODE_T *dvp,
    mfs_pn_char_t *nm,
    VATTR_T *va,
    VNODE_T **vpp,
    CALL_DATA_T *cd
)
{
    int xerr;
    VNODE_T *vw;
    mfs_mnode_t *mnp;
    struct timeval dtm_save;
    HEAP_ALLOC_RPC_ARGS(view_create);

    ASSERT(MFS_ISVOB(VTOM(dvp)));

    vw = MFS_VIEW(dvp);
    if (vw == NULL) {
        error = ESRCH;
        goto done;
    }

    if (mfs_hmname(nm, NULL)) {
        error = EROFS;  /* History mode names an error */
        goto done;
    }

    rap->hdr.view = VTOM(vw)->mn_view.vh;
    rap->hdr.build_handle = MFS_BH(cd);
    rap->create.d_fhandle = MFS_VFH(dvp);
    rap->create.name = nm;

    mfs_vattr_to_sattr(va, &rap->iattr);

    rrp->text = KMEM_ALLOC(MAXPATHLEN, KM_SLEEP);
    if (rrp->text == NULL) {
	error = ENOMEM;
        goto done;
    }
    rap->max_text_size = MAXPATHLEN;

    mnp = VTOM(dvp);
    MLOCK(mnp);
    dtm_save = rap->dir_dtm = mnp->mn_vob.attr.fstat.mtime;

    MVFS_VWCALL(vw, dvp->v_vfsp, VIEW_CREATE, view_create,
                rrp_v8->text = rrp->text);

    MFS_ATTRINVAL(dvp);
    if (!error) error = mfs_geterrno(rrp->hdr.status);
    if (!error) {
        mvfs_attrcache(mnp, dvp->v_vfsp, &(rrp->dir_mod.dvstat),
                       rrp->dir_mod.dir_dtm_valid, cd);
        MVFS_CLNT_RDDIR_CACHE_CHECK(mnp, dvp, nm, dtm_save, "mvfs_clnt_create");
        error = mvfs_makevobnode(&(rrp->vstat), 0, vw, &(rrp->fhandle),
                                 dvp->v_vfsp, cd, vpp, TRUE);
	
	if (!error) {
	    /* Creates must be in view, and these are BH invariant */
	    mfs_dncadd(dvp, MFS_DNC_BHINVARIANT, nm, *vpp, cd);
	    MLOCK(VTOM(*vpp));
	    /* Creates can't be in VOB */
	    VTOM(*vpp)->mn_vob.cleartext.isvob = 0;
            ASSERT(VTOM(*vpp)->mn_vob.open_wcount == 0 &&
                   VTOM(*vpp)->mn_vob.open_count == 0);
            /* XXX this returns an error, why aren't we checking it? */
	    (void)mfs_set_cpname(VTOM(*vpp), rrp->text, rrp->text_size);
	    MUNLOCK(VTOM(*vpp));
	}
    } else {
	MFS_CHK_STALE(error, dvp);
    }

    MUNLOCK(mnp);

    KMEM_FREE(rrp->text, MAXPATHLEN);
  done:
    HEAP_FREE(rap);
    HEAP_FREE(rrp);
    return(error);
}

/*
 * MFS_CLNT_LINK
 */

int
mfs_clnt_link(
    register VNODE_T *vp,
    VNODE_T *tdvp,
    mfs_pn_char_t *tnm,
    CALL_DATA_T *cd
)
{
    VNODE_T *vw;
    mfs_mnode_t *tdmnp;
    struct timeval dtm_save;
    HEAP_ALLOC_RPC_ARGS(view_link);

    ASSERT(MFS_ISVOB(VTOM(vp)));

    vw = MFS_VIEW(tdvp);
    if (vw == NULL) {
        error = ESRCH;
        goto done;
    }
  
    if (mfs_hmname(tnm, NULL)) {
        error = EROFS;
        goto done;
    }

    rap->hdr.view = VTOM(vw)->mn_view.vh;
    rap->hdr.build_handle = MFS_BH(cd);
    rap->fhandle = MFS_VFH(vp);
    rap->to.d_fhandle = MFS_VFH(tdvp);
    rap->to.name = tnm;

    tdmnp = VTOM(tdvp);
    MLOCK(tdmnp);
    dtm_save = rap->dir_dtm = tdmnp->mn_vob.attr.fstat.mtime;

    MVFS_VWCALL(vw, vp->v_vfsp, VIEW_LINK, view_link, );

    MFS_ATTRINVAL(vp);		/* link count changed */
    if (!error) error = mfs_geterrno(rrp->hdr.status);
    if (!error) {
        mvfs_attrcache(tdmnp, tdvp->v_vfsp, &(rrp->dir_mod.dvstat),
                       rrp->dir_mod.dir_dtm_valid, cd);
        MVFS_CLNT_RDDIR_CACHE_CHECK(tdmnp, tdvp, tnm, dtm_save, "mvfs_clnt_link");

	/* Link must be in view, and this is BH invariant */
	mfs_dncadd(tdvp, MFS_DNC_BHINVARIANT, tnm, vp, cd);
    } else {
        MFS_CHK_STALE(error, vp);
        MFS_CHK_STALE(error, tdvp);
    }
    MUNLOCK(tdmnp);
  done:
    HEAP_FREE(rap);
    HEAP_FREE(rrp);
    return (error);
}

/*
 * MFS_CLNT_RENAME
 */

int
mfs_clnt_rename(
    VNODE_T *odvp,
    mfs_pn_char_t *onm,
    VNODE_T *tdvp,
    mfs_pn_char_t *tnm,
    CALL_DATA_T *cd
)
{
    VNODE_T *vw;
    mfs_mnode_t *tdmnp;
    mfs_mnode_t *odmnp;
    struct timeval odtm_save, tdtm_save;
    HEAP_ALLOC_RPC_ARGS(view_rename);

    ASSERT(MFS_ISVOB(VTOM(odvp)));
    ASSERT(MFS_ISVOB(VTOM(tdvp)));

    if (mfs_hmname(tnm, NULL)) {
        error = EROFS;
        goto done;
    }

    /* Get the view of the original dir.  Do this first
       so user gets ESRCH in preference to EXDEV if
       no view for one of the dirs. */

    vw = MFS_VIEW(odvp);
    if (vw == NULL) {
        error = ESRCH;
        goto done;
    }

    /* Make sure same view.  No rename across views */

    if (vw != MFS_VIEW(tdvp)) {
        error = EXDEV;
        goto done;
    }
	
    rap->hdr.view = VTOM(vw)->mn_view.vh;
    rap->hdr.build_handle = MFS_BH(cd);
    rap->from.d_fhandle = MFS_VFH(odvp);
    rap->from.name = onm;
    rap->to.d_fhandle = MFS_VFH(tdvp);
    rap->to.name = tnm;

    /* We need to lock multiple objects.  To avoid deadlock
       enforce a known ordering on the locks. */

    odmnp = VTOM(odvp);
    tdmnp = VTOM(tdvp);

    if (odvp != tdvp) {
        MLOCK2(odmnp, tdmnp);
    } else {
    	MLOCK(odmnp);
    }
    tdtm_save = rap->dir_dtm = tdmnp->mn_vob.attr.fstat.mtime;
    odtm_save = odmnp->mn_vob.attr.fstat.mtime;

    /* Dump name cache entries before we do the rename.
       Otherwise, we may inactivate an object that no longer
       exists when we do dump the name cache entries and the
       refcnt goes to zero (rename over an existing file) */

    mfs_dncremove(odvp, onm, cd);
    mfs_dncremove(tdvp, tnm, cd);

    MVFS_VWCALL(vw, odvp->v_vfsp, VIEW_RENAME, view_rename, );

    MFS_ATTRINVAL(odvp);	/* mod time changed */
    if (!error) error = mfs_geterrno(rrp->hdr.status);
    if (!error) {
        mvfs_attrcache(tdmnp, tdvp->v_vfsp, &(rrp->dir_mod.dvstat),
                       rrp->dir_mod.dir_dtm_valid, cd);
        MVFS_CLNT_RDDIR_CACHE_CHECK(tdmnp, tdvp, tnm, tdtm_save,
                                    "mvfs_clnt_rename");

        if ((odvp != tdvp) && odmnp->mn_vob.rddir_cache &&
            (odmnp->mn_vob.rddir_cache->entries[0]).valid)
        {
            mvfs_log(MFS_LOG_DEBUG,
                     "rename:flushing source rddir explicitly:vp=%"KS_FMT_PTR_T
                     ", nm=%s; mtime: before view RPC=%"KS_FMT_TV_SEC_T_X
                     ".%"KS_FMT_TV_USEC_T_X" after view RPC=0x%"KS_FMT_TV_SEC_T_X
                     ".%"KS_FMT_TV_USEC_T_X"\n", odvp, onm, odtm_save.tv_sec, 
                     odtm_save.tv_usec, odmnp->mn_vob.attr.fstat.mtime.tv_sec,
                     odmnp->mn_vob.attr.fstat.mtime.tv_usec);
            mvfs_rddir_cache_flush(odmnp); 
        }
    } else {
        MFS_CHK_STALE(error, odvp);
        MFS_CHK_STALE(error, tdvp);
    }
    if (odvp != tdvp) MUNLOCK(tdmnp);
    MUNLOCK(odmnp);
  done:
    HEAP_FREE(rap);
    HEAP_FREE(rrp);
    return(error);
}
/*
 * MFS_CLNT_MKDIR
 */

int
mfs_clnt_mkdir(
    VNODE_T *dvp,
    mfs_pn_char_t *nm,
    VATTR_T *va,
    VNODE_T **vpp,
    CALL_DATA_T *cd
)
{
    VNODE_T *vw;
    mfs_mnode_t *mnp;
    struct timeval dtm_save;
    HEAP_ALLOC_RPC_ARGS(view_mkdir);

    ASSERT(MFS_ISVOB(VTOM(dvp)));

    /* Get view to talk to */

    vw = MFS_VIEW(dvp);
    if (vw == NULL) {
        error = ESRCH;
        goto done;
    }

    if (mfs_hmname(nm, NULL)) {
        error = EROFS;   /* History Mode name an error */
        goto done;
    }

    rap->hdr.view = VTOM(vw)->mn_view.vh;
    rap->hdr.build_handle = MFS_BH(cd);
    rap->create.d_fhandle = MFS_VFH(dvp);
    rap->create.name = nm;

    /* 
     * mkdir should not set the SIZE field. Change the va_mask to
     * ignore the size field. Pyramid dc/osx and Sinix's nfs
     * send garbage for size.
     */
    VATTR_SET_MASK(va, (VATTR_GET_MASK(va) & (~AT_SIZE)));

    /* FIXME:  what about correct gid SysV.3 vs inherit from dir */
    mfs_vattr_to_sattr(va, &rap->iattr);

    mnp = VTOM(dvp);
    MLOCK(mnp);
    dtm_save = rap->dir_dtm = mnp->mn_vob.attr.fstat.mtime;
	
    MVFS_VWCALL(vw, dvp->v_vfsp, VIEW_MKDIR, view_mkdir, );

    MFS_ATTRINVAL(dvp);	/* mod time changed */
    if (!error) error = mfs_geterrno(rrp->hdr.status);
    if (!error) {
        mvfs_attrcache(mnp, dvp->v_vfsp, &(rrp->dir_mod.dvstat),
                       rrp->dir_mod.dir_dtm_valid, cd);
        MVFS_CLNT_RDDIR_CACHE_CHECK(mnp, dvp, nm, dtm_save, "mvfs_clnt_mkdir");
        error = mvfs_makevobnode(&(rrp->vstat), 0, vw, &(rrp->fhandle),
                                 dvp->v_vfsp, cd, vpp, TRUE);
        if (!error) {
	    /* Mkdir must be in view, and these are BH invariant */
	    mfs_dncadd(dvp, MFS_DNC_BHINVARIANT, nm, *vpp, cd);
        }
    } else {
        MFS_CHK_STALE(error, dvp);
    }

    MUNLOCK(mnp);
  done:
    HEAP_FREE(rap);
    HEAP_FREE(rrp);
    return(error);
}

/* 
 * MFS_CLNT_RMDIR - remove dir operation 
 */

int
mfs_clnt_rmdir(
    VNODE_T *dvp,
    mfs_pn_char_t *nm,
    CALL_DATA_T *cd
)
{
    VNODE_T *vw; 
    mfs_mnode_t *mnp;
    struct timeval dtm_save;
    HEAP_ALLOC_RPC_ARGS(view_rmdir);

    ASSERT(MFS_ISVOB(VTOM(dvp)));

    /* dnlc_remove(dvp, nm); */

    vw = MFS_VIEW(dvp);
    if (vw == NULL) {
        error = ESRCH;
        goto done;
    }

    if (mfs_hmname(nm, NULL)) {
        error = EROFS;  /* HM name an error */
        goto done;
    }

    rap->hdr.view = VTOM(vw)->mn_view.vh;
    rap->hdr.build_handle = MFS_BH(cd);
    rap->file.d_fhandle = MFS_VFH(dvp);
    rap->file.name = nm;

    mnp = VTOM(dvp);
    MLOCK(mnp);
    dtm_save = rap->dir_dtm = mnp->mn_vob.attr.fstat.mtime;

    mfs_dncremove(dvp, nm, cd);    /* Remove NC ent before call to view */

    MVFS_VWCALL(vw, dvp->v_vfsp, VIEW_RMDIR, view_rmdir, );

    if (!error) error = mfs_geterrno(rrp->hdr.status);
    if (!error) {
        mvfs_attrcache(mnp, dvp->v_vfsp, &(rrp->dir_mod.dvstat), 
                       rrp->dir_mod.dir_dtm_valid, cd);
        MVFS_CLNT_RDDIR_CACHE_CHECK(mnp, dvp, nm, dtm_save, "mvfs_clnt_rmdir");
    } else {
        MFS_CHK_STALE(error, dvp);
    }
    MUNLOCK(mnp);
  done:
    HEAP_FREE(rap);
    HEAP_FREE(rrp);
    return(error);
}

/*
 * MFS_CLNT_SYMLINK
 */	

int
mfs_clnt_symlink(
    VNODE_T *dvp,
    mfs_pn_char_t *lnm,
    VATTR_T *tva,
    mfs_pn_char_t *tnm,
    VNODE_T **vpp,
    CALL_DATA_T *cd
)
{
    VNODE_T *vw;
    mfs_mnode_t *mnp;
    struct timeval dtm_save;
    HEAP_ALLOC_RPC_ARGS(view_symlink);	

    vw = MFS_VIEW(dvp);
    if (vw == NULL) {
        error = ESRCH;
        goto done;
    }

    if (mfs_hmname(tnm, NULL)) {
        error = EROFS;
        goto done;
    }
	
    rap->hdr.view = VTOM(vw)->mn_view.vh;
    rap->hdr.build_handle = MFS_BH(cd);
    rap->create.d_fhandle = MFS_VFH(dvp);
    rap->create.name = lnm;
    rap->text = tnm;
    mfs_vattr_to_sattr(tva, &rap->iattr);

    mnp = VTOM(dvp);
    MLOCK(mnp);
    dtm_save = rap->dir_dtm = mnp->mn_vob.attr.fstat.mtime;

    /* Remove name in case ENOENT cached */
    mfs_dncremove(dvp, lnm, cd);

    MVFS_VWCALL(vw, dvp->v_vfsp, VIEW_SYMLINK, view_symlink, );

    MFS_ATTRINVAL(dvp);	/* mod time changed */	
    if (!error) error = mfs_geterrno(rrp->hdr.status);
    if (!error) {
	mvfs_attrcache(mnp, dvp->v_vfsp, &(rrp->dir_mod.dvstat),
                       rrp->dir_mod.dir_dtm_valid, cd);
        MVFS_CLNT_RDDIR_CACHE_CHECK(mnp, dvp, tnm, dtm_save,
                                    "mvfs_clnt_symlink");
        error = mvfs_makevobnode(&(rrp->vstat), 0, vw, &(rrp->fhandle),
                                 dvp->v_vfsp, cd, vpp, TRUE);
        if (!error) {
	    /* Create symlink must be in view, and these are BH invariant */
	    mfs_dncadd(dvp, MFS_DNC_BHINVARIANT, lnm, *vpp, cd);
        }
    } else {
	MFS_CHK_STALE(error, dvp);
    }
    MUNLOCK(mnp);
  done:
    HEAP_FREE(rap);
    HEAP_FREE(rrp);
    return(error);
}

/*
 * MFS_CLNT_READDIR
 * There are some weird things to look out for here.  The uio_offset
 * field is either 0 or it is the offset returned from a previous
 * readdir.  It is an opaque value used by the server to find the
 * correct directory block to read.  The byte count must be at least
 * 1 dir entry of bytes.  The count field is the number of blocks to
 * read on the server.  This is advisory only, the server may return
 * only one block's worth of entries.  Entries may be compressed on
 * the server.
 * Note: Directory size is limited to 31 bits.
 */
int
mfs_clnt_readdir(
    VNODE_T *dvp,
    struct uio *uiop,
    CALL_DATA_T *cd,
    int *eofp
)
{
    register mfs_mnode_t *mnp;
    VNODE_T *vw;
    struct mvfs_rce entry;
    MVFS_UIO_RESID_T count;
    size_t size;
    mvfs_common_data_t *mcdp = MDKI_COMMON_GET_DATAP();
    HEAP_ALLOC_RPC_ARGS(view_readdir);

    ASSERT(MFS_ISVOB(VTOM(dvp)));

    vw = MFS_VIEW(dvp);
    if (vw == NULL) {
        error = ESRCH;
        mnp = NULL;
        goto cleanup;
    }

    mnp = VTOM(dvp);

    /* 
     * We need to lock the mnode around this section if the rddir
     * cache is enabled.  If we don't lock here its possible that
     * we can race with another process attempting to create or remove 
     * an entry in this directory, causing us to populate the rddir cache 
     * with stale information.
     */
    if (mcdp->mvfs_rdcenabled) {
        MLOCK(mnp);
    }

    /* Return with 0 if at EOF of directory. */

    if (mnp->mn_vob.dir_eof &&
        ((u_long)MVFS_UIO_OFFSET(uiop) == mnp->mn_vob.rddir_off) &&
        (mnp->mn_vob.rddir_off != 0))
    {
        if (eofp) {
            *eofp = TRUE;
        }

        if (mcdp->mvfs_rdcenabled) {
            entry.eof = TRUE;
            entry.valid = TRUE;
            entry.offset = entry.endoffset = MVFS_UIO_OFFSET(uiop);
            entry.size = 0;
            entry.block = 0;
            entry.bsize = 0;
            mvfs_rddir_cache_enter_mnlocked(mnp, &entry);
        }

        error = 0;
        goto cleanup;
    }

    rap->hdr.view = VTOM(vw)->mn_view.vh;
    rap->hdr.build_handle = MFS_BH(cd);
    rap->d_fhandle = MFS_VFH(dvp);

    rap->offset = (u_long)MVFS_UIO_OFFSET(uiop);
    count = KS_MIN(uiop->uio_resid, MFS_MAXRPCDATA);
    rap->max_dirent_size = (size_t)count;
    rrp->ents = (view_dirent_t *)KMEM_ALLOC(count, KM_SLEEP|KM_PAGED);

    if (rrp->ents == NULL) {
        error = ENOMEM;
        goto cleanup;
    }

    rrp->max_dirent_size = (size_t)count;

    MVFS_VWCALL_NO_XREV(vw, dvp->v_vfsp, VIEW_READDIR, view_readdir);

    if (error == 0 && (error = mfs_geterrno(rrp->hdr.status)) == 0) {
        if (rrp->size != 0) {
            /* We put the size on the stack instead of using it
             * directly because the linux readdir_uiomove will 0
             * the size value passed in on a buffer overflow so
             * that we don't skip entries.  So we need a temp
             * value so that the readdir cache is not trashed.
             */
            size = rrp->size;

            /*
             * On Linux, in the non-clearcase access case, NFS calls getattr
             * on the directory entries during the copyout, causing recursive
             * lock panic.  Macros for recursive lock flag define a flag local
             * to the containing block, and need conditional for whether mnode
             * locked.
             */
            if (mcdp->mvfs_rdcenabled) {
                MVFS_RDDIR_MNLOCK_SET_RECURSIVE(mnp);
                error = READDIR_UIOMOVE((caddr_t)rrp->ents,
                                        &size,
                                        UIO_READ,
                                        uiop,
                                        MVFS_UIO_OFFSET(uiop));
                MVFS_RDDIR_MNLOCK_CLEAR_RECURSIVE(mnp);
            } else {
                error = READDIR_UIOMOVE((caddr_t)rrp->ents,
                                        &size,
                                        UIO_READ,
                                        uiop,
                                        MVFS_UIO_OFFSET(uiop));
            }

            if (!READDIR_BUF_FULL(uiop)) {
                /* XXX Why is this necessary? What if there wasn't
                 * enough room for the whole buffer--we might then
                 * miss the end of the buffer?  (Probably never the
                 * case on systems with block-oriented getdirents()
                 * calls.)
                 */
                MVFS_UIO_OFFSET(uiop) = rrp->offset;
            }

            entry.endoffset = rrp->offset;

            /* Offset for EOF only valid if got some data */

            if (rrp->eof) {
                mnp->mn_vob.dir_eof = 1;
                mnp->mn_vob.rddir_off = rrp->offset;
            }
        } else {
            if (rrp->eof) {
                mnp->mn_vob.dir_eof = 1;
                mnp->mn_vob.rddir_off = rap->offset;
            }

            entry.endoffset = rap->offset;
        }

        if (eofp) {
            *eofp = rrp->eof;
        }

        if (mcdp->mvfs_rdcenabled) {
            entry.eof = rrp->eof;
            entry.valid = TRUE;
            entry.offset = rap->offset;
            entry.size = rrp->size;
            entry.block = rrp->ents;
            entry.bsize = (size_t) count;

            mvfs_rddir_cache_enter_mnlocked(mnp, &entry);
            /* rddir cache holds reference to allocated block of return data,
               and is responsible for freeing it when done. */
        } else {
            KMEM_FREE(rrp->ents, count);
        }
    } else {
        MFS_CHK_STALE(error, dvp);
        KMEM_FREE(rrp->ents, count);
    }

cleanup:
    if (mnp != NULL && mcdp->mvfs_rdcenabled) {
        MUNLOCK(mnp);
    }

    HEAP_FREE(rap);
    HEAP_FREE(rrp);
    return(error);
}

/*
 * MFS_CLNT_INVAL - invalidate an object.
 * Note that this call takes the view vnode as its first arg!
 * FIXME: fix broken retry args initted below.. should be looked
 * up in mount tabs by vob oid!
 */

static struct mfs_retryinfo inval_retry = { 
	1,	/* Soft mount */
	0,	/* Intrs allowed */
	1,	/* rebind please */
	0,	/* MBZ bits */
	2,	/* Default timeout */
	10	/* # of retries */
};

int
mfs_clnt_inval(
    VNODE_T *vw,
    view_invalidate_type_t itype,
    A_CONST tbs_oid_t *voboidp,
    A_CONST tbs_oid_t *oidp,
    mfs_pn_char_t *nm,
    CALL_DATA_T *cd
)
{
    int no_retries;
    mfs_mnode_t *mnp;
    HEAP_ALLOC_RPC_ARGS(view_invalidate);

    if (vw == NULL) {
        error = ESRCH;
        goto done;
    }
    mnp = VTOM(vw);

    ASSERT(MFS_ISVIEW(VTOM(vw)));

    rap->hdr.view = mnp->mn_view.vh;
    rap->hdr.build_handle = MFS_BH(cd);
    rap->type = itype;
    rap->vob_oid = *voboidp;
    rap->obj_oid = *oidp;
    rap->name = nm ? nm : "";

    /* Since we don't call mfs_vwcall(), we must do its job for view idle
     * checking.
     */

    if (mnp->mn_view.rpctime + mvfs_view_rebind_timeout < MDKI_CTIME()) {
        /* try to probe ALBD first, ignoring errors */
        (void)mvfs_bindsvr_port(&VTOM(vw)->mn_view.svr, NULL,
                                MVFS_CD2CRED(cd), vw);
    }

    /*
     * Try at least one rebinding just in
     * case the view server has been restarted
     */

    for (no_retries = 1 ; no_retries >= 0; no_retries--) {

        error = mfscall(mfs_viewcall, VIEW_INVALIDATE_UUID, 0,
	                &mnp->mn_view.svr, &inval_retry, 
	                (xdrproc_t)xdr_view_invalidate_req_t, (caddr_t)rap, 
	                (xdrproc_t)xdr_view_invalidate_reply_t, (caddr_t)rrp,
                        MVFS_CD2CRED(cd), vw);

        /* 
         * Try rebind if TIMEOUT error
         */
        
        if (!error)
            /* get fresh time after talking to server */
            mnp->mn_view.rpctime = MDKI_CTIME(); /* ignore locking */

        if (error != EAGAIN)
            break;

        /*
         * First, rebind/restart the view server by calling the
         * albd.
         */

        error = mvfs_bindsvr_port(&mnp->mn_view.svr, NULL, MVFS_CD2CRED(cd), vw);

        if (error != 0) {
            mvfs_log(MFS_LOG_ERR, "Views op %s failed rebind \n",
                     mfs_viewopnames[VIEW_INVALIDATE_UUID] ? 
                     mfs_viewopnames[VIEW_INVALIDATE_UUID]
                     : "unknown operation");
            goto done;
        }
    }

    if (!error) error = mfs_geterrno(rrp->hdr.status);
    if (error == ESTALE) {
	mvfs_logperr(MFS_LOG_ESTALE, error, "clnt_inval vw=%s itype=%d",
		mfs_vw2nm(vw), itype);
    }
  done:
    HEAP_FREE(rap);
    HEAP_FREE(rrp);
    return(error);
}

int
mfs_clnt_choid(
    VNODE_T *vp,
    view_change_oid_option_t opts,
    tbs_oid_t *prevoidp,
    CALL_DATA_T *cd
)
{
    int error;

    MLOCK(VTOM(vp));
    error = mfs_clnt_choid_locked(vp, opts, prevoidp, cd);
    MUNLOCK(VTOM(vp));
    return(error);
}

int
mfs_clnt_choid_locked(
    register VNODE_T *vp,
    view_change_oid_option_t opts,
    tbs_oid_t *prevoidp,
    CALL_DATA_T *cd
)
{
    VNODE_T *vw;
    mfs_mnode_t *mnp;
    HEAP_ALLOC_RPC_ARGS(view_change_oid);

    mnp = VTOM(vp);
    ASSERT(MFS_ISVOB(mnp));
    ASSERT(MISLOCKED(mnp));

    vw = MFS_VIEW(vp);
    if (vw == NULL) {
        error = ESRCH;
        goto done;
    }

    rap->hdr.view = VTOM(vw)->mn_view.vh;
    rap->hdr.build_handle = MFS_BH(cd);
    rap->fhandle = MFS_VFH(vp);
    rap->option = opts;

    /* Must be locked around saving of previous OID and creating new one
       to prevent races from giving false choid records in audits */

    if (prevoidp) 
	BCOPY(&mnp->mn_vob.attr.obj_oid, prevoidp, sizeof(tbs_oid_t));

    MVFS_VWCALL(vw, vp->v_vfsp, VIEW_CHANGE_OID, view_change_oid, );

    if (!error) error = mfs_geterrno(rrp->hdr.status);
    if (!error) {
	/* Cache new attributes */
		mvfs_attrcache(mnp, vp->v_vfsp, &(rrp->vstat), 0, cd);
	/* NOTE: cleartext may have changed... higher layer must handle! */

    } else {
	MFS_CHK_STALE(error, vp);
    }
  done:
    HEAP_FREE(rap);
    HEAP_FREE(rrp);
    return(error);
}

/* MFS_CLNT_BINDROOT - perform bindroot rpc */

int
mfs_clnt_bindroot(
    int root,
    VNODE_T *vw,
    VFS_T *vfsp,
    mfs_pn_char_t *nm,
    VNODE_T **vpp,
    CALL_DATA_T *cd
)
{
    register struct mfs_mntinfo *mmi;
    timestruc_t start_time;	/* For stats/debug */
    timestruc_t dtime, dummy;
    HEAP_ALLOC_RPC_ARGS(view_replica_root);

    ASSERT(MFS_ISVIEW(VTOM(vw)));

    *vpp = NULL;

    mmi = VFS_TO_MMI(vfsp);

    /* Build RPC request */

    rap->hdr.view = VTOM(vw)->mn_view.vh;
    rap->hdr.build_handle = MFS_BH(cd);
    rap->vob_root = mmi->mmi_voboid;
    rap->replica_root = mmi->mmi_vobuuid;
    rap->host_name = mmi->mmi_svr.host;
    rap->host_pathname = mmi->mmi_svr.rpn;
    rap->pname = nm;  /* subdir pname in vob */
    rap->hm_warp_opt = VTOM(vw)->mn_view.hm ?
        VIEW_HM_WARP_OPT_ELEMENT_WARP : VIEW_HM_WARP_OPT_NONE; 

    MDKI_HRTIME(&start_time);
    MVFS_VWCALL(vw, vfsp, VIEW_REPLICA_ROOT, view_replica_root, );
    if (!error) {
	MVFS_TIME_DELTA(start_time, dtime, dummy);
	if (dtime.tv_sec > mvfs_max_rpcdelay) {
	    mvfs_log(MFS_LOG_INFO,
		    "replica root for VOB %s:%s took a long time!\n",
		     mmi->mmi_svr.host, mmi->mmi_svr.rpn);
	}
    }
    if (!error) error = mfs_geterrno(rrp->hdr.status);
    if (!error) {

	/* Check/update root element dbid so we can identify root
           synonyms when we need to. */

        if (root) {
            if (mmi->mmi_root_edbid != MFS_UNK_DBID &&
                mmi->mmi_root_edbid != rrp->fhandle.elem_dbid)
            {
		mvfs_log(MFS_LOG_INFO, 
                         "bindroot: root element dbid changed fm: 0x%x "
                         "to: 0x%x\n",
                         mmi->mmi_root_edbid, rrp->fhandle.elem_dbid);
            }
            mmi->mmi_root_edbid = rrp->fhandle.elem_dbid;
	}

	/* Now we can make the vnode */
        error = mvfs_makevobnode(&(rrp->vstat), &(rrp->lvut), vw,
                                 &(rrp->fhandle), vfsp, cd, vpp, TRUE);
    } else {
	if (error == ENOENT) {
	    if (rrp->hdr.status == TBS_ST_VIEW_NO_VER) {
                mvfs_log(MFS_LOG_WARN, 
                         "bindroot: vw=%s vob=%s - no version selected\n",
                         mfs_vw2nm(vw), VFS_TO_MMI(vfsp)->mmi_mntpath);
	    }
	} else if (error == ESTALE) {
            mvfs_logperr(MFS_LOG_ESTALE, error, "bindroot: vw=%s vob=%s",
                         mfs_vw2nm(vw), VFS_TO_MMI(vfsp)->mmi_mntpath);
	}
    }
  done:
    HEAP_FREE(rap);
    HEAP_FREE(rrp);
    return(error);
}

/*
 * MFS_CLNT_REBIND - get current latest version for a dir
 */

int
mfs_clnt_rebind_dir(
    VNODE_T *dvp,
    VNODE_T **vpp,
    CALL_DATA_T *cd
)
{
    mfs_mnode_t *mnp;
    VNODE_T *vw;
    int modflags;
    timestruc_t start_time;	/* For stats/debug */
    timestruc_t dtime, dummy;
    HEAP_ALLOC_RPC_ARGS(view_revalidate);

    mnp = VTOM(dvp);
    ASSERT(MFS_ISVOB(mnp));

    vw = MFS_VIEW(dvp);
    if (vw == NULL) {
        error = ESRCH;
        goto done;
    }

    rap->hdr.view = VTOM(vw)->mn_view.vh;
    rap->hdr.build_handle = MFS_BH(cd);
    rap->fhandle = MFS_VFH(dvp);

    /* 
     * No need to lock here.  Just want to check and get new
     * file handle.
     */

    MDKI_HRTIME(&start_time);
    MVFS_VWCALL(vw, dvp->v_vfsp, VIEW_REVALIDATE, view_revalidate, );
    if (!error) {
	register struct mfs_mntinfo *mmi;
	MVFS_TIME_DELTA(start_time, dtime, dummy);
	if (dtime.tv_sec > mvfs_max_rpcdelay) {
	    mmi = VFS_TO_MMI(dvp->v_vfsp);
	    mvfs_log(MFS_LOG_INFO,
		    "view revalidate for %s:%s took a long time!\n",
		     mmi->mmi_svr.host, mmi->mmi_svr.rpn);
	}
    }
    if (!error) error = mfs_geterrno(rrp->hdr.status);
    if (!error) {
	/*
	 * See if we got back same file handle.  If so, return same
 	 * vnode as passed in.
	 */
	if (BCMP(&rrp->fhandle, &mnp->mn_vob.vfh, sizeof(rrp->fhandle)) == 0) {
	    *vpp = dvp;
	    VN_HOLD(*vpp);
	    MLOCK(mnp);
            mvfs_attrcache(mnp, dvp->v_vfsp, &(rrp->vstat), FALSE,
                           cd);
	    MUNLOCK(mnp);
            error = 0;
            goto done;
	}

	/* 
	 * Not same file handle - make the new vnode.
	 * NOTE: if dir were locked here, potential for deadlock
 	 * since no locking order enforced between different versions
	 * of the same dir.  SO... dvp must be unlocked!
         */
        error = mvfs_makevobnode(&(rrp->vstat), 0, vw, &(rrp->fhandle),
                                 dvp->v_vfsp, cd, vpp, TRUE);
    } else {
	MFS_CHK_STALE(error, dvp);
    }
  done:
    HEAP_FREE(rap);
    HEAP_FREE(rrp);
    return(error);
}

/*
 * Get the gpath of the "element" for this vnode (not the version).
 * This is only used from "mfs_rebind_vpp" for rebinding the
 * current directory when its version has changed.
 */
int
mfs_clnt_gpath_elem(
    VNODE_T *vp,
    mfs_pn_char_t **nmp,
    CALL_DATA_T *cd
)
{
    VNODE_T *vw;
    char *cp;
    HEAP_ALLOC_RPC_ARGS(view_gpath);

    ASSERT(MFS_ISVOB(VTOM(vp)));
    *nmp = NULL;

    vw = MFS_VIEW(vp);

    rap->hdr.view = VTOM(vw)->mn_view.vh;
    rap->hdr.build_handle = MFS_BH(cd);
    rap->fhandle = MFS_VFH(vp);		/* Fhandle of version */
    if (rap->fhandle.elem_dbid == MFS_UNK_DBID) {
	mvfs_log(MFS_LOG_INFO, 
		"no elem dbid for gpath vw=%s vob=%s dbid=0x%x\n", 
                 mfs_vp2vw(vp), mfs_vp2dev(vp), mfs_vp2dbid(vp));
        error = EINVAL;
        goto done;
    }
    rap->fhandle.ver_dbid = rap->fhandle.elem_dbid;		/* Fhandle of elem! */
    rap->max_path_size = MAXPATHLEN;
    rap->max_ext_path_size = MAXPATHLEN;

    rrp->path = KMEM_ALLOC(MAXPATHLEN, KM_SLEEP);
    if (rrp->path == NULL) {
        error = ENOMEM;
        goto done;
    }

    rrp->ext_path = KMEM_ALLOC(MAXPATHLEN, KM_SLEEP);
    if (rrp->ext_path == NULL) {
	KMEM_FREE(rrp->path, MAXPATHLEN);
        HEAP_FREE(rap);
        HEAP_FREE(rrp);
	return(ENOMEM);
    }

    MVFS_VWCALL_NO_XREV(vw, vp->v_vfsp, VIEW_GPATH, view_gpath);
    if (!error) error = mfs_geterrno(rrp->hdr.status);
    if (!error) {
	/*
	 * NOTE: the following is horrendously slow, but since this
         * call is only used very rarely (cwd rebinding), who cares ....
	 *
	 * The expected returned values are:
         * rrp->path = pname as far as is configured in this view
 	 * 		(usually the parent of the element)
         * rrp->ext_path = element pname (usually leafname of element)
         *
         * but ... if a parent dir has also been changed and is
         *	no longer configured in this view, then rrp->ext_path
         *	will contain multiple components (for the version extended
         *	name of a higher level dir), and we can not contruct
         *	a suitable name for bindroot to bind to... so give up
         *	in this case.
         */

	if (rrp->ext_path_size) {
	    /* Add a slash */
	    if (rrp->path_size < MAXPATHLEN-1) {
	        rrp->path[rrp->path_size] = MVFS_PN_SEP_CHAR;
	        rrp->path_size++;
	    }
	    /* Add rest of extended pname up to the first slash. */
	    for (cp=rrp->ext_path; 
                 rrp->path_size < MAXPATHLEN-1 && *cp && !PN_IS_SEPCHAR(*cp); 
                 cp++)
            {
		rrp->path[rrp->path_size] = *cp;
		rrp->path_size++;
	    }
	    rrp->path[rrp->path_size] = '\0';
	    /* 
             * If did not terminate on NULL, then the ext path is not
             * right (either total pname too long, or an unexpected
             * multi-component ext pname).  The root is an exception:
             * for the root, the ext_path is "/", and that is OK.. we
             * just don't add anything to the path.
             */
	    if (*cp && STRCMP(rrp->ext_path, "/") != 0 &&
                STRCMP(rrp->ext_path, "\\") != 0)
            {
		mvfs_log(MFS_LOG_INFO, 
                         "bad gpath name %s,%s\n", rrp->path, rrp->ext_path);
	    }
	}
	*nmp = STRDUP(rrp->path);		/* Dup to str caller can free. */
    }
 
    KMEM_FREE(rrp->path, MAXPATHLEN);
    KMEM_FREE(rrp->ext_path, MAXPATHLEN);
  done:
    HEAP_FREE(rap);
    HEAP_FREE(rrp);
    return(error);
}

/* 
 * MFS_CLNT_CLTXT_LOCKED - get(fetch) the cleartext pathname for this object.
 * CALL THIS ROUTINE WITH MNODE LOCKED!
 */

int
mfs_clnt_cltxt_locked(
    VNODE_T *vp,
    CALL_DATA_T *cd
)
{

    register VNODE_T *vw;
    register struct mfs_mnode *mnp;
    timestruc_t start_time;	/* For stats/debug */
    timestruc_t dtime, dummy;
    HEAP_ALLOC_RPC_ARGS(view_cltxt);

    if (!MVFS_ISVTYPE(vp, VREG)) {
        MDB_XLOG((MDB_CLEAROPS, "mfs_clnt_cltxt: vp=%"KS_FMT_PTR_T
                  ", err=%d\n", vp, EISDIR));
        error = EISDIR;
        goto done;
    }

    vw = MFS_VIEW(vp);
    ASSERT(vw);

    mnp = VTOM(vp);
    ASSERT(MFS_ISVOB(mnp));
    ASSERT(MISLOCKED(mnp));
    /* Detect problems with converting the isvob bit. */
    ASSERT(mnp->mn_vob.open_count == 0);

    /* Make sure someone didn't get the cleartext name between the
       time it was tested in an outer routine and locking the mnode.
       Typically the test is done without the MNODE lock to avoid
       extra locking overhead all the time. */

    if (mnp->mn_vob.cleartext.nm) {
        HEAP_FREE(rap);
        HEAP_FREE(rrp);
	return(0);	/* Already have name */
    }

    rap->hdr.view = VTOM(vw)->mn_view.vh;
    rap->hdr.build_handle = MFS_BH(cd);
    rap->fhandle = MFS_VFH(vp);
    rap->max_text_size = MAXPATHLEN;
    rrp->text = (char *)KMEM_ALLOC(MAXPATHLEN, KM_NOSLEEP);
    if (rrp->text == NULL) {
        MDB_XLOG((MDB_CLEAROPS, "mfs_clnt_cltxt: vp=%"KS_FMT_PTR_T
                  ", null rrp->text: err=%d\n", vp, ENOMEM));
        error = ENOMEM;
        goto done;
    }

    MDKI_HRTIME(&start_time);
    MVFS_VWCALL_NO_XREV(vw, vp->v_vfsp, VIEW_CLTXT, view_cltxt);
    if (!error) {
	register struct mfs_mntinfo *mmi;
	MVFS_TIME_DELTA(start_time, dtime, dummy);
	if (dtime.tv_sec > mvfs_max_rpcdelay) {
	    mmi = VFS_TO_MMI(vp->v_vfsp);
	    mvfs_log(MFS_LOG_INFO,
		    "view_cltxt for %s:%s took a long time!\n",
		     mmi->mmi_svr.host, mmi->mmi_svr.rpn);
	}
	error = mfs_geterrno(rrp->hdr.status);
        /* KLUDGE: to get status to higher level for
         * better error display to user when no cleartext available
         * i.e. data for that version does not exist du to rmver -data!
	 * Note: mfs_getcleartext() slams EIO over the error to
	 * user space when the fetch op fails, so this error
	 * should not make it out to the user.
         */
        if (rrp->hdr.status == TBS_ST_NOT_FOUND) error = TBS_ST_NOT_FOUND;
    }
    if (!error) {
	/* 
         * Construct full cleartext name and save it in vnode.
	 * "set_cpname" makes a copy of the data in rrp->text
         * into a minimum size buffer, so we can free up the maxpathlen
         * buffer allocated at this level.
         */
	mnp->mn_vob.cleartext.isvob = rrp->vob;
	error = mfs_set_cpname(mnp, rrp->text, rrp->text_size);
    } else {
	MFS_CHK_STALE(error, vp);
    }
    KMEM_FREE(rrp->text, MAXPATHLEN);

    if (mnp->mn_vob.cleartext.nm) {
        MDB_XLOG((MDB_CLEAROPS,"mfs_clnt_cltxt: vp=%"KS_FMT_PTR_T
                  ", cpname=%s, err=%d\n",
                  vp, mnp->mn_vob.cleartext.nm, error));
    } else {
        MDB_XLOG((MDB_CLEAROPS,"mfs_clnt_cltxt: vp=%"KS_FMT_PTR_T
                  ", null cpname, err=%d\n", vp, error));
    }
  done:
    HEAP_FREE(rap);
    HEAP_FREE(rrp);
    return(error);
}

int
mfs_clnt_change_mtype(
    register VNODE_T *vp,
    vob_mtype_t mtype,
    tbs_status_t *statusp,	/* Returned tbs_status */
    CALL_DATA_T *cd
)
{
    int xerror;
    VNODE_T *vw;
    mfs_mnode_t *mnp;
    HEAP_ALLOC_RPC_ARGS(view_change_mtype);

    mnp = VTOM(vp);
    ASSERT(MFS_ISVOB(mnp));

    vw = MFS_VIEW(vp);
    if (vw == NULL) {
        error = ESRCH;
        goto done;
    }

    rap->hdr.view = VTOM(vw)->mn_view.vh;
    rap->hdr.build_handle = MFS_BH(cd);
    rap->fhandle = MFS_VFH(vp);
    rap->mtype = mtype;

    MLOCK(mnp);		/* Lock to prevent multiple change mtype races */
    MVFS_VWCALL(vw, vp->v_vfsp, VIEW_CHANGE_MTYPE, view_change_mtype, );
    if (!error) {
        *statusp = rrp->hdr.status;
    } else {
	*statusp = TBS_ST_OK;
    }

    if (!error && *statusp == TBS_ST_OK) {
	/* Cache new attributes */
		mvfs_attrcache(mnp, vp->v_vfsp, &(rrp->vstat), 0, cd);
    } else {
	xerror = mfs_geterrno(rrp->hdr.status);
	MFS_CHK_STALE(xerror, vp);
    }
    MUNLOCK(mnp);
  done:
    HEAP_FREE(rap);
    HEAP_FREE(rrp);
    return(error);
}

/*
 * MVFS_CLNT_PING_SERVER 
 * Simple call to send a NULL RPC to the view server
 * to check if it's alive. We do not pass in a view vnode
 * for this call, to limit the overhead and the need to lock
 * the view during the RPC call.
 */
int
mvfs_clnt_ping_server(
   struct mfs_svr *svr,
   CRED_T *cred
)
{
   struct mfs_retryinfo rinfo;

   rinfo = VFS_TO_MMI(MDKI_VIEWROOT_GET_DATAP()->mfs_viewroot_vfsp)->mmi_retry;
   rinfo.soft = 1;

   return mfscall(mfs_viewcall,		/* trait */
		  NULLPROC,		/* op */
		  0,			/* xid */
		  svr,			/* mfs svr info */
		  &rinfo,		/* retry info */
		  (xdrproc_t)xdr_void,	/* xdrargs */
		  (void *)NULL,		/* args */
		  (xdrproc_t)xdr_void,	/* xdrresp */
		  (void *)NULL,		/* resp */
		  cred,			/* cred */
		  NULL			/* view vp */
		 );
}

/* MVFS_CLNT_GET_EACL_MNP - Get an effective ACL by using the rolemap oid.  The
** caller is assumed to have the mnode locked.  This routine is like
** mfs_getattr_mnp since the caller might not have a vnode ptr (e.g. we might
** have been called from mvfs_makevobnode() after the mfs_mnget() but before the
** MVFS_VNGET).
*/
int
mvfs_clnt_get_eacl_mnp(
    mfs_mnode_t *mnp,
    VFS_T *vfsp,
    CRED_T *cred,
    tbs_oid_t *rolemap_oid_p,     /* In: rolemap OID to convert to an EACL. */
    view_eacl_cursor_t *cursor_p, /* In/Out: cursor for chunks */
    tbs_sid_acl_h_t *eaclh_p,     /* In/Out: EACL, caller must free. */
    struct timeval *eacl_lut,     /* Out: EACL last update time. */
    tbs_oid_t *policy_oid_p,      /* Out: policy OID, need for flushing. */
    tbs_status_t *statusp         /* Out: TBS status. */
)
{
    VNODE_T *vw;
    MVFS_DECLARE_TEMP_CD(temp_cd);
    tbs_status_t status = TBS_ST_OK;
    HEAP_ALLOC_RPC_ARGS(view_eacl_rolemap);
    
    ASSERT(MISLOCKED(mnp));
    ASSERT(MFS_ISVOB(mnp));

    if ((vw = mnp->mn_hdr.viewvp) == NULL) {
        error = ESRCH;
        goto done;
    }
    rap->hdr.view = VTOM(vw)->mn_view.vh;
    /* See comment above about lack of initializers for these types. */
    rap->hdr.build_handle.build_session = VIEW_BUILD_SESSION_NONE;
    rap->hdr.build_handle.target_id = 0;
    rap->replica_uuid = VFS_TO_MMI(mnp->mn_hdr.vfsp)->mmi_vobuuid;
    rap->rolemap_oid = *rolemap_oid_p;
    rap->cursor = *cursor_p;
    /*
     * XDR needs start cursor and eaclh filled in the reply structure
     * in order to unpack correctly
     */
    rrp->cursor = *cursor_p;
    rrp->eaclh = *eaclh_p;

    MVFS_VWCALL_NO_XREV_CR(vw, vfsp, VIEW_EACL_ROLEMAP, view_eacl_rolemap,
                           cred);

    /* If error == 0 the hdr.status is valid, so it's OK to get it. */
    if ((error == 0) && ((status = rrp->hdr.status) == TBS_ST_OK)) {
        /* The xdr code already allocated space and filled in the EACL, so we
        ** just want to return it to our caller.  They are responsible for
        ** freeing it with tbs_sid_acl_free() when they're done.  The HEAP_FREE
        ** below just "frees" the handle (which is a ptr), so the actual EACL
        ** storage isn't affected (and we've copied the handle here).
        */
        *eaclh_p = rrp->eaclh;
        *eacl_lut = rrp->eacl_mtime;
        *policy_oid_p = rrp->policy_oid;
        *cursor_p = rrp->cursor;
    } else {
        /* On an error, we set the eaclh to NULL and the mtime to the current
        ** time (we only care about seconds, I guess) so our caller knows when
        ** we last tried the RPC.  We leave the policy oid alone because it goes
        ** with the rolemap_oid even if we didn't get the eaclh.
        */
        *eaclh_p = TBS_SID_ACL_H_NULL;
        (*eacl_lut).tv_sec = MDKI_CTIME();

        /* If (error != 0) the RPC code has indicated a problem so we probably
        ** don't want to do any ESTALE handling (e.g. invalidate caches), which
        ** we won't, because status is still TBS_ST_OK from our initialization
        ** and mfs_geterrno() will return 0 (not ESTALE).  If (error == 0), the
        ** status was returned from the view server, and mfs_geterrno() converts
        ** it to whatever is appropriate.
        **
        ** The EACL caching/flushing on error is handled by our caller (which is
        ** different from the attribute caching handled by CHK_STALE).
        */
        MVFS_INIT_TEMP_CD(temp_cd_p, cred, mvfs_mythread(NULL));
        MVFS_CHK_STALE_MNP(mfs_geterrno(status), mnp, vfsp, temp_cd_p);
    }
  done:
    HEAP_FREE(rap);
    HEAP_FREE(rrp);
    *statusp = status;
    return(error);
}
static const char vnode_verid_mvfs_clnt_c[] = "$Id:  d5a51817.64234059.beee.01:be:88:29:f8:2e $";
