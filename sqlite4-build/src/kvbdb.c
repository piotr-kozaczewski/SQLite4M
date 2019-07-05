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


//// ---------------- decl begin --------------------------
//
//#include "sqliteInt.h"
//
//#include <stdio.h>
//
//#include <db.h>
//
////#include "kvbdb.h" /* for Declaration/typing of the Dictionary Structure and global init routine */
//
///* Forward declarations of object names */
//typedef struct KVBdb KVBdb;
//typedef struct KVBdbCursor KVBdbCursor;
//
///*
//** A complete BerkeleyDB Key/Value store 
//*/
//struct KVBdb {
//  KVStore base;         /* Base class, must be first */
//  unsigned openFlags;   /* Flags used at open */
//  int nCursor;          /* Number of outstanding cursors */
//  int iMagicKVBdbBase;  /* Magic number of sanity */
//  unsigned int iMeta;   /* Schema cookie value */
//  
//  DB_ENV * envp;        /* BerkeleyDB environment for connection / database */
//  DB     * dbp;         /* connection to BerkeleyDB database                */
//  
//  char name[128];
//};
//#define SQLITE4_KVBDBBASE_MAGIC  0xcedc46e1
//
///*
//** A cursor used for scanning through the tree
//*/
//struct KVBdbCursor {
//  KVCursor base;        /* Base class. Must be first */
//  KVBdb *pOwner;        /* The tree that owns this cursor */
//  int iMagicKVBdbCur;   /* Magic number for sanity */
//};
//#define SQLITE4_KVBDBCUR_MAGIC   0xc0abed20
//
//// ---------------- decl end   --------------------------


#include "kvbdb.h" /* for Declaration/typing of the Dictionary Structure and global init routine */

#include <time.h>
#include <stdio.h>
#include <errno.h>
//#include <stdlib.h> /*for min and max macros*/

#ifndef max
#define max(a,b) (((a) (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

static u_int32_t nGlobalDefaultInitialCursorKeyBufferCapacity  = 16384; // 16 k
static u_int32_t nGlobalDefaultInitialCursorDataBufferCapacity = 16384; // 16 k

// ----------------Extended / Helper API functions -- begin ---------
int kvbdbSetGlobalDefaultInitialCursorBuffersCapacities(u_int32_t nKeyBufferCapacity, u_int32_t nDataBufferCapacity)
{
   nGlobalDefaultInitialCursorKeyBufferCapacity = nKeyBufferCapacity;
   nGlobalDefaultInitialCursorDataBufferCapacity = nDataBufferCapacity;
   return 0;
}
int kvbdbGetGlobalDefaultInitialCursorBuffersCapacities(u_int32_t * pnKeyBufferCapacity, u_int32_t * pnDataBufferCapacity)
{
   *pnKeyBufferCapacity = nGlobalDefaultInitialCursorKeyBufferCapacity;
   *pnDataBufferCapacity = nGlobalDefaultInitialCursorDataBufferCapacity;
   return 0;
}
int kvbdbSetInitialCursorBuffersCapacities(KVBdb * kvbdb, u_int32_t nKeyBufferCapacity, u_int32_t nDataBufferCapacity)
{
   kvbdb->nInitialCursorKeyBufferCapacity = nKeyBufferCapacity;
   kvbdb->nInitialCursorDataBufferCapacity = nDataBufferCapacity;
   return 0;
}

int kvbdbGetInitialCursorBuffersCapacities(KVBdb * kvbdb, u_int32_t * pnKeyBufferCapacity, u_int32_t * pnDataBufferCapacity)
{
   *pnKeyBufferCapacity = kvbdb->nInitialCursorKeyBufferCapacity;
   *pnDataBufferCapacity = kvbdb->nInitialCursorDataBufferCapacity;
   return 0;
}
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

static int use_mutexed_in_kvbdb = 0;
void enable_mutexes_in_kvbdb()
{
   use_mutexed_in_kvbdb = 1;
}
 

///* forward declarations of object names */
//typedef struct bdb_dict_node_t bdb_dict_node_t;
//typedef struct bdb_dict_t bdb_dict_t;
//
///* Berkeley DB dictionary node */
//struct bdb_dict_node_t
//{
//    struct sqlite4_mutex * mutexp;
//    char name[128];
//    DB * dbp;
//    DB_ENV * envp;
//    uint32_t nref;
//    bdb_dict_node_t * next;
//};
//
///* Berkeley DB dictionary */
//struct bdb_dict_t
//{
//    struct sqlite4_mutex * mutexp;
//    bdb_dict_node_t * nodes;
//};

/* global static instance of BerkeleyDB dictionary*/
static bdb_dict_t * s_bdb_dict_p = NULL;

/* 
** initialize the global static BerkeleyDB dictionary, 
** incl. mutex(es). 
** this code should be called in the early, 
** single-threaded, initialization phase
*/
int init_global_bdb_dict()
{
   int ret = init_bdb_dict(&s_bdb_dict_p);
   return(ret);
}

/* 
** initialize a BerkeleyDB dictionary, 
** incl. mutex(es). 
** this code should be called in the early, 
** single-threaded, initialization phase
*/
int init_bdb_dict(bdb_dict_t ** a_bdb_dict_pp)
{
    if( *a_bdb_dict_pp == NULL)
    {
        // not initialized yet
        //
        // allocate a new bdb_dict_t
        bdb_dict_t * pNew = (bdb_dict_t *)malloc(sizeof(bdb_dict_t));
        if(!pNew)
        {
            // cannot allocate -- no memory?
            //printf("init_dbd_dict() : cannot allocate a new bdb_dict_t\n");
            return SQLITE4_NOMEM;
        }
        //
        // NULL-initialize a list of nodes`
        pNew->nodes = NULL;
        //
        // allocate a new sqlite4_mutex
		if(use_mutexed_in_kvbdb)
		{
           struct sqlite4_mutex * pNewMutex = NULL;
           pNewMutex = sqlite4_mutex_alloc(0, SQLITE4_MUTEX_FAST);
           if(pNewMutex == NULL)
           {
              //printf("init_dbd_dict() : cannot allocate a new sqlite4_mutex\n");
              free((void*)pNew);
              return SQLITE4_NOMEM;
           }
           pNew->mutexp = pNewMutex;
		}
		
        *a_bdb_dict_pp = pNew;
		
		// initialized
        return SQLITE4_OK;
    }
	else
	{
	   // *a_bdb_dict_pp != NULL
	   // i.e. already initialized
	   //printf("init_bdb_dict(...) : BerkeleyDB Dictionary, ptr='%p', is already initialized\n", (void*)(*a_bdb_dict_pp));
	   return SQLITE4_ERROR;
	}
}

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
                  ( bdb_dict_t * a_bdb_dict_p, 
                    char * node_name )
{
    assert(a_bdb_dict_p);
	if(use_mutexed_in_kvbdb)
	{
       assert(a_bdb_dict_p->mutexp);
	}
    assert(node_name);
    assert(strlen(node_name) > 0);
    assert(strlen(node_name) < 127);

    // lock dictionary 
    // on dictionary's own mutex
	if(use_mutexed_in_kvbdb)
	{
       sqlite4_mutex_enter(a_bdb_dict_p->mutexp);
	}
    
    // initialize ptr to dict node
    bdb_dict_node_t * pCurrDictNode = a_bdb_dict_p->nodes;
    
    // initialize target ptr-to-ptr / insertion point 
    bdb_dict_node_t ** pInsertionPoint = &(a_bdb_dict_p->nodes);
    
    // search among existing nodes
    while(pCurrDictNode)
    {
        // match by node_name
        if(!strcmp(node_name, pCurrDictNode->name))
        {
            // found
            break;
        }
        else
        {
            // not found - continue search
            pInsertionPoint = &(pCurrDictNode->next);
            pCurrDictNode = pCurrDictNode->next;
        }
    } // end of search, i.e end of loop : while(pCurrDictNode){...}
    
    if(pCurrDictNode)
    {
        // found!
        
        // lock node 
        // using node's own mutex
		if(use_mutexed_in_kvbdb)
		{
           sqlite4_mutex_enter(pCurrDictNode->mutexp);
		}
        
        // unlock dictionary
		if(use_mutexed_in_kvbdb)
		{
           sqlite4_mutex_leave(a_bdb_dict_p->mutexp);
		}
        
        return pCurrDictNode;
    }
    
    // not found
    
    // create a new dictionary node
    bdb_dict_node_t * pNew = (bdb_dict_node_t *)malloc(sizeof(bdb_dict_node_t));
    if(!pNew)
    {
        // not allocated - probably no memory
        
        //printf("acquire_locked_dict_node() : cannot allocate new bdb_dict_node_t\n");
        
        // unlock dictionary
		if(use_mutexed_in_kvbdb)
		{
           sqlite4_mutex_enter(a_bdb_dict_p->mutexp); 
		}
        
        return 0;
    }
    
    // NULL-/zero-set the members
    pNew->mutexp = NULL;
    strcpy(pNew->name, "");
    pNew->dbp = NULL;
    pNew->envp = NULL;
    pNew->nref = 0;
    pNew->next = NULL;
    
    // allocate node's own mutex
	if(use_mutexed_in_kvbdb)
	{
       struct sqlite4_mutex * pNewMutex = NULL;
       pNewMutex = sqlite4_mutex_alloc(0, SQLITE4_MUTEX_FAST);
       if(pNewMutex == NULL)
       {
          //printf("acquire_locked_dict_node() : cannot allocate new node's own sqlite4_mutex\n");
          free((void*)pNew);
        
          // unlock dictionary
          sqlite4_mutex_leave(a_bdb_dict_p->mutexp);
        
          return NULL;
       }
       // assign the new mutex to new node
       pNew->mutexp = pNewMutex;
	}
    
    // register name
    strcpy(pNew->name, node_name);
    
    // insert the new node into dictionary 
    // at the insertion point
    (*pInsertionPoint) = pNew;
    
    // lock the new node using it's own mutex
	if(use_mutexed_in_kvbdb)
	{
       sqlite4_mutex_enter(pNew->mutexp);
	}
	
	// unlock dictionary
	if(use_mutexed_in_kvbdb)
	{
       sqlite4_mutex_leave(a_bdb_dict_p->mutexp);
	}
    
    return pNew;
}

bdb_dict_node_t * global_acquire_locked_dict_node( char * node_name )
{
    if(s_bdb_dict_p == NULL)
	{
	    //printf("global_acquire_locked_dict_node() : Global BerkeleyDB Dictionary not initialized\n");
		return NULL;
	}
	bdb_dict_node_t * ret = acquire_locked_dict_node(s_bdb_dict_p, node_name);
	return(ret);
}
					
/*
** unlock the (previously returned as locked) 
** locked dictionary node
*/
void unlock_locked_dict_node(bdb_dict_node_t * node)
{
    assert(node);
	if(use_mutexed_in_kvbdb)
	{
       assert(node->mutexp);
	}
    
	if(use_mutexed_in_kvbdb)
	{
       sqlite4_mutex_leave(node->mutexp);
	}
}

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
                  ( bdb_dict_t * a_bdb_dict_p, char * node_name)
{
    assert(a_bdb_dict_p);
	if(use_mutexed_in_kvbdb)
	{
       assert(a_bdb_dict_p->mutexp);
	}
    assert(node_name);
    assert(strlen(node_name) > 0);
    assert(strlen(node_name) < 127);

    // lock dictionary 
    // on dictionary's own mutex
	if(use_mutexed_in_kvbdb)
	{
       sqlite4_mutex_enter(a_bdb_dict_p->mutexp);
	}
    
    // initialize ptr to dict node
    bdb_dict_node_t * pCurrDictNode = a_bdb_dict_p->nodes;
    
    // initialize target ptr-to-ptr / insertion point 
    bdb_dict_node_t ** pInsertionPoint = &(a_bdb_dict_p->nodes);
    
    // search among existing nodes
    while(pCurrDictNode)
    {
        // match by node_name
        if(!strcmp(node_name, pCurrDictNode->name))
        {
            // found
            break;
        }
        else
        {
            // not found - continue search
            pInsertionPoint = &(pCurrDictNode->next);
            pCurrDictNode = pCurrDictNode->next;
        }
    } // end of search, i.e end of loop : while(pCurrDictNode){...}
    
    if(pCurrDictNode)
    {
        // found!
        
        // lock node 
        // using node's own mutex
		if(use_mutexed_in_kvbdb)
		{
           sqlite4_mutex_enter(pCurrDictNode->mutexp);
		}
        
        // unlock dictionary
		if(use_mutexed_in_kvbdb)
		{
           sqlite4_mutex_leave(a_bdb_dict_p->mutexp);
		}
        
        return pCurrDictNode;
    }
    
    // not found
	
	// unlock dictionary
	if(use_mutexed_in_kvbdb)
	{
       sqlite4_mutex_leave(a_bdb_dict_p->mutexp);
	}
		
    return NULL;
}

bdb_dict_node_t * global_get_locked_existing_dict_node_or_null( char * node_name )
{
    if(s_bdb_dict_p == NULL)
	{
	    //printf("global_get_locked_existing_dict_node_or_null() : Global BerkeleyDB Dictionary not initialized\n");
		return NULL;
	}
	bdb_dict_node_t * ret = get_locked_existing_dict_node_or_null(s_bdb_dict_p, node_name);
	return(ret);
}

/*
** KVBdd Configuration API
*/

/*
**  KWBdb Configurator Class creation/destruction API
*/
kvbdb_config_t * create_empty_kvbdb_config()
{
   //printf("STUB : create_empty_kvbdb_config() entered\n");
   
   return NULL;
}

kvbdb_config_t * create_empty_named_kvbdb_config(char * name)
{
   //printf("STUB : create_empty_named_kvbdb_config(), name='%s' entered\n", name);
   
   return NULL;
}

kvbdb_config_t * create_default_kvbdb_config()
{
   //printf("STUB : create_default_kvbdb_config() entered\n");
   
   return NULL;
}

kvbdb_config_t * create_default_named_kvbdb_config(char * name)
{
   //printf("STUB : create_default_named_kvbdb_config(), name='%s' entered\n", name);
   
   return NULL;
}

int destroy_kvbdb_config(kvbdb_config_t * a_kvbdb_config_p)
{
   //printf("STUB : destroy_kvbdb_config(), a_kvbdb_config_p='%p' entered\n", a_kvbdb_config_p);
   
   return NULL;
}

/*
** global static instance of KWBdb Configuration Register Class 
*/
// declared in file "kvbdb.h"
static kvbdb_config_register_t * s_kvbdb_config_register_p = NULL;

/*
** KWBdb Configuration Register : Init API
*/
int init_kvbdb_config_register(kvbdb_config_register_t ** a_kvbdb_config_register_pp)
{
   //printf("STUB : init_kvbdb_config_register(), a_kvbdb_config_register_pp='%p', *a_kvbdb_config_register_pp='%p' entered\n", a_kvbdb_config_register_pp, a_kvbdb_config_register_pp);
   
   return SQLITE4_OK;
}

int init_global_kvbdb_config_register()
{
   //printf("STUB : init_global_kvbdb_config_register()\n");
   
   return SQLITE4_OK;
}

/*
** KWBdb Configuration Register : Registration & Lookup API
*/
int register_new_kvbdb_config(kvbdb_config_t * a_kvbdb_config_p)
{
   //printf("STUB : register_new_kvbdb_config(), a_kvbdb_config_p='%p' entered\n", a_kvbdb_config_p);
   
   return SQLITE4_OK;
}
int update_existing_kvbdb_config(kvbdb_config_t * a_kvbdb_config_p)
{
   //printf("STUB : update_existing_kvbdb_config(), a_kvbdb_config_p='%p' entered\n", a_kvbdb_config_p);
   
   return SQLITE4_OK;
}

kvbdb_config_t * get_existing_kvbdb_config(char * name)
{
   //printf("STUB : get_existing_kvbdb_config(), name='%s' entered\n", name);
   
   return NULL;
}

kvbdb_config_t * get_locked_existing_kvbdb_config(char * name)
{
   //printf("STUB : get_locked_existing_kvbdb_config(), name='%s' entered\n", name);
   
   return NULL;
}

int unlock_kvbdb_config(kvbdb_config_t * a_kvbdb_config_p)
{
   //printf("STUB : unlock_kvbdb_config(), a_kvbdb_config_p='%p' entered\n", a_kvbdb_config_p);
   
   return SQLITE4_OK;
}

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
int kvbdbReplace(
  KVStore *pKVStore,
  const KVByteArray *aKey, KVSize nKey,
  const KVByteArray *aData, KVSize nData
){
  //printf("-----> kvbdbReplace()\n");  
    
  KVBdb *p;

  p = (KVBdb *)pKVStore;
  assert( p->iMagicKVBdbBase==SQLITE4_KVBDBBASE_MAGIC );  
  
  int rc = SQLITE4_OK;

  // Retrievee KVStore and BerkeleyDB "context" variables -- begin
  int nCurrTxnLevel = pKVStore->iTransLevel; // p->base.iTransLevel
  DB_TXN * pCurrTxn = p->pTxn[nCurrTxnLevel];
  DB * dbp = p->dbp;
  // Retrievee KVStore and BerkeleyDB "context" variables -- end

  // Cross-checks -- begin
  assert(nCurrTxnLevel >= 2);
  if (nCurrTxnLevel < 2)
  {
     //printf("Internal Error : nCurrTxnLevel < 2\n");
     rc = SQLITE4_INTERNAL; // or: SQLITE4_MISUSE
  }

  assert(pCurrTxn != NULL);
  if (pCurrTxn == NULL)
  {
     //printf("Internal Error : pCurrTxn == NULL\n");
     rc = SQLITE4_INTERNAL; // or: SQLITE4_MISUSE
  }

  assert(dbp != NULL);
  if (dbp == NULL)
  {
     //printf("Internal Error : dbp == NULL\n");
     rc = SQLITE4_INTERNAL; // or: SQLITE4_MISUSE
  }
  // Cross-checks -- end

  if (rc == SQLITE4_OK)
  {
     // So far OK

     // Prepare DBT's for Key and Data -- begin
     DBT keyDBT;
     DBT dataDBT;
     //
     memset(&keyDBT, 0, sizeof(DBT));
     memset(&dataDBT, 0, sizeof(DBT));
     //
     keyDBT.data = (void*)aKey;
     keyDBT.size = (u_int32_t)nKey;
     dataDBT.data = (void*)aData;
     dataDBT.size = (u_int32_t)nData;
     // Prepare DBT's for Key and Data -- end

     int ret = 0;

     ret = dbp->put(dbp, pCurrTxn, &keyDBT, &dataDBT, 0); // flag==0 taken from file "txn_guide.c", line 256. Is this flag OK?
     switch (ret)
     {
     case 0: // OK
        rc = SQLITE4_OK;
        break;
     case DB_FOREIGN_CONFLICT: // constraint violation
        rc = SQLITE4_CONSTRAINT;
        break;
     case DB_HEAP_FULL:
        rc = SQLITE4_FULL;
        break;
     case DB_LOCK_DEADLOCK:
     case DB_LOCK_NOTGRANTED:
     case  DB_REP_HANDLE_DEAD:
     case DB_REP_LOCKOUT:
        rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
        break;
     case EACCES:              // An attempt was made to modify a read-only database. 
        rc = SQLITE4_READONLY; // Attempt to write a readonly database 
        break;
     case EINVAL:
        rc = SQLITE4_MISUSE; // Library used incorrectly (!!!)
        break;
     case ENOSPC:            // A btree exceeded the maximum btree depth (255). 
        rc = SQLITE4_FULL;   // Insertion failed because database is full
        break;
     default:
        rc = SQLITE4_ERROR;
        break;
     };  // end of switch (ret){...}
  } // end of if (rc == SQLITE4_OK){...}

  return rc;
} // end of kvbdbReplace(...){...}

