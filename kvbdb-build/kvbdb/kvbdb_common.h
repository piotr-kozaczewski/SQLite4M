/*
** 2018 May 30
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
**
** A BerkeleyDB key/value storage subsystem that presents the interface
** defined by kv.h
*/


#include "kvbdb.h" /* for Declaration/typing of the Dictionary Structure and global init routine */

#include <time.h>
#include <stdio.h>
#include <errno.h>
//#include <stdlib.h> /*for min and max macros*/

#include <mutex>


// ---------------- decl begin --------------------------

#include "sqliteInt.h"

#include <stdio.h>

#include <db.h>


#ifdef WIN32
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
#endif


//#include "kvbdb.h" /* for Declaration/typing of the Dictionary Structure and global init routine */

/* Forward declarations of object names */
typedef struct KVBdb KVBdb;
typedef struct KVBdbCursor KVBdbCursor;

/*
** A complete BerkeleyDB Key/Value store
*/
#define SQLITE4_KV_BDB_MAX_TXN_DEPTH 16

struct KVBdb {
   KVStore base;         /* Base class, must be first */
   unsigned openFlags;   /* Flags used at open */
   int nCursor;          /* Number of outstanding cursors */
   int iMagicKVBdbBase;  /* Magic number of sanity */
   unsigned int iMeta;   /* Schema cookie value */

   DB_ENV * envp;        /* BerkeleyDB environment for connection / database */
   DB     * dbp;         /* connection to BerkeleyDB database                */

   char name[128];

   DBC * pCsr;            /* Cursor for read-only ops "outside" {?] transaction(s) [?]} */

   DB_TXN * pTxn[SQLITE4_KV_BDB_MAX_TXN_DEPTH + 1]; /* transaction(s) open in given database/connection*/

                                                    // for cursors -- begin
   u_int32_t nInitialCursorKeyBufferCapacity;
   u_int32_t nInitialCursorDataBufferCapacity;
   // for cursors -- end
};
#define SQLITE4_KVBDBBASE_MAGIC  0xcedc46e1

/*
** A cursor used for scanning through the tree
*/
#define SEEK_DIR_NONE      0
#define SEEK_DIR_EQ        1
#define SEEK_DIR_GT        2
#define SEEK_DIR_GE        3
#define SEEK_DIR_LT        4
#define SEEK_DIR_LE        5

struct KVBdbCursor {
   KVCursor base;        /* Base class. Must be first */
   KVBdb *pOwner;        /* The tree that owns this cursor */
   int iMagicKVBdbCur;   /* Magic number for sanity */
                         //
   DBC * pCsr; // the underlying BerkeleyDB Cursor
               //
               // Cached Key & Data Buffers -- begin
   int nHasKeyAndDataCached;
   void * pCachedKey;
   u_int32_t nCachedKeySize;
   u_int32_t nCachedKeyCapacity;
   void * pCachedData;
   u_int32_t nCachedDataSize;
   u_int32_t nCachedDataCapacity;
   // Cached Key & Data Buffers -- end

   int nIsEOF;

   int nLastSeekDir;
};
#define SQLITE4_KVBDBCUR_MAGIC   0xc0abed20

// ----------------Extended / Helper API functions -- begin ---------
int kvbdbSetGlobalDefaultInitialCursorBuffersCapacities(u_int32_t nKeyBufferCapacity, u_int32_t nDataBufferCapacity);
int kvbdbGetGlobalDefaultInitialCursorBuffersCapacities(u_int32_t * pnKeyBufferCapacity, u_int32_t * pnDataBufferCapacity);
int kvbdbSetInitialCursorBuffersCapacities(KVBdb * kvbdb, u_int32_t nKeyBufferCapacity, u_int32_t nDataBufferCapacity);
int kvbdbGetInitialCursorBuffersCapacities(KVBdb * kvbdb, u_int32_t * pnKeyBufferCapacity, u_int32_t * pnDataBufferCapacity);
// ----------------Extended / Helper API functions -- end   ---------



// ---------------- decl end   --------------------------

void enable_mutexes_in_kvbdb();

/*
** Declaration/typing of the Dictionary Structure
** for caching DB and DB_ENV objects
** among connections / databases
** identified with the same database_name.
** The size of database_name is limited to 128 bytes
** including the terminating ASCII(0).
*/

