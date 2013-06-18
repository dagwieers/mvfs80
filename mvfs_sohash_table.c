/* * (C) Copyright IBM Corporation 2012. */
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

#include <linux/errno.h>

#include "mvfs_systm.h"
#include "mvfs.h"
#include "mvfs_debug.h"
#include "mvfs_sohash_table.h"

/* FUNCTION PROTOTYPES FOR INTERNAL ROUTINES.  */
static int
sohash_delete_entry_subr(
    sohash_table_t       *hashd,
    SOHASH_REVERSE_KEY_T reverse_so_key,
    sohash_hazard_ref_t  *myhp,
    sohash_entry_t       **bucket,
    SOHASH_BOOL_T        entry_type,
    void                 *arg_verify_callback
);

static int
sohash_initialize_bucket(
    sohash_table_t *hashd,
    SOHASH_HASHINDEX_T  hashindex,
    sohash_hazard_ref_t *myhp,
    sohash_entry_t ***new_bucket
);

static int
sohash_hp_get(
    sohash_table_t *hashd,
    sohash_hazard_ref_t **myhp
);

static void sohash_hp_release(sohash_hazard_ref_t *myhp);

static int sohash_flush_hp_list(sohash_table_t *hashd);

/* Prototypes of functions to work on the lock less linked list.  */
static int
sohash_list_insert(
    sohash_table_t      *hashd,
    sohash_entry_t      **head,
    sohash_entry_t      *node,
    sohash_hazard_ref_t *myhp,
    void                *arg_verify_callback,
    sohash_entry_t      **found_node
);

static sohash_entry_t *
sohash_list_find(
    sohash_table_t       *hashd,
    sohash_entry_t       **head,
    SOHASH_REVERSE_KEY_T sohash_reverse_key,
    SOHASH_BOOL_T        entry_type,
    void                 *arg_verify_callback,
    sohash_hazard_ref_t  *myhp
);

static int
sohash_list_delete(
    sohash_table_t       *hashd,
    sohash_entry_t       **head,
    SOHASH_REVERSE_KEY_T sohash_reverse_key,
    SOHASH_BOOL_T        entry_type,
    void                 *arg_verify_callback,
    sohash_hazard_ref_t  *myhp
);

/* Function prototypes of the Safe Memory Reclamation (SMR) routines.  */
static void
sohash_retire_node(
    sohash_table_t      *hashd,
    sohash_entry_t      *node,
    sohash_hazard_ref_t *myhp
);

static void
sohash_hprec_helpscan(
    sohash_table_t      *hashd,
    sohash_hazard_ref_t *myhp
);

static void
sohash_hprec_scan(
    sohash_table_t      *hashd,
    sohash_hazard_ref_t *hp_head,
    sohash_hazard_ref_t *myhp
);

static void
sohash_sort_arr(
    SOHASH_REVERSE_KEY_T      *arr,
    SOHASH_HPCOUNT_INTEGRAL_T count
);

int
sohash_search_arr(
    SOHASH_REVERSE_KEY_T *arr, 
    SOHASH_REVERSE_KEY_T rev_sokey, 
    int                  low,
    int                  high
);

static int
sohash_cleanup_bucket(
    sohash_table_t *hashd,
    SOHASH_INDEX_T segment_index,
    SOHASH_INDEX_T bucket_index,
    sohash_hazard_ref_t *myhp
);

static sohash_entry_t **
sohash_get_bucket(
    sohash_table_t *hashd,
    SOHASH_HASHINDEX_T hashindex
);

static int
sohash_set_bucket(
    sohash_table_t *hashd,
    SOHASH_HASHINDEX_T hashindex,
    sohash_entry_t *bucket_head,
    sohash_entry_t ***bucket,
    SOHASH_BOOL_T  insert_success
);

/* Prototypes for sohash_entry_t allocation and de-allocation routines.  */
static sohash_entry_t *sohash_alloc_node(void);

static void sohash_free_node(sohash_table_t *hashd, sohash_entry_t *node);

static void sohash_free_data(sohash_table_t *hashd, void *datap);

/* Prototypes of functions for computing SOHASH_REVERSE_KEY_T from
 * SOHASH_KEY_T.  
 */
static inline SOHASH_REVERSE_KEY_T sohash_regularkey(SOHASH_KEY_T key);

static inline SOHASH_REVERSE_KEY_T sohash_sentinelkey(SOHASH_KEY_T key);

/* Prototypes of functions to atomically increment and decrement counters.  */
static SOHASH_KEYCOUNT_INTEGRAL_T
sohash_fetch_and_inc(
    SOHASH_KEYCOUNT_T *counter
);

static SOHASH_KEYCOUNT_INTEGRAL_T
sohash_fetch_and_dec(
    SOHASH_KEYCOUNT_T *counter
);

#ifdef MVFS_USE_SPLIT_ORDERED_HASH
#if defined(ATRIA_LINUX) && (!(defined(ATRIA_LINUX_390) || defined(ATRIA_LINUX_PPC)))
int mvfs_lockless_thrhash_enabled = 1;
#else
int mvfs_lockless_thrhash_enabled = 0;
#endif
#endif

/* Prototypes of functions for bit manipulation.  */
static SOHASH_HASHINDEX_T unsetmsb(SOHASH_HASHINDEX_T input);

static inline SOHASH_REVERSE_KEY_T sohash_reverse_bit(SOHASH_KEY_T input);

/* Macro Definitions for return value of routines for internal use.  */
#define SOHASH_RETVAL_FAILURE 1
#define SOHASH_RETVAL_SUCCESS 0

/* The following booleans are used to represent the state of the hp structure
 * on the hazard pointer reference linked list (hp_head).  The values are 
 * intentionally 1 and 2, rather than 0 and 1.  
 */
#define SOHASH_HP_FREE   1
#define SOHASH_HP_INUSE  2

/* Macros for bit manipulation.  */
#define SOHASH_SET_DELETE_BIT(addr) \
    ((sohash_entry_t *)(((unsigned long) (addr)) | 1))

#define SOHASH_IS_DELETE_BIT_SET(addr) \
    (((unsigned long) (addr)) & 1) /* bitwise AND */

#define SOHASH_UNSET_DELETE_BIT(addr) \
    ((sohash_entry_t *)(((unsigned long) (addr)) & ~1))

#define SOHASH_MAX_LOAD (1)

/* The bit mask below is applicable only for the uint32_t types considered for
 * the so_key and reverse_so_key variables. 
 */
#define SOKEY_MASK_SET_MSB (0x80000000)
#define SOKEY_SETMSB(input) ((input) | SOKEY_MASK_SET_MSB)

#define SOHASH_GETPARENT_BUCKET(bucket) (unsetmsb(bucket))

/* Function to create a hashtable. 
 * Requires as input pointer to the sohash_init_args_t structure duely 
 * populated.
 */