/*
** Create a new cursor object.
*/
int kvbdbOpenCursor(KVStore *pKVStore, KVCursor **ppKVCursor){
  //printf("-----> kvbdbOpenCursor()\n");  
    
  KVBdb *p;

  p = (KVBdb *)pKVStore;
  assert( p->iMagicKVBdbBase==SQLITE4_KVBDBBASE_MAGIC );
  //assert( p->base.iTransLevel>=2 ); 
  
  int rc = SQLITE4_OK;
  
  KVBdbCursor * pCsr = NULL;
  pCsr = (KVBdbCursor*)sqlite4_malloc(pKVStore->pEnv, sizeof(KVBdbCursor));
  if (pCsr == 0)
  {
     // sqlite4_malloc failed
     rc = SQLITE4_NOMEM;
  }
  else
  {
     // sqlite4_malloc succeeded
     memset(pCsr, 0, sizeof(KVBdbCursor));
     // retrieve necessary BerkeleyDB objects -- begin
     DB_ENV * envp = p->envp;
     assert(envp != NULL);
     DB * dbp = p->dbp;
     assert(dbp != NULL);
     int nCurrTxnLevel = pKVStore->iTransLevel; // p->base.iTransLevel
     DB_TXN * pCurrTxn = p->pTxn[nCurrTxnLevel];
     // retrieve necessary BerkeleyDB objects -- end
     DBC * pNewBerkeleyDBCursor = NULL;
     int ret = 0;
     if (nCurrTxnLevel == 0)
     {
        pNewBerkeleyDBCursor = p->pCsr;

        if (pNewBerkeleyDBCursor == NULL)
        {
           // (re-)open a cursor for read-only ops
           ret = dbp->cursor(dbp, NULL, &pNewBerkeleyDBCursor, DB_READ_COMMITTED);
           if (ret == 0)
           {
              // success
              // save the (re-)opened cursor for read-nly ops
              p->pCsr = pNewBerkeleyDBCursor;
           }
        }
     } // end of if (nCurrTxnLevel == 0){...}
     else if (nCurrTxnLevel == 1)
     {
        if (pCurrTxn == 0)
        {
           // no BerkeleyDB TXN, handle it similarly to nCurrTxnLevel == 0 // ???
           pNewBerkeleyDBCursor = p->pCsr;

           if (pNewBerkeleyDBCursor == NULL)
           {
              // (re-)open a cursor for read-only ops
              ret = dbp->cursor(dbp, NULL, &pNewBerkeleyDBCursor, DB_READ_COMMITTED);
              if (ret == 0)
              {
                 // success 
                 // save the (re-)opened cursor for read-nly ops
                 p->pCsr = pNewBerkeleyDBCursor;
              }
           }
        }
        else
        {
           // existent BerkeleyDB TXN, handle it similarly to nCurrTxnLevel >= 2 // ??? 
           // open a cursor for current TXN
           ret = dbp->cursor(dbp, pCurrTxn, &pNewBerkeleyDBCursor, DB_READ_COMMITTED);
        }
     } // end of else if (nCurrTxnLevel == 1){...}
     else if (nCurrTxnLevel >= 2)
     {
        assert(pCurrTxn); // It is probably an INTERNAL ERROR, i.e. inconsistent use case ...

        // existent BerkeleyDB TXN, handle it similarly to nCurrTxnLevel >= 2 // ??? 
        // open a cursor for current TXN
        ret = dbp->cursor(dbp, pCurrTxn, &pNewBerkeleyDBCursor, DB_READ_COMMITTED);
     } // end of else if (nCurrTxnLevel >= 2){...}
     switch (ret)
     {
     case 0: // OK
        // Fill members of the base structure sqlite4_kvcursor, 
        // as defined in "sqlite.h.in" file
        pCsr->base.pStore = pKVStore;
        pCsr->base.pStoreVfunc = pKVStore->pStoreVfunc;
        pCsr->base.pEnv = pKVStore->pEnv;
        pCsr->base.iTransLevel = pKVStore->iTransLevel;
        pCsr->base.fTrace = 0; // disable tracing
        // Fill members of the derived structure KVBdbCursor, 
        // as defined in "kvbdb.h" file
        pCsr->pOwner = p; // ptr to the underlying KVStore, i.e. to the structure KVBdb
        pCsr->pCsr = pNewBerkeleyDBCursor; // the underlying BerkeleyDB cursor
        // Cached Key & Data Buffers -- begin
        pCsr->nHasKeyAndDataCached = 0;
        pCsr->pCachedKey = NULL;
        pCsr->nCachedKeySize = 0;
        pCsr->nCachedKeyCapacity = p->nInitialCursorKeyBufferCapacity;
        pCsr->pCachedData = NULL;
        pCsr->nCachedDataSize = 0;
        pCsr->nCachedDataCapacity = p->nInitialCursorDataBufferCapacity;
        // Cached Key & Data Buffers -- end
        //
        pCsr->iMagicKVBdbCur = SQLITE4_KVBDBCUR_MAGIC;
        //
        pCsr->nIsEOF = 0; // EOF not encountered yet 
        pCsr->nLastSeekDir = SEEK_DIR_NONE;
        //
        *ppKVCursor = (KVCursor*)pCsr;
        rc = SQLITE4_OK;
        break;
     case DB_REP_HANDLE_DEAD:
     case DB_REP_LOCKOUT:
        rc = SQLITE4_BUSY; // or SQLITE4_LOCKED ???
        break;
     case EINVAL: // An invalid flag value or parameter was specified. 
        //rc = SQLITE4_ERROR; 
        rc = SQLITE4_MISUSE; // Library used incorrectly (!!!)
        break;
     default:
        rc = SQLITE4_ERROR;
        break;
     }; // end of switch (ret){...}
  } // end of else-block for if-block  if (pCsr == 0){...}
  if (rc != SQLITE4_OK)
  {
     if (rc != SQLITE4_NOMEM)
     {
        // free the useless allocated memory
        sqlite4_free(pKVStore->pEnv, pCsr);
     }
  }
//KVBdbOpenCursor_nomem:
//  return SQLITE4_NOMEM;  
  return rc;
} // end of kvbdbOpenCursor(...){...}

/*
** Reset a cursor
*/
int kvbdbReset(KVCursor *pKVCursor){
  //printf("-----> kvbdbReset()\n");  
    
  KVBdbCursor *pCur;
  KVBdb *p;

  pCur = (KVBdbCursor*)pKVCursor;
  assert( pCur->iMagicKVBdbCur==SQLITE4_KVBDBCUR_MAGIC );
  p = (KVBdb *)(pCur->pOwner);
  assert( p->iMagicKVBdbBase==SQLITE4_KVBDBBASE_MAGIC );
  //assert( p->base.iTransLevel>=2 ); 
  
  // Cached Key & Data Buffers -- begin
  pCur->nCachedKeySize = 0;
  pCur->nCachedDataSize = 0;
  pCur->nHasKeyAndDataCached = 0;
  // Cached Key & Data Buffers -- end

  pCur->nIsEOF = 0; // EOF not encountered yet
  pCur->nLastSeekDir = SEEK_DIR_NONE;

  DBC * pCurrBerkeleyDBCursor = pCur->pCsr; // the underlying BerkeleyDB cursor

  // Q: Do we need to do anything about pCurrBerkeleyDBCursor, 
  //    e.g. scroll it to some first/last/after-last position?
  // A: At the moment we do _NOTHING_ about pCurrBerkeleyDBCursor 
  //    but will see at "running-in", :-).

  return SQLITE4_OK;
} // end of kvbdbReset(...){...}

/*
** Destroy a cursor object
*/
int kvbdbCloseCursor(KVCursor *pKVCursor){
  //printf("-----> kvbdbCloseCursor()\n");  
    
  KVBdbCursor *pCur;
  KVBdb *p;

  pCur = (KVBdbCursor*)pKVCursor;
  assert( pCur->iMagicKVBdbCur==SQLITE4_KVBDBCUR_MAGIC );
  p = (KVBdb *)(pCur->pOwner);
  assert( p->iMagicKVBdbBase==SQLITE4_KVBDBBASE_MAGIC );
  //assert( p->base.iTransLevel>=2 ); 
 
  int rc = SQLITE4_OK;

  // Cached Key & Data Buffers -- begin
  if (pCur->pCachedKey != NULL)
  {
     free(pCur->pCachedKey);
     pCur->pCachedKey = NULL;
     pCur->nCachedKeySize = 0;
     pCur->nCachedKeyCapacity = 0;
  }
  if (pCur->pCachedData != NULL)
  {
     free(pCur->pCachedData);
     pCur->pCachedData = NULL;
     pCur->nCachedDataSize = 0;
     pCur->nCachedDataCapacity = 0;
  }
  pCur->nHasKeyAndDataCached = 0;
  // Cached Key & Data Buffers -- end

  pCur->nIsEOF = 0; // EOF not encountered yet
  pCur->nLastSeekDir = SEEK_DIR_NONE;

  int nCurrTxnLevel = pCur->base.pStore->iTransLevel; // via: KVBdbCursor::sqlite4_kvcursor::sqlite4_kvstore::iTransLevel
  int nCurrOwnerTxnLevel = p->base.iTransLevel; // via: KVBdbCursor::KVBdb::sqlite4_kvstore::iTransLevel 
  assert(nCurrTxnLevel == nCurrOwnerTxnLevel);
  DBC * pCurrBerkeleyDBCursor = pCur->pCsr;
  DBC * pCurrKVBdbBerkeleyDBReadOnlyCursor = p->pCsr;

  if (pCurrBerkeleyDBCursor == pCurrKVBdbBerkeleyDBReadOnlyCursor)
  {
     assert(nCurrTxnLevel <= 1);
     p->pCsr = NULL;
  }

  pCur->pCsr = NULL;

  int ret = pCurrBerkeleyDBCursor->close(pCurrBerkeleyDBCursor);

  switch (ret)
  {
  case 0: // OK
     rc = SQLITE4_OK;
     break;
  case DB_LOCK_DEADLOCK:
  case DB_LOCK_NOTGRANTED:
     rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY // ???
     break;
  case EINVAL: // If the cursor is already closed; or if an invalid flag value or parameter was specified. 
     //rc = SQLITE4_ERROR; 
     rc = SQLITE4_MISUSE; // Library used incorrectly (!!!)
     break;
  default:
     rc = SQLITE4_ERROR;
     break;
  };

  return rc;
} // end of kvbdbCloseCursor(...){...}

/*
** Move a cursor to the next non-deleted node.
*/
int kvbdbNextEntry(KVCursor *pKVCursor){
  //printf("-----> kvbdbNextEntry()\n");  
    
  KVBdbCursor *pCur;
  KVBdb *p;

  pCur = (KVBdbCursor*)pKVCursor;
  assert( pCur->iMagicKVBdbCur==SQLITE4_KVBDBCUR_MAGIC );
  p = (KVBdb *)(pCur->pOwner);
  assert( p->iMagicKVBdbBase==SQLITE4_KVBDBBASE_MAGIC );
  //assert( p->base.iTransLevel>=2 );  
  

  int rc = SQLITE4_OK;

  // Retrievee KVStore and BerkeleyDB "context" variables -- begin
  int nCurrTxnLevel = p->base.iTransLevel;
  DB_TXN * pCurrTxn = p->pTxn[nCurrTxnLevel];
  DB * dbp = p->dbp;
  DBC * pCurrBerkeleyDBCursor = pCur->pCsr;
  // Retrievee KVStore and BerkeleyDB "context" variables -- end

  if( pCur->nLastSeekDir != SEEK_DIR_GE 
     && pCur->nLastSeekDir != SEEK_DIR_EQ 
     && pCur->nLastSeekDir != SEEK_DIR_GT )
  {
     // inconsisten use case 
     rc = SQLITE4_MISMATCH;
  }
  else
  {
     // consistent use case
     if(pCur->nIsEOF)
     {
        // already on EOF 
        // Is this an inconsistent use case?
        rc = SQLITE4_MISUSE; // Library used incorrectly
     }
     else
     {
        // probably so far OK
        //
        // Prepare DBT's
        DBT keyDBT; // dont allocate keyDBT on a heap
        DBT dataDBT; // dont allocate dataDBT on a heap
        memset(&keyDBT, 0, sizeof(DBT));
        memset(&dataDBT, 0, sizeof(DBT));
        //
        int ret = pCurrBerkeleyDBCursor->get(pCurrBerkeleyDBCursor,
           &keyDBT, &dataDBT, DB_NEXT | DB_READ_COMMITTED); // or simply DB_NEXT ???
        switch (ret)
        {
        case 0: 
           rc = SQLITE4_OK;
           pCur->nIsEOF = 0; // false; // ???
           break;
        case DB_NOTFOUND:
           rc = SQLITE4_NOTFOUND;
           pCur->nIsEOF = 1; // true; // ???
           break;
        case DB_LOCK_DEADLOCK:
        case DB_LOCK_NOTGRANTED:
        case  DB_REP_HANDLE_DEAD:
        case DB_REP_LEASE_EXPIRED:
        case DB_REP_LOCKOUT:
           rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
           pCur->nIsEOF = 1; // true; // ???
           break;
        case DB_SECONDARY_BAD: // A secondary index references a nonexistent primary key. 
           rc = SQLITE4_NOTFOUND; // ???
           pCur->nIsEOF = 1; // true; // ???
           break;
        case EINVAL:
           rc = SQLITE4_MISUSE; // Library used incorrectly
           pCur->nIsEOF = 1; // true; // ???
           break;
        default:
           rc = SQLITE4_ERROR; // ???
           pCur->nIsEOF = 1; // true; // ???
           break;
        } // end of switch (ret){...}
     } // end of else block for block : if(nIsEOF){...}
  } // end of else block for block : if( pCur->nLastSeekDir != SEEK_DIR_GE && ...){...}

  // Key/Data Cache invalidation -- begin
  pCur->nHasKeyAndDataCached = 0;
  pCur->nCachedKeySize = 0;
  pCur->nCachedDataSize = 0;
  // Key/Data Cache invalidation -- end
   
  return rc;
} // end of kvbdbNextEntry(...){...}

/*
** Move a cursor to the previous non-deleted node.
*/
int kvbdbPrevEntry(KVCursor *pKVCursor){
  //printf("-----> kvbdbPrevEntry()\n");  
    
  KVBdbCursor *pCur;
  KVBdb *p;

  pCur = (KVBdbCursor*)pKVCursor;
  assert( pCur->iMagicKVBdbCur==SQLITE4_KVBDBCUR_MAGIC );
  p = (KVBdb *)(pCur->pOwner);
  assert( p->iMagicKVBdbBase==SQLITE4_KVBDBBASE_MAGIC );
  //assert( p->base.iTransLevel>=2 );  
  

  int rc = SQLITE4_OK;

  // Retrievee KVStore and BerkeleyDB "context" variables -- begin
  int nCurrTxnLevel = p->base.iTransLevel;
  DB_TXN * pCurrTxn = p->pTxn[nCurrTxnLevel];
  DB * dbp = p->dbp;
  DBC * pCurrBerkeleyDBCursor = pCur->pCsr;
  // Retrievee KVStore and BerkeleyDB "context" variables -- end

  if( pCur->nLastSeekDir != SEEK_DIR_LE 
     && pCur->nLastSeekDir != SEEK_DIR_EQ 
     && pCur->nLastSeekDir != SEEK_DIR_LT )
  {
     // inconsisten use case 
     rc = SQLITE4_MISMATCH;
  }
  else
  {
     // consistent use case
     if(pCur->nIsEOF)
     {
        // already on EOF 
        // Is this an inconsistent use case?
        rc = SQLITE4_MISUSE; // Library used incorrectly
     }
     else
     {
        // probably so far OK
        //
        // Prepare DBT's
        DBT keyDBT; // dont allocate keyDBT on a heap
        DBT dataDBT; // dont allocate dataDBT on a heap
        memset(&keyDBT, 0, sizeof(DBT));
        memset(&dataDBT, 0, sizeof(DBT));
        //
        int ret = pCurrBerkeleyDBCursor->get(pCurrBerkeleyDBCursor,
           &keyDBT, &dataDBT, DB_PREV | DB_READ_COMMITTED); // or simply DB_PREV ???
        switch (ret)
        {
        case 0: 
           rc = SQLITE4_OK;
           pCur->nIsEOF = 0; // false; // ???
           break;
        case DB_NOTFOUND:
           rc = SQLITE4_NOTFOUND;
           pCur->nIsEOF = 1; // true; // ???
           break;
        case DB_LOCK_DEADLOCK:
        case DB_LOCK_NOTGRANTED:
        case  DB_REP_HANDLE_DEAD:
        case DB_REP_LEASE_EXPIRED:
        case DB_REP_LOCKOUT:
           rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
           pCur->nIsEOF = 1; // true; // ???
           break;
        case DB_SECONDARY_BAD: // A secondary index references a nonexistent primary key. 
           rc = SQLITE4_NOTFOUND; // ???
           pCur->nIsEOF = 1; // true; // ???
           break;
        case EINVAL:
           rc = SQLITE4_MISUSE; // Library used incorrectly
           pCur->nIsEOF = 1; // true; // ???
           break;
        default:
           rc = SQLITE4_ERROR; // ???
           pCur->nIsEOF = 1; // true; // ???
           break;
        } // end of switch (ret){...}
     } // end of else block for block : if(nIsEOF){...}
  } // end of else block for block : if( pCur->nLastSeekDir != SEEK_DIR_LE && ...){...}

  // Key/Data Cache invalidation -- begin
  pCur->nHasKeyAndDataCached = 0;
  pCur->nCachedKeySize = 0;
  pCur->nCachedDataSize = 0;
  // Key/Data Cache invalidation -- end
   
  return rc;
} // end of kvbdbPrevEntry(...){...}

/*
** Seek a cursor.
*/
int kvbdbSeekEQ(
   DBC * pCurrBerkeleyDBCursor,
   DBT * pKeyDBT,
   int * pnIsEOF)
{
   //printf("---------->kvbdbSeekEQ(), DBC=%p\n", pCurrBerkeleyDBCursor);

   int rc = SQLITE4_OK;

   DBT dataDBT;
   memset(&dataDBT, 0, sizeof(DBT));

   int ret = 0;

   ret = pCurrBerkeleyDBCursor->get(pCurrBerkeleyDBCursor,
      pKeyDBT, &dataDBT, DB_SET | DB_READ_COMMITTED);

   switch (ret)
   {
   case 0: // OK
      rc = SQLITE4_OK;
      *pnIsEOF = 0; // false
      break;
   case DB_NOTFOUND:
      rc = SQLITE4_NOTFOUND;
      *pnIsEOF = 1; // true; // ???
      break;
   case DB_LOCK_DEADLOCK:
   case DB_LOCK_NOTGRANTED:
   case  DB_REP_HANDLE_DEAD:
   case DB_REP_LEASE_EXPIRED:
   case DB_REP_LOCKOUT:
      rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
      *pnIsEOF = 1; // true; // ???
      break;
   case DB_SECONDARY_BAD: // A secondary index references a nonexistent primary key. 
      rc = SQLITE4_NOTFOUND; // ???
      *pnIsEOF = 1; // true; // ???
      break;
   case EINVAL:
      rc = SQLITE4_MISUSE; // Library used incorrectly
      *pnIsEOF = 1; // true; // ???
      break;
   default:
      rc = SQLITE4_ERROR; // ???
      *pnIsEOF = 1; // true; // ???
      break;
   };

   return rc;
} // end of kvbdbSeekEQ(...){...}

//int kvbdbSeekLE(
//   DBC * pCurrBerkeleyDBCursor,
//   DBT * pKeyDBT,
//   int * pnIsEOF)
//{
//   // STUB !!!
//   //printf("STUB -----> kvbdbSeekLE()\n");
//
//   return SQLITE4_OK;
//} // end of kvbdbSeekLE(...){...}


