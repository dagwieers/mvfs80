/* * (C) Copyright IBM Corporation 2011. */
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
#ifndef MVFS_SOHASH_TABLE_H_
#define MVFS_SOHASH_TABLE_H_

#define SOHASH_ATOMIC_INTEGRAL_T int
#define SOHASH_SEG_COUNT_T atomic_t
#define SOHASH_HP_STATE_T atomic_t
#define SOHASH_HPCOUNT_T atomic_t
#define SOHASH_KEYCOUNT_T atomic_t
#define SOHASH_SZ_T atomic_t
#define SOHASH_HASH_STATE_T atomic_t
#define SOHASH_REVERSE_KEY_T uint32_t
#define SOHASH_KEY_T uint32_t
#define SOHASH_SEG_SZ_T int
#define SOHASH_BOOL_T uint32_t
#define SOHASH_HASHINDEX_T uint32_t
#define SOHASH_INDEX_T uint32_t
#define SOHASH_RCOUNT_T uint32_t
#define SOHASH_SEG_COUNT_INTEGRAL_T SOHASH_ATOMIC_INTEGRAL_T
#define SOHASH_HASH_STATE_INTEGRAL_T SOHASH_ATOMIC_INTEGRAL_T
#define SOHASH_SZ_INTEGRAL_T SOHASH_ATOMIC_INTEGRAL_T
#define SOHASH_HPCOUNT_INTEGRAL_T SOHASH_ATOMIC_INTEGRAL_T
#define SOHASH_KEYCOUNT_INTEGRAL_T SOHASH_ATOMIC_INTEGRAL_T

#if !defined(SOHASH_REVERSE_KEY_T)
typedef uint32_t SOHASH_REVERSE_KEY_T;
#endif
#if !defined(SOHASH_KEY_T)
typedef uint32_t SOHASH_KEY_T;
#endif
#if !defined(SOHASH_SEG_COUNT_T)
typedef int SOHASH_SEG_COUNT_T;
#endif
#if !defined(SOHASH_SEG_SZ_T)
typedef int SOHASH_SEG_SZ_T;
#endif
#if !defined(SOHASH_BOOL_T)
typedef uint32_t SOHASH_BOOL_T;
#endif
#if !defined(SOHASH_HP_STATE_T)
typedef uint32_t SOHASH_HP_STATE_T;
#endif
#if !defined(SOHASH_HASHINDEX_T)
typedef uint32_t SOHASH_HASHINDEX_T;
#endif
#if !defined(SOHASH_INDEX_T)
typedef uint32_t SOHASH_INDEX_T;
#endif
#if !defined(SOHASH_HPCOUNT_T)
typedef uint32_t SOHASH_HPCOUNT_T;
#endif
#if !defined(SOHASH_SZ_T)
typedef uint32_t SOHASH_SZ_T;
#endif
#if !defined(SOHASH_KEYCOUNT_T)
typedef uint32_t SOHASH_KEYCOUNT_T;
#endif
#if !defined(SOHASH_RCOUNT_T)
typedef uint32_t SOHASH_RCOUNT_T;
#endif
#if !defined(SOHASH_HASH_STATE_T)
typedef uint32_t SOHASH_HASH_STATE_T;
#endif
#if !defined(SOHASH_SEG_COUNT_INTEGRAL_T)
typedef SOHASH_SEG_COUNT_T SOHASH_SEG_COUNT_INTEGRAL_T;
#endif
#if !defined(SOHASH_HASH_STATE_INTEGRAL_T)
typedef SOHASH_HASH_STATE_T SOHASH_HASH_STATE_INTEGRAL_T;
#endif
#if !defined(SOHASH_SZ_INTEGRAL_T)
typedef SOHASH_SZ_T SOHASH_SZ_INTEGRAL_T;
#endif
#if !defined(SOHASH_HPCOUNT_INTEGRAL_T)
typedef SOHASH_HPCOUNT_T SOHASH_HPCOUNT_INTEGRAL_T;
#endif
#if !defined(SOHASH_KEYCOUNT_INTEGRAL_T)
typedef SOHASH_KEYCOUNT_T SOHASH_KEYCOUNT_INTEGRAL_T;
#endif

/* This is the definition for nodes on the singly linked list.  "data" is
 * the value stored in the hash corresponding to the split ordered hash key.
 * The nodes in the linked list are ordered based on reverse split ordered
 * key.  Hash index is computed as (so_key % hashd->sohash_current_size).
 * The key-value pairs stored in the hash are referred to as regular nodes,
 * denoted by entry_type set to SOHASH_REGULAR_NODE.  The linked list has
 * nodes representing the start of a bucket.  These nodes are called 
 * sentinel nodes and is denoted by entry_type set to SOHASH_SENTINEL_NODE.
 * Note that the sentinel nodes will have the corresponding data field set
 * to NULL.
 */