sohash_table_t *
sohash_init_hashtable(sohash_init_args_t *init_args)
{
    /* Split ordered hash descriptor which will be used for identifying and
     * operating on the hash.  
     */
    int error = 0;
    int len = 0;
    sohash_table_t *hashd = NULL;
    sohash_entry_t *sentinel_node = NULL;
    SOHASH_SEG_SZ_T  segment_sz = 0;
    SOHASH_SEG_COUNT_INTEGRAL_T num_segments = 0;

    if ((init_args == NULL) || 
        (init_args->fn_data_free == NULL) ||
        (init_args->fn_compute_sohashkey == NULL) ||
        ((init_args->sohash_keys_unique == SOKEY_UNIQUE_FALSE) &&
         (init_args->fn_verify_matched_entry == NULL))) 
    {
        /* sohash_init_args_t is required for initialising the hash.
         * Log the error and abort.
         */
        MDB_XLOG((MDB_SOHASH, "sohash init: Invalid argument\n")); 
        return(NULL);
    }

    if (MDKI_ATOMIC_READ_UINT32(&init_args->sohash_num_segments) ==
            SOHASH_SET_SEG_DEFAULT) 
    {
        /* Setting to the default value.  */
        num_segments = SOHASH_DEFAULT_SEG_COUNT;
    } 
    else if (MDKI_ATOMIC_READ_UINT32(
               &init_args->sohash_num_segments) < SOHASH_MIN_SEG_COUNT) 
    {
        /* Log the error and return NULL.  */
        MDB_XLOG((MDB_SOHASH, "sohash init: Number of segments should be "
                "greater than or equal to %u\n", SOHASH_MIN_SEG_COUNT));
        return(NULL); 
    }

    if (init_args->sohash_segment_sz == SOHASH_SET_SEG_DEFAULT) {
        /* Setting to the default value.  */
        segment_sz = SOHASH_DEFAULT_SEG_SZ;
    } else if (init_args->sohash_segment_sz < SOHASH_MIN_SEG_SZ) {
        /* Log the error and return NULL.  */
        MDB_XLOG((MDB_SOHASH, "sohash init: Segment size should be "
                "greater than or equal to %u\n", SOHASH_MIN_SEG_SZ));
        return(NULL);
    }

    hashd = (sohash_table_t *)KMEM_ALLOC(sizeof(sohash_table_t), KM_SLEEP);
    if (hashd == (sohash_table_t *)NULL) {
        MDB_XLOG((MDB_SOHASH,
                 "sohash init: Memory allocation failure (sohash_table_t)\n"));
        error = ENOMEM;
    }

    if (error == 0) {
        BZERO(hashd, sizeof(sohash_table_t));

        /* Allocate for the array of segments.  */
        len = sizeof(sohash_segment_t) * num_segments;
        MDKI_ATOMIC_PTR_SET(&hashd->hashtable,
                      (sohash_segment_t *) KMEM_ALLOC(len, KM_SLEEP));
        if (MDKI_ATOMIC_PTR_READ(&hashd->hashtable) == NULL) {
            MDB_XLOG((MDB_SOHASH, "sohash init: Memory allocation failure "
                     "(sohash_segment_t)\n"));
            error = ENOMEM;
            /* Cleanup the memory allocated for sohash_table_t.  */
            KMEM_FREE(hashd, sizeof(sohash_table_t));
            hashd = NULL;
        } else {
            /* Set all the entries to NULL.  */
            BZERO(MDKI_ATOMIC_PTR_READ(&hashd->hashtable), len);

            /* Initializing some of the fields in hashd.  */
            MDKI_ATOMIC_SET_UINT32(&hashd->sohash_num_segments, num_segments);
            hashd->sohash_segment_sz = segment_sz;
        }
    }

    if (error == 0) {
        /* Initialize the first segment and its first bucket to point to the 
         * sentinel node with key 0.  
         */
        sentinel_node = sohash_alloc_node();
        if (sentinel_node == NULL) {
            error = ENOMEM;

            /* Cleanup before returning.  */
            KMEM_FREE(hashd->hashtable, len);
            KMEM_FREE(hashd, sizeof(sohash_table_t));
            hashd = NULL;
        } else {
            /* Sentinel node successfully allocated.  Initialize it.  */
            sentinel_node->reverse_so_key = sohash_sentinelkey(0);
            sentinel_node->so_key = 0;
            MDKI_ATOMIC_PTR_SET(&sentinel_node->hashentry_next, NULL);
            sentinel_node->entry_type = SOHASH_SENTINEL_NODE;

            /* Initialize the first bucket of the first segment in the 
             * hashtable to the sentinel node that was allocated.  Note that
             * this will be the head of the lock less singly linked list that
             * will store all the hash entries.
             */
            error = sohash_set_bucket(hashd, 0, sentinel_node, NULL,
                                      SOHASH_TRUE);
            if (error != 0) {
                MDB_XLOG((MDB_SOHASH, "sohash init: Initialization of first" 
                         "bucket of first segment failed. hashd 0x%x, "
                         "error %u\n", hashd, error));

                /* Cleanup before returning.  */
                sohash_free_node(hashd, sentinel_node);
                KMEM_FREE(hashd->hashtable, len);
                KMEM_FREE(hashd, sizeof(sohash_table_t));
                hashd = NULL;
            } else {
                MDB_XLOG((MDB_SOHASH, "sohash init: First bucket of first "
                         "segment initialized. hashd 0x%x bucket head 0x%x\n",
                         hashd, sentinel_node));
                sentinel_node->hashindex = 0;
            }
        }
    }

    if (error == 0) {
        /* Initialize the hpref linked list.  */
        hashd->hp_head = (sohash_hazard_ref_t *) 
                      KMEM_ALLOC(sizeof(sohash_hazard_ref_t), KM_SLEEP);
        if (hashd->hp_head == NULL) {
            error = ENOMEM;

            /* Cleanup before returning.  */
            sohash_free_node(hashd, sentinel_node);
            KMEM_FREE(hashd->hashtable, len);
            KMEM_FREE(hashd, sizeof(sohash_table_t));
            hashd = NULL;
        }
    }

    if (error == 0) {
        BZERO(hashd->hp_head, sizeof(sohash_hazard_ref_t));

        hashd->hp_head->hp_next = NULL;

        /* Initialize the fields of the new hpref head.  */
        MDKI_ATOMIC_PTR_SET(&hashd->hp_head->hprec.rlist, NULL);
        hashd->hp_head->hprec.rcount = 0;
        MDKI_ATOMIC_SET_UINT32(&hashd->hp_head->hp_state, SOHASH_HP_FREE);

        hashd->hp_head->hp0 = &(hashd->hp_head->hprec.hazard_ptrs[0]);
        hashd->hp_head->hp1 = &(hashd->hp_head->hprec.hazard_ptrs[1]);
        hashd->hp_head->hp2 = &(hashd->hp_head->hprec.hazard_ptrs[2]);

        MDKI_ATOMIC_SET_UINT32(&hashd->hazard_ptrs_total, 3);
    }

    if (error == 0) {
        /* Initialize the other fields in sohash_table_t.  */

        hashd->sohash_keys_unique = init_args->sohash_keys_unique;
        if (hashd->sohash_keys_unique == SOKEY_UNIQUE_FALSE) {
            hashd->fn_verify_matched_entry = init_args->fn_verify_matched_entry;
        } else {
            hashd->fn_verify_matched_entry = NULL;
        }

        hashd->fn_data_free = init_args->fn_data_free;
        hashd->fn_compute_sohashkey = init_args->fn_compute_sohashkey;
        hashd->fn_on_delete = init_args->fn_on_delete;

        /* Initialize the other private variables in sohash_table_t.  */
        MDKI_ATOMIC_SET_UINT32(&hashd->sohash_current_size, 2);

        MDKI_ATOMIC_SET_UINT32(&hashd->sohash_regularkey_count, 0);

        MDKI_ATOMIC_SET_UINT32(&hashd->hash_state, SOHASH_STATE_ACTIVE);

        MDB_XLOG((MDB_SOHASH, "sohash init: Hashtable successfully initialized."
                "hashd 0x%x, number of segments %u, segment size %u, "
                " sentinelkey count %u, regularkey count %u, hp head 0x%x "
                "hazard ptr total %u, error %u\n", hashd, 
                MDKI_ATOMIC_READ_UINT32(&hashd->sohash_num_segments),
                hashd->sohash_segment_sz, 
                MDKI_ATOMIC_READ_UINT32(&hashd->sohash_sentinelkey_count),
                MDKI_ATOMIC_READ_UINT32(&hashd->sohash_regularkey_count),
                MDKI_ATOMIC_PTR_READ(&hashd->hp_head),
                MDKI_ATOMIC_READ_UINT32(&hashd->hazard_ptrs_total), error));
    } else {
        mvfs_log(MFS_LOG_ERR, "sohash init: Hashtable initialization failed\n");
    }

    return(hashd);
}

/* Routine to cleanup the hash by flushing all the entries and freeing the
 * memory allocated for all the associated data structures, including the hash
 * descriptor.
 */