int kvbdbSeekLE(
   DBC * pCurrBerkeleyDBCursor,
   DBT * pKeyDBT,
   int * pnIsEOF)
{
   //printf("---------->kvbdbSeekLE(), DBC=%p \n", pCurrBerkeleyDBCursor);

   int rc = SQLITE4_OK;

   DBT keyDBT = *pKeyDBT; // original pKeyDBT gets modified and MUST NOT be used as a PATTERN!!!
   DBT dataDBT;
   memset(&dataDBT, 0, sizeof(DBT));

   //int ret = pCurrBerkeleyDBCursor->get(pCurrBerkeleyDBCursor,
   //   pKeyDBT, &dataDBT, DB_SET_RANGE | DB_READ_COMMITTED);

   //printf("kvbdbSeekLE() : ->get() #1, DBC=%p\n", pCurrBerkeleyDBCursor);
   int ret = pCurrBerkeleyDBCursor->get(pCurrBerkeleyDBCursor,
      &keyDBT, &dataDBT, DB_SET_RANGE | DB_READ_COMMITTED);

   switch (ret)
   {
   case 0: // exact or inexact match
      { // scope 0 in case 0 -- begin
         // Retrieve the actual, CURRENT, Key
         // and determine, whether the match 
         // is EXACT or INEXACT.
         DBT auxKeyDBT;
         DBT auxDataDBT;
         memset(&auxKeyDBT, 0, sizeof(DBT));
         memset(&auxDataDBT, 0, sizeof(DBT));

         { // scope 1 in case 0 -- begin
            //printf("kvbdbSeekLE() : ->get() #2, DBC=%p\n", pCurrBerkeleyDBCursor);
            int ret1 = pCurrBerkeleyDBCursor->get(pCurrBerkeleyDBCursor,
               &auxKeyDBT, &auxDataDBT, DB_CURRENT | DB_READ_COMMITTED); // or simply DB_CURRENT ???
            switch (ret1)
            {
            case 0: // retrieval successful, compare size and then content
               if (auxKeyDBT.size != pKeyDBT->size)
               {
                  // sizes differ -- inexact match
                 //printf("ret#1\n");
                  rc = SQLITE4_INEXACT;
               }
               else
               {
                  // sizes equal -- compare contents
                  if (!memcmp(auxKeyDBT.data, pKeyDBT->data, pKeyDBT->size))
                  {
                     // contents identical -- exact match
                    //printf("ret#2\n");
                     rc = SQLITE4_OK;
                  }
                  else
                  {
                     // contents differ -- inexact match
                    //printf("ret#3\n");
                     rc = SQLITE4_INEXACT;
                  }
               }
               if(rc == SQLITE4_INEXACT)
               {
                  // So far Inexact GT match 
                  // We try to find max{key >= pKeyDBT->data}
                  // 
                  // */ We are at the item "smallest-entry-GT-searchKey"
                  // */ Go "left-wards" from the current position.
                  // */ Should we iterate using DB_PREV?
                  // */ Well, do we need to iterate at all?
                  //    **/ We didn't match EXACTly
                  //    **/ Entry "GT Key" existd
                  //    **/ Entry "EQ key" doesn't extst, 
                  //        thus we can simply do DB_PREV once only 
                  //        and we either encounter Entry "LT Key" 
                  //        or EOF, i.e. NOT_FOUND.

                  // ---------- DB_PREV -- begin
                  memset(&auxKeyDBT, 0, sizeof(DBT));
                  memset(&auxDataDBT, 0, sizeof(DBT));
                  //printf("kvbdbSeekLE() : ->get() #3, DBC=%p\n", pCurrBerkeleyDBCursor);
                  int ret2 = pCurrBerkeleyDBCursor->get(pCurrBerkeleyDBCursor,
                     &auxKeyDBT, &auxDataDBT, DB_PREV | DB_READ_COMMITTED); // or simply DB_PREV ???
                  switch (ret2)
                  {
                  case 0: // found INEXACT
                    //printf("ret#4\n");
                     rc = SQLITE4_INEXACT;
                     *pnIsEOF = 0; // false; // ???
                     break;
                  case DB_NOTFOUND:
                    //printf("ret#5\n");
                     rc = SQLITE4_NOTFOUND;
                     *pnIsEOF = 1; // true; // ???
                     break;
                  case DB_LOCK_DEADLOCK:
                  case DB_LOCK_NOTGRANTED:
                  case  DB_REP_HANDLE_DEAD:
                  case DB_REP_LEASE_EXPIRED:
                  case DB_REP_LOCKOUT:
                    //printf("ret#6\n");
                     rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
                     *pnIsEOF = 1; // true; // ???
                     break;
                  case DB_SECONDARY_BAD: // A secondary index references a nonexistent primary key. 
                    //printf("ret#7\n");
                     rc = SQLITE4_NOTFOUND; // ???
                     *pnIsEOF = 1; // true; // ???
                     break;
                  case EINVAL:
                    //printf("ret#8\n");
                     rc = SQLITE4_MISUSE; // Library used incorrectly
                     *pnIsEOF = 1; // true; // ???
                     break;
                  default:
                    //printf("ret#9\n");
                     rc = SQLITE4_ERROR; // ???
                     *pnIsEOF = 1; // true; // ???
                     break;
                  } // end of switch (ret2){...}
                  // ---------- DB_PREV -- end  
               } // end of block : if(rc == SQLITE4_INEXACT){...}
               else
               {
                  // Exact i.e. EQ-like match
                  // We are done.
                 //printf("ret#10 <--- ?\n");
                  *pnIsEOF = 0; // false; // ???
               } // end of else block for block : if(rc == SQLITE4_INEXACT){...}

               break;
            case DB_KEYEMPTY: // the cursor's key/data pair was deleted ...
              //printf("ret#11\n");
               rc = SQLITE4_NOTFOUND;
               //*pnIsEOF = 0; // false; // ??? // PERHAPS DON'T TOUCH THIS!!!
               break;
            case DB_NOTFOUND: // GE no found
               { // scope 0 in case NOTFOUND -- begin
                  // */ Entry "GE searchKey" has not been found.
                  // */ There are apparently no "GE searchKey" entries 
                  //    in a database.
                  // */ Thus ALL key's in a database are "LT searchKey" 
                  //    or the database is EMPTY.
                  // */ Therefore:
                  //    **/ Check DB_LAST entry
                  //        ***/ if it exist, it skould have key < searchKey
                  //             and thus, it would be max{key < searchKey}
                  //        ***/ if it doesn't exist, 
                  //             then the database is EMPTY
                  //
                  // ---------- DB_LAST -- begin
                  memset(&auxKeyDBT, 0, sizeof(DBT));
                  memset(&auxDataDBT, 0, sizeof(DBT));
                  //printf("kvbdbSeekLE() : ->get() #4, DBC=%p\n", pCurrBerkeleyDBCursor);
                  int ret3 = pCurrBerkeleyDBCursor->get(pCurrBerkeleyDBCursor,
                     &auxKeyDBT, &auxDataDBT, DB_LAST | DB_READ_COMMITTED); // or simply DB_PREV ???
                  switch (ret3)
                  {
                  case 0: // found DB_LAST, i.e we found an INEXACT entry "LE searchKey"
                     // cross check contents -- begin
                     if (memcmp(auxKeyDBT.data, pKeyDBT->data, min(auxKeyDBT.size, pKeyDBT->size)) <= 0) // LE
                     {
                        // foundKey LE searchKey -- INEXACT match
                       //printf("ret#12\n");
                        rc = SQLITE4_INEXACT;
                     }
                     else
                     {
                        // foundKey GT searchKey -- NOT MATCH, NOT FOUND, or:
                        // */ some "cursor instability" detected by a cross-check
                        // */ some INTERNAL ERROR detected by a cross-check
                       //printf("ret#13\n");
                        rc = SQLITE4_NOTFOUND;
                     }
                     // cross check contents -- end
                    //printf("ret#14 <--- ?\n");
                     *pnIsEOF = 0; // false; // ???
                     break;
                  case DB_NOTFOUND:
                    //printf("ret#15\n");
                     rc = SQLITE4_NOTFOUND;
                     *pnIsEOF = 1; // true; // ???
                     break;
                  case DB_LOCK_DEADLOCK:
                  case DB_LOCK_NOTGRANTED:
                  case  DB_REP_HANDLE_DEAD:
                  case DB_REP_LEASE_EXPIRED:
                  case DB_REP_LOCKOUT:
                    //printf("ret#16\n");
                     rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
                     *pnIsEOF = 1; // true; // ???
                     break;
                  case DB_SECONDARY_BAD: // A secondary index references a nonexistent primary key.
                    //printf("ret#17\n");
                     rc = SQLITE4_NOTFOUND; // ???
                     *pnIsEOF = 1; // true; // ???
                     break;
                  case EINVAL:
                    //printf("ret#18\n");
                     rc = SQLITE4_MISUSE; // Library used incorrectly
                     *pnIsEOF = 1; // true; // ???
                     break;
                  default:
                    //printf("ret#19\n");
                     rc = SQLITE4_ERROR; // ???
                     *pnIsEOF = 1; // true; // ???
                     break;
                  } // end of switch (ret3){...}
                  // ---------- DB_LAST -- end  
               } // scope 0 in case NOTFOUND -- end  
              ////printf("ret#20 <--- ?\n");
               //rc = SQLITE4_NOTFOUND;
               //*pnIsEOF = 1; // true; // ???
               break;
            case DB_LOCK_DEADLOCK:
            case DB_LOCK_NOTGRANTED:
            case  DB_REP_HANDLE_DEAD:
            case DB_REP_LEASE_EXPIRED:
            case DB_REP_LOCKOUT:
              //printf("ret#21\n");
               rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
               *pnIsEOF = 1; // true; // ???
               break;
            case DB_SECONDARY_BAD: // A secondary index references a nonexistent primary key. 
              //printf("ret#22\n");
               rc = SQLITE4_NOTFOUND; // ???
               *pnIsEOF = 1; // true; // ???
               break;
            case EINVAL:
              //printf("ret#23\n");
               rc = SQLITE4_MISUSE; // Library used incorrectly
               *pnIsEOF = 1; // true; // ???
               break;
            default:
              //printf("ret#24\n");
               rc = SQLITE4_ERROR; // ???
               *pnIsEOF = 1; // true; // ???
               break;
            }; // end of switch (ret1){...}
         } // scope 1 in case 0 -- end
         //rc = SQLITE4_OK; // WTF ???
      } // scope 0 in case 0 -- end
     //printf("ret#25 <--- ?\n");
      *pnIsEOF = 0; // false
      break;
   case DB_NOTFOUND:
     //printf("ret#26\n");
      { // scope NOTFOUND in case NOTFOUND -- begin
         // */ Entry "GE searchKey" has not been found.
         // */ There are apparently no "GE searchKey" entries 
         //    in a database.
         // */ Thus ALL key's in a database are "LT searchKey" 
         //    or the database is EMPTY.
         // */ Therefore:
         //    **/ Check DB_LAST entry
         //        ***/ if it exist, it skould have key < searchKey
         //             and thus, it would be max{key < searchKey}
         //        ***/ if it doesn't exist, 
         //             then the database is EMPTY
         //
         // ---------- DB_LAST -- begin
         int ret4 = 0;
         DBT auxKeyDBT;
         DBT auxDataDBT;
         memset(&auxKeyDBT, 0, sizeof(DBT));
         memset(&auxDataDBT, 0, sizeof(DBT));
         //printf("kvbdbSeekLE() : ->get() #5, DBC=%p\n", pCurrBerkeleyDBCursor);
         ret4 = pCurrBerkeleyDBCursor->get(pCurrBerkeleyDBCursor,
            &auxKeyDBT, &auxDataDBT, DB_LAST | DB_READ_COMMITTED); // or simply DB_PREV ???
         switch (ret4)
         {
         case 0: // found DB_LAST, i.e we found an INEXACT entry "LE searchKey"
            // cross check contents -- begin
            if (memcmp(auxKeyDBT.data, pKeyDBT->data, min(auxKeyDBT.size, pKeyDBT->size)) <= 0) // LE
            {
               // foundKey LE searchKey -- INEXACT match
              //printf("ret#26-12\n");
               rc = SQLITE4_INEXACT;
            }
            else
            {
               // foundKey GT searchKey -- NOT MATCH, NOT FOUND, or:
               // */ some "cursor instability" detected by a cross-check
               // */ some INTERNAL ERROR detected by a cross-check
              //printf("ret#26-13\n");
               rc = SQLITE4_NOTFOUND;
            }
            // cross check contents -- end
           //printf("ret#26-14 <--- ?\n");
            *pnIsEOF = 0; // false; // ???
            break;
         case DB_NOTFOUND:
           //printf("ret#26-15\n");
            rc = SQLITE4_NOTFOUND;
            *pnIsEOF = 1; // true; // ???
            break;
         case DB_LOCK_DEADLOCK:
         case DB_LOCK_NOTGRANTED:
         case  DB_REP_HANDLE_DEAD:
         case DB_REP_LEASE_EXPIRED:
         case DB_REP_LOCKOUT:
           //printf("ret#26-16\n");
            rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
            *pnIsEOF = 1; // true; // ???
            break;
         case DB_SECONDARY_BAD: // A secondary index references a nonexistent primary key.
           //printf("ret#26-17\n");
            rc = SQLITE4_NOTFOUND; // ???
            *pnIsEOF = 1; // true; // ???
            break;
         case EINVAL:
           //printf("ret#26-18\n");
            rc = SQLITE4_MISUSE; // Library used incorrectly
            *pnIsEOF = 1; // true; // ???
            break;
         default:
           //printf("ret#26-19\n");
            rc = SQLITE4_ERROR; // ???
            *pnIsEOF = 1; // true; // ???
            break;
         } // end of switch (ret4){...}
         // ---------- DB_LAST -- end  
      } // scope NOTFOUND in case NOTFOUND -- end
      //rc = SQLITE4_NOTFOUND;
      //*pnIsEOF = 1; // true; // ???
      break;
   case DB_LOCK_DEADLOCK:
   case DB_LOCK_NOTGRANTED:
   case  DB_REP_HANDLE_DEAD:
   case DB_REP_LEASE_EXPIRED:
   case DB_REP_LOCKOUT:
     //printf("ret#27\n");
      rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
      *pnIsEOF = 1; // true; // ???
      break;
   case DB_SECONDARY_BAD: // A secondary index references a nonexistent primary key. 
     //printf("ret#28\n");
      rc = SQLITE4_NOTFOUND; // ???
      *pnIsEOF = 1; // true; // ???
      break;
   case EINVAL:
     //printf("ret#29\n");
      rc = SQLITE4_MISUSE; // Library used incorrectly
      *pnIsEOF = 1; // true; // ???
      break;
   default:
     //printf("ret#30\n");
      rc = SQLITE4_ERROR; // ???
      *pnIsEOF = 1; // true; // ???
      break;
   }; // switch (ret){...}

   return rc;
} // end of kvbdbSeekLE(...){...}


int kvbdbSeekGE(
   DBC * pCurrBerkeleyDBCursor,
   DBT * pKeyDBT,
   int * pnIsEOF)
{
   //printf("---------->kvbdbSeekGE(), DBC=%p \n", pCurrBerkeleyDBCursor);

   int rc = SQLITE4_OK;

   DBT keyDBT = *pKeyDBT; // original pKeyDBT gets modified and MUST NOT be used as a PATTERN!!!
   DBT dataDBT;
   memset(&dataDBT, 0, sizeof(DBT));

   //int ret = pCurrBerkeleyDBCursor->get(pCurrBerkeleyDBCursor, 
   //   pKeyDBT, &dataDBT, DB_SET_RANGE | DB_READ_COMMITTED);

   int ret = pCurrBerkeleyDBCursor->get(pCurrBerkeleyDBCursor,
      &keyDBT, &dataDBT, DB_SET_RANGE | DB_READ_COMMITTED);

   switch (ret)
   {
   case 0: // exact or inexact match
   {
      // Retrieve the actual, CURRENT, Key
      // and determine, whether the match 
      // is EXACT or INEXACT.
      DBT auxKeyDBT;
      DBT auxDataDBT;
      memset(&auxKeyDBT, 0, sizeof(DBT));
      memset(&auxDataDBT, 0, sizeof(DBT));

      {
         int ret1 = pCurrBerkeleyDBCursor->get(pCurrBerkeleyDBCursor,
            &auxKeyDBT, &auxDataDBT, DB_CURRENT | DB_READ_COMMITTED); // or simply DB_CURRENT ???
         switch (ret1)
         {
         case 0: // retrieval successful, compare size and then content
            if (auxKeyDBT.size != pKeyDBT->size)
            {
               // sizes differ -- inexact match
               ////printf("ret#1 : auxKeyDBT.size=%d, pKeyDBT->size=%d\n", auxKeyDBT.size, pKeyDBT->size);
               rc = SQLITE4_INEXACT;
            }
            else
            {
               // sizes equal -- compare contents
               if (!memcmp(auxKeyDBT.data, pKeyDBT->data, pKeyDBT->size))
               {
                  // contents identical -- exact match
                  ////printf("ret#2 : auxKeyDBT.data='%s', auxKeyDBT.size=%d, pKeyDBT->data='%s', pKeyDBT->size=%d\n", auxKeyDBT.data, auxKeyDBT.size, pKeyDBT->data, pKeyDBT->size);
                  rc = SQLITE4_OK;
               }
               else
               {
                  // contents differ -- inexact match
                  ////printf("ret#3 : auxKeyDBT.data='%s', auxKeyDBT.size=%d, pKeyDBT->data='%s', pKeyDBT->size=%d\n", auxKeyDBT.data, auxKeyDBT.size, pKeyDBT->data, pKeyDBT->size);
                  rc = SQLITE4_INEXACT;
               }
            }
            *pnIsEOF = 0; // false; // ???
            break;
         case DB_KEYEMPTY: // the cursor's key/data pair was deleted ...
            rc = SQLITE4_NOTFOUND;
            //*pnIsEOF = 0; // false; // ??? // PERHAPS DON'T TOUCH THIS!!!
            break;
         case DB_NOTFOUND:
            rc = SQLITE4_NOTFOUND;
            *pnIsEOF = 1; // true; // ???
            break;
         case DB_LOCK_DEADLOCK:
         case DB_LOCK_NOTGRANTED:
         case  DB_REP_HANDLE_DEAD:
         case DB_REP_LEASE_EXPIRED:
         case DB_REP_LOCKOUT:
            rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
            *pnIsEOF = 1; // true; // ???
            break;
         case DB_SECONDARY_BAD: // A secondary index references a nonexistent primary key. 
            rc = SQLITE4_NOTFOUND; // ???
            *pnIsEOF = 1; // true; // ???
            break;
         case EINVAL:
            rc = SQLITE4_MISUSE; // Library used incorrectly
            *pnIsEOF = 1; // true; // ???
            break;
         default:
            rc = SQLITE4_ERROR; // ???
            *pnIsEOF = 1; // true; // ???
            break;
         }; // end of switch (ret1){...}
      }
   }
      *pnIsEOF = 0; // false
      break;
   case DB_NOTFOUND:
      rc = SQLITE4_NOTFOUND;
      *pnIsEOF = 1; // true; // ???
      break;
   case DB_LOCK_DEADLOCK:
   case DB_LOCK_NOTGRANTED:
   case  DB_REP_HANDLE_DEAD:
   case DB_REP_LEASE_EXPIRED:
   case DB_REP_LOCKOUT:
      rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
      *pnIsEOF = 1; // true; // ???
      break;
   case DB_SECONDARY_BAD: // A secondary index references a nonexistent primary key. 
      rc = SQLITE4_NOTFOUND; // ???
      *pnIsEOF = 1; // true; // ???
      break;
   case EINVAL:
      rc = SQLITE4_MISUSE; // Library used incorrectly
      *pnIsEOF = 1; // true; // ???
      break;
   default:
      rc = SQLITE4_ERROR; // ???
      *pnIsEOF = 1; // true; // ???
      break;
   }; // switch (ret){...}

   return rc;
} // end of kvbdbSeekGE(...){...}

int kvbdbSeek(
  KVCursor *pKVCursor, 
  const KVByteArray *aKey,
  KVSize nKey,
  int direction
)
{
  //printf("-----> kvbdbSeek(), KVCursor=%p", pKVCursor);
    
  KVBdbCursor *pCur;
  KVBdb *p;

  pCur = (KVBdbCursor*)pKVCursor;
  assert( pCur->iMagicKVBdbCur==SQLITE4_KVBDBCUR_MAGIC );
  p = (KVBdb *)(pCur->pOwner);
  assert( p->iMagicKVBdbBase==SQLITE4_KVBDBBASE_MAGIC );
  //assert( p->base.iTransLevel>=2 ); 
  
  int rc = SQLITE4_OK;

  // Retrievee KVStore and BerkeleyDB "context" variables -- begin
  int nCurrTxnLevel = p->base.iTransLevel;
  DB_TXN * pCurrTxn = p->pTxn[nCurrTxnLevel];
  DB * dbp = p->dbp;
  DBC * pCurrBerkeleyDBCursor = pCur->pCsr;
  //
  //printf(" , DBC=%p\n", pCurrBerkeleyDBCursor);
  // Retrievee KVStore and BerkeleyDB "context" variables -- end

  // Cross-checks -- begin
  //assert(nCurrTxnLevel >= 2);
  //if (nCurrTxnLevel < 2)
  //{
  //   //printf("Internal Error : nCurrTxnLevel < 2\n");
  //   rc = SQLITE4_INTERNAL; // or: SQLITE4_MISUSE
  //}

  //assert(pCurrTxn != NULL);
  //if (pCurrTxn == NULL)
  //{
  //   //printf("Internal Error : pCurrTxn == NULL\n");
  //   rc = SQLITE4_INTERNAL; // or: SQLITE4_MISUSE
  //}

  assert(dbp != NULL);
  if (dbp == NULL)
  {
     //printf("Internal Error : dbp == NULL\n");
     rc = SQLITE4_INTERNAL; // or: SQLITE4_MISUSE
  }

  assert(pCurrBerkeleyDBCursor != NULL);
  if (pCurrBerkeleyDBCursor == NULL)
  {
     //printf("Internal Error : pCurrBerkeleyDBCursor == NULL\n");
     rc = SQLITE4_INTERNAL; // or: SQLITE4_MISUSE
  }
  // Cross-checks -- end

  // Cached Key & Data Buffers -- begin
  pCur->nCachedKeySize = 0;
  pCur->nCachedDataSize = 0;
  pCur->nHasKeyAndDataCached = 0;
  // Cached Key & Data Buffers -- end

  pCur->nIsEOF = 1; // pro-forma EOF
  pCur->nLastSeekDir = SEEK_DIR_NONE;

  // Prepare DBT's
  // At the moment it is not obvious/clear,
  // whether we sould use: 
  // 1/ local, on-stackDBT's
  // 2/ member DBT's, i.e. members of structure KVBdbCursor
  // 3/ bot of above cases 1/ and 2/
  // 4/ something else ... ?
  DBT keyDBT; // dont allocate keyDBT on a heap
  DBT dataDBT; // dont allocate dataDBT on a heap
  memset(&keyDBT, 0, sizeof(DBT));
  memset(&dataDBT, 0, sizeof(DBT));
  keyDBT.data = (void*)aKey;
  keyDBT.size = nKey;

  // Use helper routines to seek in appropriate direction
  if (direction == 0) // exact match requested, seek EQ
  {
     rc = kvbdbSeekEQ(
        pCurrBerkeleyDBCursor,
        &keyDBT,
        &pCur->nIsEOF);
     if(rc == SQLITE4_OK || rc == SQLITE4_INEXACT)
     {
        pCur->nLastSeekDir = SEEK_DIR_EQ;
     }
  }
  else if (direction < 0) // seek LE
  {
     rc = kvbdbSeekLE(
        pCurrBerkeleyDBCursor,
        &keyDBT,
        &pCur->nIsEOF);
     if(rc == SQLITE4_OK || rc == SQLITE4_INEXACT)
     {
        pCur->nLastSeekDir = SEEK_DIR_LE;
     }
  }
  else if (direction > 0) // seek GE
  {
     rc = kvbdbSeekGE(
        pCurrBerkeleyDBCursor,
        &keyDBT,
        &pCur->nIsEOF);
     if(rc == SQLITE4_OK || rc == SQLITE4_INEXACT)
     {
        pCur->nLastSeekDir = SEEK_DIR_GE;
     }
  }

  return rc;
} // end of kvbdbSeek(...){...}

