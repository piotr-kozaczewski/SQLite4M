#ifndef _SQLITE4_KVWT_COMMON_H_
#define _SQLITE4_KVWT_COMMON_H_

#include "kvwt.h"

#include "sqlite4.h"
#include "sqliteInt.h"

#include <assert.h>
#include <malloc.h>
#include "stdlib.h"
#include "stdio.h"

#include "wiredtiger.h"

#include <mutex>
#include <atomic>

extern uint32_t nGlobalDefaultInitialCursorKeyBufferCapacity;
extern uint32_t nGlobalDefaultInitialCursorDataBufferCapacity;

//typedef struct sqlite4_env sqlite4_env;
//typedef struct sqlite4_kvstore sqlite4_kvstore;

typedef struct KVWT KVWT;
typedef struct KVWTCursor KVWTCursor;

//#ifdef WIN32 // already typedef'ed in "kvwt.h"
//typedef __int32 int32_t;
//typedef unsigned __int32 uint32_t;
//#endif

#define SQLITE4_KV_WT_MAX_TXN_DEPTH 16
#define SQLITE4_KVWTBASE_MAGIC  0xdfeb57f1
struct KVWT
{
   sqlite4_kvstore base;

   unsigned openFlags;   /* Flags used at open */
   int nCursor;          /* Number of outstanding cursors */
   int iMagicKVWTBase;  /* Magic number of sanity */
   unsigned int iMeta;   /* Schema cookie value */


   WT_CONNECTION *conn;
   WT_SESSION * session;

   char name[128];
   char table_name[128];

   WT_CURSOR *pCsr; /* Cursor for read-only ops "outside" {?] transaction(s) [?]} */

   // Please note, however, 
   // that WiredTiger does not support Nested Transactions 
   // at least when operating at  READ_COMMITTED 
   // Transaction Isolation Level.
   // pTxnCsr[] member is preserved from KVBDB, 
   // to facilitate reuse of the well tested methods.
   WT_CURSOR * pTxnCsr[SQLITE4_KV_WT_MAX_TXN_DEPTH + 1]; /* transaction(s) open in given database/connection*/

   // for cursors -- begin
   uint32_t nInitialCursorKeyBufferCapacity;
   uint32_t nInitialCursorDataBufferCapacity;
   // for cursors -- end

   KVWT()
      : openFlags(0)
      , nCursor(0)
      , iMagicKVWTBase(SQLITE4_KVWTBASE_MAGIC)
      , iMeta(0)
      , conn(nullptr)
      , session(nullptr)
      , pCsr(nullptr)
      //, nInitialCursorKeyBufferCapacity(0)
      , nInitialCursorKeyBufferCapacity(nGlobalDefaultInitialCursorKeyBufferCapacity)
      //, nInitialCursorDataBufferCapacity(0)
      , nInitialCursorDataBufferCapacity(nGlobalDefaultInitialCursorDataBufferCapacity)
   {
      memset(name, 0, 128);
      memset(table_name, 0, 128);

      for (size_t i = 0; i < SQLITE4_KV_WT_MAX_TXN_DEPTH + 1; ++i)
      {
         pTxnCsr[i] = 0;
      }
   } // KVWT(){...}

   KVWT(const char * zName)
      : KVWT()
   {
      // set name(s)
      strcpy(this->name, zName);
      // REMARK: 
      // table_name will be taken from the appropriate KVWTEnv object
   }

   ~KVWT()
   {
      if (pCsr)
      {
         pCsr->close(pCsr);
         pCsr = nullptr;
      }
      //
      if (session)
      {
         session->close(session, nullptr);
         session = nullptr;
      }
      //
      conn = nullptr; // We don't own conn.
      //
      nCursor = 0;
      //
      memset(name, 0, 128);
      memset(table_name, 0, 128);
      //
      for (size_t i = 0; i < SQLITE4_KV_WT_MAX_TXN_DEPTH + 1; ++i)
      {
         pTxnCsr[i] = 0;
      }
      //
      nInitialCursorKeyBufferCapacity = 0;
      nInitialCursorDataBufferCapacity = 0;
   } // ~KVWT(){...}
};
//#define SQLITE4_KVWTBASE_MAGIC  0xdfeb57f1

/*
** A cursor used for scanning through the tree
*/
#define SEEK_DIR_NONE      0
#define SEEK_DIR_EQ        1
#define SEEK_DIR_GT        2
#define SEEK_DIR_GE        3
#define SEEK_DIR_LT        4
#define SEEK_DIR_LE        5

struct KVWTCursor {
   KVCursor base;        /* Base class. Must be first */
   KVWT *pOwner;         /* The tree that owns this cursor */
   int iMagicKVWTCur;    /* Magic number for sanity */
   //
   WT_CURSOR * pCsr; // the underlying WiredTiger Cursor
   //
   // Cached Key & Data Buffers -- begin
   int nHasKeyAndDataCached;
   void * pCachedKey;
   uint32_t nCachedKeySize;
   uint32_t nCachedKeyCapacity;
   void * pCachedData;
   uint32_t nCachedDataSize;
   uint32_t nCachedDataCapacity;
   // Cached Key & Data Buffers -- end

   int nIsEOF;

   int nLastSeekDir;
};
#define SQLITE4_KVWTCUR_MAGIC   0xd09afc30

/* Virtual methods for the WiredTiger storage engine */
extern const KVStoreMethods kvwtMethods;

#endif /* end of #ifndef _SQLITE4_KVWT_COMMON_H_ */