typedef struct sohash_entry {
    struct sohash_entry  *hashentry_next;/* Next entry on the singly linked 
                                            list */
    void                 *data;          /* Value corresponding to so_key */
    SOHASH_REVERSE_KEY_T reverse_so_key; /* Reverse split ordered key. */
    SOHASH_KEY_T         so_key;         /* Split-ordered hash key */
    SOHASH_HASHINDEX_T   hashindex;      /* Hash index */
    SOHASH_INDEX_T       segment_index;  /* Hash segment index */
    SOHASH_INDEX_T       bucket_index;   /* Hash bucket index */
    SOHASH_BOOL_T        entry_type;     /* Boolean denoting entry type */
} sohash_entry_t;

/* This is the structure for the hazard pointer record to be maintained one 
 * per thread.  The safe memory reclamation algorithm and the solution for 
 * ABA uses this record and will be potentially reused several times 
 * in its life time.
 */
typedef struct sohash_hazardptr_record {
    sohash_entry_t                 *hazard_ptrs[3];
    sohash_entry_t                 *rlist; /* Head of the linked list
                                              maintaining retired nodes */
    SOHASH_RCOUNT_T                rcount; /* Number of retired nodes */
} sohash_hazardptr_record_t;

/* This is the structure with the hazard pointers and references to a snapshot
 * of the lock free linked list.  
 */
typedef struct sohash_hazard_ref {
    struct sohash_hazard_ref       *hp_next;

    /* prev, cur and next pointers will save a snapshot of the lock free
     * linked list.  These variables will be set by sohash_list_find(). 
     * In the context of the ABA solution for split-ordered hash sets, these
     * are the hazardous references.  
     */
    sohash_entry_t                 **prev;
    sohash_entry_t                 *cur;
    sohash_entry_t                 *next;

    /* Quick reference to the elements of the hazard pointer array in
     * sohash_hazardptr_record_t.  This is used by the solution to the ABA
     * problem using hazard pointers.  
     */
    sohash_entry_t                 **hp0;
    sohash_entry_t                 **hp1;
    sohash_entry_t                 **hp2;

    /* Hazard pointer record is used by the Safe Memory Reclamation (SMR)
     * algorithm.  
     */
    sohash_hazardptr_record_t      hprec;

    SOHASH_HP_STATE_T              hp_state; /* Boolean INUSE/FREE. */
} sohash_hazard_ref_t;

typedef sohash_entry_t**  sohash_segment_t;

/* Macros for the hash segment sizes and number of entries per segment. */
#define SOHASH_DEFAULT_SEG_COUNT 511
#define SOHASH_DEFAULT_SEG_SZ  511
#define SOHASH_MIN_SEG_COUNT 1
#define SOHASH_MIN_SEG_SZ 251
#define SOHASH_SET_SEG_DEFAULT -1

/* This is the definition of the split-ordered hash table descriptor.
 * Contains fields for determining the characteristics of the hash and 
 * also those required for its operation.  For example, at the time of 
 * hash initialization user is required to register four call backs 
 * that are required for proper functioning of the hash.  
 * They are:
 *     fn_verify_matched_entry: Pointer to function for verifying if 
 *                              the sohash_entry_t found in the hash is
 *                              the one expected by the caller.  If the 
 *                              sohash keys generated by the caller are 
 *                              not gauranteed to be unique, then, 
 *                              sohash_keys_unique, should be set to 
 *                              SOKEY_UNIQUE_FALSE and this function
 *                              pointer needs to be initialized.  The
 *                              routine should return SOHASH_TRUE if the
 *                              passed sohash_entry_t is the one that was 
 *                              expected, if not SOHASH_FALSE.
 *     fn_data_free: Pointer to routine to free the memory allocated by
 *                   the user for "data" in sohash_entry_t.
 *     fn_compute_sohashkey: Pointer to function for computing sohashkey.
 *     fn_on_delete: Pointer to a function call back for the user to perform
 *                   any actions right after the hash entry is removed from
 *                   the hash, but before it is freed up.
 *
 * In order to achieve dynamic sized array that can be grown, as and when
 * the number of elements in the hash increases, without the overheads of 
 * reallocation, the hashtable array is implemented as an array with
 * sohash_num_segments, each capable of storing sohash_segment_sz number
 * of sohash_entry_t * entries. The default for number of segments is
 * SOHASH_DEFAULT_NUM_SEG and that for segment size is 
 * SOHASH_DEFAULT_SEG_SZ.
 */