/*
** Delete the entry that the cursor is pointing to.
**
** Though the entry is "deleted", it still continues to exist as a
** phantom.  Subsequent xNext or xPrev calls will work, as will
** calls to xKey and xData, thought the result from xKey and xData
** are undefined.
*/
int kvbdbDelete(KVCursor *pKVCursor)
{
  //printf("-----> kvbdbDelete()\n");  
    
  KVBdbCursor *pCur;
  KVBdb *p;

  pCur = (KVBdbCursor*)pKVCursor;
  assert( pCur->iMagicKVBdbCur==SQLITE4_KVBDBCUR_MAGIC );
  p = (KVBdb *)(pCur->pOwner);
  assert( p->iMagicKVBdbBase==SQLITE4_KVBDBBASE_MAGIC );
  //assert( p->base.iTransLevel>=2 ); 

  int rc = SQLITE4_OK;

  // Retrievee KVStore and BerkeleyDB "context" variables -- begin
  int nCurrTxnLevel = p->base.iTransLevel;
  DB_TXN * pCurrTxn = p->pTxn[nCurrTxnLevel];
  DB * dbp = p->dbp;
  DBC * pCurrBerkeleyDBCursor = pCur->pCsr;
  // Retrievee KVStore and BerkeleyDB "context" variables -- end

  // Cross-checks -- begin
  assert(nCurrTxnLevel >= 2);
  if (nCurrTxnLevel < 2)
  {
     //printf("Internal Error : nCurrTxnLevel < 2\n");
     rc = SQLITE4_INTERNAL; // or: SQLITE4_MISUSE
  }

  assert(pCurrTxn != NULL);
  if (pCurrTxn == NULL)
  {
     //printf("Internal Error : pCurrTxn == NULL\n");
     rc = SQLITE4_INTERNAL; // or: SQLITE4_MISUSE
  }

  assert(dbp != NULL);
  if (dbp == NULL)
  {
     //printf("Internal Error : dbp == NULL\n");
     rc = SQLITE4_INTERNAL; // or: SQLITE4_MISUSE
  }

  assert(pCurrBerkeleyDBCursor != NULL);
  if (pCurrBerkeleyDBCursor == NULL)
  {
     //printf("Internal Error : pCurrBerkeleyDBCursor == NULL\n");
     rc = SQLITE4_INTERNAL; // or: SQLITE4_MISUSE
  }
  // Cross-checks -- end

  if (rc == SQLITE4_OK)
  {
     // So far OK

     int ret = 0;

     ret = pCurrBerkeleyDBCursor->del(pCurrBerkeleyDBCursor, 0);

     switch (ret)
     {
     case 0: // OK
        rc = SQLITE4_OK;
        break;
     case DB_FOREIGN_CONFLICT: // constraint violation
        rc = SQLITE4_CONSTRAINT;
        break;
     case DB_LOCK_DEADLOCK:
     case DB_LOCK_NOTGRANTED:
     case  DB_REP_HANDLE_DEAD:
     case DB_REP_LOCKOUT:
        rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
        break;
     case DB_SECONDARY_BAD: // A secondary index references a nonexistent primary key. 
        rc = SQLITE4_CONSTRAINT; // Abort due to constraint violation
        break;
     case EACCES:              // An attempt was made to modify a read-only database. 
        rc = SQLITE4_READONLY; // Attempt to write a readonly database 
        break;
     case EINVAL:
        rc = SQLITE4_MISUSE; // Library used incorrectly (!!!)
        break;
     case EPERM:              // Write attempted on read-only cursor when the DB_INIT_CDB flag was specified to DB_ENV->open().  
        rc = SQLITE4_READONLY; // Attempt to write a readonly database 
        break;
     default:
        rc = SQLITE4_ERROR;
        break;
     };  // end of switch (ret){...}
  } // end of : if (rc == SQLITE4_OK){...}

  return rc;
} // end of kvbdbDelete(...){...}

/*
** Return the key of the node the cursor is pointing to.
*/
int kvbdbKey(
  KVCursor *pKVCursor,         /* The cursor whose key is desired */
  const KVByteArray **paKey,   /* Make this point to the key */
  KVSize *pN                   /* Make this point to the size of the key */
){
  //printf("-----> kvbdbKey(), KVCursor=%p", pKVCursor);
    
  KVBdbCursor *pCur;
  KVBdb *p;

  pCur = (KVBdbCursor*)pKVCursor;
  assert( pCur->iMagicKVBdbCur==SQLITE4_KVBDBCUR_MAGIC );
  p = (KVBdb *)(pCur->pOwner);
  assert( p->iMagicKVBdbBase==SQLITE4_KVBDBBASE_MAGIC );
    
   int rc = SQLITE4_OK;

  // Retrievee KVStore and BerkeleyDB "context" variables -- begin
  int nCurrTxnLevel = p->base.iTransLevel;
  DB_TXN * pCurrTxn = p->pTxn[nCurrTxnLevel];
  DB * dbp = p->dbp;
  DBC * pCurrBerkeleyDBCursor = pCur->pCsr;
  //
  //printf(" , DBC=%p\n", pCurrBerkeleyDBCursor);
  // Retrievee KVStore and BerkeleyDB "context" variables -- end

  if(pCur->nHasKeyAndDataCached)
  {
     // Cached Key and Data present
     // 
     // we expect consistent cache, 
     // i.e. non-NULL key buffer ptr
     if(pCur->pCachedKey != NULL)
     {
        // consistent cache
        *paKey = (KVByteArray *)(pCur->pCachedKey);
        *pN = (KVSize)(pCur->nCachedKeySize);
        //printf("key: #1\n");
        rc = SQLITE4_OK;
     }
     else
     {
        // inconsistent cache 
        // */ Library used incorrectly ?
        // */ some internal error ?
        //printf("key: #2\n");
        rc = SQLITE4_MISUSE; // or: SQLITE4_INTERNAL // ???
     } // end of else block for block : if(pCur->pCachedKey != NULL)
  }
  else
  {
     // No Cached Key and Data
     //
     // we check whether we have 
     // allocated buffers 
     // for key and data
     if(pCur->pCachedKey == NULL)
     {
        pCur->pCachedKey = malloc(p->nInitialCursorKeyBufferCapacity);
        if(pCur->pCachedKey == NULL)
        {
           //printf("key: #3\n");
           rc = SQLITE4_NOMEM;
           goto label_nomem;
        }
        pCur->nCachedKeySize = 0;
        pCur->nCachedKeyCapacity = p->nInitialCursorKeyBufferCapacity;
     }

     if(pCur->pCachedData == NULL)
     {
        pCur->pCachedData = malloc(p->nInitialCursorDataBufferCapacity);
        if(pCur->pCachedData == NULL)
        {
           //printf("key: #4\n");
           rc = SQLITE4_NOMEM;
           goto label_nomem;
        }
        pCur->nCachedDataSize = 0;
        pCur->nCachedDataCapacity = p->nInitialCursorDataBufferCapacity;
     }
     
     // Try to retrieve <key, data>, re-allocate buffers if necessary -- begin
     // Prepare DBT's
     DBT keyDBT; // dont allocate keyDBT on a heap
     DBT dataDBT; // dont allocate dataDBT on a heap
     memset(&keyDBT, 0, sizeof(DBT));
     memset(&dataDBT, 0, sizeof(DBT));
     keyDBT.data = pCur->pCachedKey;
     keyDBT.ulen = pCur->nCachedKeyCapacity;
     keyDBT.flags = DB_DBT_USERMEM;
     dataDBT.data = pCur->pCachedData;
     dataDBT.ulen = pCur->nCachedDataCapacity;
     dataDBT.flags = DB_DBT_USERMEM;

     //printf("kvbdbKey() : ->get() #1, DBC=%p, keyDBT.data=%p, keyDBT.ulen=%d, dataDBT.data=%p, dataDBT.ulen=%d\n", pCurrBerkeleyDBCursor, keyDBT.data, keyDBT.ulen, dataDBT.data, dataDBT.ulen);
     int ret = pCurrBerkeleyDBCursor->get(pCurrBerkeleyDBCursor,
               &keyDBT, &dataDBT, DB_CURRENT | DB_READ_COMMITTED); // or simply DB_CURRENT ???
     switch (ret)
     {
     case 0: // OK
        // Cache data in pCur -- begin
        pCur->pCachedKey = keyDBT.data; // I know it's redundant.
        pCur->nCachedKeySize = keyDBT.size;
        pCur->pCachedData = dataDBT.data; // I know it's redundant.
        pCur->nCachedDataSize = dataDBT.size;
        pCur->nHasKeyAndDataCached = 1;
        ////printf("-----> kvbdbKey() : ---------------> keyDBT.data='%s', keyDBT.size=%d\n", (char*)(keyDBT.data), keyDBT.size);
        ////printf("-----> kvbdbKey() : ---------------> dataDBT.data='%s', dataDBT.size=%d\n", (char*)(dataDBT.data), dataDBT.size);
        // Cache data in pCur -- end
        //
        // Prepare return -- begin
        *paKey = (KVByteArray *)(pCur->pCachedKey);
        *pN = (KVSize)(pCur->nCachedKeySize);
        //printf("key: #5\n");
        rc = SQLITE4_OK;
        // Prepare return -- end  
        // 
        pCur->nIsEOF = 0; // false; // ???
        break;
     case DB_KEYEMPTY: // the cursor's key/data pair was deleted ...
        // invalidate cache -- begin
        pCur->nHasKeyAndDataCached = 0;
        pCur->nCachedKeySize = 0;
        pCur->nCachedDataSize = 0;
        // invalidate cache -- end
        //printf("key: #6\n");
        rc = SQLITE4_NOTFOUND;
        pCur->nIsEOF = 1; // true; // ???
        break;
     case DB_NOTFOUND:
        // invalidate cache -- begin
        pCur->nHasKeyAndDataCached = 0;
        pCur->nCachedKeySize = 0;
        pCur->nCachedDataSize = 0;
        // invalidate cache -- end
        //printf("key: #7\n");
        rc = SQLITE4_NOTFOUND;
        pCur->nIsEOF = 1; // true; // ???
        break;
     case DB_LOCK_DEADLOCK:
     case DB_LOCK_NOTGRANTED:
     case  DB_REP_HANDLE_DEAD:
     case DB_REP_LEASE_EXPIRED:
     case DB_REP_LOCKOUT:
        // invalidate cache -- begin
        pCur->nHasKeyAndDataCached = 0;
        pCur->nCachedKeySize = 0;
        pCur->nCachedDataSize = 0;
        // invalidate cache -- end
        //printf("key: #8\n");
        rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
        pCur->nIsEOF = 1; // true; // ???
        break;
     case DB_SECONDARY_BAD: // A secondary index references a nonexistent primary key. 
        // invalidate cache -- begin
        pCur->nHasKeyAndDataCached = 0;
        pCur->nCachedKeySize = 0;
        pCur->nCachedDataSize = 0;
        // invalidate cache -- end
        //printf("key: #9\n");
        rc = SQLITE4_NOTFOUND; // ???
        pCur->nIsEOF = 1; // true; // ???
        break;
     case EINVAL:
        // invalidate cache -- begin
        pCur->nHasKeyAndDataCached = 0;
        pCur->nCachedKeySize = 0;
        pCur->nCachedDataSize = 0;
        // invalidate cache -- end
        //printf("key: #10\n");
        rc = SQLITE4_MISUSE; // Library used incorrectly
        pCur->nIsEOF = 1; // true; // ???
        break;
     case DB_BUFFER_SMALL: // The requested item could not be returned due to undersized buffer. 
        { // case DB_BUFFER_SMALL -- begin
           // Some buffer, for key, for data or both, was too small.
           // We don't know, which buffer caused a problem.
           // We will ask BerkeleyDB about required minimum size for both buffers.
           // Applications can determine the length of a record 
           // by setting the ulen field to 0 and checking 
           // the return value in the size field.
           // The question is, whether we should iterate until we succeede or only once?
           // To make the code more robust, I will "chance" to iterate, :-).
           //
           int ret1 = 0;
           // 
           do // iterate : while(ret1 == DB_BUFFER_SMALL){...}
           {
              // Applications can determine the length of a record 
              // by setting the ulen field to 0 and checking 
              // the return value in the size field.
              keyDBT.size = 0;
              keyDBT.ulen = 0;
              dataDBT.size = 0;
              dataDBT.ulen = 0;
              //
              //printf("kvbdbKey() : ->get() #2, DBC=%p\n", pCurrBerkeleyDBCursor);
              ret1 = pCurrBerkeleyDBCursor->get(pCurrBerkeleyDBCursor,
                 &keyDBT, &dataDBT, DB_CURRENT | DB_READ_COMMITTED); // or simply DB_CURRENT ???
              if(ret1 != 0)
              {
                 // retrieval failed
                 //
                 // invalidate cache -- begin
                 pCur->nHasKeyAndDataCached = 0;
                 pCur->nCachedKeySize = 0;
                 pCur->nCachedDataSize = 0;
                 // invalidate cache -- end
                 //printf("key: #11\n");
                 rc = SQLITE4_ERROR; // ??? 
                 //*pnIsEOF = 1; // true; // ???
              }
              else
              {
                 // retrieval succeeded 
                 //
                 // resize buffer for key
                 if(keyDBT.size > pCur->nCachedKeyCapacity)
                 {
                    // need more buffer for key
                    // 
                    // free current buffer
                    free(pCur->pCachedKey);
                    pCur->pCachedKey = 0;
                    pCur->nCachedKeySize = 0;
                    //
                    // calculate the new buffer capacity 
                    // by multiplying-by-2
                    while(keyDBT.size > pCur->nCachedKeyCapacity)
                    {
                       pCur->nCachedKeyCapacity = pCur->nCachedKeyCapacity * 2;
                    }
                    // 
                    // allocate a new buffer for key
                    pCur->pCachedKey = malloc(pCur->nCachedKeyCapacity);
                    if(pCur->pCachedKey == NULL)
                    {
                       pCur->nCachedKeyCapacity = 0;
                       //printf("key: #12\n");
                       rc = SQLITE4_NOMEM;
                       goto label_nomem;
                    }
                    keyDBT.ulen = pCur->nCachedKeyCapacity;
                    keyDBT.flags = DB_DBT_USERMEM;
                 } // end of else of block : if(keyDBT.size > pCur->nCachedKeyCapacity){...}
                 //
                 // resize buffer for data
                 if(dataDBT.size > pCur->nCachedDataCapacity)
                 {
                    // need more buffer for data
                    // 
                    // free current buffer
                    free(pCur->pCachedData);
                    pCur->pCachedData = 0;
                    pCur->nCachedDataSize = 0;
                    //
                    // calculate the new buffer capacity 
                    // by multiplying-by-2
                    while(dataDBT.size > pCur->nCachedDataCapacity)
                    {
                       pCur->nCachedDataCapacity = pCur->nCachedDataCapacity * 2;
                    }
                    // 
                    // allocate a new buffer for data
                    pCur->pCachedData = malloc(pCur->nCachedDataCapacity);
                    if(pCur->pCachedData == NULL)
                    {
                       pCur->nCachedDataCapacity = 0;
                       //printf("key: #13\n");
                       rc = SQLITE4_NOMEM;
                       goto label_nomem;
                    }
                    dataDBT.ulen = pCur->nCachedDataCapacity;
                    dataDBT.flags = DB_DBT_USERMEM;
                 } // end of else of block : if(dataDBT.size > pCur->nCachedDataCapacity){...}
                 // 
                 // try to retrieve key and data again
                 //printf("kvbdbKey() : ->get() #3, DBC=%p\n", pCurrBerkeleyDBCursor);
                 ret1 = pCurrBerkeleyDBCursor->get(pCurrBerkeleyDBCursor,
                    &keyDBT, &dataDBT, DB_CURRENT | DB_READ_COMMITTED); // or simply DB_CURRENT ???
                 switch (ret1)
                 {
                 case 0: // OK
                    // This time we succeeded to retrieve 
                    // key and data from cursor's current position.
                    // 
                    // Cache data in pCur -- begin
                    pCur->pCachedKey = keyDBT.data; // I know it's redundant.
                    pCur->nCachedKeySize = keyDBT.size;
                    pCur->pCachedData = dataDBT.data; // I know it's redundant.
                    pCur->nCachedDataSize = dataDBT.size;
                    pCur->nHasKeyAndDataCached = 1;
                    // Cache data in pCur -- end
                    //
                    // Prepare return -- begin
                    *paKey = (KVByteArray *)(pCur->pCachedKey);
                    *pN = (KVSize)(pCur->nCachedKeySize);
                    //printf("key: #14\n");
                    rc = SQLITE4_OK;
                    // Prepare return -- end  
                    // 
                    pCur->nIsEOF = 0; // false; // ???
                    break;
                 case DB_KEYEMPTY: // the cursor's key/data pair was deleted ...
                    // invalidate cache -- begin
                    pCur->nHasKeyAndDataCached = 0;
                    pCur->nCachedKeySize = 0;
                    pCur->nCachedDataSize = 0;
                    // invalidate cache -- end
                    //printf("key: #15\n");
                    rc = SQLITE4_NOTFOUND;
                    pCur->nIsEOF = 1; // true; // ???
                    break;
                 case DB_NOTFOUND:
                    // invalidate cache -- begin
                    pCur->nHasKeyAndDataCached = 0;
                    pCur->nCachedKeySize = 0;
                    pCur->nCachedDataSize = 0;
                    // invalidate cache -- end
                    //printf("key: #16\n");
                    rc = SQLITE4_NOTFOUND;
                    pCur->nIsEOF = 1; // true; // ???
                    break;
                 case DB_LOCK_DEADLOCK:
                 case DB_LOCK_NOTGRANTED:
                 case  DB_REP_HANDLE_DEAD:
                 case DB_REP_LEASE_EXPIRED:
                 case DB_REP_LOCKOUT:
                    // invalidate cache -- begin
                    pCur->nHasKeyAndDataCached = 0;
                    pCur->nCachedKeySize = 0;
                    pCur->nCachedDataSize = 0;
                    // invalidate cache -- end
                    //printf("key: #17\n");
                    rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
                    pCur->nIsEOF = 1; // true; // ???
                    break;
                 case DB_SECONDARY_BAD: // A secondary index references a nonexistent primary key. 
                    // invalidate cache -- begin
                    pCur->nHasKeyAndDataCached = 0;
                    pCur->nCachedKeySize = 0;
                    pCur->nCachedDataSize = 0;
                    // invalidate cache -- end
                    //printf("key: #18\n");
                    rc = SQLITE4_NOTFOUND; // ???
                    pCur->nIsEOF = 1; // true; // ???
                    break;
                 case EINVAL:
                    // invalidate cache -- begin
                    pCur->nHasKeyAndDataCached = 0;
                    pCur->nCachedKeySize = 0;
                    pCur->nCachedDataSize = 0;
                    // invalidate cache -- end
                    //printf("key: #19\n");
                    rc = SQLITE4_MISUSE; // Library used incorrectly
                    pCur->nIsEOF = 1; // true; // ???
                    break;
                 case DB_BUFFER_SMALL: // The requested item could not be returned due to undersized buffer. 
                    // continue iteration !!!
                    //printf("key: #20\n");
                    rc = SQLITE4_OK; // continue iteration , :-) !!!
                    break;
                 default:
                    // invalidate cache -- begin
                    pCur->nHasKeyAndDataCached = 0;
                    pCur->nCachedKeySize = 0;
                    pCur->nCachedDataSize = 0;
                    // invalidate cache -- end
                    //printf("key: #21\n");
                    rc = SQLITE4_ERROR; // ???
                    pCur->nIsEOF = 1; // true; // ???
                    break;
                 }; // end of switch (ret1){...}
              } // end of else of block : if(ret1 != 0) {...}
           //} while(rc = SQLITE4_OK && ret1 == DB_BUFFER_SMALL);
           } while (rc == SQLITE4_OK && ret1 == DB_BUFFER_SMALL);
        } // case DB_BUFFER_SMALL -- end
        break;
     default:
        // invalidate cache -- begin
        pCur->nHasKeyAndDataCached = 0;
        pCur->nCachedKeySize = 0;
        pCur->nCachedDataSize = 0;
        // invalidate cache -- end
        //printf("key: #22\n");
        rc = SQLITE4_ERROR; // ???
        pCur->nIsEOF = 1; // true; // ???
        break;
     } // end of switch (ret){...}
     // Try to retrieve <key, data>, re-allocate buffers if necessary -- end

  } // end of else block for block : if(pCur->nHasKeyAndDataCached){...}

label_nomem:
  if(rc == SQLITE4_NOMEM)
  {
     if(pCur->pCachedKey != NULL)
     {
        free(pCur->pCachedKey);
        pCur->pCachedKey = 0;
        pCur->nCachedKeySize = 0;
        pCur->nCachedKeyCapacity = 0;
     }
     if(pCur->pCachedData != NULL)
     {
        free(pCur->pCachedData);
        pCur->pCachedData = 0;
        pCur->nCachedDataSize = 0;
        pCur->nCachedDataCapacity = 0;
     }
     pCur->nHasKeyAndDataCached = 0;
  }

  return rc;
} // end of kvbdbKey(...){...}