/* forward declarations of object names */
typedef struct bdb_dict_node_t bdb_dict_node_t;
typedef struct bdb_dict_t bdb_dict_t;

/* Berkeley DB dictionary node */
struct bdb_dict_node_t
{
   std::mutex mutex;
   char name[128];
   DB * dbp;
   DB_ENV * envp;
   uint32_t nref;
   bdb_dict_node_t * next;

   bdb_dict_node_t()
      : dbp(nullptr)
      , envp(nullptr)
      , nref(0)
      , next(nullptr)
   {
      memset(name, 0, sizeof(name));
   }

   ~bdb_dict_node_t()
   {
      dbp = nullptr;
      envp = nullptr;
      nref = 0;
      next = nullptr;
   }
};

/* Berkeley DB dictionary */
struct bdb_dict_t
{
   std::mutex mutex;
   bdb_dict_node_t * nodes;

   bdb_dict_t()
      : nodes(nullptr)
   {
   }

   ~bdb_dict_t()
   {
      nodes = nullptr;
   }
};

/*
** initialize the global static BerkeleyDB dictionary,
** incl. mutex(es).
** this code should be called in the early,
** single-threaded, initialization phase
*/
int init_global_bdb_dict();

/*
** BerkeleyDB Dictionary API
** exposed for (unit) test purposes
*/
bdb_dict_node_t * global_acquire_locked_dict_node(char * node_name);
void unlock_locked_dict_node(bdb_dict_node_t * node);
bdb_dict_node_t * global_get_locked_existing_dict_node_or_null(char * node_name);


/*
** KVBdd Configuration API
*/

/*
** KWBdb Configurator Class : kvbdb_config_t
*/
typedef struct kvbdb_config_t kvbdb_config_t;

struct kvbdb_config_t
{
   std::mutex mutex;
   char * name[128];
   int flags;

   // more members can come here in the future

   kvbdb_config_t * next;
};

/*
**  KWBdb Configurator Class creation/destruction API
*/
kvbdb_config_t * create_empty_kvbdb_config();
kvbdb_config_t * create_empty_named_kvbdb_config(char * name);
kvbdb_config_t * create_default_kvbdb_config();
kvbdb_config_t * create_default_named_kvbdb_config(char * name);
int destroy_kvbdb_config(kvbdb_config_t * a_kvbdb_config_p);

/*
** KWBdb Configuration Register Class : kvbdb_config_register_t
*/
typedef struct kvbdb_config_register_t kvbdb_config_register_t;

struct kvbdb_config_register_t
{
   std::mutex mutex;
   kvbdb_config_t * nodes;
};

/*
** global static instance of KWBdb Configuration Register Class
*/
// defined in file "kvbdb.c"

/*
** KWBdb Configuration Register : Init API
*/
int init_kvbdb_config_register(kvbdb_config_register_t ** a_kvbdb_config_register_pp);
int init_global_kvbdb_config_register();

/*
** KWBdb Configuration Register : Registration & Lookup API
*/
int register_new_kvbdb_config(kvbdb_config_t * a_kvbdb_config_p);
int update_existing_kvbdb_config(kvbdb_config_t * a_kvbdb_config_p);
kvbdb_config_t * get_existing_kvbdb_config(char * name);
kvbdb_config_t * get_locked_existing_kvbdb_config(char * name);
int unlock_kvbdb_config(kvbdb_config_t * a_kvbdb_config_p);


#ifndef max
#define max(a,b) (((a) (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

extern u_int32_t nGlobalDefaultInitialCursorKeyBufferCapacity;
extern u_int32_t nGlobalDefaultInitialCursorDataBufferCapacity;

// ----------------Extended / Helper API functions -- begin ---------
int kvbdbSetGlobalDefaultInitialCursorBuffersCapacities(u_int32_t nKeyBufferCapacity, u_int32_t nDataBufferCapacity);
int kvbdbGetGlobalDefaultInitialCursorBuffersCapacities(u_int32_t * pnKeyBufferCapacity, u_int32_t * pnDataBufferCapacity);
int kvbdbSetInitialCursorBuffersCapacities(KVBdb * kvbdb, u_int32_t nKeyBufferCapacity, u_int32_t nDataBufferCapacity);
int kvbdbGetInitialCursorBuffersCapacities(KVBdb * kvbdb, u_int32_t * pnKeyBufferCapacity, u_int32_t * pnDataBufferCapacity);
// ----------------Extended / Helper API functions -- end   ---------