typedef struct sohash_table {
    sohash_segment_t          *hashtable;
                                            
    /* Function pointers for call backs registered by user.  */
    SOHASH_BOOL_T             (*fn_verify_matched_entry)(sohash_entry_t *entry,
                                                    void *arg_verify_callback);
    void                      (*fn_data_free)(void *ptr);
    SOHASH_KEY_T              (*fn_compute_sohashkey)(void *data);
    void                      (*fn_on_delete)(sohash_entry_t *entry);

    sohash_hazard_ref_t       *hp_head; /* Head of the linked list of 
                                              hazard pointer references */

    SOHASH_HASH_STATE_T       hash_state; /* Current state of hash */

    SOHASH_SEG_COUNT_T        sohash_num_segments; /* Number of segments */
                                         
    SOHASH_SEG_SZ_T           sohash_segment_sz; /* Segment size */

    SOHASH_BOOL_T             sohash_keys_unique; /* Boolean for sokey 
                                                     uniqueness */

    SOHASH_HPCOUNT_T          hazard_ptrs_total; /* Number of hazard ptrs */

    SOHASH_SZ_T               sohash_current_size; /* Size/Capacity of the 
                                                      hash */

    SOHASH_KEYCOUNT_T         sohash_regularkey_count; /* Number of regular 
                                                          nodes */

    SOHASH_KEYCOUNT_T         sohash_sentinelkey_count; /* Number of initialized 
                                                           sentinel nodes */
} sohash_table_t;

/* This is the structure to be populated and passed as argument to 
 * sohash_init_hashtable().  These values will be used in creating
 * the hash and initializing it.  Some of the fields are explained 
 * below.
 *   sohash_num_segments - Number of segments that the hash has to 
 *                         be created with.  sohash_num_segments field
 *                         in sohash_table_t will be initialized to 
 *                         the default value of SOHASH_DEFAULT_NUM_SEG
 *                         if the value of this argument is set to 
 *                         SOHASH_SET_SEG_DEFAULT.
 *   sohash_segment_sz - Size of each segment.  sohash_segment_sz field
 *                       in sohash_table_t will be initialized to
 *                       the default value of SOHASH_DEFAULT_SEG_SZ if
 *                       the value of this argument is set to
 *                       SOHASH_SET_SEG_DEFAULT.
 *
 *   Please refer to the comment for sohash_table_t for more 
 *   information on segments.  
 *
 *   sohash_keys_unique - Flag to denote if the sohash keys that will
 *                        be passed by the caller are unique or if 
 *                        duplicates could be encountered.  Set 
 *                        SOKEY_UNIQUE_TRUE or SOKEY_UNIQUE_FALSE 
 *                        accordingly.  If this boolean is set to
 *                        SOKEY_UNIQUE_FALSE then, 
 *                        fn_verify_matched_entry is a mandatory
 *                        argument.
 *   
 *   Please refer to sohash_table_t for details of function pointers
 *   to be passed as argument.  fn_on_delete is an optional argument.
 */
typedef struct sohash_init_args {
    SOHASH_SEG_COUNT_T    sohash_num_segments; 
    SOHASH_SEG_SZ_T       sohash_segment_sz;  
    SOHASH_BOOL_T         sohash_keys_unique;  
    void                  (*fn_data_free)(void *ptr);
    SOHASH_KEY_T          (*fn_compute_sohashkey)(void *data);
    SOHASH_BOOL_T         (*fn_verify_matched_entry)(sohash_entry_t *entry,
                                                     void *arg_verify_callback);
    void                  (*fn_on_delete)(sohash_entry_t *entry);
} sohash_init_args_t;

#define SOHASH_NOT_FOUND 0
#define SOHASH_FOUND 1

#define SOHASH_FALSE 0
#define SOHASH_TRUE 1

#define SOHASH_UNINITIALIZED 0

/* Macros for specifying if sohash keys are unique or otherwise.  */
#define SOKEY_UNIQUE_TRUE 1
#define SOKEY_UNIQUE_FALSE 0

/* Macros for type of sohash_entry_t.  */
#define SOHASH_SENTINEL_NODE 1
#define SOHASH_REGULAR_NODE 2

/* Macros for identifying the current state of the hash.  */
#define SOHASH_STATE_ACTIVE 1
#define SOHASH_STATE_FLUSH  2

/* Function prototypes of the exported routines.  */
sohash_table_t *sohash_init_hashtable(sohash_init_args_t *init_args);

int sohash_cleanup_hashtable(sohash_table_t *hashd);

int sohash_insert_entry(
    sohash_table_t *hashd,
    SOHASH_KEY_T   sohash_key,
    void           *data,
    void           *arg_verify_callback,
    sohash_entry_t **node
);

sohash_entry_t *
sohash_find_entry(
    sohash_table_t *hashd,
    SOHASH_KEY_T   sohash_key,
    void           *arg_verify_callback
);

int
sohash_delete_entry(
    sohash_table_t *hashd,
    SOHASH_KEY_T   sohash_key,
    void           *arg_verify_callback
);

#endif /* MVFS_SOHASH_TABLE_H_ */

/* $Id: 91863e90.c4c011e0.9754.00:01:83:9c:f6:11 $ */