int
sohash_cleanup_hashtable(sohash_table_t *hashd)
{
    int error = 0;
    sohash_hazard_ref_t *myhp;
    sohash_entry_t *sentinel_node;
    SOHASH_INDEX_T bucket_index;
    SOHASH_INDEX_T segment_index;
    SOHASH_HASH_STATE_INTEGRAL_T cur_hash_state;
    sohash_segment_t segment;

    ASSERT(hashd != NULL);

    cur_hash_state = MDKI_ATOMIC_READ_UINT32(&hashd->hash_state);
    if (cur_hash_state == SOHASH_STATE_FLUSH) {
        /* A hash flush is already in progress. Log the state and return.  */
        return(EBUSY);
    } else if (cur_hash_state == SOHASH_STATE_ACTIVE) {
        /* It is safe only when the hash state is transitioned from 
         * SOHASH_STATE_ACTIVE state.  CAS could still fail if some other thread
         * raced ahead to set the state to SOHASH_STATE_FLUSH or the current 
         * hash state is not SOHASH_STATE_ACTIVE and has changed since it was 
         * last checked. 
         */
        if (!MDKI_ATOMIC_CAS_UINT32(&hashd->hash_state, 
                               SOHASH_STATE_ACTIVE, 
                               SOHASH_STATE_FLUSH)) 
        {
            return(EBUSY);
        }
    } else {
        /* Currently SOHASH_STATE_FLUSH and SOHASH_STATE_ACTIVE are the only 
         * valid hash states.  Log the error if the state is anything other than
         * the two and return.  
         */
        return(EFAULT);
    }

    if ((error = sohash_hp_get(hashd, &myhp)) != 0) {
        return(error);
    }

    /* Before cleaning up the buckets, set the sohash_keys_unique flag in
     * the hash descriptor to SOKEY_UNIQUE_TRUE, so that while cleaning up the
     * buckets, verification routine (fn_verify_matched_entry()) is not called.  
     */
    hashd->sohash_keys_unique = SOKEY_UNIQUE_TRUE;

    /* Step 1: Cleanup one segment after another and each bucket in the segment,
     *         deleting entries from the hash.
     *         Cleanup Sentinel nodes as well and mark the corresponding buckets
     *         in the hashtable as SOHASH_UNINITIALIZED.
     *
     *   The sohash_sentinelkey_count will also signify the number of
     *   initialized buckets.  Going thro' the entire hash until all the
     *   buckets are uninitialized.  Leave bucket 0 of segment 0 for last.  
     */
retry_loop:
    segment_index = 0;
    while ((segment_index <= 
                MDKI_ATOMIC_READ_UINT32(&hashd->sohash_num_segments)) &&
           (MDKI_ATOMIC_READ_UINT32(&hashd->sohash_sentinelkey_count) > 1)) 
    {
        if (MDKI_ATOMIC_PTR_READ(&hashd->hashtable[segment_index]) !=
                            SOHASH_UNINITIALIZED) 
        {
            for (bucket_index = 0; 
                 bucket_index < hashd->sohash_segment_sz; 
                 bucket_index++) 
            {
                if ((segment_index == 0) && (bucket_index == 0)) {
                    /* cleanup bucket 0 of segment 0 in the last.  */
                    continue;
                }

                if (MDKI_ATOMIC_PTR_READ(
                            &hashd->hashtable[segment_index][bucket_index]) != 
                            SOHASH_UNINITIALIZED) 
                {
                    /* Delete all the entries in the bucket.  */
                    sohash_cleanup_bucket(hashd, 
                                          segment_index, 
                                          bucket_index, 
                                          myhp);
                    /* Mark the bucket as uninitialized.  */
                    sentinel_node = 
                        MDKI_ATOMIC_PTR_READ(
                        &hashd->hashtable[segment_index][bucket_index]);
                    MDKI_ATOMIC_CAS_PTR(
                            &(MDKI_ATOMIC_PTR_READ(
                            &hashd->hashtable[segment_index][bucket_index])),
                            sentinel_node,
                            SOHASH_UNINITIALIZED);
                    ASSERT(MDKI_ATOMIC_PTR_READ(
                           &hashd->hashtable[segment_index][bucket_index]) ==
                           SOHASH_UNINITIALIZED);

                    /* Delete the sentinel node.  */
                    if (sohash_list_delete(hashd,
                                           &sentinel_node,
                                           sentinel_node->reverse_so_key,
                                           SOHASH_SENTINEL_NODE,
                                           NULL,
                                           myhp) != 0) 
                    {
                        return(SOHASH_RETVAL_FAILURE);
                    }

                    /* Decrement the number of sentinel keys in the hash.  */
                    MDKI_ATOMIC_DECR_UINT32(&(hashd->sohash_sentinelkey_count));
                }
            }

            if (segment_index != 0) {
                /* Free the memory allocated for the segment.  */
                segment = 
                    MDKI_ATOMIC_PTR_READ(&hashd->hashtable[segment_index]);
                MDKI_ATOMIC_CAS_PTR(&(hashd->hashtable[segment_index]), 
                               segment, 
                               SOHASH_UNINITIALIZED);

                ASSERT(MDKI_ATOMIC_PTR_READ(&hashd->hashtable[segment_index]) ==
                       SOHASH_UNINITIALIZED);

                KMEM_FREE(segment,
                         (sizeof(sohash_entry_t *) * hashd->sohash_segment_sz));
            }
        }
        segment_index++;
    }

    /* Deal with segement 0 bucket 0. */
    sohash_cleanup_bucket(hashd, 0, 0, myhp);

    if ((MDKI_ATOMIC_READ_UINT32(&hashd->sohash_regularkey_count) != 0) ||
        (MDKI_ATOMIC_READ_UINT32(&hashd->sohash_sentinelkey_count) > 1)) 
    {
        /* Some threads might have added a few more regular keys to the hash or
         * re-initialized some buckets.  Will have to go over the loop again to
         * clean them up.
         */
        goto retry_loop;
    }

    /* Step 2: Mark the first bucket of the first segment as uninitialized and 
     *         then free the memory allocated for the first sentinel node.
     */
    sentinel_node = MDKI_ATOMIC_PTR_READ(&hashd->hashtable[0][0]);
    MDKI_ATOMIC_CAS_PTR(&(hashd->hashtable[0][0]), 
                   sentinel_node, 
                   SOHASH_UNINITIALIZED);
    ASSERT(MDKI_ATOMIC_PTR_READ(&hashd->hashtable[0][0]) == 
           SOHASH_UNINITIALIZED);

    MDKI_ATOMIC_DECR_UINT32(&(hashd->sohash_sentinelkey_count));
    sohash_free_node(hashd, sentinel_node);

    /* At this stage all the buckets must have have been unset and all regular
     * keys must have been removed.  If not we have reached an inconsistent
     * state.  
     */
    ASSERT((MDKI_ATOMIC_READ_UINT32(&hashd->sohash_sentinelkey_count) == 0) &&
           (MDKI_ATOMIC_READ_UINT32(&hashd->sohash_regularkey_count) == 0));

    /* Step 3: Free the memory allocated for the first segment and then 
     *         sohash_segment_t *hashtable and set hashd->hashtable to NULL.  
     */
    KMEM_FREE(hashd->hashtable[0], 
              (sizeof(sohash_entry_t *) * hashd->sohash_segment_sz));
    KMEM_FREE(hashd->hashtable,
              (sizeof(sohash_segment_t) *
              MDKI_ATOMIC_READ_UINT32(&hashd->sohash_num_segments)));
    MDKI_ATOMIC_PTR_SET(&hashd->hashtable, NULL);

    /* Step 4: Walk thro' rlists in every hprec of every hp in the list and
     *         clean them up.  All rlist must be empty and rcount should be zero
     *         to mark completion of this stage.  
     */
    /* First free the sohash_hazard_ref_t reference as this is no longer
     * required. 
     */
    sohash_hp_release(myhp);

    sohash_flush_hp_list(hashd);

    /* Step 5: Free the memory allocated for hashd.  All the associated data
     *         structures have been removed.  Only the hash descriptor
     *         remains.  Free its memory and return.  
     */
    KMEM_FREE(hashd, sizeof(sohash_table_t));

    return(0);
}

int
sohash_insert_entry(
    sohash_table_t *hashd,
    SOHASH_KEY_T   sohash_key,
    void           *data,
    void           *arg_verify_callback,
    sohash_entry_t **node
)
{
    int error = 0;
    SOHASH_SZ_INTEGRAL_T sohash_size;
    SOHASH_HASHINDEX_T hashindex;
    sohash_entry_t *new_entry;
    sohash_entry_t *bucket_head = NULL;
    sohash_entry_t **bucket = NULL;
    sohash_hazard_ref_t *myhp;

    ASSERT(hashd != NULL);

    if (node != NULL) {
        *node = NULL;
    }

    if (MDKI_ATOMIC_READ_UINT32(&hashd->hash_state) != SOHASH_STATE_ACTIVE) {
        MDB_XLOG((MDB_SOHASH,
                 "sohash: Failed to insert entry %lx, the specified "
                 "hash is not active.\n", sohash_key));
        sohash_free_data(hashd, data);
        return(EBUSY);
    }

    if ((error = sohash_hp_get(hashd, &myhp)) != 0) {
        sohash_free_data(hashd, data);
        return(error);
    }

    hashindex = sohash_key % 
                    MDKI_ATOMIC_READ_UINT32(&hashd->sohash_current_size);

    if ((new_entry = sohash_alloc_node()) == NULL) {
        error = ENOMEM;
        sohash_free_data(hashd, data);
        goto cleanup;
    }

    /* Initialize the fields of the node.  */
    new_entry->reverse_so_key = sohash_regularkey(sohash_key);
    new_entry->so_key = sohash_key;
    new_entry->hashindex = hashindex;
    new_entry->entry_type = SOHASH_REGULAR_NODE;
    new_entry->data = data;

    bucket = sohash_get_bucket(hashd, hashindex);

    if ((bucket == SOHASH_UNINITIALIZED) &&
        (sohash_initialize_bucket(hashd, hashindex, myhp, &bucket) == 
             SOHASH_RETVAL_FAILURE))
    {
        MDB_XLOG((MDB_SOHASH,
                 "sohash insert entry: Failed to insert 0x%x, unable to " 
                 "initialize hash bucket, hashindex = %u.\n", 
                 sohash_key, hashindex));
        error = EFAULT;
        sohash_free_node(hashd, new_entry);
        goto cleanup;
    }
  
    ASSERT(bucket != NULL);

    bucket_head = *bucket;
    new_entry->segment_index = bucket_head->segment_index;
    new_entry->bucket_index = bucket_head->bucket_index;

    error = sohash_list_insert(hashd, bucket, new_entry, myhp,
                               arg_verify_callback, node);
    if (error != 0) {
        /* Found the key in the hash.  */
        sohash_free_node(hashd, new_entry);
        
        ASSERT(error != EEXIST);

        goto cleanup;
    }

    MDKI_ATOMIC_INCR_UINT32(&(hashd->sohash_regularkey_count));

    sohash_size = MDKI_ATOMIC_READ_UINT32(&hashd->sohash_current_size);

    if ((MDKI_ATOMIC_READ_UINT32(&hashd->sohash_regularkey_count) / sohash_size)
            > SOHASH_MAX_LOAD) 
    {
        /* It shouldn't matter if this operation fails.  If it does, we
         * can safely assume the hash size was increased by some other thread
         * because the size of the hash can never be decreased.
         */
        MDKI_ATOMIC_CAS_UINT32(&(hashd->sohash_current_size), 
                          sohash_size, 
                          sohash_size * 2);
    }

    error = 0;
    if (node != NULL) {
        *node = new_entry;
    }

    MDB_XLOG((MDB_SOHASH, 
             "sohash insert entry: insert of entry with sohash key 0x%x "
             "completed successfully. reverse sokey 0x%x, segment index %u, "
             "bucket index %u, current hash size %u\n", sohash_key,
             new_entry->reverse_so_key, new_entry->segment_index, 
             new_entry->bucket_index,
             MDKI_ATOMIC_READ_UINT32(&hashd->sohash_current_size)));

cleanup:
    sohash_hp_release(myhp);

    MDB_XLOG((MDB_SOHASH, "sohash insert entry: hashd 0x%x, error %u\n",
             hashd, error));

    return(error);
}

