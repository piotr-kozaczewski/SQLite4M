#ifndef _SQLITE4_KVBDB_H_
#define _SQLITE4_KVBDB_H_

#include "sqlite4.h"
#include <db.h>
#include "sqliteInt.h"
#include <stdio.h>

/*
** Declarations for 
** a BerkeleyDB key/value storage subsystem 
** that presents the interface
** defined by "kv.h"
** Implementation can be found in "kvbdb.c".
*/
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
  
  DB_TXN * pTxn[SQLITE4_KV_BDB_MAX_TXN_DEPTH+1]; /* transaction(s) open in given database/connection*/

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

// ---------------- API functions -- begin --------------------------

int sqlite4KVStoreOpenBdb(
  sqlite4_env *pEnv,              /* Runtime environment */
  KVStore **ppKVStore,            /* OUT: Write the new KVStore here */
  const char *zName,              /* Name of BerkeleyDB storage unit */
  unsigned openFlags              /* Flags */
);

int kvbdbReplace(
  KVStore *pKVStore,
  const KVByteArray *aKey, KVSize nKey,
  const KVByteArray *aData, KVSize nData);
  
int kvbdbOpenCursor(KVStore *pKVStore, KVCursor **ppKVCursor);

int kvbdbSeek(
  KVCursor *pKVCursor, 
  const KVByteArray *aKey,
  KVSize nKey,
  int direction);
  
int kvbdbNextEntry(KVCursor *pKVCursor); 

int kvbdbPrevEntry(KVCursor *pKVCursor);

int kvbdbDelete(KVCursor *pKVCursor);

int kvbdbKey(
  KVCursor *pKVCursor,         /* The cursor whose key is desired */
  const KVByteArray **paKey,   /* Make this point to the key */
  KVSize *pN                   /* Make this point to the size of the key */
  ); 
  
int kvbdbData(
  KVCursor *pKVCursor,         /* The cursor from which to take the data */
  KVSize ofst,                 /* Offset into the data to begin reading */
  KVSize n,                    /* Number of bytes requested */
  const KVByteArray **paData,  /* Pointer to the data written here */
  KVSize *pNData               /* Number of bytes delivered */
);

int kvbdbReset(KVCursor *pKVCursor);

int kvbdbCloseCursor(KVCursor *pKVCursor);

int kvbdbBegin(KVStore *pKVStore, int iLevel);

int kvbdbCommitPhaseOne(KVStore *pKVStore, int iLevel);

int kvbdbCommitPhaseOneXID(KVStore *pKVStore, int iLevel, void * xid);

int kvbdbCommitPhaseTwo(KVStore *pKVStore, int iLevel);

int kvbdbRollback(KVStore *pKVStore, int iLevel);

int kvbdbRevert(KVStore *pKVStore, int iLevel);

int kvbdbClose(KVStore *pKVStore);

int kvbdbControl(KVStore *pKVStore, int op, void *pArg);

int kvbdbGetMeta(KVStore *pKVStore, unsigned int *piVal);

int kvbdbPutMeta(KVStore *pKVStore, unsigned int iVal);  

// ---------------- API functions -- end   --------------------------

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
    struct sqlite4_mutex * mutexp;
    char name[128];
    DB * dbp;
    DB_ENV * envp;
    uint32_t nref;
    bdb_dict_node_t * next;
};

/* Berkeley DB dictionary */
struct bdb_dict_t
{
    struct sqlite4_mutex * mutexp;
    bdb_dict_node_t * nodes;
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
bdb_dict_node_t * global_acquire_locked_dict_node( char * node_name );
void unlock_locked_dict_node(bdb_dict_node_t * node);	
bdb_dict_node_t * global_get_locked_existing_dict_node_or_null( char * node_name );	


/*
** KVBdd Configuration API
*/			

/*
** KWBdb Configurator Class : kvbdb_config_t
*/
typedef struct kvbdb_config_t kvbdb_config_t;

struct kvbdb_config_t
{
   struct sqlite4_mutex * mutexp;
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
   struct sqlite4_mutex * mutexp;
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

#endif /* end of #ifndef _SQLITE4_KVBDB_H_ */