/*
** Return the data of the node the cursor is pointing to.
*/
int kvbdbData(
  KVCursor *pKVCursor,         /* The cursor from which to take the data */
  KVSize ofst,                 /* Offset into the data to begin reading */
  KVSize n,                    /* Number of bytes requested */
  const KVByteArray **paData,  /* Pointer to the data written here */
  KVSize *pNData               /* Number of bytes delivered */
){
  //printf("-----> kvbdbData()\n");  
    
  KVBdbCursor *pCur;
  KVBdb *p;

  pCur = (KVBdbCursor*)pKVCursor;
  assert( pCur->iMagicKVBdbCur==SQLITE4_KVBDBCUR_MAGIC );
  p = (KVBdb *)(pCur->pOwner);
  assert( p->iMagicKVBdbBase==SQLITE4_KVBDBBASE_MAGIC );
    
   int rc = SQLITE4_OK;

  // Retrievee KVStore and BerkeleyDB "context" variables -- begin
  int nCurrTxnLevel = p->base.iTransLevel;
  DB_TXN * pCurrTxn = p->pTxn[nCurrTxnLevel];
  DB * dbp = p->dbp;
  DBC * pCurrBerkeleyDBCursor = pCur->pCsr;
  // Retrievee KVStore and BerkeleyDB "context" variables -- end

  if(pCur->nHasKeyAndDataCached)
  {
     // Cached Key and Data present
     // 
     // we expect consistent cache, 
     // i.e. non-NULL key buffer ptr
     if(pCur->pCachedData != NULL)
     {
        // consistent cache
        //*paData = (KVByteArray *)(pCur->pCachedData);
        //*pNData = (KVSize)(pCur->nCachedDataSize);
        if (n<0) 
        {
           *paData = pCur->pCachedData;
           *pNData = pCur->nCachedDataSize;
        }
        else 
        {
           int nOut = n;
           if ((ofst + n)>pCur->nCachedDataSize) nOut = pCur->nCachedDataSize - ofst;
           if (nOut<0) nOut = 0;

           *paData = &((u8 *)(pCur->pCachedData))[ofst];
           *pNData = nOut;
        }
        rc = SQLITE4_OK;
     }
     else
     {
        // inconsistent cache 
        // */ Library used incorrectly ?
        // */ some internal error ?
        rc = SQLITE4_MISUSE; // or: SQLITE4_INTERNAL // ???
     } // end of else block for block : if(pCur->pCachedData != NULL)
  }
  else
  {
     // No Cached Key and Data
     //
     // we check whether we have 
     // allocated buffers 
     // for key and data
     if(pCur->pCachedKey == NULL)
     {
        pCur->pCachedKey = malloc(p->nInitialCursorKeyBufferCapacity);
        if(pCur->pCachedKey == NULL)
        {
           rc = SQLITE4_NOMEM;
           goto label_nomem;
        }
        pCur->nCachedKeySize = 0;
        pCur->nCachedKeyCapacity = p->nInitialCursorKeyBufferCapacity;
     }

     if(pCur->pCachedData == NULL)
     {
        pCur->pCachedData = malloc(p->nInitialCursorDataBufferCapacity);
        if(pCur->pCachedData == NULL)
        {
           rc = SQLITE4_NOMEM;
           goto label_nomem;
        }
        pCur->nCachedDataSize = 0;
        pCur->nCachedDataCapacity = p->nInitialCursorDataBufferCapacity;
     }
     
     // Try to retrieve <key, data>, re-allocate buffers if necessary -- begin
     // Prepare DBT's
     DBT keyDBT; // dont allocate keyDBT on a heap
     DBT dataDBT; // dont allocate dataDBT on a heap
     memset(&keyDBT, 0, sizeof(DBT));
     memset(&dataDBT, 0, sizeof(DBT));
     keyDBT.data = pCur->pCachedKey;
     keyDBT.ulen = pCur->nCachedKeyCapacity;
     keyDBT.flags = DB_DBT_USERMEM;
     dataDBT.data = pCur->pCachedData;
     dataDBT.ulen = pCur->nCachedDataCapacity;
     dataDBT.flags = DB_DBT_USERMEM;

     int ret = pCurrBerkeleyDBCursor->get(pCurrBerkeleyDBCursor,
               &keyDBT, &dataDBT, DB_CURRENT | DB_READ_COMMITTED); // or simply DB_CURRENT ???
     switch (ret)
     {
     case 0: // OK
        // Cache data in pCur -- begin
        pCur->pCachedKey = keyDBT.data; // I know it's redundant.
        pCur->nCachedKeySize = keyDBT.size;
        pCur->pCachedData = dataDBT.data; // I know it's redundant.
        pCur->nCachedDataSize = dataDBT.size;
        pCur->nHasKeyAndDataCached = 1;
        // Cache data in pCur -- end
        //
        // Prepare return -- begin
        if (n<0)
        {
           *paData = pCur->pCachedData;
           *pNData = pCur->nCachedDataSize;
        }
        else
        {
           int nOut = n;
           if ((ofst + n)>pCur->nCachedDataSize) nOut = pCur->nCachedDataSize - ofst;
           if (nOut<0) nOut = 0;

           *paData = &((u8 *)(pCur->pCachedData))[ofst];
           *pNData = nOut;
        }
        rc = SQLITE4_OK;
        // Prepare return -- end  
        // 
        pCur->nIsEOF = 0; // false; // ???
        break;
     case DB_KEYEMPTY: // the cursor's key/data pair was deleted ...
        // invalidate cache -- begin
        pCur->nHasKeyAndDataCached = 0;
        pCur->nCachedKeySize = 0;
        pCur->nCachedDataSize = 0;
        // invalidate cache -- end
        rc = SQLITE4_NOTFOUND;
        pCur->nIsEOF = 1; // true; // ???
        break;
     case DB_NOTFOUND:
        // invalidate cache -- begin
        pCur->nHasKeyAndDataCached = 0;
        pCur->nCachedKeySize = 0;
        pCur->nCachedDataSize = 0;
        // invalidate cache -- end
        rc = SQLITE4_NOTFOUND;
        pCur->nIsEOF = 1; // true; // ???
        break;
     case DB_LOCK_DEADLOCK:
     case DB_LOCK_NOTGRANTED:
     case  DB_REP_HANDLE_DEAD:
     case DB_REP_LEASE_EXPIRED:
     case DB_REP_LOCKOUT:
        // invalidate cache -- begin
        pCur->nHasKeyAndDataCached = 0;
        pCur->nCachedKeySize = 0;
        pCur->nCachedDataSize = 0;
        // invalidate cache -- end
        rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
        pCur->nIsEOF = 1; // true; // ???
        break;
     case DB_SECONDARY_BAD: // A secondary index references a nonexistent primary key. 
        // invalidate cache -- begin
        pCur->nHasKeyAndDataCached = 0;
        pCur->nCachedKeySize = 0;
        pCur->nCachedDataSize = 0;
        // invalidate cache -- end
        rc = SQLITE4_NOTFOUND; // ???
        pCur->nIsEOF = 1; // true; // ???
        break;
     case EINVAL:
        // invalidate cache -- begin
        pCur->nHasKeyAndDataCached = 0;
        pCur->nCachedKeySize = 0;
        pCur->nCachedDataSize = 0;
        // invalidate cache -- end
        rc = SQLITE4_MISUSE; // Library used incorrectly
        pCur->nIsEOF = 1; // true; // ???
        break;
     case DB_BUFFER_SMALL: // The requested item could not be returned due to undersized buffer. 
        { // case DB_BUFFER_SMALL -- begin
           // Some buffer, for key, for data or both, was too small.
           // We don't know, which buffer caused a problem.
           // We will ask BerkeleyDB about required minimum size for both buffers.
           // Applications can determine the length of a record 
           // by setting the ulen field to 0 and checking 
           // the return value in the size field.
           // The question is, whether we should iterate until we succeede or only once?
           // To make the code more robust, I will "chance" to iterate, :-).
           //
           int ret1 = 0;
           // 
           do // iterate : while(ret1 == DB_BUFFER_SMALL){...}
           {
              // Applications can determine the length of a record 
              // by setting the ulen field to 0 and checking 
              // the return value in the size field.
              keyDBT.size = 0;
              keyDBT.ulen = 0;
              dataDBT.size = 0;
              dataDBT.ulen = 0;
              //
              ret1 = pCurrBerkeleyDBCursor->get(pCurrBerkeleyDBCursor,
                 &keyDBT, &dataDBT, DB_CURRENT | DB_READ_COMMITTED); // or simply DB_CURRENT ???
              if(ret1 != 0)
              {
                 // retrieval failed
                 //
                 // invalidate cache -- begin
                 pCur->nHasKeyAndDataCached = 0;
                 pCur->nCachedKeySize = 0;
                 pCur->nCachedDataSize = 0;
                 // invalidate cache -- end
                 rc = SQLITE4_ERROR; // ??? 
                 //*pnIsEOF = 1; // true; // ???
              }
              else
              {
                 // retrieval succeeded 
                 //
                 // resize buffer for key
                 if(keyDBT.size > pCur->nCachedKeyCapacity)
                 {
                    // need more buffer for key
                    // 
                    // free current buffer
                    free(pCur->pCachedKey);
                    pCur->pCachedKey = 0;
                    pCur->nCachedKeySize = 0;
                    //
                    // calculate the new buffer capacity 
                    // by multiplying-by-2
                    while(keyDBT.size > pCur->nCachedKeyCapacity)
                    {
                       pCur->nCachedKeyCapacity = pCur->nCachedKeyCapacity * 2;
                    }
                    // 
                    // allocate a new buffer for key
                    pCur->pCachedKey = malloc(pCur->nCachedKeyCapacity);
                    if(pCur->pCachedKey == NULL)
                    {
                       pCur->nCachedKeyCapacity = 0;
                       rc = SQLITE4_NOMEM;
                       goto label_nomem;
                    }
                    keyDBT.ulen = pCur->nCachedKeyCapacity;
                    keyDBT.flags = DB_DBT_USERMEM;
                 } // end of else of block : if(keyDBT.size > pCur->nCachedKeyCapacity){...}
                 //
                 // resize buffer for data
                 if(dataDBT.size > pCur->nCachedDataCapacity)
                 {
                    // need more buffer for data
                    // 
                    // free current buffer
                    free(pCur->pCachedData);
                    pCur->pCachedData = 0;
                    pCur->nCachedDataSize = 0;
                    //
                    // calculate the new buffer capacity 
                    // by multiplying-by-2
                    while(dataDBT.size > pCur->nCachedDataCapacity)
                    {
                       pCur->nCachedDataCapacity = pCur->nCachedDataCapacity * 2;
                    }
                    // 
                    // allocate a new buffer for data
                    pCur->pCachedData = malloc(pCur->nCachedDataCapacity);
                    if(pCur->pCachedData == NULL)
                    {
                       pCur->nCachedDataCapacity = 0;
                       rc = SQLITE4_NOMEM;
                       goto label_nomem;
                    }
                    dataDBT.ulen = pCur->nCachedDataCapacity;
                    dataDBT.flags = DB_DBT_USERMEM;
                 } // end of else of block : if(dataDBT.size > pCur->nCachedDataCapacity){...}
                 // 
                 // try to retrieve key and data again
                 ret1 = pCurrBerkeleyDBCursor->get(pCurrBerkeleyDBCursor,
                    &keyDBT, &dataDBT, DB_CURRENT | DB_READ_COMMITTED); // or simply DB_CURRENT ???
                 switch (ret1)
                 {
                 case 0: // OK
                    // This time we succeeded to retrieve 
                    // key and data from cursor's current position.
                    // 
                    // Cache data in pCur -- begin
                    pCur->pCachedKey = keyDBT.data; // I know it's redundant.
                    pCur->nCachedKeySize = keyDBT.size;
                    pCur->pCachedData = dataDBT.data; // I know it's redundant.
                    pCur->nCachedDataSize = dataDBT.size;
                    pCur->nHasKeyAndDataCached = 1;
                    // Cache data in pCur -- end
                    //
                    // Prepare return -- begin
                    if (n<0)
                    {
                       *paData = pCur->pCachedData;
                       *pNData = pCur->nCachedDataSize;
                    }
                    else
                    {
                       int nOut = n;
                       if ((ofst + n)>pCur->nCachedDataSize) nOut = pCur->nCachedDataSize - ofst;
                       if (nOut<0) nOut = 0;

                       *paData = &((u8 *)(pCur->pCachedData))[ofst];
                       *pNData = nOut;
                    }
                    rc = SQLITE4_OK;
                    // Prepare return -- end  
                    // 
                    pCur->nIsEOF = 0; // false; // ???
                    break;
                 case DB_KEYEMPTY: // the cursor's key/data pair was deleted ...
                    // invalidate cache -- begin
                    pCur->nHasKeyAndDataCached = 0;
                    pCur->nCachedKeySize = 0;
                    pCur->nCachedDataSize = 0;
                    // invalidate cache -- end
                    rc = SQLITE4_NOTFOUND;
                    pCur->nIsEOF = 1; // true; // ???
                    break;
                 case DB_NOTFOUND:
                    // invalidate cache -- begin
                    pCur->nHasKeyAndDataCached = 0;
                    pCur->nCachedKeySize = 0;
                    pCur->nCachedDataSize = 0;
                    // invalidate cache -- end
                    rc = SQLITE4_NOTFOUND;
                    pCur->nIsEOF = 1; // true; // ???
                    break;
                 case DB_LOCK_DEADLOCK:
                 case DB_LOCK_NOTGRANTED:
                 case  DB_REP_HANDLE_DEAD:
                 case DB_REP_LEASE_EXPIRED:
                 case DB_REP_LOCKOUT:
                    // invalidate cache -- begin
                    pCur->nHasKeyAndDataCached = 0;
                    pCur->nCachedKeySize = 0;
                    pCur->nCachedDataSize = 0;
                    // invalidate cache -- end
                    rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
                    pCur->nIsEOF = 1; // true; // ???
                    break;
                 case DB_SECONDARY_BAD: // A secondary index references a nonexistent primary key. 
                    // invalidate cache -- begin
                    pCur->nHasKeyAndDataCached = 0;
                    pCur->nCachedKeySize = 0;
                    pCur->nCachedDataSize = 0;
                    // invalidate cache -- end
                    rc = SQLITE4_NOTFOUND; // ???
                    pCur->nIsEOF = 1; // true; // ???
                    break;
                 case EINVAL:
                    // invalidate cache -- begin
                    pCur->nHasKeyAndDataCached = 0;
                    pCur->nCachedKeySize = 0;
                    pCur->nCachedDataSize = 0;
                    // invalidate cache -- end
                    rc = SQLITE4_MISUSE; // Library used incorrectly
                    pCur->nIsEOF = 1; // true; // ???
                    break;
                 case DB_BUFFER_SMALL: // The requested item could not be returned due to undersized buffer. 
                    // continue iteration !!!
                    rc = SQLITE4_OK; // continue iteration , :-) !!!
                    break;
                 default:
                    // invalidate cache -- begin
                    pCur->nHasKeyAndDataCached = 0;
                    pCur->nCachedKeySize = 0;
                    pCur->nCachedDataSize = 0;
                    // invalidate cache -- end
                    rc = SQLITE4_ERROR; // ???
                    pCur->nIsEOF = 1; // true; // ???
                    break;
                 }; // end of switch (ret1){...}
              } // end of else of block : if(ret1 != 0) {...}
           } while(rc = SQLITE4_OK && ret1 == DB_BUFFER_SMALL);
        } // case DB_BUFFER_SMALL -- end
        break;
     default:
        // invalidate cache -- begin
        pCur->nHasKeyAndDataCached = 0;
        pCur->nCachedKeySize = 0;
        pCur->nCachedDataSize = 0;
        // invalidate cache -- end
        rc = SQLITE4_ERROR; // ???
        pCur->nIsEOF = 1; // true; // ???
        break;
     } // end of switch (ret){...}
     // Try to retrieve <key, data>, re-allocate buffers if necessary -- end

  } // end of else block for block : if(pCur->nHasKeyAndDataCached){...}

label_nomem:
  if(rc == SQLITE4_NOMEM)
  {
     if(pCur->pCachedKey != NULL)
     {
        free(pCur->pCachedKey);
        pCur->pCachedKey = 0;
        pCur->nCachedKeySize = 0;
        pCur->nCachedKeyCapacity = 0;
     }
     if(pCur->pCachedData != NULL)
     {
        free(pCur->pCachedData);
        pCur->pCachedData = 0;
        pCur->nCachedDataSize = 0;
        pCur->nCachedDataCapacity = 0;
     }
     pCur->nHasKeyAndDataCached = 0;
  }

  return rc;
} // end of kvbdbData(...){...}


/*
** Begin a transaction or subtransaction.
**
** If iLevel==1 then begin an outermost read transaction.
**
** If iLevel==2 then begin an outermost write transaction.
**
** If iLevel>2 then begin a nested write transaction.
**
** iLevel may not be less than 1.  After this routine returns successfully
** the transaction level will be equal to iLevel.  The transaction level
** must be at least 1 to read and at least 2 to write.
*/
#if 0
int kvbdbBegin(KVStore *pKVStore, int iLevel){
  //printf("-----> kvbdbBegin(%p,%d)\n", pKVStore, iLevel); 
    
  KVBdb *p = (KVBdb*)pKVStore;
  assert( p->iMagicKVBdbBase==SQLITE4_KVBDBBASE_MAGIC );
  assert( iLevel>0 );
  assert( iLevel==2 || iLevel==p->base.iTransLevel+1 );
  if( iLevel>=2 ){
    //KVMemChng **apNewLog;
    //apNewLog = sqlite4_realloc(p->base.pEnv, p->apLog,
    //                           sizeof(apNewLog[0])*(iLevel-1) );
    //if( apNewLog==0 ) return SQLITE4_NOMEM;
    //p->apLog = apNewLog;
    //p->apLog[iLevel-2] = 0;
  }
  p->base.iTransLevel = iLevel;
  return SQLITE4_OK;
}
#endif