sohash_entry_t *
sohash_find_entry(
    sohash_table_t *hashd,
    SOHASH_KEY_T   sohash_key,
    void           *arg_verify_callback
)
{
    sohash_hazard_ref_t *myhp;
    SOHASH_HASHINDEX_T hashindex;
    sohash_entry_t *ret;
    SOHASH_REVERSE_KEY_T reverse_so_key;
    sohash_entry_t **bucket = NULL;

    ASSERT(hashd != NULL);

    if (MDKI_ATOMIC_READ_UINT32(&hashd->hash_state) != SOHASH_STATE_ACTIVE) {
        MDB_XLOG((MDB_SOHASH,
                 "sohash: Unable to search for entry 0x%x, the "
                 "specified hash is not active.\n",
                 sohash_key));
        return(NULL);
    }

    if (sohash_hp_get(hashd, &myhp) != 0) {
        return(NULL);
    }

    hashindex = sohash_key % 
                    MDKI_ATOMIC_READ_UINT32(&hashd->sohash_current_size);
    bucket = sohash_get_bucket(hashd, hashindex);
    
    if ((bucket == SOHASH_UNINITIALIZED) &&
        (sohash_initialize_bucket(hashd, hashindex, myhp, &bucket) == 
             SOHASH_RETVAL_FAILURE))
    {
        MDB_XLOG((MDB_SOHASH,
                 "sohash find entry: Failed to initialize hash bucket, "
                 "hashd = 0x%x, sohash key 0x%x hashindex = 0x%x.\n",
                 hashd, sohash_key, hashindex));
        ret = NULL;
        goto cleanup;
    }

    reverse_so_key = sohash_regularkey(sohash_key);    
    ret = sohash_list_find(hashd, bucket, reverse_so_key, SOHASH_REGULAR_NODE, 
                           arg_verify_callback, myhp);

cleanup:
    sohash_hp_release(myhp);

    MDB_XLOG((MDB_SOHASH, 
             "sohash find entry: hashd = 0x%x, sohash key 0x%x, reverse so key "
             "0x%x, hashindex = 0x%x, ret = 0x%x\n", 
             hashd, sohash_key, reverse_so_key, hashindex, ret));

    return(ret);
}

int
sohash_delete_entry(
    sohash_table_t *hashd,
    SOHASH_KEY_T   sohash_key,
    void           *arg_verify_callback
)
{
    int error;
    sohash_hazard_ref_t *myhp;
    SOHASH_HASHINDEX_T hashindex;
    SOHASH_REVERSE_KEY_T reverse_so_key;
    sohash_entry_t **bucket = NULL;

    ASSERT(hashd != NULL);

    if (MDKI_ATOMIC_READ_UINT32(&hashd->hash_state) != SOHASH_STATE_ACTIVE) {
        MDB_XLOG((MDB_SOHASH,
                 "sohash: Failed to delete entry %lx, the specified "
                 "hash is not active. Hash state = %d\n",
                 sohash_key, MDKI_ATOMIC_READ_UINT32(&hashd->hash_state)));
        return(EBUSY);
    }

    if (sohash_hp_get(hashd, &myhp) != 0) {
        return(ENOMEM);
    }

    hashindex = sohash_key % 
                    MDKI_ATOMIC_READ_UINT32(&hashd->sohash_current_size);
    bucket = sohash_get_bucket(hashd, hashindex);

    if ((bucket == SOHASH_UNINITIALIZED) &&
        (sohash_initialize_bucket(hashd, hashindex, myhp, &bucket) == 
             SOHASH_RETVAL_FAILURE))
    {
        MDB_XLOG((MDB_SOHASH,
                 "sohash delete entry: Failed to delete entry, bucket "
                 "initialization failed. hashd 0x%x, hashindex 0x%x, "
                 "sohash key 0x%x, bucket 0x%x\n", 
                 hashd, hashindex, sohash_key, bucket));

        return(EFAULT);
    }

    reverse_so_key = sohash_regularkey(sohash_key);

    error = sohash_delete_entry_subr(hashd,
                                     reverse_so_key,
                                     myhp,
                                     bucket,
                                     SOHASH_REGULAR_NODE,
                                     arg_verify_callback);

    sohash_hp_release(myhp);

    MDB_XLOG((MDB_SOHASH, "sohash delete entry: hashd 0x%x, hashindex 0x%x, "
             "sohash key 0x%x, reverse sokey 0x%x, bucket %u, error %u\n", 
             hashd, hashindex, sohash_key, reverse_so_key, bucket, error));

    return(error);
}

/* INTERNAL ROUTINES */

static int
sohash_delete_entry_subr(
    sohash_table_t       *hashd,
    SOHASH_REVERSE_KEY_T reverse_so_key,
    sohash_hazard_ref_t  *myhp,
    sohash_entry_t       **bucket,
    SOHASH_BOOL_T        entry_type,
    void                 *arg_verify_callback
)
{
    int error;

    ASSERT((hashd != NULL) && (myhp != NULL));

    error = sohash_list_delete(hashd, bucket, reverse_so_key, entry_type,
                               arg_verify_callback, myhp);
    if (error != 0) {
        MDB_XLOG((MDB_SOHASH, "sohash delete entry subr: list delete for the "
                 "key failed. hashd 0x%x, reverse sokey 0x%x, error %u\n", 
                 hashd, reverse_so_key, error));

        return(error);
    }

    if (MDKI_ATOMIC_READ_UINT32(&hashd->sohash_regularkey_count) == 0) {
        MDKI_PANIC("sohash_delete_entry_subr: regularkey count is zero");
    }
    MDKI_ATOMIC_DECR_UINT32(&(hashd->sohash_regularkey_count));

    return(0);
}

static int
sohash_initialize_bucket(
    sohash_table_t      *hashd,
    SOHASH_HASHINDEX_T  hashindex,
    sohash_hazard_ref_t *myhp,
    sohash_entry_t ***new_bucket
)
{
    int error;
    SOHASH_BOOL_T insert_success = SOHASH_FALSE;
    SOHASH_HASHINDEX_T parent_hashindex;
    sohash_entry_t *sentinel_node;
    sohash_entry_t **bucket = NULL;
    sohash_entry_t **parent_bucket = NULL;

    ASSERT(hashd != NULL);

    if (MDKI_ATOMIC_READ_UINT32(&hashd->hash_state) == SOHASH_STATE_FLUSH) {
        /* Hash is getting flushed.  There is no point in initializing 
         * buckets at this point in time.  Log the condition and error
         * out.  
         */
        if (new_bucket) {
            *new_bucket = NULL;
        }
        MDB_XLOG((MDB_SOHASH, "sohash initialize bucket: hash is being "
                 "flushed.  hashd 0x%x, hashindex 0x%x\n", hashd, hashindex));
        return(SOHASH_RETVAL_FAILURE);
    }

    bucket = sohash_get_bucket(hashd, hashindex);
    if (bucket != SOHASH_UNINITIALIZED) {
        /* The bucket is already initialized.  Nothing to do.  Either some
         * other thread raced us to initialize it or the caller did not check
         * if the bucket was already initialized.  However, this is not an
         * error in the context of the algorithm.  Return success.
         */
        if (new_bucket) {
            *new_bucket = bucket;
        }
        MDB_XLOG((MDB_SOHASH, "sohash initialize bucket: bucket already "
                 "initialized.  hashd 0x%x, hashindex 0x%x, bucket 0x%x\n", 
                 hashd, hashindex, bucket));
        return(SOHASH_RETVAL_SUCCESS);
    }

    if ((hashindex == 0) && (bucket == SOHASH_UNINITIALIZED)) {
        /* The bucket 0 is uninitialized.  This can't be possible as 
         * it should have been setup when the hash table was created
         * See sohash_init_hashtable().
         */
        MDKI_PANIC("sohash initialize bucket: bucket 0 is uninitialized");
    }

    parent_hashindex = SOHASH_GETPARENT_BUCKET(hashindex);

    parent_bucket = sohash_get_bucket(hashd, parent_hashindex);
    if ((parent_bucket == SOHASH_UNINITIALIZED) &&
        (sohash_initialize_bucket(hashd, 
                                  parent_hashindex, 
                                  myhp, 
                                  &parent_bucket) == SOHASH_RETVAL_FAILURE))
    {
        MDB_XLOG((MDB_SOHASH, "sohash initialize bucket: Failed to initialize "
                 "parent bucket.  hashd 0x%x, parent hashindex 0x%x, hashindex "
                 "0x%x\n", hashd, parent_hashindex, hashindex));
        return(SOHASH_RETVAL_FAILURE);
    }

    if ((sentinel_node = sohash_alloc_node()) == NULL) {
        MDB_XLOG((MDB_SOHASH, "sohash initialize bucket: Failed to allocate "
                 "memory for sentinel node.  hashd 0x%x, hashindex 0x%x\n", 
                 hashd, hashindex));
        return(SOHASH_RETVAL_FAILURE);
    }

    sentinel_node->reverse_so_key = sohash_sentinelkey(hashindex);
    sentinel_node->so_key = hashindex;
    sentinel_node->entry_type = SOHASH_SENTINEL_NODE;
    sentinel_node->hashindex = hashindex;
    /* Sentinel node is a dummy node inserted in the linked list to denote
     * the start of a bucket.  There is no "data" in the sentinel node.  
     */
    sentinel_node->data = NULL;

    error = sohash_list_insert(hashd, parent_bucket, sentinel_node, 
                               myhp, NULL, NULL);
    if (error != 0) {
        if (error == EEXIST) {
            /* Some other thread beat us to it.  The sentinel node is 
             * already initialized.  This is not an error.  Record the event
             * for later use.  
             */
            insert_success = SOHASH_FALSE;

            sohash_free_node(hashd, sentinel_node);
            /* The sohash_list_find() which was called by sohash_list_insert() 
             * found the sentinel node to be already initialized, cur pointer in 
             * sohash_hazard_ref_t would be pointing to it.
             */
            sentinel_node = myhp->cur;

            MDB_XLOG((MDB_SOHASH, "sohash initialize bucket: sentinel node for "
                     "the bucket already initialized. hashd 0x%x, hashindex "
                     "0x%x, parent hashindex 0x%x, parent bucket 0x%x, "
                     "sentinel node found 0x%x, error %u\n", hashd, hashindex, 
                     parent_hashindex, parent_bucket, sentinel_node, error));
        } else {
            MDB_XLOG((MDB_SOHASH, "sohash initialize bucket: failed to insert "
                     "sentinel node in linked list.  hashd 0x%x, hashindex "
                     "0x%x, parent hashindex 0x%x, parent bucket 0x%x, "
                     "sentinel node  0x%x, error %u\n", hashd, hashindex, 
                     parent_hashindex, parent_bucket, sentinel_node, error));

            return(SOHASH_RETVAL_FAILURE); 
        }
    } else {
        /* Insert was successful.  */
        insert_success = SOHASH_TRUE;
    }
    
    error = sohash_set_bucket(hashd, 
                              hashindex, 
                              sentinel_node, 
                              &bucket,
                              insert_success);

    if (error != 0) {
        MDB_XLOG((MDB_SOHASH, "sohash initialize bucket: set bucket failed.  "
                 "hashd 0x%x, hashindex 0x%x, parent hashindex 0x%x, sentinel "
                 "node 0x%x error %u\n", hashd, hashindex, parent_hashindex, 
                 sentinel_node, error));

        return(SOHASH_RETVAL_FAILURE);
    }
 
    if (error == 0) {
        if (new_bucket) {
            *new_bucket = bucket;
        }

        MDB_XLOG((MDB_SOHASH, "sohash initialize bucket: success. hashd 0x%x, "
                 "hashindex 0x%x, sentinel node 0x%x error %u\n", hashd,
                 hashindex, sentinel_node, error));
        return(SOHASH_RETVAL_SUCCESS);
    }
    return(SOHASH_RETVAL_FAILURE);
}