/*
** Implementation of the Dictionary Structure,  
** as declared in "kvbdb.h", 
** for caching DB and DB_ENV objects 
** among connections / databases 
** identified with the same database_name.
** The size of database_name is limited to 128 bytes 
** including the terminating ASCII(0).
*/

/* global static instance of BerkeleyDB dictionary*/
extern bdb_dict_t * s_bdb_dict_p;

/* 
** initialize the global static BerkeleyDB dictionary, 
** incl. mutex(es). 
** this code should be called in the early, 
** single-threaded, initialization phase
*/
int init_global_bdb_dict();

/* 
** initialize a BerkeleyDB dictionary, 
** incl. mutex(es). 
** this code should be called in the early, 
** single-threaded, initialization phase
*/
int init_bdb_dict(bdb_dict_t ** a_bdb_dict_pp);


/*
** Acquire 
** (i.e. get existing or register and return new) 
** bdb_dict_node_t (by ptr) 
** from given bdb_dict_t 
** for given node_name.
** The returned bdb_dict_node_t is locked 
** by its own mutex.
*/
bdb_dict_node_t * acquire_locked_dict_node
(bdb_dict_t * a_bdb_dict_p,
   char * node_name);


bdb_dict_node_t * global_acquire_locked_dict_node(char * node_name);

					
/*
** unlock the (previously returned as locked) 
** locked dictionary node
*/
void unlock_locked_dict_node(bdb_dict_node_t * node);


/*
** Get existing  
** bdb_dict_node_t (by ptr) 
** from given bdb_dict_t 
** for given node_name.
** The returned bdb_dict_node_t is locked 
** by its own mutex.
** When a node 
** associated with given node_name 
** does not exist, 
** return a NULL.
*/
bdb_dict_node_t * get_locked_existing_dict_node_or_null
(bdb_dict_t * a_bdb_dict_p, char * node_name);


bdb_dict_node_t * global_get_locked_existing_dict_node_or_null(char * node_name);


/*
** KVBdd Configuration API
*/

/*
**  KWBdb Configurator Class creation/destruction API
*/
kvbdb_config_t * create_empty_kvbdb_config();


kvbdb_config_t * create_empty_named_kvbdb_config(char * name);


kvbdb_config_t * create_default_kvbdb_config();


kvbdb_config_t * create_default_named_kvbdb_config(char * name);


int destroy_kvbdb_config(kvbdb_config_t * a_kvbdb_config_p);


/*
** global static instance of KWBdb Configuration Register Class 
*/
// declared in file "kvbdb.h"
extern kvbdb_config_register_t * s_kvbdb_config_register_p;

/*
** KWBdb Configuration Register : Init API
*/
int init_kvbdb_config_register(kvbdb_config_register_t ** a_kvbdb_config_register_pp);


int init_global_kvbdb_config_register();


/*
** KWBdb Configuration Register : Registration & Lookup API
*/
int register_new_kvbdb_config(kvbdb_config_t * a_kvbdb_config_p);

int update_existing_kvbdb_config(kvbdb_config_t * a_kvbdb_config_p);


kvbdb_config_t * get_existing_kvbdb_config(char * name);


kvbdb_config_t * get_locked_existing_kvbdb_config(char * name);


int unlock_kvbdb_config(kvbdb_config_t * a_kvbdb_config_p);


/*
** Implementation of the xReplace(X, aKey, nKey, aData, nData) method.
**
** Insert or replace the entry with the key aKey[0..nKey-1].  The data for
** the new entry is aData[0..nData-1].  Return SQLITE4_OK on success or an
** error code if the insert fails.
**
** The inputs aKey[] and aData[] are only valid until this routine
** returns.  If the storage engine needs to keep that information
** long-term, it will need to make its own copy of these values.
**
** A transaction will always be active when this routine is called.
*/



extern DB_ENV * s_envp;         /* BerkeleyDB environment for connection / database */
extern struct sqlite4_mutex * s_env_mutexp;

extern DB     * s_dbp;         /* connection to BerkeleyDB database                */
extern struct sqlite4_mutex * s_db_mutexp;


/* Virtual methods for the BerkeleyDB storage engine */
extern const KVStoreMethods kvbdbMethods;