int kvbdbBegin(KVStore *pKVStore, int iLevel){
  //printf("-----> kvbdbBegin(%p,%d)\n", pKVStore, iLevel); 
 
  int rc = SQLITE4_OK;
  
  int ret = 0;
  
  KVBdb *p = (KVBdb*)pKVStore;

  assert( p->iMagicKVBdbBase==SQLITE4_KVBDBBASE_MAGIC );

  assert( iLevel>0 );
  assert( iLevel==2 || iLevel==p->base.iTransLevel+1 );

  /*
  ** In BerkeleyDB, we open Cursor's in context of Transactions.
  ** If we need a new Transaction, we open it in context 
  ** of KVBdb's current Transaction, i.e. 
  ** in context of p->pTxn[p-base.iTransLevel], 
  ** i.e. parent_of(p->pTxn[iLevel]) == p->pTxn[p->base.iTransLevel] .
  */
  
  /*
  ** KVBdb.pCsr is a "cursor holding read-trans ops", 
  ** as stated in description of KVLsm.pCsr.
  ** It is not clear, however, which BerkeleyDB DB_TXN Transaction
  ** should be a parent of KVBdb.pCsr:
  ** *) 0 / NULL
  ** *) p->pTxn[0], which is very likely 0 / NULL, "by design ?"
  ** *) current transaction of p, 
  **    i.e. p->pTxn[p->base.iTransLevel]
  ** *) [?] After setting value of p->pTxn[iLevel], 
  **    actually that value of p->pTxn[iLevel]
  ** We start with 0 / NULL, which is probably (?) 
  ** similar to what KVLsm is doing.  
  */
  
  if(p->pCsr == NULL) // no Cursor allocated for read-trans ops
  {
      DBC * pNewCursor = NULL;
      int ret = 0;
      assert(p->dbp);
      DB * dbp = p->dbp;
      ret = dbp->cursor(dbp, NULL, &pNewCursor, DB_READ_COMMITTED); // 1/ parentTxn == NULL 2/ or: DB_READ_COMMITTED | DB_CURSOR_BULK
      if(ret != 0)
      { 
         // failed
         //printf("Failed read-only cursor creation in kvbdbBegin() : KVStore = %p, error='%s'\n", pKVStore, db_strerror(ret));
         rc = SQLITE4_ERROR;
      }
      else
      {
         // succeeded    
         //printf("Succeeded read-only cursor creation in kvbdbBegin() : KVStore = %p, pNewCursor = %p\n", pKVStore, pNewCursor);
         rc = SQLITE4_OK;
      }
      if(rc == SQLITE4_OK)
      {
          p->pCsr = pNewCursor;
      }
  }

  
  //if(rc == SQLITE4_OK && iLevel >= 2 && iLevel >= pKVStore->iTransLevel) // KVLsm uses iLevel >= pKVStore->iTransLevel
  if(rc == SQLITE4_OK && iLevel >= 2 && iLevel > pKVStore->iTransLevel) // we separately handle iLevel == pKVStore->iTransLevel
  {
    // (till now OK ) && (WRITE TXN requested) && (requested Txn Level > curr Txn lLevel) 
    
    // We open a new Txn at level iLevel, 
    // having KVStore's current Txn as a Parent.
    
    // When current Txn Level of KVStore is 0, 
    // then KVStores current Txn is / must be NULL.
    // ? What about Level == 0 and Txn for that Level?
    // Probably must be NULL as well ?
    // These are my (initial) design decisions.
    
    // Thus we provide:
    // parent_of(p->pTxn[iLevel]) == p->pTxn[p->base.iTransLevel] ; 

    assert(iLevel <= SQLITE4_KV_BDB_MAX_TXN_DEPTH)    ;
    //assert(p->pTxn[iLevel] == NULL); or: if(p->pTxn[iLevel] == NULL){...}
    if(p->pTxn[iLevel] == NULL)
    {
        assert( (p->base.iTransLevel == 0 && p->pTxn[p->base.iTransLevel] == 0) 
        || (p->base.iTransLevel == 1 && p->pTxn[p->base.iTransLevel] == 0) 
        || (p->base.iTransLevel == 2 && p->pTxn[p->base.iTransLevel] != 0));
        assert(p->envp != NULL);
        DB_ENV * envp = p->envp;
        DB_TXN * pNewTxn = NULL;
        DB_TXN * pParentTxn = p->pTxn[p->base.iTransLevel];
        // Remarks:
        //          R1/ In presence of Two-Phase-Commit (2PC), 
        //              only the parental-transactions, 
        //              that is the transactions without a parent specified 
        //              / with NULL parent specified can be passed 
        //              as an argument to DB_TXN->prepare(...) function.
        //          R2/ Preparing a parental-transaction, 
        //              means (conditional) commit of all of its descendants 
        //              / children / nested-transactions.        
        //          R3/ Fate of the nested-transaction, 
        //              despite of its successful conditional commit, 
        //              is fully determined by parental-transaction, 
        //              i.e. if the parental-transaction aborts, 
        //              all its nested-transactions will abort.
        
        u_int32_t flags = DB_READ_COMMITTED;                  
        
        ret = envp->txn_begin(envp, pParentTxn, &pNewTxn, flags);                           
        if(ret != 0)
        { 
           // failed
           //printf("Failed DB_TXN Transaction [1] creation in kvbdbBegin() : KVStore = %p, error='%s'\n", pKVStore, db_strerror(ret));
           rc = SQLITE4_ERROR;
        }
        else
        {
           // succeeded    
           //printf("Succeeded DB_TXN Transaction [1] in kvbdbBegin() : KVStore = %p, pNewTxn = %p\n", pKVStore, pNewTxn);
           rc = SQLITE4_OK;
        }  
        if(rc == SQLITE4_OK)        
        {
            p->base.iTransLevel = iLevel;
            p->pTxn[iLevel] = pNewTxn;
        }
            
    } // end of block : if(p->pTxn[iLevel] == NULL){...}
    else if(rc == SQLITE4_OK && iLevel >= 2 && iLevel == pKVStore->iTransLevel) // we separately handle iLevel == pKVStore->iTransLevel  
    {
        // (till now OK ) && (WRITE TXN requested) && (requested Txn Level == curr Txn lLevel) 
        
        // Due to condition (iLevel == pKVStore->iTransLevel) 
        // we may or may not need to open a new DB_TXN.

        assert(iLevel <= SQLITE4_KV_BDB_MAX_TXN_DEPTH)    ;
        //assert(p->pTxn[iLevel] == NULL); or: if(p->pTxn[iLevel] == NULL){...}
        if(p->pTxn[iLevel] == NULL)
        {
           assert( (p->base.iTransLevel == 0 && p->pTxn[p->base.iTransLevel] == 0) 
                || (p->base.iTransLevel == 1 && p->pTxn[p->base.iTransLevel] == 0) 
                || (p->base.iTransLevel == 2 && p->pTxn[p->base.iTransLevel] != 0));
           assert(p->envp != NULL);
           DB_ENV * envp = p->envp;
           DB_TXN * pNewTxn = NULL;
           DB_TXN * pParentTxn = p->pTxn[p->base.iTransLevel];
           // Remarks:
           //          R1/ In presence of Two-Phase-Commit (2PC), 
           //              only the parental-transactions, 
           //              that is the transactions without a parent specified 
           //              / with NULL parent specified can be passed 
           //              as an argument to DB_TXN->prepare(...) function.
           //          R2/ Preparing a parental-transaction, 
           //              means (conditional) commit of all of its descendants 
           //              / children / nested-transactions.        
           //          R3/ Fate of the nested-transaction, 
           //              despite of its successful conditional commit, 
           //              is fully determined by parental-transaction, 
           //              i.e. if the parental-transaction aborts, 
           //              all its nested-transactions will abort.
        
           u_int32_t flags = DB_READ_COMMITTED;
          
          // Determine Parent Txn -- begin                   
          {
             int i = pKVStore->iTransLevel;
             for(; i >= 0; --i)
             {
                 pParentTxn = p->pTxn[i];
                 if(pParentTxn != NULL)
                 {
                     // Found a possible parent/immediate-predecessor, 
                     // although not necessarily 
                     // a 2PC-Committable parental-transaction !
                     //printf("parent txn found for KVStore=%p, pKVStore->iTransLevel=%d, iLevel=%d, at pos i=%d : pParentTxn=%p \n", pKVStore, pKVStore->iTransLevel, iLevel, i, pParentTxn);
                     break;
                 }
             }
          }
          // Determine Parent Txn -- end
          
          ret = envp->txn_begin(envp, pParentTxn, &pNewTxn, flags);                           
          if(ret != 0)
          { 
             // failed
             //printf("Failed DB_TXN Transaction [2] creation in kvbdbBegin() : KVStore = %p, error='%s'\n", pKVStore, db_strerror(ret));
             rc = SQLITE4_ERROR;
          }
          else
          {
             // succeeded    
             //printf("Succeeded DB_TXN Transaction [2] in kvbdbBegin() : KVStore = %p, pNewTxn = %p\n", pKVStore, pNewTxn);
             rc = SQLITE4_OK;
          }  
          if(rc == SQLITE4_OK)        
          {
              p->base.iTransLevel = iLevel;
              p->pTxn[iLevel] = pNewTxn;
          }
        }
    }
  } // end of block : else if(rc == SQLITE4_OK && iLevel >= 2 && iLevel == pKVStore->iTransLevel){...}
  else
  {
      //printf("INFO : No DB_TXN Transaction [3] opened for pKVStore=%p, iLevel=%d\n", pKVStore, iLevel);
  }

  // Epilogue -- begin
  if(rc == SQLITE4_OK)
  {
     pKVStore->iTransLevel = SQLITE4_MAX(iLevel, pKVStore->iTransLevel);    
  }
  else 
  {
      // not SQLITE4_OK -- some error occurred 
      if(pKVStore->iTransLevel == 0)
      {
          // "initial" transaction level
          if(p->pCsr != NULL)
          {
              // if a Cursor 
              // for read-only txn ops exists, 
              // then close it.
              DBC * pcsr = p->pCsr;
              pcsr->close(pcsr);
              p->pCsr = NULL;
          }
      }
  }
  // Epilogue -- end
  
  return rc;
} // end of kvbdbBegin()

/*
** Commit a transaction or subtransaction.
**
** Make permanent all changes back through the most recent xBegin 
** with the iLevel+1.  If iLevel==0 then make all changes permanent.
** The argument iLevel will always be less than the current transaction
** level when this routine is called.
**
** Commit is divided into two phases.  A rollback is still possible after
** phase one completes.  In this implementation, phase one is a no-op since
** phase two cannot fail.
**
** After this routine returns successfully, the transaction level will be 
** equal to iLevel.
*/
#if 0
int kvbdbCommitPhaseOne(KVStore *pKVStore, int iLevel){
  //printf("-----> kvbdbCommitPhaseOne(%p,%d)\n", pKVStore, iLevel);  
    
  return SQLITE4_OK;
}
#endif

int kvbdbCommitPhaseOne(KVStore *pKVStore, int iLevel){
  //printf("-----> kvbdbCommitPhaseOne(%p,%d)\n", pKVStore, iLevel);  
    
  int rc = SQLITE4_OK;
  
  KVBdb * p = (KVBdb*)pKVStore;
  
  if(pKVStore->iTransLevel > iLevel)
  {
     if(pKVStore->iTransLevel >= 2) 
	 {
	    // Not committed writes present.
		// Find a candidate-BerkeleyDB-transaction to prepare.
		DB_TXN * pTxnPrepareCandidate = p->pTxn[iLevel+1]; // ?
		if(pTxnPrepareCandidate != NULL)
		{
		   // Non-NULL candidate can be prepared, 
		   // if no parental-txn exists, 
		   // i.e. if the candidate itself 
		   // is a parental txn.
		   DB_TXN * pParentTxn = NULL;
		   int i = 0;
		   for( i = iLevel; i >= 0; --i )
		   {
		      pParentTxn = p->pTxn[i];
			  if(pParentTxn != NULL)
			  {
			     break;
			  }
		   } // end loop : for( i = iLevel; i >= 0; --i ){...}
		   if(pParentTxn != NULL)
		   {
		      // parental-txn exists, 
			  // thus don't prepare 
			  // the candidate-txn.
			  // 
			  // Remember setting 
			  // txn level to iLevel 
			  // before return!
			  // Well, perhaps at commit phase two???
			  
			  rc = SQLITE4_OK; // ?
		   }
		   else
		   {
		      // parental-txn does not exist 
			  // for canditate-txn, 
			  // i.e. candidate-txn itself 
			  // is a parental-txn,
			  // which means that candidate-txn 
			  // can be prepared.
			  //
			  // Prepare a GID for tghe txn to be prepared.
			  u_int8_t gid[DB_GID_SIZE]; // DB_GID_SIZE == 128 bytes
			  memset(gid, 0, DB_GID_SIZE);
			  time_t currTime;
			  currTime = time(NULL); // think of finer granularity or, e.g.  <thread, per-thread counter>
			  sprintf(gid, "%p:%ld", (void*)pTxnPrepareCandidate, currTime);

              int ret = 0;
              ret = pTxnPrepareCandidate->prepare(pTxnPrepareCandidate, gid);	
              switch(ret)
              {
			     case 0:
				   //printf("kvbdbCommitPhaseOne() succeeded : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnPrepareCandidate = %p\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnPrepareCandidate); 
				   rc = SQLITE4_OK;
				   break;
				 case DB_LOCK_DEADLOCK:
				 case DB_LOCK_NOTGRANTED:
				   //printf("kvbdbCommitPhaseOne() deadlock_resolved / needs_rollback_and_then_can_be_retried : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnPrepareCandidate = %p : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnPrepareCandidate, db_strerror(ret)); 
				   rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
                   break;				   
				 case EINVAL:  // If the cursor is already closed; or if an invalid flag value or parameter was specified. 
				   //printf("kvbdbCommitPhaseOne() failed : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnPrepareCandidate = %p : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnPrepareCandidate, db_strerror(ret)); 
				   //rc = SQLITE4_ERROR; // ?
               rc = SQLITE4_MISUSE; // Library used incorrectly (!!!)
               break;
                 default:	
                   //printf("kvbdbCommitPhaseOne() FAILED : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnPrepareCandidate = %p : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnPrepareCandidate, db_strerror(ret)); 
				   rc = SQLITE4_ERROR; // ?
                   break;				 
              }			  
			  // CAUTION:
			  // During kvbdbCommitPhaseOTwo() or kvbdbRollback(), 
			  // all pTxn[j]'s above iLevel should be (re-)set to NULL.
			  //
		   } // else block for : if(pParentTxn != NULL){...}
		} // end of block : if(pTxnPrepareCandidate != NULL){...}
	 } // end block : if(KVStore->iTransLevel >= 2){...}
  } // end block : if(KVStore->iTransLevel > iLevel){...}
  
  // Well, perhaps at commit phase two???
  //if(rc = SQLITE4_OK)
  //{
  //   pKVStore->iTransLevel = iLevel;
  //}
  
  return rc;
} // end of : kvbdbCommitPhaseOne(KVStore *pKVStore, int iLevel){...}

int kvbdbCommitPhaseOneXID(KVStore *pKVStore, int iLevel, void * xid){
  //printf("-----> kvbdbCommitPhaseOneXID(%p,%d,%p)\n", pKVStore, iLevel, xid);  
    
  return SQLITE4_OK;
}

#if 0
int kvbdbCommitPhaseTwo(KVStore *pKVStore, int iLevel){
  //printf("-----> kvbdbCommitPhaseTwo(%p,%d)\n", pKVStore, iLevel);  
    
  KVBdb *p = (KVBdb*)pKVStore;
  assert( p->iMagicKVBdbBase==SQLITE4_KVBDBBASE_MAGIC );
  assert( iLevel>=0 );
  assert( iLevel<p->base.iTransLevel );
  //assertUpPointers(p->pRoot);

  //assertUpPointers(p->pRoot);
  p->base.iTransLevel = iLevel;
  return SQLITE4_OK;
}
#endif

int kvbdbCommitPhaseTwo(KVStore *pKVStore, int iLevel){
  //printf("-----> kvbdbCommitPhaseTwo(%p,%d)\n", pKVStore, iLevel);  
    
  int rc = SQLITE4_OK;
  
  KVBdb * p = (KVBdb*)pKVStore;
  
  if(pKVStore->iTransLevel > iLevel)
  {
     if(pKVStore->iTransLevel >= 2) 
	 {
	    // Not committed writes present.
		// Find a candidate-BerkeleyDB-transaction to commit.
		// According to "BerkeleyDB C Reference", 
		// we don't need to commit all child-txn's separately.
		// I understand it so, that I don't need to iterate 
		// between iTransLevel and iLevel+1, 
		// when I [prepare() or] commit() at iLevel+1.
		// I assume, that, what I need after successful commit() at iLevel+1, 
		// is to re-set to NULL all pTxn[j]'s, 
		// for j between iTransLevel and iLevel+1, inclusively.
		
		DB_TXN * pTxnCommitCandidate = p->pTxn[iLevel+1]; // ?
        int nTxnCommitCandidateLevel = iLevel+1;
		if(pTxnCommitCandidate != NULL)
		{
		   int flags =  DB_TXN_NOSYNC 
		         | DB_TXN_WRITE_NOSYNC;
				 
              int ret = 0;
              ret = pTxnCommitCandidate->commit(pTxnCommitCandidate, flags);	
              switch(ret)
              {
			     case 0:
				   //printf("kvbdbCommitPhaseTwo() succeeded : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnCommitCandidate = %p, nTxnCommitCandidateLevel = %d\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnCommitCandidate, nTxnCommitCandidateLevel); 
				   rc = SQLITE4_OK;
				   break;
				 case DB_LOCK_DEADLOCK:
				 case DB_LOCK_NOTGRANTED:
				   //printf("kvbdbCommitPhaseTwo() deadlock_resolved / needs_rollback_and_then_can_be_retried : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnCommitCandidate = %p, nTxnCommitCandidateLevel = %d : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnCommitCandidate, nTxnCommitCandidateLevel, db_strerror(ret)); 
				   rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
                   break;				   
				 //case EINVAL:  
				 //  //printf("kvbdbCommitPhaseTwo() failed : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnCommitCandidate = %p : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnCommitCandidate, db_strerror(ret)); 
				 //  rc = SQLITE4_ERROR; // ?
                 //  break;
                 default:	
                   //printf("kvbdbCommitPhaseTwo() FAILED : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnCommitCandidate = %p, nTxnCommitCandidateLevel = %d : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnCommitCandidate, nTxnCommitCandidateLevel, db_strerror(ret)); 
				   rc = SQLITE4_ERROR; // ?
                   break;				 
              }			  
			  // CAUTION:
			  // During kvbdbCommitPhaseOTwo() or kvbdbRollback(), 
			  // all pTxn[j]'s above iLevel should be (re-)set to NULL.
			  if(rc == SQLITE4_OK)
			  {
			     int i = 0;
				 for(i = pKVStore->iTransLevel; i > iLevel; --i)
				 {
				    p->pTxn[i] = NULL; 
				 }
			  }
			  
			  // At iLevel == 0 close a read-only Cursor, 
			  // if any present
			  if(iLevel == 0)
			  {
			     DBC * pCsr = p->pCsr;
				 if(pCsr != NULL)
				 {
				    p->pCsr = NULL;
					pCsr->close(pCsr);
				 }
			  }
		} // end of block : if(pTxnPrepareCandidate != NULL){...}
	 } // end block : if(KVStore->iTransLevel >= 2){...}
  } // end block : if(KVStore->iTransLevel > iLevel){...}
  
  // CAUTION:
  // During kvbdbCommitPhaseOTwo() or kvbdbRollback(), 
  // all pTxn[j]'s above iLevel should be (re-)set to NULL.
  // QUESTION:
  // Should we do this clean-up here, 
  // i.e. also for commit-canditate-txn's == NULL, 
  // i.e. when actual commit() has not been executed?
  //if(rc == SQLITE4_OK)
  //{
  //	int i = 0;
  //	for(i = pKVStore->iTransLevel; i > iLevel; --i)
  //	{
  //	    p->pTxn[i] = NULL; 
  //	}
  //}
	
  // At iLevel == 0 close a read-only Cursor, 
  // if any present.
  // QUESTION:
  // Should we do this clean-up here, 
  // i.e. also for commit-canditate-txn's == NULL, 
  // i.e. when actual commit() has not been executed?
  //if(iLevel == 0)
  //{
  //	DBC * pCsr = p->pCsr;
  //	if(pCsr != NULL)
  //	{
  //	    p->pCsr = NULL;
  //		pCsr->close(pCsr);
  //	 }
  //}	
 
  // Well, perhaps at commit phase two???
  if(rc == SQLITE4_OK)
  {
     pKVStore->iTransLevel = iLevel;
  }
  
  return rc;
} // end of : kvbdbCommitPhaseTwo(KVStore *pKVStore, int iLevel){...}

/*
** Rollback a transaction or subtransaction.
**
** Revert all uncommitted changes back through the most recent xBegin or 
** xCommit with the same iLevel.  If iLevel==0 then back out all uncommited
** changes.
**
** After this routine returns successfully, the transaction level will be
** equal to iLevel.
*/

#if 0
int kvbdbRollback(KVStore *pKVStore, int iLevel){
  //printf("-----> kvbdbRollback(%p,%d)\n", pKVStore, iLevel);   
    
  KVBdb *p = (KVBdb*)pKVStore;
  assert( p->iMagicKVBdbBase==SQLITE4_KVBDBBASE_MAGIC );
  assert( iLevel>=0 );

  p->base.iTransLevel = iLevel;
  return SQLITE4_OK;
}
#endif