/* Routines for allocating and de-allocating sohash_hazard_ref_t.  */

/* This routine has to be called to allocate for sohash_hazard_ref
 * before any operation, for example, insert, delete or find
 * on the split ordered hash.  The routine tries to get a free hp 
 * structure from the hp_head list.  If no free structures are found
 * then a new one is allocated.  
 */
static int
sohash_hp_get(
    sohash_table_t *hashd,
    sohash_hazard_ref_t **myhp
)
{
    sohash_hazard_ref_t *oldhead;
    sohash_hazard_ref_t *hp;
    SOHASH_HPCOUNT_INTEGRAL_T oldcount = 0;
    int error = 0;

    ASSERT(hashd != NULL);

    for (hp = MDKI_ATOMIC_PTR_READ(&hashd->hp_head); 
         hp != NULL; 
         hp = hp->hp_next)
    {
        if (MDKI_ATOMIC_READ_UINT32(&hp->hp_state) == SOHASH_HP_INUSE) continue;

        if (!MDKI_ATOMIC_CAS_UINT32(&hp->hp_state, 
                                    SOHASH_HP_FREE, 
                                    SOHASH_HP_INUSE)) 
        {
            continue;
        }

        /* Succeeded in getting a hp structure, return it.  */
        *myhp = hp;
        return(error);
    }

    /* No free hp in the list, allocate a new one.  */
    hp = (sohash_hazard_ref_t *) KMEM_ALLOC(
                            sizeof(sohash_hazard_ref_t), KM_SLEEP);
    if (hp == NULL) {
        *myhp = NULL;
        error = ENOMEM;
        return(error);
    }

    BZERO(hp, sizeof(sohash_hazard_ref_t));

    /* Initialize the fields of the new hp structure.  */
    hp->hprec.rlist = NULL;
    hp->hprec.rcount = 0;
    MDKI_ATOMIC_SET_UINT32(&hp->hp_state, SOHASH_HP_INUSE);

    hp->hp0 = &(hp->hprec.hazard_ptrs[0]);
    hp->hp1 = &(hp->hprec.hazard_ptrs[1]);
    hp->hp2 = &(hp->hprec.hazard_ptrs[2]);

    /* Insert the newly allocated hazard reference structure as the hp_head 
     * in the linked list.  
     */
    do {
        oldhead = MDKI_ATOMIC_PTR_READ(&hashd->hp_head);
        hp->hp_next = oldhead;
    } while (!MDKI_ATOMIC_CAS_PTR(&(hashd->hp_head), oldhead, hp));

    do {
        oldcount = MDKI_ATOMIC_READ_UINT32(&hashd->hazard_ptrs_total);
    } while (!MDKI_ATOMIC_CAS_UINT32(&(hashd->hazard_ptrs_total), 
                                oldcount, oldcount + 3));

    MDB_XLOG((MDB_SOHASH, 
            "sohash hp init: hp allocated. hashd 0x%x, hp 0x%x, "
            "hazard ptr total: old count %u, new count %u\n",
            hashd, hp, oldcount, 
            MDKI_ATOMIC_READ_UINT32(&hashd->hazard_ptrs_total)));

    *myhp = hp;

    return (error);
}

static void
sohash_hp_release(sohash_hazard_ref_t *myhp)
{
    int i;

    if (myhp == NULL) {
        return;
    }

    for (i = 0; i < 3; i++) {
        myhp->hprec.hazard_ptrs[i] = NULL;
    }

    MDKI_ATOMIC_SET_UINT32(&myhp->hp_state, SOHASH_HP_FREE);
}

static int
sohash_flush_hp_list(sohash_table_t *hashd)
{
    sohash_hazard_ref_t *hp;
    sohash_entry_t *node = NULL;

    ASSERT(hashd != NULL);

    hp = MDKI_ATOMIC_PTR_READ(&hashd->hp_head);
    while (hp != NULL) {
        ASSERT(MDKI_ATOMIC_READ_UINT32(&hp->hp_state) == SOHASH_HP_FREE);

        MDKI_ATOMIC_PTR_SET(&hashd->hp_head, hp->hp_next);

        node = hp->hprec.rlist;
        while (node != NULL) {
            hp->hprec.rlist = MDKI_ATOMIC_PTR_READ(&node->hashentry_next);
            hp->hprec.rcount--;
            sohash_free_node(hashd, node);
            node = hp->hprec.rlist;
        }

        if (hp->hprec.rcount != 0) {
            /* rcount is inconsistent with the number of entries in the
             * rlist.  Should this be treated as an error or considering that
             * the rlist is empty should this be just a warning??
             */
            MDKI_PANIC("sohash_flush_hp_list: rcount not zero");
        }

        /* Free the hp structure*/
        KMEM_FREE(hp, sizeof(sohash_hazard_ref_t));
        hp = MDKI_ATOMIC_PTR_READ(&hashd->hp_head);
    }

    return(SOHASH_RETVAL_SUCCESS);
}

/* ROUTINES FOR OPERATING ON THE LOCK FREE SINGLY LINKED LIST.  */

/* Routine to insert a node in the singly linked list in lock free way.  
 * This routine returns 0 if the key was successfully inserted into
 * the hash and EEXIST if that key already existed in the hash set.  If
 * the key already exists then the pointer to that entry is returned thro'
 * found_node if it is passed.
 */
static int
sohash_list_insert(
    sohash_table_t      *hashd,
    sohash_entry_t      **head,
    sohash_entry_t      *node,
    sohash_hazard_ref_t *myhp,
    void                *arg_verify_callback,
    sohash_entry_t      **found_node
)
{
    int error;
    sohash_entry_t *existing_node = NULL;

    ASSERT((hashd != NULL) && (head != NULL));

    while (1) {
        /* Bubble up the node that was found if found_node is set.  */
        existing_node = sohash_list_find(hashd, 
                                         head, 
                                         node->reverse_so_key, 
                                         node->entry_type,
                                         arg_verify_callback,
                                         myhp);

        if (existing_node != NULL) {
           error = EEXIST;

           if (found_node) {
               *found_node = existing_node;
           }
           break;
        }

        MDKI_ATOMIC_PTR_SET(&node->hashentry_next, myhp->cur);

        if (MDKI_ATOMIC_CAS_PTR(myhp->prev, myhp->cur, node)) {
            error = 0;
            MDB_XLOG((MDB_SOHASH, 
                     "sohash list insert: insert successful. *(myhp->prev) 0x%x," 
                     "myhp->cur 0x%x, myhp->next 0x%x, node 0x%x, sokey 0x%x, "
                     "reverse sokey 0x%x, error %u\n",
                     *(MDKI_ATOMIC_PTR_READ(&myhp->prev)), myhp->cur, 
                     myhp->next, node, node->so_key,
                     node->reverse_so_key, error));
            break;
        }
    }

    /* Resetting all hazard pointers to NULL (part of solution to ABA
     * problem). 
     */
    *(myhp->hp0) = NULL;
    *(myhp->hp1) = NULL;
    *(myhp->hp2) = NULL;

    return(error);
}

/* Routine to search for a key in the singly linked list.  */
static sohash_entry_t *
sohash_list_find(
    sohash_table_t       *hashd,
    sohash_entry_t       **head,
    SOHASH_REVERSE_KEY_T sohash_reverse_key,
    SOHASH_BOOL_T        entry_type,
    void                 *arg_verify_callback,
    sohash_hazard_ref_t  *myhp
)
{
    SOHASH_REVERSE_KEY_T cur_reverse_key;

    ASSERT((hashd != NULL) && (head != NULL));

try_again:
    MDKI_ATOMIC_PTR_SET(&myhp->prev, head);

    myhp->cur = *(MDKI_ATOMIC_PTR_READ(&myhp->prev));

    *(myhp->hp1) = myhp->cur;
    if (*(MDKI_ATOMIC_PTR_READ(&myhp->prev)) != myhp->cur) {
        goto try_again;
    }

    while (1) {
        if (myhp->cur == NULL) {
            return(NULL);
        }
        myhp->next = MDKI_ATOMIC_PTR_READ(&myhp->cur->hashentry_next);

        *(myhp->hp0) = myhp->next;
        if (MDKI_ATOMIC_PTR_READ(&myhp->cur->hashentry_next) != myhp->next) {
            goto try_again;
        }

        cur_reverse_key = myhp->cur->reverse_so_key;

        if (*(MDKI_ATOMIC_PTR_READ(&myhp->prev)) != myhp->cur) goto try_again;

        if (SOHASH_IS_DELETE_BIT_SET(myhp->next)) {
            goto try_again;
        } else {
            if (cur_reverse_key == sohash_reverse_key) {
                /* If sohash_keys_unique is set to SOKEY_UNIQUE_FALSE then
                 * the user has specified that the sohash key for regular
                 * nodes that has been generated is not guaranteed to be 
                 * unique (user generates sohash key for only regular nodes,
                 * sohash keys for sentinel nodes are always unique).  Hence 
                 * the sohash implementation has to deal with the possibility 
                 * of duplicate keys in the hash.  In which case the function 
                 * call back specified by the caller would be invoked to let 
                 * the caller verify if the sohash_entry_t that is found here 
                 * is the one that is expected.  arg_verify_callback will be 
                 * passed as argument to the call back routine.  If the entry
                 * is found to be the expected one the call back routine
                 * should return SOHASH_TRUE and that entry would be returned
                 * to the caller of sohash_list_find().  If the call back
                 * routine returns SOHASH_FALSE, search in the list is
                 * continued.  
                 */
                if ((entry_type == SOHASH_SENTINEL_NODE) || 
                    ((entry_type == SOHASH_REGULAR_NODE) &&
                     (hashd->sohash_keys_unique == SOKEY_UNIQUE_TRUE))) 
                {
                    MDB_XLOG((MDB_SOHASH, 
                             "sohash list find: Found reverse so key 0x%x, "
                             "hashd 0x%x, bucket head 0x%x\n",
                             sohash_reverse_key, hashd, head));
                    return(myhp->cur);
                } 
                else if ((entry_type == SOHASH_REGULAR_NODE) &&
                         (hashd->sohash_keys_unique == SOKEY_UNIQUE_FALSE)) 
                {
                    ASSERT(hashd->fn_verify_matched_entry != NULL);
                    if ((*hashd->fn_verify_matched_entry)(myhp->cur, 
                                 arg_verify_callback) == SOHASH_TRUE) 
                    {
                        /* Entry matched.  */
                        return(myhp->cur);
                    }
                }
            } else if (cur_reverse_key > sohash_reverse_key) {
                MDB_XLOG((MDB_SOHASH, 
                         "sohash list find: not found. cur_reverse_key 0x%x > "
                         "sohash_reverse_key 0x%x. hashd 0x%x, bucket head "
                         "0x%x\n",
                         cur_reverse_key, sohash_reverse_key, hashd, head));
                return(NULL);
            }
            MDKI_ATOMIC_PTR_SET(&myhp->prev, &myhp->cur->hashentry_next);
            *(myhp->hp2) = myhp->cur;
            myhp->cur = myhp->next;
        }
        *(myhp->hp1) = myhp->next;
    }
}

static int
sohash_list_delete(
    sohash_table_t       *hashd,
    sohash_entry_t       **head,
    SOHASH_REVERSE_KEY_T sohash_reverse_key,
    SOHASH_BOOL_T        entry_type,
    void                 *arg_verify_callback,
    sohash_hazard_ref_t  *myhp
)
{
    int error;

    ASSERT(hashd != NULL);

    if (head == NULL) return(EINVAL);

    while (1) {
        if (sohash_list_find(hashd, head, sohash_reverse_key, entry_type,
                             arg_verify_callback, myhp) == NULL) {
            error = ENOENT;

            MDB_XLOG((MDB_SOHASH, 
                      "sohash_list_delete: find returned ENOENT.\n"));
            break;
        }

        if (MDKI_ATOMIC_CAS_PTR(&(myhp->cur->hashentry_next), 
                           myhp->next, 
                           SOHASH_SET_DELETE_BIT(myhp->next)))
        {
            ASSERT(MDKI_ATOMIC_PTR_READ(&myhp->cur->hashentry_next) == 
                                   SOHASH_SET_DELETE_BIT(myhp->next));
            if (MDKI_ATOMIC_CAS_PTR(myhp->prev, myhp->cur, myhp->next)) {
                sohash_retire_node(hashd, myhp->cur, myhp);
            } else {
                /* CAS could fail either because some other thread raced to
                 * delete the same node or the prev node is also marked for
                 * deletion (i.e delete bit is set on hashentry_next of previous
                 * node) or a node has been inserted between the previous node
                 * and current.  Unset the delete bit before retrying.  
                 */
                if (MDKI_ATOMIC_CAS_PTR(&(myhp->cur->hashentry_next),
                                   SOHASH_SET_DELETE_BIT(myhp->next),
                                   myhp->next)) 
                {
                    continue;
                } 
                else {
                    MDKI_PANIC("sohash_list_delete: Unsetting delete bit failed");
                }
            }

            error = 0;
            break;
        }
    }

    /* Resetting all hazard pointers to NULL.  */
    *(myhp->hp0) = NULL;
    *(myhp->hp1) = NULL;
    *(myhp->hp2) = NULL;

    return(error);
}

static int
sohash_cleanup_bucket(
    sohash_table_t *hashd,
    SOHASH_INDEX_T segment_index,
    SOHASH_INDEX_T bucket_index,
    sohash_hazard_ref_t *myhp
)
{
    sohash_entry_t **bucket;
    sohash_entry_t *bucket_head;
    sohash_entry_t *node;

    ASSERT((hashd != NULL) && 
           (MDKI_ATOMIC_PTR_READ(&hashd->hashtable[segment_index]) !=
           SOHASH_UNINITIALIZED));

    bucket_head = MDKI_ATOMIC_PTR_READ(
                  &hashd->hashtable[segment_index][bucket_index]);
    bucket = 
        &(MDKI_ATOMIC_PTR_READ(&hashd->hashtable)[segment_index][bucket_index]);
    if (bucket_head != SOHASH_UNINITIALIZED) {
        node = MDKI_ATOMIC_PTR_READ(&bucket_head->hashentry_next);
        while ((node != NULL) && (node->entry_type == SOHASH_REGULAR_NODE)) {
            if (sohash_delete_entry_subr(hashd,
                                         node->reverse_so_key,
                                         myhp, bucket, 
                                         SOHASH_REGULAR_NODE, NULL) != 0) 
            {
                return(SOHASH_RETVAL_FAILURE);
            } else {
                node = MDKI_ATOMIC_PTR_READ(&bucket_head->hashentry_next);
            }
        }
    }
    return(SOHASH_RETVAL_SUCCESS);
}

/* Routines for Safe Memory Reclamation(SMR) and solution for ABA problem.  */
static void
sohash_retire_node(
    sohash_table_t      *hashd,
    sohash_entry_t      *node,
    sohash_hazard_ref_t *myhp
)
{
    sohash_hazard_ref_t *hp_head;

    ASSERT((hashd != NULL) && (node != NULL));

    if ((node->entry_type == SOHASH_SENTINEL_NODE) && 
        (MDKI_ATOMIC_READ_UINT32(&hashd->hash_state) == SOHASH_STATE_ACTIVE)) 
    {
        MDKI_PANIC("sohash retire node: Sentinel node delete when hash active");
    }

    /* Add the node to the head of the rlist.  */
    MDKI_ATOMIC_PTR_SET(&node->hashentry_next, myhp->hprec.rlist);
    myhp->hprec.rlist = node;

    myhp->hprec.rcount++;

    hp_head = MDKI_ATOMIC_PTR_READ(&hashd->hp_head);

    /* The node has been unchained from the hash.  If this is a regular node
     * and if the on delete function call back (i.e. (*fn_on_delete)()) is set,
     * invoke it here, so that the user can perform any cleanup/action after 
     * the node has been removed from the hash but before it is freed.  
     */
    if ((node->entry_type == SOHASH_REGULAR_NODE) && 
        (hashd->fn_on_delete != NULL)) 
    {
        (*hashd->fn_on_delete)(node);
    }

    /* For achieving a constant expected amortized processing time 
     * per retired node, rcount is advised to be such that,
     * rcount >= (1 + k)*hazard_ptrs_total, where k is a small
     * positive constant, say 1/4.
     */
    /* The same hp structure gets reused several times, thereby hprec 
     * in it gets reused several times.  
     * Consider a case where there were 8 hazard reference structures
     * allocated, then,
     *    hazard_ptrs_total = 3 * 8 = 24, which means the
     * rlist in the hprec of these hp structures  should have atleast 30 (i.e.
     * ((1 + 1/4) * hazard_ptrs_total) ==> (5 * hazard_ptrs_total) / 4)
     * sohash_entry_t structures for the scan to be invoked on it.
     * Even if there are 100 sohash_entry_t structures but distributed
     * among the rlists of different hprec, but none of them have 30 or
     * more, then scan would not be called.
     */
    if ((MDKI_ATOMIC_READ_UINT32(&hashd->hash_state) != SOHASH_STATE_FLUSH) &&
        (myhp->hprec.rcount >= 
                (1 * MDKI_ATOMIC_READ_UINT32(&hashd->hazard_ptrs_total)))) 
    {
        sohash_hprec_scan(hashd, hp_head, myhp);
       /* sohash_hprec_helpscan(hashd, myhp); */
    }
}