int kvbdbRollback(KVStore *pKVStore, int iLevel)
{
  //printf("-----> kvbdbRollback(%p,%d)\n", pKVStore, iLevel);   
  
  int rc = SQLITE4_OK;
  int ret = 0;
    
  KVBdb *p = (KVBdb*)pKVStore;
  
  assert( p->iMagicKVBdbBase==SQLITE4_KVBDBBASE_MAGIC );
  assert( iLevel>=0 );

  if(pKVStore->iTransLevel >= iLevel)
  {
     if(pKVStore->iTransLevel >= 2)
     {	 
	    // WRITE ops may already be present, 
		// thus we may have something-to-rollback
		
		// */ ROLLBACK, i.e. abort(), all levels above iLevel 
		// */ cleanup appropriate ->pTxn[i]'s for those levels
        // */ then, _perhaps_ :
        //    **/ abort() iLevel itself
        //    **/ cleanup ->pTxn[iLevel] 		
		
		//// ROLLBACK/abort() & cleanup all levels above iLevel -- begin
      // ROLLBACK/abort() & cleanup all levels above iLevel and iLevel itself -- begin
		{
		   int i = pKVStore->iTransLevel;
		   //for(; i > iLevel && i >= 0; --i)
         for (; i >= iLevel && i >= 0; --i) // ... and iLevel itself ...
		   {
		      DB_TXN * pCurrTxn = p->pTxn[i];
			  if(pCurrTxn != NULL)
			  {
			     // BerkeleyDB transaction exists, 
				 // ROLLBACK it using abort()
				 // and clean-up p->pTxn[i]
				 ret = pCurrTxn->abort(pCurrTxn); // abort()
				 p->pTxn[i] = NULL; // clean-up
				 if(ret != 0)
				 {
				    // ROLLBACK failed
					if(rc == SQLITE4_OK)
					{
					   rc = SQLITE4_ERROR;
					}
					//printf("kvbdbRollback() : abort() failed : pKVStore=%p, pKVStore->iTransLevel=%d, iLevel=%d, i=%d, pCurrTxn=%p, error='%s'\n", pKVStore, pKVStore->iTransLevel, iLevel, i, pCurrTxn, db_strerror(ret));
				 }
				 else
				 {
				    // ROLLBACK succeeded
					//printf("kvbdbRollback() : abort() succeeded : pKVStore=%p, pKVStore->iTransLevel=%d, iLevel=%d, i=%d, pCurrTxn=%p\n", pKVStore, pKVStore->iTransLevel, iLevel, i, pCurrTxn);
				 }
			  }
		   }
		}
      // ROLLBACK/abort() & cleanup all levels above iLevel and iLevel itself -- end
		//// ROLLBACK/abort() & cleanup all levels above iLevel -- end
		
//		// _possibly_ : ROLLBACK/abort() & cleanup level iLevel itself -- begin
//		{
//		   DB_TXN * pCurrTxn = p->pTxn[iLevel];
//		   if(pCurrTxn != NULL)
//		   {
//		      // BerkeleyDB transaction exists, 
//			  // ROLLBACK it using abort()
//			  // and clean-up p->pTxn[iLevel]
//			  ret = pCurrTxn->abort(pCurrTxn); // abort()
//			  p->pTxn[iLevel] = NULL; // clean-up
//			  if(ret != 0)
//			  {
//			    // ROLLBACK failed
//				if(rc == SQLITE4_OK)
//				{
//				   rc = SQLITE4_ERROR;
//				}
//				//printf("kvbdbRollback() : abort() 'at iLevel itself' failed : pKVStore=%p, pKVStore->iTransLevel=%d, iLevel=%d, pCurrTxn=%p, error='%s'\n", pKVStore, pKVStore->iTransLevel, iLevel, pCurrTxn, db_strerror(ret));
//			 }
//			 else
//			 {
//			    // ROLLBACK succeeded
//				//printf("kvbdbRollback() : abort() 'at iLevel itself' succeeded : pKVStore=%p, pKVStore->iTransLevel=%d, iLevel=%d, pCurrTxn=%p\n", pKVStore, pKVStore->iTransLevel, iLevel, pCurrTxn);
//			 }
//		   }
//		}
//		// _possibly_ : ROLLBACK/abort() & cleanup level iLevel itself -- end

	 } // end of block: if(pKVStore->iTransLevel >= 2){...}
	 
	 if(iLevel == 0)
	 {
	    if(p->pCsr != NULL)
		{
		   // Cursor for read-only ops exists -- 
		   // -- close it and clean it up!
		   ret = p->pCsr->close(p->pCsr); // closing
		   p->pCsr = NULL; // cleaning up
		   if (ret != 0) 
           {
		       if(rc == SQLITE4_OK)
			   {
				  rc = SQLITE4_ERROR;
			   }
               //printf("kvbdbRollback() : failed closing Cursor while iLevel==0 : pKVStore=%p, pKVStore->iTransLevel=%d. Error message: '%s'\n", pKVStore, pKVStore->iTransLevel, db_strerror(ret));
           }
           else
           {
                //printf("kvbdbRollback() : succeeded closing Cursor while iLevel==0 : pKVStore=%p, pKVStore->iTransLevel=%d.\n", pKVStore, pKVStore->iTransLevel);
           }
		} // end of block : if(p->pCsr != NULL){...}
	 } // end of block : if(iLevel == 0){...}
	 
	 if(rc == SQLITE4_OK)
	 {
	    //pKVStore->iTransLevel = iLevel;
       pKVStore->iTransLevel = iLevel - 1;// ... and iLevel itself ...
	 }

    // Restart at iLevel to restore a SAVEPOINT -- begin
    // QUESTIONS:
    // Q1/ Is this restart unconditional?
    // Q2/ Or should we use, e.g.: if(iLevel > 0) {rc = kvbdbBegin(pKVStore, iLevel);} ?
    rc = kvbdbBegin(pKVStore, iLevel);
    // Restart at iLevel to restore a SAVEPOINT -- end

  } // end of block: if(pKVStore->iTransLevel >= iLevel){...}
  
  return rc;
} // end of kvbdbRollback(...)

/*
** Revert a transaction back to what it was when it started.
*/

int kvbdbRevert(KVStore *pKVStore, int iLevel){
   //printf("-----> kvbdbRevert(%p,%d)\n", pKVStore, iLevel); 
  // */ kvbdbRevert(..) will, actually, 
  //    be similar to kvbdbRollback(...).
  // */ Perhaps, within kvbdbRollback(...), 
  //    we should COMMIT at level "iLevel" ?
  // */ Or, perhaps, compared to kvbdbRollback(...), 
  //    within kvbdbRevert(...) we should 
  //    ROLLBACK/abort(...) at level "iLevel" ?
  // */ Test will, probably, ( :-) ), 
  //    show the proper answer, :-) !
  int rc = kvbdbRollback(pKVStore, iLevel-1);
  if( rc==SQLITE4_OK ){
    rc = kvbdbBegin(pKVStore, iLevel);
  }
  return rc;
}

/*
** Destructor for the entire in-memory storage tree.
*/
//static int kvbdbClose(KVStore *pKVStore){
//  KVBdb *p = (KVBdb*)pKVStore;
//  sqlite4_env *pEnv;
//  if( p==0 ) return SQLITE4_OK;
//  assert( p->iMagicKVBdbBase==SQLITE4_KVBDBBASE_MAGIC );
//  assert( p->nCursor==0 );
//  pEnv = p->base.pEnv;
//  if( p->base.iTransLevel ){
//    kvbdbCommitPhaseOne(pKVStore, 0);
//    kvbdbCommitPhaseTwo(pKVStore, 0);
//  }
//  sqlite4_free(pEnv, p->apLog);
//  //kvmemClearTree(pEnv, p->pRoot);
//  memset(p, 0, sizeof(*p));
//  sqlite4_free(pEnv, p);
//  return SQLITE4_OK;
//}
#if 0
static int kvbdbClose(KVStore *pKVStore){
    if((KVBdb*)pKVStore)
    {
       //printf("-----> kvbdbClose() , database_name='%s'\n", ((KVBdb*)pKVStore)->name );
    }
    else
    {
       //printf("-----> kvbdbClose() , database_name='_none_'\n" );    
    }
  
  KVBdb *p = (KVBdb*)pKVStore;
  DB_ENV * envp = NULL;         
  DB     * dbp  = NULL;
  sqlite4_env *pEnv;
  int ret;
  
  if( p==0 ) 
      return SQLITE4_OK;
  
  assert( p->iMagicKVBdbBase==SQLITE4_KVBDBBASE_MAGIC );
  assert( p->nCursor==0 );
  
  envp = p->envp;
  dbp = p->dbp;
  
  if(dbp)
  {
    ret = dbp->close(dbp, 0);
	if (ret != 0) 
    {
	    //printf(" database '%s' close failed: %s\n",
		p->name, db_strerror(ret));
	    return SQLITE4_ERROR;
	}
  }

  return SQLITE4_OK;
}
#endif

int kvbdbClose(KVStore *pKVStore){
    if((KVBdb*)pKVStore)
    {
       //printf("-----> kvbdbClose() , database_name='%s'\n", ((KVBdb*)pKVStore)->name );
    }
    else
    {
       //printf("-----> kvbdbClose() , database_name='_none_'\n" );    
    }
  
  KVBdb *p = (KVBdb*)pKVStore;
  
  //DB_ENV * envp = NULL;         
  //DB     * dbp  = NULL;
  //sqlite4_env *pEnv;
  
  int ret;
  
  if( p==0 ) 
      return SQLITE4_ERROR;
  
  assert( p->iMagicKVBdbBase==SQLITE4_KVBDBBASE_MAGIC );
  assert( p->nCursor==0 );
  
  char * zName = p->name;	  

  bdb_dict_node_t * pDictNode_zName = global_acquire_locked_dict_node(zName);
  if(!pDictNode_zName)
  {
     // cannot acquire a node, i.e. neither a locked node
	 // thus no node to unlock
	 //printf("Failed to acquire a (locked) BerkeleyDB Dictionary Node for database '%s'\n", zName);
	 return SQLITE4_ERROR;
  }
  else
  {
     //printf("------>Succeeded to acquire a (locked) BerkeleyDB Dictionary Node for database '%s'\n", zName);
  }
  
  assert(p->envp == pDictNode_zName->envp);
  assert(p->dbp == pDictNode_zName->dbp);
  
  // decrease the reference counter of pDictNode_zName
  if(pDictNode_zName->nref > 0)
  {
     pDictNode_zName->nref -= 1;
  }
  
  if(pDictNode_zName->nref == 0)
  {
     if(pDictNode_zName->dbp)
	 {
	     ret = pDictNode_zName->dbp->close(pDictNode_zName->dbp, 0);
		 if(ret != 0)
		 {
		    //printf("KVBdb=%p, zName='%s' : Failed to close dbp=%p, Error='%s'\n", p, zName, pDictNode_zName->dbp, db_strerror(ret));
		    unlock_locked_dict_node(pDictNode_zName);
		    return SQLITE4_ERROR;
		 }
		 else
		 {
            //printf("------>KVBdb=%p, zName='%s' : Succeeded to close dbp=%p\n", p, zName, pDictNode_zName->dbp);		    
		 }
		 pDictNode_zName->dbp = NULL;
	 }
	 if(pDictNode_zName->envp)
	 {
	    ret = pDictNode_zName->envp->close(pDictNode_zName->envp, 0);
		if(ret != 0)
		 {
		    //printf("KVBdb=%p, zName='%s' : Failed to close envp=%p, Error='%s'\n", p, zName, pDictNode_zName->envp, db_strerror(ret));
		    unlock_locked_dict_node(pDictNode_zName);
		    return SQLITE4_ERROR;
		 }
		 else
		 {
            //printf("------>KVBdb=%p, zName='%s' : Succeeded to close envp=%p\n", p, zName, pDictNode_zName->envp);		    
		 }
		pDictNode_zName->envp = NULL;
	 }
  }
  
  // TO-D0:
  // free/dealloc a pointer "p->name", and then "p" itself
  //free(p->name);
  //p->name = NULL;
  //free(p);
  
  unlock_locked_dict_node(pDictNode_zName);
  
  return SQLITE4_OK;
}

int kvbdbControl(KVStore *pKVStore, int op, void *pArg){
  //printf("-----> kvbdbControl()\n");  
    
  return SQLITE4_NOTFOUND;
}

int kvbdbGetMeta(KVStore *pKVStore, unsigned int *piVal){
  //printf("-----> kvbdbGetMeta()\n");  
    
  KVBdb *p = (KVBdb*)pKVStore;
  *piVal = p->iMeta;
  return SQLITE4_OK;
}

int kvbdbPutMeta(KVStore *pKVStore, unsigned int iVal){
  //printf("-----> kvbdbPutMeta()\n");
  
  KVBdb *p = (KVBdb*)pKVStore;
  p->iMeta = iVal;
  return SQLITE4_OK;
}


static DB_ENV * s_envp = NULL;         /* BerkeleyDB environment for connection / database */
static struct sqlite4_mutex * s_env_mutexp = NULL;

static DB     * s_dbp  = NULL;         /* connection to BerkeleyDB database                */
static struct sqlite4_mutex * s_db_mutexp = NULL;


/* Virtual methods for the BerkeleyDB storage engine */
static const KVStoreMethods kvbdbMethods = {
  1,                        /* iVersion */
  sizeof(KVStoreMethods),   /* szSelf */
  kvbdbReplace,             /* xReplace */
  kvbdbOpenCursor,          /* xOpenCursor */
  kvbdbSeek,                /* xSeek */
  kvbdbNextEntry,           /* xNext */
  kvbdbPrevEntry,           /* xPrev */
  kvbdbDelete,              /* xDelete */
  kvbdbKey,                 /* xKey */
  kvbdbData,                /* xData */
  kvbdbReset,               /* xReset */
  kvbdbCloseCursor,         /* xCloseCursor */
  kvbdbBegin,               /* xBegin */
  kvbdbCommitPhaseOne,      /* xCommitPhaseOne */
  kvbdbCommitPhaseOneXID,   /* xCommitPhaseOneXID */
  kvbdbCommitPhaseTwo,      /* xCommitPhaseTwo */
  kvbdbRollback,            /* xRollback */
  kvbdbRevert,              /* xRevert */
  kvbdbClose,               /* xClose */
  kvbdbControl,             /* xControl */
  kvbdbGetMeta,             /* xGetMeta */
  kvbdbPutMeta              /* xPutMeta */
};

/*
** Create a new in-memory storage engine and return a pointer to it.
*/
#if 0
int sqlite4KVStoreOpenBdb(
  sqlite4_env *pEnv,              /* Runtime environment */
  KVStore **ppKVStore,            /* OUT: Write the new KVStore here */
  const char *zName,              /* Name of BerkeleyDB storage unit */
  unsigned openFlags              /* Flags */
){
  //printf("-----> sqlite4KVStoreOpenBdb(), ZName=%s, pEnv=%p\n", zName, pEnv);
  
  DB_ENV * envp = NULL;
  u_int32_t env_flags = 0;
  
  DB     * dbp  = NULL;
  u_int32_t extra_flags = 0;
  u_int32_t open_flags = 0;
  
  int ret;
    
  //KVBdb *pNew = sqlite4_malloc(pEnv, sizeof(*pNew) );
  KVBdb *pNew = (KVBdb*)sqlite4_malloc(pEnv, sizeof(KVBdb) );
  if( pNew==0 ) return SQLITE4_NOMEM;
  //memset(pNew, 0, sizeof(*pNew));
  memset(pNew, 0, sizeof(KVBdb));
  pNew->base.pStoreVfunc = &kvbdbMethods;
  pNew->base.pEnv = pEnv;
  pNew->iMagicKVBdbBase = SQLITE4_KVBDBBASE_MAGIC;
  pNew->openFlags = openFlags;
  //*ppKVStore = (KVStore*)pNew;
  
  if(use_mutexed_in_kvbdb)
  {
    ret = sqlite4_initialize(pEnv);
    if(ret != 0)
    {
     //printf("sqlite4_initialize(pEnv=%p) failed, : %s \n", pEnv, db_strerror(ret));
    }
  
    ret = sqlite4MutexInit(pEnv);
    if(ret != 0)
    {
     //printf("sqlite4MutexInit(pEnv=%p) failed, : %s \n", pEnv, db_strerror(ret));
    }
  
    //printf("----------> s_env_mutexp == %p\n", s_env_mutexp);
    if(s_env_mutexp == NULL)
    {
      //printf("----------> s_env_mutexp == NULL, need to be allocated\n");
	  
      //struct sqlite4_mutex * mutexp = sqlite4_mutex_alloc(0, SQLITE4_MUTEX_FAST);  
	  struct sqlite4_mutex * mutexp = sqlite4_mutex_alloc(pEnv, SQLITE4_MUTEX_FAST);  
      if (mutexp == NULL) 
      {
	    //printf("Error creating s_env_mutexp: %s\n", db_strerror(ret));
        return SQLITE4_ERROR;
      } 
      s_env_mutexp = mutexp;      
    }
    //printf("----------> s_env_mutexp == %p\n", s_env_mutexp);
  
    //printf("----------> s_db_mutexp == %p\n", s_db_mutexp);
    if(s_db_mutexp == NULL)
    {
      //printf("----------> s_db_mutexp == NULL, need to be allocated\n");
	  
      //struct sqlite4_mutex * mutexp = sqlite4_mutex_alloc(0, SQLITE4_MUTEX_FAST);  
	  struct sqlite4_mutex * mutexp = sqlite4_mutex_alloc(pEnv, SQLITE4_MUTEX_FAST);  
      if (mutexp == NULL) 
      {
	    //printf("Error creating s_db_mutexp: %s\n", db_strerror(ret));
        return SQLITE4_ERROR;
      } 
      s_db_mutexp = mutexp;      
    }
    //printf("----------> s_db_mutexp == %p\n", s_db_mutexp);
 } // end of if(use_mutexed_in_kvbdb){...} 
 
  // lock on global static mutex s_env_mutexp
  // protecting global static DB_ENV * s_envp
  //ret = sqlite4_mutex_enter(s_env_mutexp);
  //if(ret != 0)
  //{
  //   //printf("Error entering/locking s_env_mutexp=%p. Error : '%s'\n", s_env_mutexp, db_strerror(ret));
  //   return SQLITE4_ERROR;
  //}
  if(use_mutexed_in_kvbdb)
  {  
     sqlite4_mutex_enter(s_env_mutexp);
  }
  
  if(s_envp == NULL)
  {
      //printf("----------> s_envp == NULL\n");
      
      /* Create the environment */
      ret = db_env_create(&envp, 0);
      if (ret != 0) 
      {
	    //printf("Error creating environment handle: %s\n", db_strerror(ret));
		if(use_mutexed_in_kvbdb)
		{
           sqlite4_mutex_leave(s_env_mutexp);
		}
        return SQLITE4_ERROR;
      }
	  else
	  {
	       //printf("------>Succeeded creating BerkeleyDB environment, envp=%p \n", envp);
	  }
      
      /*
      * Indicate that we want db to perform lock detection internally.
      * Also indicate that the transaction with the fewest number of
      * write locks will receive the deadlock notification in
      * the event of a deadlock.
      */
      ret = envp->set_lk_detect(envp, DB_LOCK_MINWRITE);
      if (ret != 0) 
      {
	     //printf("Error setting lock detect: %s\n", db_strerror(ret));
		 if(use_mutexed_in_kvbdb)
		 {
            sqlite4_mutex_leave(s_env_mutexp);
		 }
         return SQLITE4_ERROR;
      }
      
      env_flags =
         DB_CREATE     |  /* Create the environment if it does not exist */
         DB_RECOVER    |  /* Run normal recovery. */
         DB_INIT_LOCK  |  /* Initialize the locking subsystem */
         DB_INIT_LOG   |  /* Initialize the logging subsystem */
         DB_INIT_TXN   |  /* Initialize the transactional subsystem. This also turns on logging. */
         DB_INIT_MPOOL |  /* Initialize the memory pool (in-memory cache) */
         DB_THREAD;       /* Cause the environment to be free-threaded */
         
      /* Now actually open the environment */
      ret = envp->open(envp, NULL, env_flags, 0);
      if (ret != 0) 
      {
	     //printf("Error opening environment: %s\n", db_strerror(ret));
		 if(use_mutexed_in_kvbdb)
		 {
            sqlite4_mutex_leave(s_env_mutexp);
		 }
         return SQLITE4_ERROR;
      }
      else
	  {
	     //printf("------>Succeeded opening BerkeleyDB environment, envp=%p \n", envp);
	  }
	  
    
      /* modify a global static DB_ENV */
      s_envp = envp;
  }
  
  // un-lock on global static mutex s_env_mutexp
  // protecting global static DB_ENV * s_envp
  //ret = sqlite4_mutex_leave(s_env_mutexp);
  //if(ret != 0)
  //{
  //   //printf("Error leaving/un-locking s_env_mutexp=%p. Error : '%s'\n", s_env_mutexp, db_strerror(ret));
  //   return SQLITE4_ERROR;
  //}
  if(use_mutexed_in_kvbdb)
  {
     sqlite4_mutex_leave(s_env_mutexp);
  }
  
  /*
  * If we had utility threads (for running checkpoints or
  * deadlock detection, for example) we would spawn those 
  * here. 
  */
  {
      
  }
   
  envp = s_envp;
  
  // lock on global static mutex s_db_mutexp
  // protecting global static DB * s_dbp
  //ret = sqlite4_mutex_enter(s_db_mutexp);
  //if(ret != 0)
  //{
  //   //printf("Error entering/locking s_db_mutexp=%p. Error : '%s'\n", s_db_mutexp, db_strerror(ret));
  //   return SQLITE4_ERROR;
  //}
  if(use_mutexed_in_kvbdb)
  {
     sqlite4_mutex_enter(s_db_mutexp);
  }
  
  if(s_dbp == NULL)
  {
      //printf("----------> s_dbp == NULL\n");
      
      /* create the database */
      ret = db_create(&dbp, envp, 0);
      if (ret != 0)
      {
         //printf("Error creating database: %s\n", db_strerror(ret));
		 if(use_mutexed_in_kvbdb)
		 {
            sqlite4_mutex_leave(s_db_mutexp);
		 }
         return SQLITE4_ERROR;
      }
	  else
	  {
	     //printf("------>Succeeded creating BerkeleyDB database, dbp=%p, envp=%p \n", dbp, envp);
	  }
      
      /* set extra flags before open, if any needed */
      if (extra_flags != 0) 
      {
	     ret = dbp->set_flags(dbp, extra_flags);
	     if (ret != 0) 
         {
	        //printf("Error setting extra flags: %s\n", db_strerror(ret));
			if(use_mutexed_in_kvbdb)
			{
               sqlite4_mutex_leave(s_db_mutexp);
			}
            return SQLITE4_ERROR;
	     }
		 else
	     {
	        //printf("------>Succeeded setting flags for BerkeleyDB database, dbp=%p \n", dbp);
	     }
      }
      
      /* prepare open flags */
      open_flags = DB_CREATE              | /* Allow database creation */
		           DB_READ_UNCOMMITTED    | /* Allow dirty reads */
		           DB_AUTO_COMMIT         | /* Allow autocommit */
 		           DB_THREAD;               /* Cause the database to be free-threaded */
         
      /* Now open the database */
      ret = dbp->open(dbp, /* Pointer to the database */
		    NULL,          /* Txn pointer */
		    zName,         /* File name */
		    NULL,          /* Logical db name */
		    DB_BTREE,      /* Database type (using btree) */
		    open_flags,    /* Open flags */
		    0);
      if (ret != 0)
      {
          //printf("Error opening database '%s'. Error message: '%s'\n", zName, db_strerror(ret));
		  if(use_mutexed_in_kvbdb)
		  {
             sqlite4_mutex_leave(s_db_mutexp);
		  }
          return SQLITE4_ERROR;
      }
	  else
	  {
	     //printf("------>Succeeded opening BerkeleyDB database, zName='%s', dbp=%p \n", zName, dbp);
	  }
     
      /* modify a global static DB */     
      s_dbp = dbp;
  }
  
  // un-lock on global static mutex s_db_mutexp
  // protecting global static DB * s_dbp
  //ret = sqlite4_mutex_leave(s_db_mutexp);
  //if(ret != 0)
  //{
  //   //printf("Error leaving/un-locking s_db_mutexp=%p. Error : '%s'\n", s_db_mutexp, db_strerror(ret));
  //   return SQLITE4_ERROR;
  //}
  if(use_mutexed_in_kvbdb)
  {
     sqlite4_mutex_leave(s_db_mutexp);
  }
  
  dbp = s_dbp;
  
  pNew->dbp = dbp;   /* connection to BerkeleyDB database           */
  pNew->envp = envp; /* environment for above connection / database */
  strcpy(pNew->name, zName); /* database name */
  
  *ppKVStore = (KVStore*)pNew;
  
  return SQLITE4_OK;
}
#endif