static void
sohash_hprec_scan(
    sohash_table_t      *hashd,
    sohash_hazard_ref_t *hp_head,
    sohash_hazard_ref_t *myhp
)
{
   /* plist is a private array containing reverse sohash keys from non-null 
    * hazard pointers.  These will be matched with the reverse sohash keys 
    * of nodes on myhp->hprec.rlist.  
    */
    SOHASH_REVERSE_KEY_T *plist = NULL;

    /* tmplist is the temporary linked list to be used for comparing
     * myhp->hprec.rlist and plist.  
     */
    sohash_entry_t *tmplist = NULL;

    sohash_hazard_ref_t *hp;
    sohash_entry_t *hptr, *sohash_entry;
    int i, len; 
    SOHASH_INDEX_T plist_idx; 
    SOHASH_HPCOUNT_INTEGRAL_T hptr_total;

    ASSERT(hashd != NULL);

    /* stage 1: 
     *   - Allocate memory for the plist array.  
     *   - Scan the hprec and from the non-null hazard pointers copy the 
     *     reverse sohash key to the plist array.  
     *   - Sort the array in ascending order.  
     */
    hp = hp_head;

    /* Allocate the memory for plist array.  */
    hptr_total = MDKI_ATOMIC_READ_UINT32(&hashd->hazard_ptrs_total);
    len = hptr_total * sizeof(SOHASH_REVERSE_KEY_T);
    plist = (SOHASH_REVERSE_KEY_T *)KMEM_ALLOC(len, KM_SLEEP);
    if (plist == NULL) {
        MDKI_PANIC("sohash hprec scan: Failed to allocate memory for plist");
    } else {
        BZERO(plist, len);
    }

    /* Scan the hprec structure in every hp and copy the reverse sohash keys
     * from the hazard_ptrs array to plist array.  
     */
    plist_idx = 0;
    while (hp != NULL) {
        for (i = 0; i < 3; i++) {
            hptr = hp->hprec.hazard_ptrs[i];
            if (hptr != NULL) {
                /* It could be possible that the hazard reference has the delete
                 * bit set.  Use the address with its delete bit unset for the 
                 * rest of the computation.  SOHASH_UNSET_DELETE_BIT will unset
                 * the bit if it is set, if not, there will be no change.
                 */
                hptr = SOHASH_UNSET_DELETE_BIT(hptr);
                plist[plist_idx] = hptr->reverse_so_key; 
                plist_idx++;
            }
        }
        hp = hp->hp_next;
    }

    /* Sort the plist array.  */
    sohash_sort_arr(plist, hptr_total);

    /* stage 2: Search plist. */

    /* Move the entire rlist to a temporary list (tmplist) for parsing.  */
    tmplist =  myhp->hprec.rlist;
    myhp->hprec.rcount = 0;
    myhp->hprec.rlist = NULL;

    /* start parsing the nodes in tmplist.  */
    sohash_entry = tmplist;

    while (sohash_entry != NULL) {
        /* Save the reference for rest of the list. */
        tmplist = MDKI_ATOMIC_PTR_READ(&sohash_entry->hashentry_next);

        /* Search for node in the plist created above.  If there is a
         * match then the corresponding node cannot be deallocated and
         * and hence is pushed back to the rlist.  If it is not found
         * then it can be safely removed.  
         */
        if (sohash_search_arr(plist, 
                              sohash_entry->reverse_so_key, 
                              0, 
                              (int)(hptr_total - 1)) == SOHASH_FOUND) 
        {
            MDKI_ATOMIC_PTR_SET(&sohash_entry->hashentry_next, 
                                myhp->hprec.rlist);
            myhp->hprec.rlist = sohash_entry;
            myhp->hprec.rcount++;
        } 
        else {
            sohash_free_node(hashd, sohash_entry);
        }
        sohash_entry = tmplist;
    }

    /* Free the memory allocated for plist.  */
    KMEM_FREE(plist, len);
}

static void
sohash_hprec_helpscan(
    sohash_table_t      *hashd,
    sohash_hazard_ref_t *myhp
)
{
    sohash_hazard_ref_t *hp_head;
    sohash_hazard_ref_t *hp;
    sohash_entry_t *sohash_entry;
    int i;

    ASSERT(hashd != NULL);

    for (hp = MDKI_ATOMIC_PTR_READ(&hashd->hp_head); 
         hp != NULL;
         hp = hp->hp_next) 
    {
        if (MDKI_ATOMIC_READ_UINT32(&hp->hp_state) == SOHASH_HP_INUSE) continue;

        if (!MDKI_ATOMIC_CAS_UINT32(&hp->hp_state, 
                                    SOHASH_HP_FREE, 
                                    SOHASH_HP_INUSE)) 
        {
            continue;
        }

        if (hp->hprec.rcount > 0) {
            sohash_entry = hp->hprec.rlist;
            for (i = 0; i < hp->hprec.rcount - 1; i++) {
                sohash_entry = sohash_entry->hashentry_next;
            }

            /* Merge the entire rlist of the hprec to the head of the rlist
             * of hprec in myhp and increment the rcount in accordingly.  
             */
            sohash_entry->hashentry_next = myhp->hprec.rlist;
            myhp->hprec.rlist = hp->hprec.rlist;
            myhp->hprec.rcount += hp->hprec.rcount;
            hp->hprec.rlist = NULL;
            hp->hprec.rcount = 0;
            hp_head = hashd->hp_head;
            if ((MDKI_ATOMIC_READ_UINT32(&hashd->hash_state) != 
                    SOHASH_STATE_FLUSH) && 
                (myhp->hprec.rcount >= 
                    (2 * MDKI_ATOMIC_READ_UINT32(&hashd->hazard_ptrs_total)))) 
            {
                sohash_hprec_scan(hashd, hp_head, myhp);
            }
        }

        MDKI_ATOMIC_SET_UINT32(&hp->hp_state, SOHASH_HP_FREE);
    }
}

/* This routine implements insertion sort for sorting the array of reverse
 * sohash keys in ascending order.  
 */
static void
sohash_sort_arr(
    SOHASH_REVERSE_KEY_T      *arr,
    SOHASH_HPCOUNT_INTEGRAL_T count
)
{
    int i, j;
    SOHASH_REVERSE_KEY_T temp;

    for (i = 1; i < count; i++) {
        temp = arr[i];
        j = i - 1;
        while ((temp < arr[j]) && (j >= 0)) {
            arr[j + 1] = arr[j];
            j = j - 1;
        }
        arr[j + 1] = temp;
    }
}

/* This routine implements the search of a given element (rev_sokey) in the
 * array using binary search.  If the reverse sokey is found in the array then
 * SOHASH_FOUND is returned, if not, SOHASH_NOT_FOUND is the return value.  
 */
int
sohash_search_arr(
    SOHASH_REVERSE_KEY_T *arr, 
    SOHASH_REVERSE_KEY_T rev_sokey, 
    int                  low,
    int                  high
)
{
    int mid = 0;

    if (high < low) return SOHASH_NOT_FOUND;
    mid = low + (high - low)/2;
    if (arr[mid] < rev_sokey) {
        return(sohash_search_arr(arr, rev_sokey, mid+1, high));
    } else if (arr[mid] > rev_sokey) {
        return(sohash_search_arr(arr, rev_sokey, low, mid-1));
    } else {
        return SOHASH_FOUND;
    }
}

/* sohash_entry_t allocation and de-allocation routines.  */
static sohash_entry_t *
sohash_alloc_node(void)
{
    sohash_entry_t *node = NULL;
    int error = 0;

    node = (sohash_entry_t *)KMEM_ALLOC(sizeof(sohash_entry_t), KM_SLEEP);
    if (node == NULL) {
        /* Print error log */
        error = ENOMEM;
        return(NULL);
    }

    BZERO(node, sizeof(*node));

    return(node);
}

static void
sohash_free_node(
    sohash_table_t *hashd,
    sohash_entry_t *node
)
{
    if (node->data != NULL) {
        /* Sentinel node cotains no data. If there is a valid data
         * pointer it must be a regular node.
         */
        sohash_free_data(hashd, node->data);
    }

    BZERO(node, sizeof(*node));

    /* Free sohash_entr_t.  */
    KMEM_FREE(node, sizeof(sohash_entry_t));
}