int sqlite4KVStoreOpenBdb(
  sqlite4_env *pEnv,              /* Runtime environment */
  KVStore **ppKVStore,            /* OUT: Write the new KVStore here */
  const char *zName,              /* Name of BerkeleyDB storage unit */
  unsigned openFlags              /* Flags */
){
  //printf("-----> sqlite4KVStoreOpenBdb(), ZName=%s, pEnv=%p\n", zName, pEnv);
  
  DB_ENV * envp = NULL;
  u_int32_t env_flags = 0;
  
  DB     * dbp  = NULL;
  u_int32_t extra_flags = 0;
  u_int32_t open_flags = 0;
  
  int ret;
  
  bdb_dict_node_t * pDictNode_zName = global_acquire_locked_dict_node(zName);
  if(!pDictNode_zName)
  {
     // cannot acquire a node, i.e. neither a locked node
	 // thus no node to unlock
	 //printf("Failed to acquire a (locked) BerkeleyDB Dictionary Node for database '%s'\n", zName);
	 return SQLITE4_ERROR;
  }
  else
  {
     //printf("Succeeded to acquire a (locked) BerkeleyDB Dictionary Node for database '%s'\n", zName);
  }
    
  envp = pDictNode_zName->envp;
  if(envp == NULL)
  {
      //printf("----------> envp == NULL\n");
      
      /* Create the environment */
      ret = db_env_create(&envp, 0);
      if (ret != 0) 
      {
	    //printf("Error creating environment handle: %s\n", db_strerror(ret));
		unlock_locked_dict_node(pDictNode_zName);
        return SQLITE4_ERROR;
      }
	  else
	  {
	       //printf("------>Succeeded creating BerkeleyDB environment, envp=%p \n", envp);
	  }
      
      /*
      * Indicate that we want db to perform lock detection internally.
      * Also indicate that the transaction with the fewest number of
      * write locks will receive the deadlock notification in
      * the event of a deadlock.
      */
      ret = envp->set_lk_detect(envp, DB_LOCK_MINWRITE);
      if (ret != 0) 
      {
	     //printf("Error setting lock detect: %s\n", db_strerror(ret));
		 unlock_locked_dict_node(pDictNode_zName);
         return SQLITE4_ERROR;
      }
      
      env_flags =
         DB_CREATE     |  /* Create the environment if it does not exist */
         DB_RECOVER    |  /* Run normal recovery. */
         DB_INIT_LOCK  |  /* Initialize the locking subsystem */
         DB_INIT_LOG   |  /* Initialize the logging subsystem */
         DB_INIT_TXN   |  /* Initialize the transactional subsystem. This also turns on logging. */
         DB_INIT_MPOOL |  /* Initialize the memory pool (in-memory cache) */
         DB_THREAD;       /* Cause the environment to be free-threaded */
         
      /* Now actually open the environment */
      ret = envp->open(envp, NULL, env_flags, 0);
      if (ret != 0) 
      {
	     //printf("Error opening environment: %s\n", db_strerror(ret));
		 unlock_locked_dict_node(pDictNode_zName);
         return SQLITE4_ERROR;
      }
      else
	  {
	     //printf("------>Succeeded opening BerkeleyDB environment, envp=%p \n", envp);
	  }
  } // end of block if(envp == NULL){...}
  
  dbp  = pDictNode_zName->dbp;
  if(dbp == NULL)
  {
      //printf("----------> dbp == NULL\n");
      
      /* create the database */
      ret = db_create(&dbp, envp, 0);
      if (ret != 0)
      {
         //printf("Error creating database: %s\n", db_strerror(ret));
		 unlock_locked_dict_node(pDictNode_zName);
         return SQLITE4_ERROR;
      }
	  else
	  {
	     //printf("------>Succeeded creating BerkeleyDB database, dbp=%p, envp=%p \n", dbp, envp);
	  }
      
      /* set extra flags before open, if any needed */
      if (extra_flags != 0) 
      {
	     ret = dbp->set_flags(dbp, extra_flags);
	     if (ret != 0) 
         {
	        //printf("Error setting extra flags: %s\n", db_strerror(ret));
			unlock_locked_dict_node(pDictNode_zName);
            return SQLITE4_ERROR;
	     }
		 else
	     {
	        //printf("------>Succeeded setting flags for BerkeleyDB database, dbp=%p \n", dbp);
	     }
      }
      
      /* prepare open flags */
      open_flags = DB_CREATE              | /* Allow database creation */
		           DB_READ_UNCOMMITTED    | /* Allow dirty reads */
		           DB_AUTO_COMMIT         | /* Allow autocommit */
 		           DB_THREAD;               /* Cause the database to be free-threaded */
         
      /* Now open the database */
      ret = dbp->open(dbp, /* Pointer to the database */
		    NULL,          /* Txn pointer */
		    zName,         /* File name */
		    NULL,          /* Logical db name */
		    DB_BTREE,      /* Database type (using btree) */
		    open_flags,    /* Open flags */
		    0);
      if (ret != 0)
      {
          //printf("Error opening database '%s'. Error message: '%s'\n", zName, db_strerror(ret));
		  unlock_locked_dict_node(pDictNode_zName);
          return SQLITE4_ERROR;
      }
	  else
	  {
	     //printf("------>Succeeded opening BerkeleyDB database, zName='%s', dbp=%p \n", zName, dbp);
	  }
  } // end of block if(dbp == NULL){...}

  // Substitute back to pDictNode_zName
  pDictNode_zName->envp = envp;
  pDictNode_zName->dbp = dbp;
  
  // increase pDictNode_zName's reference counter
  pDictNode_zName->nref += 1;
  
  //printf("'%s' : envp=%p, dbp=%p, nref=%d\n", zName, pDictNode_zName->envp, pDictNode_zName->dbp, pDictNode_zName->nref);
    
  //KVBdb *pNew = sqlite4_malloc(pEnv, sizeof(*pNew) );
  KVBdb *pNew = (KVBdb*)sqlite4_malloc(pEnv, sizeof(KVBdb) );
  if( pNew==0 ) 
  {
     unlock_locked_dict_node(pDictNode_zName);
     return SQLITE4_NOMEM;
  }
  //memset(pNew, 0, sizeof(*pNew));
  memset(pNew, 0, sizeof(KVBdb));
  pNew->base.pStoreVfunc = &kvbdbMethods;
  pNew->base.pEnv = pEnv;
  pNew->iMagicKVBdbBase = SQLITE4_KVBDBBASE_MAGIC;
  pNew->openFlags = openFlags;
  //*ppKVStore = (KVStore*)pNew;
  
  
  
  
  pNew->dbp = dbp;   /* connection to BerkeleyDB database           */
  pNew->envp = envp; /* environment for above connection / database */
  strcpy(pNew->name, zName); /* database name */
 
  pNew->pCsr = NULL; /* Cursor for tead-only ops "outside" {?] transaction(s) [?]} */
  
  // NULL-initialize pNew->pTxn[...] 
  // i.e. transactions open in pNew, 
  // i.e. in database/connection
  // being just created
  {
      int i = 0;
      for(i = 0; i <= SQLITE4_KV_BDB_MAX_TXN_DEPTH; ++i)
      {
          pNew->pTxn[i] = NULL;
      }
  }
  
  // Set current transaction level 
  // for pNew, 
  // i.e. in database/connection
  // being just created, 
  // to 0 (ZERO).
  // We do it via a base instance/member, 
  // i.e.via KVStore and its member iTransLevel.
  pNew->base.iTransLevel = 0;

  // set initial buffers size(s) for Cursor's Key and Data Burrer(s) -- begin
  pNew->nInitialCursorKeyBufferCapacity = nGlobalDefaultInitialCursorKeyBufferCapacity;
  pNew->nInitialCursorDataBufferCapacity = nGlobalDefaultInitialCursorDataBufferCapacity;
  // set initial buffers size(s) for Cursor's Key and Data Burrer(s) -- end
  
  *ppKVStore = (KVStore*)pNew;
  
  //printf("'%s' : new KVBdb=%p\n", zName, pNew);
  
  unlock_locked_dict_node(pDictNode_zName);
  
  return SQLITE4_OK;
}

int sqlite4KVStoreOpenBdbMem(
   sqlite4_env *pEnv,              /* Runtime environment */
   KVStore **ppKVStore,            /* OUT: Write the new KVStore here */
   const char *zName,              /* Name of BerkeleyDB storage unit */
   unsigned openFlags              /* Flags */
) {
   //printf("-----> sqlite4KVStoreOpenBdb(), ZName=%s, pEnv=%p\n", zName, pEnv);

   DB_ENV * envp = NULL;
   u_int32_t env_flags = 0;

   DB     * dbp = NULL;
   u_int32_t extra_flags = 0;
   u_int32_t open_flags = 0;

   int ret;

   bdb_dict_node_t * pDictNode_zName = global_acquire_locked_dict_node(zName);
   if (!pDictNode_zName)
   {
      // cannot acquire a node, i.e. neither a locked node
      // thus no node to unlock
      //printf("Failed to acquire a (locked) BerkeleyDB Dictionary Node for database '%s'\n", zName);
      return SQLITE4_ERROR;
   }
   else
   {
      //printf("Succeeded to acquire a (locked) BerkeleyDB Dictionary Node for database '%s'\n", zName);
   }

   envp = pDictNode_zName->envp;
   if (envp == NULL)
   {
      //printf("----------> envp == NULL\n");

      /* Create the environment */
      ret = db_env_create(&envp, 0);
      if (ret != 0)
      {
         //printf("Error creating environment handle: %s\n", db_strerror(ret));
         unlock_locked_dict_node(pDictNode_zName);
         return SQLITE4_ERROR;
      }
      else
      {
         //printf("------>Succeeded creating BerkeleyDB environment, envp=%p \n", envp);
      }

      /*
      * Indicate that we want db to perform lock detection internally.
      * Also indicate that the transaction with the fewest number of
      * write locks will receive the deadlock notification in
      * the event of a deadlock.
      */
      ret = envp->set_lk_detect(envp, DB_LOCK_MINWRITE);
      if (ret != 0)
      {
         //printf("Error setting lock detect: %s\n", db_strerror(ret));
         unlock_locked_dict_node(pDictNode_zName);
         return SQLITE4_ERROR;
      }

      env_flags =
         DB_CREATE |  /* Create the environment if it does not exist */
         DB_INIT_LOCK |  /* Initialize the locking subsystem */
         DB_INIT_LOG |  /* Initialize the logging subsystem */
         DB_INIT_TXN |  /* Initialize the transactional subsystem. This also turns on logging. */
         DB_INIT_MPOOL |  /* Initialize the memory pool (in-memory cache) */
         DB_PRIVATE |  /* Region files are backed by heap memory.  */
         DB_THREAD;       /* Cause the environment to be free-threaded */

      // ==========================================================================
      /*
      * Specify in-memory logging
      */
      ret = envp->log_set_config(envp, DB_LOG_IN_MEMORY, 1);
      if (ret != 0)
      {
         printf("Error setting log subsystem to in-memory: %s\n", db_strerror(ret));
         unlock_locked_dict_node(pDictNode_zName);
         return SQLITE4_ERROR;
      }

      /*
      * Specify the size of the in-memory log buffer, 100 MB
      */
      ret = envp->set_lg_bsize(envp, 100 * 1024 * 1024);
      if (ret != 0)
      {
         printf("Error increasing the log buffer size: %s\n", db_strerror(ret));
         unlock_locked_dict_node(pDictNode_zName);
         return SQLITE4_ERROR;
      }

      /*
      * Specify the size of the in-memory cache, 2 GB
      */
      ret = envp->set_cachesize(envp, 0, 2 * 1024 * 1024 * 1024, 1);
      if (ret != 0)
      {
         printf("Error increasing the cache size: %s\n", db_strerror(ret));
         unlock_locked_dict_node(pDictNode_zName);
         return SQLITE4_ERROR;
      }

      /*
      * Indicate that we want db to perform lock detection internally.
      * Also indicate that the transaction with the fewest number of
      * write locks will receive the deadlock notification in
      * the event of a deadlock.
      */
      ret = envp->set_lk_detect(envp, DB_LOCK_MINWRITE);
      if (ret != 0)
      {
         printf("Error setting lock detect: %s\n", db_strerror(ret));
         unlock_locked_dict_node(pDictNode_zName);
         return SQLITE4_ERROR;
      }

      // ==========================================================================

                          /* Now actually open the environment */
      ret = envp->open(envp, NULL, env_flags, 0);
      if (ret != 0)
      {
         //printf("Error opening environment: %s\n", db_strerror(ret));
         unlock_locked_dict_node(pDictNode_zName);
         return SQLITE4_ERROR;
      }
      else
      {
         //printf("------>Succeeded opening BerkeleyDB environment, envp=%p \n", envp);
      }
   } // end of block if(envp == NULL){...}

   dbp = pDictNode_zName->dbp;
   if (dbp == NULL)
   {
      //printf("----------> dbp == NULL\n");

      /* create the database */
      ret = db_create(&dbp, envp, 0);
      if (ret != 0)
      {
         //printf("Error creating database: %s\n", db_strerror(ret));
         unlock_locked_dict_node(pDictNode_zName);
         return SQLITE4_ERROR;
      }
      else
      {
         //printf("------>Succeeded creating BerkeleyDB database, dbp=%p, envp=%p \n", dbp, envp);
      }

      // =================================================================
      /*
      * Keep Temporary Overflow Pages in Memory
      */
      DB_MPOOLFILE * mpf = dbp->get_mpf(dbp);
      ret = mpf->set_flags(mpf, DB_MPOOL_NOFILE, 1);
      if (ret != 0)
      {
         printf("Attempt failed to configure for no backing of temp files: %s\n", db_strerror(ret));
         unlock_locked_dict_node(pDictNode_zName);
         return SQLITE4_ERROR;
      }
      // =================================================================

      /* set extra flags before open, if any needed */
      if (extra_flags != 0)
      {
         ret = dbp->set_flags(dbp, extra_flags);
         if (ret != 0)
         {
            //printf("Error setting extra flags: %s\n", db_strerror(ret));
            unlock_locked_dict_node(pDictNode_zName);
            return SQLITE4_ERROR;
         }
         else
         {
            //printf("------>Succeeded setting flags for BerkeleyDB database, dbp=%p \n", dbp);
         }
      }

      /* prepare open flags */
      open_flags = DB_CREATE | /* Allow database creation */
         DB_AUTO_COMMIT | /* Allow autocommit */
         DB_THREAD;               /* Cause the database to be free-threaded */

                                  /* Now open the database */
      ret = dbp->open(dbp, /* Pointer to the database */
         NULL,          /* Txn pointer */
         NULL,         /* File name */
         zName,          /* Logical db name */
         DB_BTREE,      /* Database type (using btree) */
         open_flags,    /* Open flags */
         0);
      if (ret != 0)
      {
         //printf("Error opening database '%s'. Error message: '%s'\n", zName, db_strerror(ret));
         unlock_locked_dict_node(pDictNode_zName);
         return SQLITE4_ERROR;
      }
      else
      {
         //printf("------>Succeeded opening BerkeleyDB database, zName='%s', dbp=%p \n", zName, dbp);
      }
   } // end of block if(dbp == NULL){...}

     // Substitute back to pDictNode_zName
   pDictNode_zName->envp = envp;
   pDictNode_zName->dbp = dbp;

   // increase pDictNode_zName's reference counter
   pDictNode_zName->nref += 1;

   //printf("'%s' : envp=%p, dbp=%p, nref=%d\n", zName, pDictNode_zName->envp, pDictNode_zName->dbp, pDictNode_zName->nref);

   //KVBdb *pNew = sqlite4_malloc(pEnv, sizeof(*pNew) );
   KVBdb *pNew = (KVBdb*)sqlite4_malloc(pEnv, sizeof(KVBdb));
   if (pNew == 0)
   {
      unlock_locked_dict_node(pDictNode_zName);
      return SQLITE4_NOMEM;
   }
   //memset(pNew, 0, sizeof(*pNew));
   memset(pNew, 0, sizeof(KVBdb));
   pNew->base.pStoreVfunc = &kvbdbMethods;
   pNew->base.pEnv = pEnv;
   pNew->iMagicKVBdbBase = SQLITE4_KVBDBBASE_MAGIC;
   pNew->openFlags = openFlags;
   //*ppKVStore = (KVStore*)pNew;




   pNew->dbp = dbp;   /* connection to BerkeleyDB database           */
   pNew->envp = envp; /* environment for above connection / database */
   strcpy(pNew->name, zName); /* database name */

   pNew->pCsr = NULL; /* Cursor for tead-only ops "outside" {?] transaction(s) [?]} */

                      // NULL-initialize pNew->pTxn[...] 
                      // i.e. transactions open in pNew, 
                      // i.e. in database/connection
                      // being just created
   {
      int i = 0;
      for (i = 0; i <= SQLITE4_KV_BDB_MAX_TXN_DEPTH; ++i)
      {
         pNew->pTxn[i] = NULL;
      }
   }

   // Set current transaction level 
   // for pNew, 
   // i.e. in database/connection
   // being just created, 
   // to 0 (ZERO).
   // We do it via a base instance/member, 
   // i.e.via KVStore and its member iTransLevel.
   pNew->base.iTransLevel = 0;

   // set initial buffers size(s) for Cursor's Key and Data Burrer(s) -- begin
   pNew->nInitialCursorKeyBufferCapacity = nGlobalDefaultInitialCursorKeyBufferCapacity;
   pNew->nInitialCursorDataBufferCapacity = nGlobalDefaultInitialCursorDataBufferCapacity;
   // set initial buffers size(s) for Cursor's Key and Data Burrer(s) -- end

   *ppKVStore = (KVStore*)pNew;

   //printf("'%s' : new KVBdb=%p\n", zName, pNew);

   unlock_locked_dict_node(pDictNode_zName);

   return SQLITE4_OK;
}