/* Routine to free the memory allocated for the user supplied data
 * that is hashed or passed to us for the purpose of hashing.  
 */
static void
sohash_free_data(
    sohash_table_t *hashd,
    void *datap
)
{
    if (datap != NULL) {
        ASSERT((hashd != NULL) && (hashd->fn_data_free != NULL));

        /* Call the user provided free routine to free memory allocated
         * for data.  
         */
        (*hashd->fn_data_free)(datap);
    }
}

/* Routines for converting SOHASH_KEY_T to SOHASH_REVERSE_KEY_T.  */
static inline SOHASH_REVERSE_KEY_T
sohash_regularkey(SOHASH_KEY_T key)
{
    return(sohash_reverse_bit(SOKEY_SETMSB(key)));
}

static inline SOHASH_REVERSE_KEY_T
sohash_sentinelkey(SOHASH_KEY_T key)
{
    return(sohash_reverse_bit(key));
}

/* Routines to atomically increment and decrement counters.  */

static SOHASH_KEYCOUNT_INTEGRAL_T
sohash_fetch_and_inc(SOHASH_KEYCOUNT_T *counter)
{
    SOHASH_KEYCOUNT_INTEGRAL_T old_counter;

    do {
        old_counter = MDKI_ATOMIC_READ_UINT32(counter);
    } while (!MDKI_ATOMIC_CAS_UINT32(counter, old_counter, old_counter + 1));

    return old_counter;
}

static SOHASH_KEYCOUNT_INTEGRAL_T
sohash_fetch_and_dec(SOHASH_KEYCOUNT_T *counter)
{
    SOHASH_KEYCOUNT_INTEGRAL_T old_counter;

    do {
        old_counter = MDKI_ATOMIC_READ_UINT32(counter);
    } while (!MDKI_ATOMIC_CAS_UINT32(counter, old_counter, old_counter - 1));

    return old_counter;
}

/* Routines for bit manipulation.  */
static SOHASH_HASHINDEX_T
unsetmsb(SOHASH_HASHINDEX_T input)
{
    SOHASH_HASHINDEX_T output = 0;
    SOHASH_HASHINDEX_T mask = 1;
    SOHASH_HASHINDEX_T bitnullifier = 
                       (mask << (sizeof(SOHASH_HASHINDEX_T) * 8 - 1));

    if (input == 0) {
        return(0);
    }

    while ((input & bitnullifier) == 0) {
        bitnullifier >>= 1;
    }

    output = input & (~bitnullifier);

    return(output);
}

static inline SOHASH_REVERSE_KEY_T 
sohash_reverse_bit(SOHASH_KEY_T input)
{
    SOHASH_REVERSE_KEY_T output = 0;
    int i = 0;

    for (i = 0; i < (sizeof(SOHASH_KEY_T) * 8); i++) {
        output = (output << 1) + (input & 1);
        input >>= 1;
    }

    return (output);
}

static sohash_entry_t **
sohash_get_bucket(
    sohash_table_t *hashd,
    SOHASH_HASHINDEX_T hashindex    
)
{
    SOHASH_INDEX_T segment_index;
    SOHASH_INDEX_T bucket_index;
    sohash_segment_t segment;

    ASSERT(hashd != NULL);

    segment_index = hashindex / hashd->sohash_segment_sz;

    /* If segment_index is more than the number of segments 
     * allocated for then it will lead to array out of bound access. */
    ASSERT(segment_index <= 
           MDKI_ATOMIC_READ_UINT32(&hashd->sohash_num_segments));

    if (MDKI_ATOMIC_PTR_READ(&hashd->hashtable[segment_index]) == NULL) {
        MDB_XLOG((MDB_SOHASH, "sohash get bucket: segment at index %u is"
                 "uninitialized. hashd 0x%x, hashindex 0x%x\n", segment_index,
                 hashd, hashindex));
        return(SOHASH_UNINITIALIZED);
    }

    bucket_index = hashindex % hashd->sohash_segment_sz;
   
    if (MDKI_ATOMIC_PTR_READ(&hashd->hashtable[segment_index][bucket_index]) ==
        SOHASH_UNINITIALIZED) 
    {
        MDB_XLOG((MDB_SOHASH, "sohash get bucket: bucket at index %u is "
                 "uninitialized. hashd 0x%x, hashindex 0x%x segment index %u\n", 
                 bucket_index, hashd, hashindex, segment_index));
        return(SOHASH_UNINITIALIZED);
    }

    /* Return the address of the bucket only if it is initialized.   */
    return(&(
        MDKI_ATOMIC_PTR_READ(&hashd->hashtable)[segment_index][bucket_index]));
}

static int
sohash_set_bucket(
    sohash_table_t     *hashd,
    SOHASH_HASHINDEX_T hashindex,
    sohash_entry_t     *bucket_head,
    sohash_entry_t     ***bucket,
    SOHASH_BOOL_T      insert_success
)
{
    SOHASH_INDEX_T segment_index;
    SOHASH_INDEX_T bucket_index;
    sohash_segment_t new_segment;
    sohash_segment_t segment;
    int len;

    ASSERT((hashd != NULL) && (bucket_head != NULL));

    /* Calculate the segment index. */
    segment_index = hashindex / hashd->sohash_segment_sz;
    
    /* If segment_index is more than the number of segments 
     * allocated, then it will lead to array out of bound access. 
     */
    ASSERT(segment_index <= 
           MDKI_ATOMIC_READ_UINT32(&hashd->sohash_num_segments));

    if (MDKI_ATOMIC_PTR_READ(&hashd->hashtable[segment_index]) == NULL) {
        /* Allocate for a new segment.  */
        len = sizeof(sohash_entry_t *) * hashd->sohash_segment_sz;
        new_segment = (sohash_segment_t)KMEM_ALLOC(len, KM_SLEEP);
        if (new_segment == (sohash_segment_t)NULL) {
            return(ENOMEM);
        }
         
        /* Set all the buckets to SOHASH_UNINITIALIZED.  */
        BZERO(new_segment, len);
        
        MDKI_ATOMIC_CAS_PTR(&(hashd->hashtable[segment_index]), 
                            NULL, 
                            new_segment);

        /* Even if there is a race between threads to perform the above CAS,
         * and only one of them succeeds, all the threads must see
         * hashd->hashtable[segment_index] updated now.  
         */
        ASSERT(MDKI_ATOMIC_PTR_READ(&hashd->hashtable[segment_index]) != NULL);

        if (MDKI_ATOMIC_PTR_READ(&hashd->hashtable[segment_index]) 
                != new_segment) 
        {
            /* Another thread must have beat us to segment initialization,
             * so free the memory we just allocated since it is no longer
             * needed.
             */
            MDB_XLOG((MDB_SOHASH, "sohash set bucket: atomic set of newly "
                     "allocated segment failed. hashd 0x%x segment index %u, "
                     "allocated segment 0x%x existing segment 0x%x\n", hashd, 
                     segment_index, new_segment, 
                     ATOMIC_PTR_READ(&hashd->hashtable[segment_index])));
            KMEM_FREE(new_segment, len);
        }
    }

    bucket_index = hashindex % hashd->sohash_segment_sz;

    /* Compare and swap failing here is not an error.  If multiple threads are
     * racing to insert the same sentinel node in the linked list, 
     * sohash_list_insert() ensures that only one of them succeeds and the
     * other thread will get a return value of EEXIST.  However, both the 
     * threads will now be trying to initialize the bucket to the same
     * sentinel node that was successfully inserted and hence the compare 
     * and swap here is just to keep the assignment clean.  Hence, post CAS 
     * initializations will be done only by the thread which 
     * was successful (denoted by caller setting the flag "insert_success" to 
     * SOHASH_TRUE) in inserting the sentinel node to the list, so as to keep 
     * it clean.  
     */
    MDKI_ATOMIC_CAS_PTR(&(hashd->hashtable[segment_index][bucket_index]), 
                   SOHASH_UNINITIALIZED, 
                   bucket_head);

    ASSERT(MDKI_ATOMIC_PTR_READ(&hashd->hashtable[segment_index][bucket_index])
            != SOHASH_UNINITIALIZED);

    if (insert_success == SOHASH_TRUE) {
        bucket_head->segment_index = segment_index;
        bucket_head->bucket_index = bucket_index;

        /* Increment the sentinel key count.  */
        MDKI_ATOMIC_INCR_UINT32(&(hashd->sohash_sentinelkey_count));

        MDB_XLOG((MDB_SOHASH, 
                 "sohash set bucket: set the bucket at segment "
                 "index %u and bucket index %u to 0x%x. hashd 0x%x\n",
                 segment_index, bucket_index,
                 MDKI_ATOMIC_PTR_READ(
                 &hashd->hashtable[segment_index][bucket_index]), hashd)); 
    }

    if (bucket) {
        *bucket = &(MDKI_ATOMIC_PTR_READ(
                    &hashd->hashtable)[segment_index][bucket_index]);
    }

    MDB_XLOG((MDB_SOHASH, "sohash set bucket: Initialized bucket, hashd 0x%x "
             "segment index %u, bucket index %u, bucket head 0x%x\n", 
             hashd, segment_index, bucket_index,
             MDKI_ATOMIC_PTR_READ(
                 &hashd->hashtable[segment_index][bucket_index])));

    return(0);
}

static const char vnode_verid_mvfs_sohash_table_c[] = "$Id:  922a776b.ec6711e1.906d.00:01:84:c3:8a:52 $";
