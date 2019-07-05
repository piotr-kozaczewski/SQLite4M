#include "kvwt_common.h"

#include <algorithm> // for std::min<...>(...)

uint32_t nGlobalDefaultInitialCursorKeyBufferCapacity = 16384; // 16 k
uint32_t nGlobalDefaultInitialCursorDataBufferCapacity = 16384; // 16 k

std::atomic<size_t> oCounter(1); // transactions' counter, zero (0) not permitted as txn counter/timestamp!



int kvwtReplace(
   sqlite4_kvstore *pKVStore,
   const KVByteArray *aKey, KVSize nKey,
   const KVByteArray *aData, KVSize nData
   ) {
   //printf("-----> kvwtReplace()\n");  

   KVWT *p;

   p = (KVWT *)pKVStore;
   assert(p->iMagicKVWTBase == SQLITE4_KVWTBASE_MAGIC);

   int rc = SQLITE4_OK;

   // Retrievee KVStore and WiredTiger "context" variables -- begin
   int nCurrTxnLevel = pKVStore->iTransLevel; // p->base.iTransLevel
   WT_CURSOR * pCurrTxnCsr = p->pTxnCsr[nCurrTxnLevel];
   WT_SESSION * psession = p->session;
   // Retrievee KVStore and WiredTiger "context" variables -- end

   // Cross-checks -- begin
   assert(nCurrTxnLevel >= 2);
   if (nCurrTxnLevel < 2)
   {
      //printf("Internal Error : nCurrTxnLevel < 2\n");
      rc = SQLITE4_INTERNAL; // or: SQLITE4_MISUSE
   }

   assert(pCurrTxnCsr != NULL);
   if (pCurrTxnCsr == NULL)
   {
      //printf("Internal Error : pCurrTxnCsr == NULL\n");
      rc = SQLITE4_INTERNAL; // or: SQLITE4_MISUSE
   }

   assert(psession != NULL);
   if (psession == NULL)
   {
      //printf("Internal Error : psession == NULL\n");
      rc = SQLITE4_INTERNAL; // or: SQLITE4_MISUSE
   }
   // Cross-checks -- end

   if (rc == SQLITE4_OK)
   {
      // So far OK

      // Prepare WT_ITEM's for Key and Data -- begin
      WT_ITEM oKey;
      oKey.data = aKey;
      oKey.size = nKey;

      WT_ITEM oData;
      oData.data = aData;
      oData.size = nData;
      // Prepare WT_ITEM's for Key and Data -- end

      int ret = 0;

      pCurrTxnCsr->set_key(pCurrTxnCsr, oKey);
      pCurrTxnCsr->set_value(pCurrTxnCsr, oData);
      ret = pCurrTxnCsr->insert(pCurrTxnCsr);

      switch (ret)
      {
      case 0: // OK
         rc = SQLITE4_OK;
         break;
      case WT_DUPLICATE_KEY:
         rc = SQLITE4_CONSTRAINT;
         break;
      case WT_NOTFOUND: // Is it relevant for replace/insert?
         rc = SQLITE4_NOTFOUND;
         break;
      case WT_ROLLBACK:
         // Integrate with SQLite4/M diagnostics!
         printf("kvwtReplace() deadlock_resolved / needs_rollback_and_then_can_be_retried : error : '%s'\n", psession->strerror(psession, ret));
         rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
         break;
      case WT_PREPARE_CONFLICT:
         // Integrate with SQLite4/M diagnostics!
         printf("kvwtReplace() prepare conflict : error : '%s'\n", psession->strerror(psession, ret));
         rc = SQLITE4_ERROR; // or: SQLITE4_BUSY
         break;
      case WT_CACHE_FULL:
         // Integrate with SQLite4/M diagnostics!
         printf("kvwtReplace() no more cache memory : current txn may be rolled-back and retried, and failed after exhaustion on attempt number limit: error : '%s'\n", psession->strerror(psession, ret));
         //rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY // ???
         // rc = SQLITE4_FULL; // ???
         rc = SQLITE4_NOMEM; // ???
         break;
      case EINVAL:  // If the cursor is already closed; or if an invalid flag value or parameter was specified. 
                    // Integrate with SQLite4/M diagnostics!
         printf("kvwtReplace() failed : error : '%s'\n", psession->strerror(psession, ret));
         //rc = SQLITE4_ERROR; // ?
         rc = SQLITE4_MISUSE; // Library used incorrectly (!!!)
         break;
      default:
         // Integrate with SQLite4/M diagnostics!
         printf("kvwtReplace() FAILED : error : '%s'\n", psession->strerror(psession, ret));
         rc = SQLITE4_ERROR; // ?
         break;
      };  // end of switch (ret){...}
   } // end of if (rc == SQLITE4_OK){...}

   return rc;
}

int kvwtOpenCursor(sqlite4_kvstore * pkvstore, sqlite4_kvcursor ** ppkvcursor) 
{
   //printf("-----> kvwtOpenCursor()\n");  

   KVWT *p;

   p = (KVWT *)pkvstore;
   assert(p->iMagicKVWTBase == SQLITE4_KVWTBASE_MAGIC);
   //assert( p->base.iTransLevel>=2 ); 

   int rc = SQLITE4_OK;

   KVWTCursor * pCsr = NULL;
   pCsr = (KVWTCursor*)sqlite4_malloc(pkvstore->pEnv, sizeof(KVWTCursor));
   if (pCsr == 0)
   {
      // sqlite4_malloc failed
      rc = SQLITE4_NOMEM;
   }
   else
   {
      // sqlite4_malloc succeeded
      memset(pCsr, 0, sizeof(KVWTCursor));
      // retrieve necessary WiredTiger objects -- begin
      WT_SESSION * psession = p->session;
      assert(psession != NULL);
      int nCurrTxnLevel = pkvstore->iTransLevel; // p->base.iTransLevel
      WT_CURSOR * pCurrTxnCsr = p->pTxnCsr[nCurrTxnLevel];
      // retrieve necessary WiredTiger objects -- end
      WT_CURSOR * pNewWiredTigerCursor = NULL;
      int ret = 0;
      if (nCurrTxnLevel == 0)
      {
         pNewWiredTigerCursor = p->pCsr;

         if (pNewWiredTigerCursor == NULL)
         {
            // (re-)open a cursor for read-only ops
            ret = psession->open_cursor(psession, p->table_name, NULL, "overwrite=false", &pNewWiredTigerCursor);
            if (ret == 0)
            {
               // success
               // save the (re-)opened cursor for read-nly ops
               p->pCsr = pNewWiredTigerCursor;
            }
            else
            {
               // What else?
            }
         }
      } // end of if (nCurrTxnLevel == 0){...}
      else if (nCurrTxnLevel == 1)
      {
         if (pCurrTxnCsr == 0)
         {
            // no WiredTiger TXN, handle it similarly to nCurrTxnLevel == 0 // ???
            pNewWiredTigerCursor = p->pCsr;

            if (pNewWiredTigerCursor == NULL)
            {
               // (re-)open a cursor for read-only ops
               ret = psession->open_cursor(psession, p->table_name, NULL, "overwrite=false", &pNewWiredTigerCursor);
               if (ret == 0)
               {
                  // success 
                  // save the (re-)opened cursor for read-nly ops
                  p->pCsr = pNewWiredTigerCursor;
               }
               else
               {
                  // What else?
               }
            }
         }
         else
         {
            // existent WiredTiger TXN, handle it similarly to nCurrTxnLevel >= 2 // ??? 
            // open a cursor for current TXN
            ret = psession->open_cursor(psession, p->table_name, NULL, "overwrite=false", &pNewWiredTigerCursor);
         }
      } // end of else if (nCurrTxnLevel == 1){...}
      else if (nCurrTxnLevel >= 2)
      {
         assert(pCurrTxnCsr); // It is probably an INTERNAL ERROR, i.e. inconsistent use case ...

                           // existent WiredTiger TXN, handle it similarly to nCurrTxnLevel >= 2 // ??? 
                           // open a cursor for current TXN
         ret = psession->open_cursor(psession, p->table_name, NULL, "overwrite=false", &pNewWiredTigerCursor);
      } // end of else if (nCurrTxnLevel >= 2){...}
      switch (ret)
      {
      case 0: // OK
              // Fill members of the base structure sqlite4_kvcursor, 
              // as defined in "sqlite.h.in" file
         pCsr->base.pStore = pkvstore;
         pCsr->base.pStoreVfunc = pkvstore->pStoreVfunc;
         pCsr->base.pEnv = pkvstore->pEnv;
         pCsr->base.iTransLevel = pkvstore->iTransLevel;
         pCsr->base.fTrace = 0; // disable tracing
                                // Fill members of the derived structure KVWTCursor, 
                                // as defined in "kvwt_common.h" file
         pCsr->pOwner = p; // ptr to the underlying KVStore, i.e. to the structure KVWT
         pCsr->pCsr = pNewWiredTigerCursor; // the underlying WiredTiger cursor
          
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
         pCsr->iMagicKVWTCur = SQLITE4_KVWTCUR_MAGIC;
         //
         pCsr->nIsEOF = 0; // EOF not encountered yet 
         pCsr->nLastSeekDir = SEEK_DIR_NONE;
         //
         *ppkvcursor = (KVCursor*)pCsr;
         rc = SQLITE4_OK;
         break;
      case WT_ROLLBACK:
         // Integrate with SQLite4/M diagnostics!
         printf("kvwtOpenCursor() deadlock_resolved / needs_rollback_and_then_can_be_retried : pKVStore = %p. pkvstore->iTransLevel = %d : error : '%s'\n", pkvstore, pkvstore->iTransLevel, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
         rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
         break;
      case WT_PREPARE_CONFLICT:
         // Integrate with SQLite4/M diagnostics!
         printf("kvwtOpenCursor() prepare conflict : pKVStore = %p. pKVStore->iTransLevel = %d : error : '%s'\n", pkvstore,  pkvstore->iTransLevel, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
         rc = SQLITE4_ERROR; // or: SQLITE4_BUSY
         break;
      case WT_CACHE_FULL:
         // Integrate with SQLite4/M diagnostics!
         printf("kvwtOpenCursor() no more cache memory : current txn may be rolled-back and retried, and failed after exhaustion on attempt number limit: pKVStore = %p. pKVStore->iTransLevel = %d : error : '%s'\n", pkvstore, pkvstore->iTransLevel, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
         //rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY // ???
         // rc = SQLITE4_FULL; // ???
         rc = SQLITE4_NOMEM; // ???
         break;
      case EINVAL:  // If the cursor is already closed; or if an invalid flag value or parameter was specified. 
                    // Integrate with SQLite4/M diagnostics!
         printf("kvwtOpenCursor() failed : pKVStore = %p, pKVStore->iTransLevel = %d : error : '%s'\n", pkvstore,  pkvstore->iTransLevel,  wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
         //rc = SQLITE4_ERROR; // ?
         rc = SQLITE4_MISUSE; // Library used incorrectly (!!!)
         break;
      default:
         // Integrate with SQLite4/M diagnostics!
         printf("kvwtOpenCursor() FAILED : pKVStore = %p,  pKVStore->iTransLevel = %d : error : '%s'\n", pkvstore,  pkvstore->iTransLevel,  wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
         rc = SQLITE4_ERROR; // ?
         break;
      }; // end of switch (ret){...}
   } // end of else-block for if-block  if (pCsr == 0){...}
   if (rc != SQLITE4_OK)
   {
      if (rc != SQLITE4_NOMEM)
      {
         // free the useless allocated memory
         sqlite4_free(pkvstore->pEnv, pCsr);
      }
   }
   //KVBdbOpenCursor_nomem:
   //  return SQLITE4_NOMEM;  
   return rc;
} // end of : kvwtOpenCursor(...){...}



int  kvwtSeekEQ(WT_CURSOR * cursor, WT_ITEM & oKey, int * nIsEOF)
{
   int ret = 0;
   int rc = 0;
   
   cursor->set_key(cursor, &oKey);
   
   ret = cursor->search(cursor);
   
   switch (ret)
   {
   case 0: // OK
      rc = SQLITE4_OK;
      break;
   case WT_NOTFOUND:
      rc = SQLITE4_NOTFOUND;
      break;
   case WT_ROLLBACK:
      // Integrate with SQLite4/M diagnostics!
      printf("kvwtSeekEQ() deadlock_resolved / needs_rollback_and_then_can_be_retried : error : '%s'\n", cursor->session->strerror(cursor->session, ret) );
      rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
      break;
   case WT_PREPARE_CONFLICT:
      // Integrate with SQLite4/M diagnostics!
      printf("kvwtSeekEQ() prepare conflict : error : '%s'\n", cursor->session->strerror(cursor->session, ret));
      rc = SQLITE4_ERROR; // or: SQLITE4_BUSY
      break;
   case WT_CACHE_FULL:
      // Integrate with SQLite4/M diagnostics!
      printf("kvwtSeekEQ() no more cache memory : current txn may be rolled-back and retried, and failed after exhaustion on attempt number limit: error : '%s'\n", cursor->session->strerror(cursor->session, ret));
      //rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY // ???
      // rc = SQLITE4_FULL; // ???
      rc = SQLITE4_NOMEM; // ???
      break;
   case EINVAL:  // If the cursor is already closed; or if an invalid flag value or parameter was specified. 
                 // Integrate with SQLite4/M diagnostics!
      printf("kvwtSeekEQ() failed : error : '%s'\n", cursor->session->strerror(cursor->session, ret));
      //rc = SQLITE4_ERROR; // ?
      rc = SQLITE4_MISUSE; // Library used incorrectly (!!!)
      break;
   default:
      // Integrate with SQLite4/M diagnostics!
      printf("kvwtSeekEQ() FAILED : error : '%s'\n", cursor->session->strerror(cursor->session, ret));
      rc = SQLITE4_ERROR; // ?
      break;
   };

   return rc;
}

int  kvwtSeekLE(WT_CURSOR * cursor, WT_ITEM & oKey, int * nIsEOF)
{
   int ret = 0;
   int rc = 0;

   int exact = 0;

   cursor->set_key(cursor, &oKey);

   rc = cursor->search_near(cursor, &exact);

   if (rc != 0)
   {
      // error || not found
      ret = SQLITE4_NOTFOUND;
   }
   else
   {
      // something found
      if (exact == 0)
      {
         // EXACT MATCH
         ret = SQLITE4_OK;
      }
      else if (exact < 0)
      {
         // INEXACT MATCH, LT/LE
         ret = SQLITE4_INEXACT;
      }
      else
      {
         // exact > 0
         // INEXACT MATCH, GT/GE
         // must scan back, using cursor->prev(...)
         WT_ITEM oKey1;
         int rc1 = 0;
         int rc2 = 0;

         while (1)
         {
            rc1 = cursor->prev(cursor);
            if (rc1 != 0)
            {
               // error (?)
               ret = SQLITE4_NOTFOUND;
               *nIsEOF = 1;
               break;
            }
            rc2 = cursor->get_key(cursor, &oKey1);
            if (rc2 != 0)
            {
               // error (?)
               ret = SQLITE4_NOTFOUND;
               *nIsEOF = 1;
               break;
            }
            size_t nCompareSize = std::min<size_t>(oKey1.size, oKey.size);
            //if (memcmp(oKey.data, (void*)cKey, nCompareSize) <= 0) // original line from test_015.cpp
            if (memcmp(oKey1.data, oKey.data, nCompareSize) <= 0)
            {
               //// oKey.data <= cKey : found LE // original line from test_015.cpp
               // oKey1.data <= oKey.data : found LE
               ret = SQLITE4_INEXACT;
               break;
            }
         } // end loop while(1){...}
         if (ret != SQLITE4_NOTFOUND && ret != SQLITE4_INEXACT)
         {
            if (rc1)
            {
               // error during prev()
               switch (rc1)
               {
               case WT_NOTFOUND:
                  ret = SQLITE4_NOTFOUND;
                  break;
               case WT_ROLLBACK:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtSeekLE() deadlock_resolved / needs_rollback_and_then_can_be_retried : error : '%s'\n", cursor->session->strerror(cursor->session, ret));
                  ret = SQLITE4_LOCKED; // or: SQLITE4_BUSY
                  break;
               case WT_PREPARE_CONFLICT:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtSeekLE() prepare conflict : error : '%s'\n", cursor->session->strerror(cursor->session, ret));
                  ret = SQLITE4_ERROR; // or: SQLITE4_BUSY
                  break;
               case WT_CACHE_FULL:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtSeekLE() no more cache memory : current txn may be rolled-back and retried, and failed after exhaustion on attempt number limit: error : '%s'\n", cursor->session->strerror(cursor->session, ret));
                  //rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY // ???
                  // rc = SQLITE4_FULL; // ???
                  ret = SQLITE4_NOMEM; // ???
                  break;
               case EINVAL:  // If the cursor is already closed; or if an invalid flag value or parameter was specified. 
                             // Integrate with SQLite4/M diagnostics!
                  printf("kvwtSeekLE() failed : error : '%s'\n", cursor->session->strerror(cursor->session, ret));
                  //rc = SQLITE4_ERROR; // ?
                  ret = SQLITE4_MISUSE; // Library used incorrectly (!!!)
                  break;
               default:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtSeekLE() FAILED : error : '%s'\n", cursor->session->strerror(cursor->session, ret));
                  ret = SQLITE4_ERROR; // ?
                  break;
               };
            }
            else
            {
               // prev() OK, maybe an error during get_key(...)
               if (rc2)
               {
                  switch (rc2)
                  {
                  case WT_NOTFOUND:
                     ret = SQLITE4_NOTFOUND;
                     break;
                  case WT_ROLLBACK:
                     // Integrate with SQLite4/M diagnostics!
                     printf("kvwtSeekLE() deadlock_resolved / needs_rollback_and_then_can_be_retried : error : '%s'\n", cursor->session->strerror(cursor->session, ret));
                     ret = SQLITE4_LOCKED; // or: SQLITE4_BUSY
                     break;
                  case WT_PREPARE_CONFLICT:
                     // Integrate with SQLite4/M diagnostics!
                     printf("kvwtSeekLE() prepare conflict : error : '%s'\n", cursor->session->strerror(cursor->session, ret));
                     ret = SQLITE4_ERROR; // or: SQLITE4_BUSY
                     break;
                  case WT_CACHE_FULL:
                     // Integrate with SQLite4/M diagnostics!
                     printf("kvwtSeekLE() no more cache memory : current txn may be rolled-back and retried, and failed after exhaustion on attempt number limit: error : '%s'\n", cursor->session->strerror(cursor->session, ret));
                     //rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY // ???
                     // rc = SQLITE4_FULL; // ???
                     ret = SQLITE4_NOMEM; // ???
                     break;
                  case EINVAL:  // If the cursor is already closed; or if an invalid flag value or parameter was specified. 
                                // Integrate with SQLite4/M diagnostics!
                     printf("kvwtSeekLE() failed : error : '%s'\n", cursor->session->strerror(cursor->session, ret));
                     //rc = SQLITE4_ERROR; // ?
                     ret = SQLITE4_MISUSE; // Library used incorrectly (!!!)
                     break;
                  default:
                     // Integrate with SQLite4/M diagnostics!
                     printf("kvwtSeekLE() FAILED : error : '%s'\n", cursor->session->strerror(cursor->session, ret));
                     ret = SQLITE4_ERROR; // ?
                     break;
                  };
               }
            }
            // some error occured that needs analysis and handling
         }
      } // end of else{...}-block for if (exact == 0){...}
   } // end of else{..}-block for if (rc != 0){...}


   return ret;
}

int  kvwtSeekGE(WT_CURSOR * cursor, WT_ITEM & oKey, int * nIsEOF)
{
   int ret = 0;
   int rc = 0;

   int exact = 0;

   cursor->set_key(cursor, &oKey);

   rc = cursor->search_near(cursor, &exact);

   if (rc != 0)
   {
      // error || not found
      ret = SQLITE4_NOTFOUND;
   }
   else
   {
      // something found
      if (exact == 0)
      {
         // EXACT MATCH
         ret = SQLITE4_OK;
      }
      else if (exact > 0)
      {
         // INEXACT MATCH, GT/GE
         ret = SQLITE4_INEXACT;
      }
      else
      {
         // exact < 0
         // INEXACT MATCH, LT/LE
         // must scan forth, using cursor->next(...)
         WT_ITEM oKey1;
         int rc1 = 0;
         int rc2 = 0;

         while (1)
         {
            rc1 = cursor->next(cursor);
            if (rc1 != 0)
            {
               // error (?)
               ret = SQLITE4_NOTFOUND;
               *nIsEOF = 1;
               break;
            }
            rc2 = cursor->get_key(cursor, &oKey1);
            if (rc2 != 0)
            {
               // error (?)
               ret = SQLITE4_NOTFOUND;
               *nIsEOF = 1;
               break;
            }
            //size_t nCompareSize = std::min<size_t>(oKey1.size, nKey);
            size_t nCompareSize = std::min<size_t>(oKey1.size, oKey.size);
            //if (memcmp(oKey.data, (void*)cKey, nCompareSize) >= 0)
            if (memcmp(oKey1.data, oKey.data, nCompareSize) >= 0)
            {
               // oKey.data >= cKey : found GE
               ret = SQLITE4_INEXACT;
               break;
            }
         } // end loop while(1){...}
         if (ret != SQLITE4_NOTFOUND && ret != SQLITE4_INEXACT)
         {
            if (rc1)
            {
               // error during next()
               switch (rc1)
               {
               case WT_NOTFOUND:
                  ret = SQLITE4_NOTFOUND;
                  break;
               case WT_ROLLBACK:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtSeekGE() deadlock_resolved / needs_rollback_and_then_can_be_retried : error : '%s'\n", cursor->session->strerror(cursor->session, ret));
                  ret = SQLITE4_LOCKED; // or: SQLITE4_BUSY
                  break;
               case WT_PREPARE_CONFLICT:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtSeekGE() prepare conflict : error : '%s'\n", cursor->session->strerror(cursor->session, ret));
                  ret = SQLITE4_ERROR; // or: SQLITE4_BUSY
                  break;
               case WT_CACHE_FULL:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtSeekGE() no more cache memory : current txn may be rolled-back and retried, and failed after exhaustion on attempt number limit: error : '%s'\n", cursor->session->strerror(cursor->session, ret));
                  //rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY // ???
                  // rc = SQLITE4_FULL; // ???
                  ret = SQLITE4_NOMEM; // ???
                  break;
               case EINVAL:  // If the cursor is already closed; or if an invalid flag value or parameter was specified. 
                             // Integrate with SQLite4/M diagnostics!
                  printf("kvwtSeekGE() failed : error : '%s'\n", cursor->session->strerror(cursor->session, ret));
                  //rc = SQLITE4_ERROR; // ?
                  ret = SQLITE4_MISUSE; // Library used incorrectly (!!!)
                  break;
               default:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtSeekGE() FAILED : error : '%s'\n", cursor->session->strerror(cursor->session, ret));
                  ret = SQLITE4_ERROR; // ?
                  break;
               };
            }
            else
            {
               // next() OK, maybe an error during get_key(...)
               if (rc2)
               {
                  switch (rc2)
                  {
                  case WT_NOTFOUND:
                     ret = SQLITE4_NOTFOUND;
                     break;
                  case WT_ROLLBACK:
                     // Integrate with SQLite4/M diagnostics!
                     printf("kvwtSeekGE() deadlock_resolved / needs_rollback_and_then_can_be_retried : error : '%s'\n", cursor->session->strerror(cursor->session, ret));
                     ret = SQLITE4_LOCKED; // or: SQLITE4_BUSY
                     break;
                  case WT_PREPARE_CONFLICT:
                     // Integrate with SQLite4/M diagnostics!
                     printf("kvwtSeekGE() prepare conflict : error : '%s'\n", cursor->session->strerror(cursor->session, ret));
                     ret = SQLITE4_ERROR; // or: SQLITE4_BUSY
                     break;
                  case WT_CACHE_FULL:
                     // Integrate with SQLite4/M diagnostics!
                     printf("kvwtSeekGE() no more cache memory : current txn may be rolled-back and retried, and failed after exhaustion on attempt number limit: error : '%s'\n", cursor->session->strerror(cursor->session, ret));
                     //rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY // ???
                     // rc = SQLITE4_FULL; // ???
                     ret = SQLITE4_NOMEM; // ???
                     break;
                  case EINVAL:  // If the cursor is already closed; or if an invalid flag value or parameter was specified. 
                                // Integrate with SQLite4/M diagnostics!
                     printf("kvwtSeekGE() failed : error : '%s'\n", cursor->session->strerror(cursor->session, ret));
                     //rc = SQLITE4_ERROR; // ?
                     ret = SQLITE4_MISUSE; // Library used incorrectly (!!!)
                     break;
                  default:
                     // Integrate with SQLite4/M diagnostics!
                     printf("kvwtSeekGE() FAILED : error : '%s'\n", cursor->session->strerror(cursor->session, ret));
                     ret = SQLITE4_ERROR; // ?
                     break;
                  };
               }
            }
            // some error occured that needs analysis and handling
         }
      } // end of else{...}-block for if (exact == 0){...}
   } // end of else{..}-block for if (rc != 0){...}


   return ret;
}

int kvwtSeek(
   sqlite4_kvcursor *pKVCursor,
   const KVByteArray *aKey,
   KVSize nKey,
   int direction
   )
{
   //printf("-----> kvwtSeek(), KVCursor=%p\n", pKVCursor);

   KVWTCursor *pCur;
   KVWT *p;

   pCur = (KVWTCursor*)pKVCursor;
   assert(pCur->iMagicKVWTCur == SQLITE4_KVWTCUR_MAGIC);
   p = (KVWT *)(pCur->pOwner);
   assert(p->iMagicKVWTBase == SQLITE4_KVWTBASE_MAGIC);
   //assert( p->base.iTransLevel>=2 ); 

   int rc = SQLITE4_OK;

   // Retrievee KVStore and WiredTiger "context" variables -- begin
   int nCurrTxnLevel = p->base.iTransLevel;
   WT_CURSOR * pCurrTxnCsr = p->pTxnCsr[nCurrTxnLevel];
   WT_SESSION * psession = p->session;
   WT_CURSOR * pCurrWiredTigerCursor = pCur->pCsr;
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

   assert(psession != NULL);
   if (psession == NULL)
   {
      //printf("Internal Error : psession == NULL\n");
      rc = SQLITE4_INTERNAL; // or: SQLITE4_MISUSE
   }

   assert(pCurrWiredTigerCursor != NULL);
   if (pCurrWiredTigerCursor == NULL)
   {
      //printf("Internal Error : pCurrWiredTigerCursor == NULL\n");
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

   // Prepare a WT_ITEM for a key.
   WT_ITEM oKey;
   oKey.data = (void*)aKey;
   oKey.size = (size_t)nKey;

   // Use helper routines to seek in appropriate direction
   if (direction == 0) // exact match requested, seek EQ
   {
      rc = kvwtSeekEQ(
         pCurrWiredTigerCursor,
         oKey,
         &pCur->nIsEOF);
      if (rc == SQLITE4_OK || rc == SQLITE4_INEXACT)
      {
         pCur->nLastSeekDir = SEEK_DIR_EQ;
      }
   }
   else if (direction < 0) // seek LE
   {
      rc = kvwtSeekLE(
         pCurrWiredTigerCursor,
         oKey,
         &pCur->nIsEOF);
      if (rc == SQLITE4_OK || rc == SQLITE4_INEXACT)
      {
         pCur->nLastSeekDir = SEEK_DIR_LE;
      }
   }
   else if (direction > 0) // seek GE
   {
      rc = kvwtSeekGE(
         pCurrWiredTigerCursor,
         oKey,
         &pCur->nIsEOF);
      if (rc == SQLITE4_OK || rc == SQLITE4_INEXACT)
      {
         pCur->nLastSeekDir = SEEK_DIR_GE;
      }
   }

   return rc;
} // end of : kvwtSeek(...){...}

int kvwtNext(sqlite4_kvcursor *pKVCursor) {
   //printf("-----> kvwtNext()\n");  

   KVWTCursor *pCur;
   KVWT *p;

   pCur = (KVWTCursor*)pKVCursor;
   assert(pCur->iMagicKVBdbCur == SQLITE4_KVWTCUR_MAGIC);
   p = (KVWT *)(pCur->pOwner);
   assert(p->iMagicKVWTBase == SQLITE4_KVWTBASE_MAGIC);
   //assert( p->base.iTransLevel>=2 );  


   int rc = SQLITE4_OK;

   // Retrievee KVStore and WiredTige "context" variables -- begin
   int nCurrTxnLevel = p->base.iTransLevel;
   WT_CURSOR * pCurrTxnCsr = p->pTxnCsr[nCurrTxnLevel];
   WT_SESSION * psession = p->session;
   WT_CURSOR * pCurrWiredTigerCursor = pCur->pCsr;
   // Retrievee KVStore and WiredTige "context" variables -- end

   if (pCur->nLastSeekDir != SEEK_DIR_GE
      && pCur->nLastSeekDir != SEEK_DIR_EQ
      && pCur->nLastSeekDir != SEEK_DIR_GT)
   {
      // inconsisten use case 
      rc = SQLITE4_MISMATCH;
   }
   else
   {
      // consistent use case
      if (pCur->nIsEOF)
      {
         // already on EOF 
         // Is this an inconsistent use case?
         rc = SQLITE4_MISUSE; // Library used incorrectly
      }
      else
      {
         // probably so far OK
         //
         
         int ret = pCurrWiredTigerCursor->next(pCurrWiredTigerCursor);

         switch (ret)
         {
         case 0: // OK
            rc = SQLITE4_OK;
            pCur->nIsEOF = 0; // false; // ???
            break;
         case WT_DUPLICATE_KEY: // ???
            rc = SQLITE4_CONSTRAINT; // ???
            pCur->nIsEOF = 1; // true; // ???
            break;
         case WT_NOTFOUND: // Is it relevant for replace/insert?
            rc = SQLITE4_NOTFOUND;
            pCur->nIsEOF = 1; // true; // ???
            break;
         case WT_ROLLBACK:
            // Integrate with SQLite4/M diagnostics!
            printf("kvwtNext() deadlock_resolved / needs_rollback_and_then_can_be_retried : error : '%s'\n", psession->strerror(psession, ret));
            rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
            pCur->nIsEOF = 1; // true; // ???
            break;
         case WT_PREPARE_CONFLICT:
            // Integrate with SQLite4/M diagnostics!
            printf("kvwtNext() prepare conflict : error : '%s'\n", psession->strerror(psession, ret));
            rc = SQLITE4_ERROR; // or: SQLITE4_BUSY
            pCur->nIsEOF = 1; // true; // ???
            break;
         case WT_CACHE_FULL:
            // Integrate with SQLite4/M diagnostics!
            printf("kvwtNext() no more cache memory : current txn may be rolled-back and retried, and failed after exhaustion on attempt number limit: error : '%s'\n", psession->strerror(psession, ret));
            //rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY // ???
            // rc = SQLITE4_FULL; // ???
            rc = SQLITE4_NOMEM; // ???
            pCur->nIsEOF = 1; // true; // ???
            break;
         case EINVAL:  // If the cursor is already closed; or if an invalid flag value or parameter was specified. 
                       // Integrate with SQLite4/M diagnostics!
            printf("kvwtNext() failed : error : '%s'\n", psession->strerror(psession, ret));
            //rc = SQLITE4_ERROR; // ?
            rc = SQLITE4_MISUSE; // Library used incorrectly (!!!)
            pCur->nIsEOF = 1; // true; // ???
            break;
         default:
            // Integrate with SQLite4/M diagnostics!
            printf("kvwtNext() FAILED : error : '%s'\n", psession->strerror(psession, ret));
            rc = SQLITE4_ERROR; // ?
            pCur->nIsEOF = 1; // true; // ???
            break;
         };  // end of switch (ret){...}
      } // end of else block for block : if(nIsEOF){...}
   } // end of else block for block : if( pCur->nLastSeekDir != SEEK_DIR_GE && ...){...}

   // Key/Data Cache invalidation -- begin
   pCur->nHasKeyAndDataCached = 0;
   pCur->nCachedKeySize = 0;
   pCur->nCachedDataSize = 0;
   // Key/Data Cache invalidation -- end

   return rc;
}


int kvwtPrev(sqlite4_kvcursor * pKVCursor)
{
   //printf("-----> kvwtPrev()\n");

   KVWTCursor *pCur;
   KVWT *p;

   pCur = (KVWTCursor*)pKVCursor;
   assert(pCur->iMagicKVBdbCur == SQLITE4_KVWTCUR_MAGIC);
   p = (KVWT *)(pCur->pOwner);
   assert(p->iMagicKVWTBase == SQLITE4_KVWTBASE_MAGIC);
   //assert( p->base.iTransLevel>=2 );  


   int rc = SQLITE4_OK;

   // Retrievee KVStore and WiredTige "context" variables -- begin
   int nCurrTxnLevel = p->base.iTransLevel;
   WT_CURSOR * pCurrTxnCsr = p->pTxnCsr[nCurrTxnLevel];
   WT_SESSION * psession = p->session;
   WT_CURSOR * pCurrWiredTigerCursor = pCur->pCsr;
   // Retrievee KVStore and WiredTige "context" variables -- end

   if (pCur->nLastSeekDir != SEEK_DIR_LE
      && pCur->nLastSeekDir != SEEK_DIR_EQ
      && pCur->nLastSeekDir != SEEK_DIR_LT)
   {
      // inconsisten use case 
      rc = SQLITE4_MISMATCH;
   }
   else
   {
      // consistent use case
      if (pCur->nIsEOF)
      {
         // already on EOF 
         // Is this an inconsistent use case?
         rc = SQLITE4_MISUSE; // Library used incorrectly
      }
      else
      {
         // probably so far OK
         //

         int ret = pCurrWiredTigerCursor->prev(pCurrWiredTigerCursor);

         switch (ret)
         {
         case 0: // OK
            rc = SQLITE4_OK;
            pCur->nIsEOF = 0; // false; // ???
            break;
         case WT_DUPLICATE_KEY: // ???
            rc = SQLITE4_CONSTRAINT; // ???
            pCur->nIsEOF = 1; // true; // ???
            break;
         case WT_NOTFOUND: // Is it relevant for replace/insert?
            rc = SQLITE4_NOTFOUND;
            pCur->nIsEOF = 1; // true; // ???
            break;
         case WT_ROLLBACK:
            // Integrate with SQLite4/M diagnostics!
            printf("kvwtPrev() deadlock_resolved / needs_rollback_and_then_can_be_retried : error : '%s'\n", psession->strerror(psession, ret));
            rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
            pCur->nIsEOF = 1; // true; // ???
            break;
         case WT_PREPARE_CONFLICT:
            // Integrate with SQLite4/M diagnostics!
            printf("kvwtPrev() prepare conflict : error : '%s'\n", psession->strerror(psession, ret));
            rc = SQLITE4_ERROR; // or: SQLITE4_BUSY
            pCur->nIsEOF = 1; // true; // ???
            break;
         case WT_CACHE_FULL:
            // Integrate with SQLite4/M diagnostics!
            printf("kvwtPrev() no more cache memory : current txn may be rolled-back and retried, and failed after exhaustion on attempt number limit: error : '%s'\n", psession->strerror(psession, ret));
            //rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY // ???
            // rc = SQLITE4_FULL; // ???
            rc = SQLITE4_NOMEM; // ???
            pCur->nIsEOF = 1; // true; // ???
            break;
         case EINVAL:  // If the cursor is already closed; or if an invalid flag value or parameter was specified. 
                       // Integrate with SQLite4/M diagnostics!
            printf("kvwtPrev() failed : error : '%s'\n", psession->strerror(psession, ret));
            //rc = SQLITE4_ERROR; // ?
            rc = SQLITE4_MISUSE; // Library used incorrectly (!!!)
            pCur->nIsEOF = 1; // true; // ???
            break;
         default:
            // Integrate with SQLite4/M diagnostics!
            printf("kvwtPrev() FAILED : error : '%s'\n", psession->strerror(psession, ret));
            rc = SQLITE4_ERROR; // ?
            pCur->nIsEOF = 1; // true; // ???
            break;
         };  // end of switch (ret){...}
      } // end of else block for block : if(nIsEOF){...}
   } // end of else block for block : if( pCur->nLastSeekDir != SEEK_DIR_GE && ...){...}

   // Key/Data Cache invalidation -- begin
   pCur->nHasKeyAndDataCached = 0;
   pCur->nCachedKeySize = 0;
   pCur->nCachedDataSize = 0;
   // Key/Data Cache invalidation -- end

   return rc;
}

int kvwtDelete(sqlite4_kvcursor *pKVCursor)
{
   //printf("-----> kvwtDelete()\n");  

   KVWTCursor *pCur;
   KVWT *p;

   pCur = (KVWTCursor*)pKVCursor;
   assert(pCur->iMagicKVWTCur == SQLITE4_KVWTCUR_MAGIC);
   p = (KVWT *)(pCur->pOwner);
   assert(p->iMagicKVWTBase == SQLITE4_KVWTBASE_MAGIC);
   //assert( p->base.iTransLevel>=2 ); 

   int rc = SQLITE4_OK;

   // Retrievee KVStore and WiredTiger "context" variables -- begin
   int nCurrTxnLevel = p->base.iTransLevel;
   WT_CURSOR * pCurrTxnCsr = p->pTxnCsr[nCurrTxnLevel];
   WT_SESSION * psession = p->session;
   WT_CURSOR * pCurrWiredTigerCursor = pCur->pCsr;
   // Retrievee KVStore and WiredTiger "context" variables -- end

   // Cross-checks -- begin
   assert(nCurrTxnLevel >= 2);
   if (nCurrTxnLevel < 2)
   {
      //printf("Internal Error : nCurrTxnLevel < 2\n");
      rc = SQLITE4_INTERNAL; // or: SQLITE4_MISUSE
   }

   assert(pCurrTxnCsr != NULL);
   if (pCurrTxnCsr == NULL)
   {
      //printf("Internal Error : pCurrTxnCsr == NULL\n");
      rc = SQLITE4_INTERNAL; // or: SQLITE4_MISUSE
   }

   assert(psession != NULL);
   if (psession == NULL)
   {
      //printf("Internal Error : psession == NULL\n");
      rc = SQLITE4_INTERNAL; // or: SQLITE4_MISUSE
   }

   assert(pCurrWiredTigerCursor != NULL);
   if (pCurrWiredTigerCursor == NULL)
   {
      //printf("Internal Error : pCurrWiredTigerCursor == NULL\n");
      rc = SQLITE4_INTERNAL; // or: SQLITE4_MISUSE
   }
   // Cross-checks -- end

   if (rc == SQLITE4_OK)
   {
      // So far OK

      int ret = 0;

      ret = pCurrWiredTigerCursor->remove(pCurrWiredTigerCursor);

      switch (ret)
      {
      case 0: // OK
         rc = SQLITE4_OK;
         break;
      case WT_DUPLICATE_KEY:
         rc = SQLITE4_CONSTRAINT;
         break;
      case WT_NOTFOUND: // Is it relevant for replace/insert?
         rc = SQLITE4_NOTFOUND;
         break;
      case WT_ROLLBACK:
         // Integrate with SQLite4/M diagnostics!
         printf("kvwtDelete() deadlock_resolved / needs_rollback_and_then_can_be_retried : error : '%s'\n", psession->strerror(psession, ret));
         rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
         break;
      case WT_PREPARE_CONFLICT:
         // Integrate with SQLite4/M diagnostics!
         printf("kvwtDelete() prepare conflict : error : '%s'\n", psession->strerror(psession, ret));
         rc = SQLITE4_ERROR; // or: SQLITE4_BUSY
         break;
      case WT_CACHE_FULL:
         // Integrate with SQLite4/M diagnostics!
         printf("kvwtDelete() no more cache memory : current txn may be rolled-back and retried, and failed after exhaustion on attempt number limit: error : '%s'\n", psession->strerror(psession, ret));
         //rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY // ???
         // rc = SQLITE4_FULL; // ???
         rc = SQLITE4_NOMEM; // ???
         break;
      case EINVAL:  // If the cursor is already closed; or if an invalid flag value or parameter was specified. 
                    // Integrate with SQLite4/M diagnostics!
         printf("kvwtDelete() failed : error : '%s'\n", psession->strerror(psession, ret));
         //rc = SQLITE4_ERROR; // ?
         rc = SQLITE4_MISUSE; // Library used incorrectly (!!!)
         break;
      default:
         // Integrate with SQLite4/M diagnostics!
         printf("kvwtDelete() FAILED : error : '%s'\n", psession->strerror(psession, ret));
         rc = SQLITE4_ERROR; // ?
         break;
      };  // end of switch (ret){...}
   } // end of : if (rc == SQLITE4_OK){...}

   // Cleanup Cached Key & Data -- begin
   pCur->nHasKeyAndDataCached = 0;
   pCur->nCachedKeySize = 0;
   pCur->nCachedDataSize = 0;
   // Cleanup Cached Key & Data -- end

   return rc;
}

int kvwtKey(
   sqlite4_kvcursor *pKVCursor,         /* The cursor whose key is desired */
   const KVByteArray **paKey,           /* Make this point to the key */
   KVSize *pN                           /* Make this point to the size of the key */
   ) {
   //printf("-----> kvwtKey(), KVCursor=%p\n", pKVCursor);

   KVWTCursor *pCur;
   KVWT *p;

   pCur = (KVWTCursor*)pKVCursor;
   assert(pCur->iMagicKVWTCur == SQLITE4_KVWTCUR_MAGIC);
   p = (KVWT *)(pCur->pOwner);
   assert(p->iMagicKVWTBase == SQLITE4_KVWTBASE_MAGIC);

   int rc = SQLITE4_OK;

   // Retrievee KVStore and WiredTiger "context" variables -- begin
   int nCurrTxnLevel = p->base.iTransLevel;
   WT_CURSOR * pCurrTxnCsr = p->pTxnCsr[nCurrTxnLevel];
   WT_SESSION * psession = p->session;
   WT_CURSOR * pCurrWiredTigerCursor = pCur->pCsr;
   //
   //printf(" , DBC=%p\n", pCurrBerkeleyDBCursor);
   // Retrievee KVStore and WiredTiger "context" variables -- end

   if (pCur->nHasKeyAndDataCached)
   {
      // Cached Key and Data present
      // 
      // we expect consistent cache, 
      // i.e. non-NULL key buffer ptr
      if (pCur->pCachedKey != NULL)
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
      if (pCur->pCachedKey == NULL)
      {
         pCur->pCachedKey = malloc(p->nInitialCursorKeyBufferCapacity);
         if (pCur->pCachedKey == NULL)
         {
            //printf("key: #3\n");
            rc = SQLITE4_NOMEM;
            goto label_nomem;
         }
         pCur->nCachedKeySize = 0;
         pCur->nCachedKeyCapacity = p->nInitialCursorKeyBufferCapacity;
      }

      if (pCur->pCachedData == NULL)
      {
         pCur->pCachedData = malloc(p->nInitialCursorDataBufferCapacity);
         if (pCur->pCachedData == NULL)
         {
            //printf("key: #4\n");
            rc = SQLITE4_NOMEM;
            goto label_nomem;
         }
         pCur->nCachedDataSize = 0;
         pCur->nCachedDataCapacity = p->nInitialCursorDataBufferCapacity;
      }

      { // Try to retrieve <key, data>, re-allocate buffers if necessary -- begin
         WT_ITEM oKey;
         WT_ITEM oData;

         int retKey = 0;
         int retData = 0;

         retKey = pCurrWiredTigerCursor->get_key(pCurrWiredTigerCursor, &oKey);
         if (!retKey)
         {
            // key retrieval succeeded, try to retrieve data
            retData = pCurrWiredTigerCursor->get_value(pCurrWiredTigerCursor, &oData);
         }

         if (!retKey && !retData)
         {
            // both, key and data retrieval succeeded
            // 
            // provide enough Key Buffer Capacity if necessary
            if (oKey.size > pCur->nCachedKeyCapacity)
            {
               // current Key Buffer is too small
               pCur->nCachedKeyCapacity = 0;
               free(pCur->pCachedKey);
               size_t nRequestedKeyBufferCapacity = oKey.size * 2; // 2x security coefficient
               pCur->pCachedKey = malloc(nRequestedKeyBufferCapacity);
               if (!pCur->pCachedKey)
               {
                  goto label_nomem;
               }
               pCur->nCachedKeyCapacity = nRequestedKeyBufferCapacity;
            }
            // 
            // provide enough Data Buffer Capacity if necessary
            if (oData.size > pCur->nCachedDataCapacity)
            {
               // current Data Buffer is too small
               pCur->nCachedDataCapacity = 0;
               free(pCur->pCachedData);
               size_t nRequestedDataBufferCapacity = oData.size * 2; // 2x security coefficient
               pCur->pCachedData = malloc(nRequestedDataBufferCapacity);
               if (!pCur->pCachedData)
               {
                  goto label_nomem;
               }
               pCur->nCachedDataCapacity = nRequestedDataBufferCapacity;
            }
            //
            // copy Key and its size
            //pCur->pCachedKey = (void*)(oKey.data); // this makes kvwtCursorClose(...) fail on free(...)
            memcpy(pCur->pCachedKey, oKey.data, oKey.size);
            pCur->nCachedKeySize = oKey.size;
            // 
            // copy Data and its size
            //pCur->pCachedData = (void*)(oData.data); // this makes kvwtCursorClose(...) fail on free(...)
            memcpy(pCur->pCachedData, oData.data, oData.size);
            pCur->nCachedDataSize = oData.size;
            // 
            // mark key & data as cached
            pCur->nHasKeyAndDataCached = 1;
            // 
            // Prepare return -- begin
            *paKey = (KVByteArray *)(pCur->pCachedKey);
            *pN = (KVSize)(pCur->nCachedKeySize);
            rc = SQLITE4_OK;
            // Prepare return -- end  
            // 
            pCur->nIsEOF = 0; // false; // ???
            // 
         } // end of if (!retKey && !retData){...}
         else
         {
            // Some errors occurred,  
            // thus we first clean-up 
            // buffer sizes, 
            // but retain buffers 
            // and their capacities 
            // unchanged.
            pCur->nCachedKeySize = 0;
            pCur->nCachedDataSize = 0;
            pCur->nHasKeyAndDataCached = 0;

            // Then we analyze errors 
            // to provide adequate diagnostic information
            // and appropriate return code.
            if (retKey)
            {
               // Key retrieval failed
               switch (retKey)
               {
               case 0: // OK
                  rc = SQLITE4_OK;
                  pCur->nIsEOF = 0; // false; // ???
                  break;
               case WT_DUPLICATE_KEY: // ???
                  rc = SQLITE4_CONSTRAINT; // ???
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               case WT_NOTFOUND: // Is it relevant for replace/insert?
                  rc = SQLITE4_NOTFOUND;
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               case WT_ROLLBACK:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtKey() deadlock_resolved / needs_rollback_and_then_can_be_retried : error : '%s'\n", psession->strerror(psession, retKey));
                  rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               case WT_PREPARE_CONFLICT:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtKey() prepare conflict : error : '%s'\n", psession->strerror(psession, retKey));
                  rc = SQLITE4_ERROR; // or: SQLITE4_BUSY
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               case WT_CACHE_FULL:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtKey() no more cache memory : current txn may be rolled-back and retried, and failed after exhaustion on attempt number limit: error : '%s'\n", psession->strerror(psession, retKey));
                  //rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY // ???
                  // rc = SQLITE4_FULL; // ???
                  rc = SQLITE4_NOMEM; // ???
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               case EINVAL:  // If the cursor is already closed; or if an invalid flag value or parameter was specified. 
                             // Integrate with SQLite4/M diagnostics!
                  printf("kvwtKey() failed : error : '%s'\n", psession->strerror(psession, retKey));
                  //rc = SQLITE4_ERROR; // ?
                  rc = SQLITE4_MISUSE; // Library used incorrectly (!!!)
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               default:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtKey() FAILED : error : '%s'\n", psession->strerror(psession, retKey));
                  rc = SQLITE4_ERROR; // ?
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               };  // end of switch (retKey){...}
            }
            else
            {
               // Key retrieval succeeded
               // but data retrieval failed
               switch (retData)
               {
               case 0: // OK
                  rc = SQLITE4_OK;
                  pCur->nIsEOF = 0; // false; // ???
                  break;
               case WT_DUPLICATE_KEY: // ???
                  rc = SQLITE4_CONSTRAINT; // ???
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               case WT_NOTFOUND: // Is it relevant for replace/insert?
                  rc = SQLITE4_NOTFOUND;
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               case WT_ROLLBACK:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtKey() deadlock_resolved / needs_rollback_and_then_can_be_retried : error : '%s'\n", psession->strerror(psession, retData));
                  rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               case WT_PREPARE_CONFLICT:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtKey() prepare conflict : error : '%s'\n", psession->strerror(psession, retData));
                  rc = SQLITE4_ERROR; // or: SQLITE4_BUSY
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               case WT_CACHE_FULL:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtKey() no more cache memory : current txn may be rolled-back and retried, and failed after exhaustion on attempt number limit: error : '%s'\n", psession->strerror(psession, retData));
                  //rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY // ???
                  // rc = SQLITE4_FULL; // ???
                  rc = SQLITE4_NOMEM; // ???
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               case EINVAL:  // If the cursor is already closed; or if an invalid flag value or parameter was specified. 
                             // Integrate with SQLite4/M diagnostics!
                  printf("kvwtKey() failed : error : '%s'\n", psession->strerror(psession, retData));
                  //rc = SQLITE4_ERROR; // ?
                  rc = SQLITE4_MISUSE; // Library used incorrectly (!!!)
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               default:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtKey() FAILED : error : '%s'\n", psession->strerror(psession, retData));
                  rc = SQLITE4_ERROR; // ?
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               };  // end of switch (retData){...}
            }
         } // end of else of if (!retKey && !retData){...}
      } // Try to retrieve <key, data>, re-allocate buffers if necessary -- end
   }

label_nomem:
   if (rc == SQLITE4_NOMEM)
   {
      if (pCur->pCachedKey != NULL)
      {
         free(pCur->pCachedKey);
         pCur->pCachedKey = 0;
         pCur->nCachedKeySize = 0;
         pCur->nCachedKeyCapacity = 0;
      }
      if (pCur->pCachedData != NULL)
      {
         free(pCur->pCachedData);
         pCur->pCachedData = 0;
         pCur->nCachedDataSize = 0;
         pCur->nCachedDataCapacity = 0;
      }
      pCur->nHasKeyAndDataCached = 0;
   }

   return rc;
}


int kvwtData(
   sqlite4_kvcursor *pKVCursor,         /* The cursor from which to take the data */
   KVSize ofst,                 /* Offset into the data to begin reading */
   KVSize n,                    /* Number of bytes requested */
   const KVByteArray **paData,  /* Pointer to the data written here */
   KVSize *pNData               /* Number of bytes delivered */
   ) {
   //printf("-----> kvwtData()\n");  

   KVWTCursor *pCur;
   KVWT *p;

   pCur = (KVWTCursor*)pKVCursor;
   assert(pCur->iMagicKVWTCur == SQLITE4_KVWTCUR_MAGIC);
   p = (KVWT *)(pCur->pOwner);
   assert(p->iMagicKVWTBase == SQLITE4_KVWTBASE_MAGIC);

   int rc = SQLITE4_OK;

   // Retrieve KVStore and WiredTiger "context" variables -- begin
   int nCurrTxnLevel = p->base.iTransLevel;
   WT_CURSOR * pCurrTxnCsr = p->pTxnCsr[nCurrTxnLevel];
   WT_SESSION * psession = p->session;
   WT_CURSOR * pCurrWiredTigerCursor = pCur->pCsr;
   // Retrieve KVStore and WiredTiger "context" variables -- end

   if (pCur->nHasKeyAndDataCached)
   {
      // Cached Key and Data present
      // 
      // we expect consistent cache, 
      // i.e. non-NULL key buffer ptr
      if (pCur->pCachedData != NULL)
      {
         // consistent cache
         //*paData = (KVByteArray *)(pCur->pCachedData);
         //*pNData = (KVSize)(pCur->nCachedDataSize);
         if (n<0)
         {
            *paData = (const KVByteArray *)(pCur->pCachedData);
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
      if (pCur->pCachedKey == NULL)
      {
         pCur->pCachedKey = malloc(p->nInitialCursorKeyBufferCapacity);
         if (pCur->pCachedKey == NULL)
         {
            rc = SQLITE4_NOMEM;
            goto label_nomem;
         }
         pCur->nCachedKeySize = 0;
         pCur->nCachedKeyCapacity = p->nInitialCursorKeyBufferCapacity;
      }

      if (pCur->pCachedData == NULL)
      {
         pCur->pCachedData = malloc(p->nInitialCursorDataBufferCapacity);
         if (pCur->pCachedData == NULL)
         {
            rc = SQLITE4_NOMEM;
            goto label_nomem;
         }
         pCur->nCachedDataSize = 0;
         pCur->nCachedDataCapacity = p->nInitialCursorDataBufferCapacity;
      }

      { // Try to retrieve <key, data>, re-allocate buffers if necessary -- begin
         WT_ITEM oKey;
         WT_ITEM oData;

         int retKey = 0;
         int retData = 0;

         retKey = pCurrWiredTigerCursor->get_key(pCurrWiredTigerCursor, &oKey);
         if (!retKey)
         {
            // key retrieval succeeded, try to retrieve data
            retData = pCurrWiredTigerCursor->get_value(pCurrWiredTigerCursor, &oData);
         }

         if (!retKey && !retData)
         {
            // both, key and data retrieval succeeded
            // 
            // provide enough Key Buffer Capacity if necessary
            if (oKey.size > pCur->nCachedKeyCapacity)
            {
               // current Key Buffer is too small
               pCur->nCachedKeyCapacity = 0;
               free(pCur->pCachedKey);
               size_t nRequestedKeyBufferCapacity = oKey.size * 2; // 2x security coefficient
               pCur->pCachedKey = malloc(nRequestedKeyBufferCapacity);
               if (!pCur->pCachedKey)
               {
                  goto label_nomem;
               }
               pCur->nCachedKeyCapacity = nRequestedKeyBufferCapacity;
            }
            // 
            // provide enough Data Buffer Capacity if necessary
            if (oData.size > pCur->nCachedDataCapacity)
            {
               // current Data Buffer is too small
               pCur->nCachedDataCapacity = 0;
               free(pCur->pCachedData);
               size_t nRequestedDataBufferCapacity = oData.size * 2; // 2x security coefficient
               pCur->pCachedData = malloc(nRequestedDataBufferCapacity);
               if (!pCur->pCachedData)
               {
                  goto label_nomem;
               }
               pCur->nCachedDataCapacity = nRequestedDataBufferCapacity;
            }
            //
            // copy Key and its size
            //pCur->pCachedKey = (void*)(oKey.data);
            memcpy(pCur->pCachedKey, oKey.data, oKey.size);
            pCur->nCachedKeySize = oKey.size;
            // 
            // copy Data and its size
            //pCur->pCachedData = (void*)(oData.data);
            memcpy(pCur->pCachedData, oData.data, oData.size);
            pCur->nCachedDataSize = oData.size;
            // 
            // mark key & data as cached
            pCur->nHasKeyAndDataCached = 1;
            // 
            // Prepare return -- begin
            if (n<0)
            {
               *paData = (const KVByteArray *)(pCur->pCachedData);
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
            // 
         } // end of if (!retKey && !retData){...}
         else
         {
            // Some errors occurred,  
            // thus we first clean-up 
            // buffer sizes, 
            // but retain buffers 
            // and their capacities 
            // unchanged.
            pCur->nCachedKeySize = 0;
            pCur->nCachedDataSize = 0;
            pCur->nHasKeyAndDataCached = 0;

            // Then we analyze errors 
            // to provide adequate diagnostic information
            // and appropriate return code.
            if (retKey)
            {
               // Key retrieval failed
               switch (retKey)
               {
               case 0: // OK
                  rc = SQLITE4_OK;
                  pCur->nIsEOF = 0; // false; // ???
                  break;
               case WT_DUPLICATE_KEY: // ???
                  rc = SQLITE4_CONSTRAINT; // ???
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               case WT_NOTFOUND: // Is it relevant for replace/insert?
                  rc = SQLITE4_NOTFOUND;
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               case WT_ROLLBACK:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtData() deadlock_resolved / needs_rollback_and_then_can_be_retried : error : '%s'\n", psession->strerror(psession, retKey));
                  rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               case WT_PREPARE_CONFLICT:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtData() prepare conflict : error : '%s'\n", psession->strerror(psession, retKey));
                  rc = SQLITE4_ERROR; // or: SQLITE4_BUSY
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               case WT_CACHE_FULL:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtData() no more cache memory : current txn may be rolled-back and retried, and failed after exhaustion on attempt number limit: error : '%s'\n", psession->strerror(psession, retKey));
                  //rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY // ???
                  // rc = SQLITE4_FULL; // ???
                  rc = SQLITE4_NOMEM; // ???
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               case EINVAL:  // If the cursor is already closed; or if an invalid flag value or parameter was specified. 
                             // Integrate with SQLite4/M diagnostics!
                  printf("kvwtData() failed : error : '%s'\n", psession->strerror(psession, retKey));
                  //rc = SQLITE4_ERROR; // ?
                  rc = SQLITE4_MISUSE; // Library used incorrectly (!!!)
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               default:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtData() FAILED : error : '%s'\n", psession->strerror(psession, retKey));
                  rc = SQLITE4_ERROR; // ?
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               };  // end of switch (retKey){...}
            }
            else
            {
               // Key retrieval succeeded
               // but data retrieval failed
               switch (retData)
               {
               case 0: // OK
                  rc = SQLITE4_OK;
                  pCur->nIsEOF = 0; // false; // ???
                  break;
               case WT_DUPLICATE_KEY: // ???
                  rc = SQLITE4_CONSTRAINT; // ???
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               case WT_NOTFOUND: // Is it relevant for replace/insert?
                  rc = SQLITE4_NOTFOUND;
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               case WT_ROLLBACK:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtData() deadlock_resolved / needs_rollback_and_then_can_be_retried : error : '%s'\n", psession->strerror(psession, retData));
                  rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               case WT_PREPARE_CONFLICT:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtData() prepare conflict : error : '%s'\n", psession->strerror(psession, retData));
                  rc = SQLITE4_ERROR; // or: SQLITE4_BUSY
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               case WT_CACHE_FULL:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtData() no more cache memory : current txn may be rolled-back and retried, and failed after exhaustion on attempt number limit: error : '%s'\n", psession->strerror(psession, retData));
                  //rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY // ???
                  // rc = SQLITE4_FULL; // ???
                  rc = SQLITE4_NOMEM; // ???
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               case EINVAL:  // If the cursor is already closed; or if an invalid flag value or parameter was specified. 
                             // Integrate with SQLite4/M diagnostics!
                  printf("kvwtData() failed : error : '%s'\n", psession->strerror(psession, retData));
                  //rc = SQLITE4_ERROR; // ?
                  rc = SQLITE4_MISUSE; // Library used incorrectly (!!!)
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               default:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtData() FAILED : error : '%s'\n", psession->strerror(psession, retData));
                  rc = SQLITE4_ERROR; // ?
                  pCur->nIsEOF = 1; // true; // ???
                  break;
               };  // end of switch (retData){...}
            }
         } // end of else of if (!retKey && !retData){...}
      } // Try to retrieve <key, data>, re-allocate buffers if necessary -- end

   } // end of else block for block : if(pCur->nHasKeyAndDataCached){...}

label_nomem:
   if (rc == SQLITE4_NOMEM)
   {
      if (pCur->pCachedKey != NULL)
      {
         free(pCur->pCachedKey);
         pCur->pCachedKey = 0;
         pCur->nCachedKeySize = 0;
         pCur->nCachedKeyCapacity = 0;
      }
      if (pCur->pCachedData != NULL)
      {
         free(pCur->pCachedData);
         pCur->pCachedData = 0;
         pCur->nCachedDataSize = 0;
         pCur->nCachedDataCapacity = 0;
      }
      pCur->nHasKeyAndDataCached = 0;
   }

   return rc;
}

int kvwtReset(sqlite4_kvcursor *pKVCursor) {
   //printf("-----> kvwtReset()\n");  

   KVWTCursor *pCur;
   KVWT *p;

   pCur = (KVWTCursor*)pKVCursor;
   assert(pCur->iMagicKVWTCur == SQLITE4_KVWTCUR_MAGIC);
   p = (KVWT *)(pCur->pOwner);
   assert(p->iMagicKVWTBase == SQLITE4_KVWTBASE_MAGIC);
   //assert( p->base.iTransLevel>=2 ); 

   // Cached Key & Data Buffers -- begin
   pCur->nCachedKeySize = 0;
   pCur->nCachedDataSize = 0;
   pCur->nHasKeyAndDataCached = 0;
   // Cached Key & Data Buffers -- end

   pCur->nIsEOF = 0; // EOF not encountered yet
   pCur->nLastSeekDir = SEEK_DIR_NONE;

   WT_CURSOR * pCurrWiredTigerCursor = pCur->pCsr; // the underlying WiredTiger cursor

                                             // Q: Do we need to do anything about pCurrWiredTigerCursor, 
                                             //    e.g. scroll it to some first/last/after-last position?
                                             // A: We will try to reset it, but will see at "running-in", :-).

   int rc = SQLITE4_OK;
   int ret = pCurrWiredTigerCursor->reset(pCurrWiredTigerCursor);
   switch (ret)
   {
   case 0:
      rc = SQLITE4_OK;
      break;
   default:
      rc = SQLITE4_ERROR;
      break;
   }

   return rc;
} // end of : kvwtReset(...){...}

int kvwtCloseCursor(sqlite4_kvcursor *pKVCursor) {
   //printf("-----> kvwtCloseCursor()\n");  

   KVWTCursor *pCur;
   KVWT *p;

   pCur = (KVWTCursor*)pKVCursor;
   assert(pCur->iMagicKVWTCur == SQLITE4_KVWTCUR_MAGIC);
   p = (KVWT *)(pCur->pOwner);

   sqlite4_kvstore * pKVStore = (sqlite4_kvstore *)(pCur->pOwner); // for diagnostics only
   assert(pKVStore);

   assert(p->iMagicKVWTBase == SQLITE4_KVWTBASE_MAGIC);
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

   int nCurrTxnLevel = pCur->base.pStore->iTransLevel; // via: KVWTCursor::sqlite4_kvcursor::sqlite4_kvstore::iTransLevel
   int nCurrOwnerTxnLevel = p->base.iTransLevel; // via: KVWTCursor::KVWT::sqlite4_kvstore::iTransLevel 
   assert(nCurrTxnLevel == nCurrOwnerTxnLevel);
   WT_CURSOR * pCurrWiredTigerCursor = pCur->pCsr;
   WT_CURSOR * pCurrKVWTWiredTigerReadOnlyCursor = p->pCsr;

   if (pCurrWiredTigerCursor == pCurrKVWTWiredTigerReadOnlyCursor)
   {
      assert(nCurrTxnLevel <= 1);
      p->pCsr = NULL;
   }

   pCur->pCsr = NULL;

   int ret = pCurrWiredTigerCursor->close(pCurrWiredTigerCursor);

   switch (ret)
   {
   case 0: // OK
      rc = SQLITE4_OK;
      break;
   case WT_ROLLBACK:
      // Integrate with SQLite4/M diagnostics!
      printf("kvwtCloseCursor() deadlock_resolved / needs_rollback_and_then_can_be_retried : pKVStore = %p. pkvstore->iTransLevel = %d : error : '%s'\n", pKVStore, pKVStore->iTransLevel, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
      rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
      break;
   case WT_PREPARE_CONFLICT:
      // Integrate with SQLite4/M diagnostics!
      printf("kvwtCloseCursor() prepare conflict : pKVStore = %p. pKVStore->iTransLevel = %d : error : '%s'\n", pKVStore, pKVStore->iTransLevel, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
      rc = SQLITE4_ERROR; // or: SQLITE4_BUSY
      break;
   case WT_CACHE_FULL:
      // Integrate with SQLite4/M diagnostics!
      printf("kvwtCloseCursor() no more cache memory : current txn may be rolled-back and retried, and failed after exhaustion on attempt number limit: pKVStore = %p. pKVStore->iTransLevel = %d : error : '%s'\n", pKVStore, pKVStore->iTransLevel, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
      //rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY // ???
      // rc = SQLITE4_FULL; // ???
      rc = SQLITE4_NOMEM; // ???
      break;
   case EINVAL:  // If the cursor is already closed; or if an invalid flag value or parameter was specified. 
                 // Integrate with SQLite4/M diagnostics!
      printf("kvwtCloseCursor() failed : pKVStore = %p, pKVStore->iTransLevel = %d : error : '%s'\n", pKVStore, pKVStore->iTransLevel, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
      //rc = SQLITE4_ERROR; // ?
      rc = SQLITE4_MISUSE; // Library used incorrectly (!!!)
      break;
   default:
      // Integrate with SQLite4/M diagnostics!
      printf("kvwtCloseCursor() FAILED : pKVStore = %p,  pKVStore->iTransLevel = %d : error : '%s'\n", pKVStore, pKVStore->iTransLevel, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
      rc = SQLITE4_ERROR; // ?
      break;
   };

   return rc;
} // end of : kvwtCloseCursor(...){...}


int kvwtBegin(sqlite4_kvstore * pKVStore, int iLevel) {
   //printf("-----> KVWT::kvwtBegin(%p,%d)\n", pKVStore, iLevel); 

   int rc = SQLITE4_OK;

   int ret = 0;

   KVWT *p = (KVWT*)pKVStore;

   assert(p->iMagicKVWTBase == SQLITE4_KVWTBASE_MAGIC);

   assert(iLevel>0);
   assert(iLevel == 2 || iLevel == p->base.iTransLevel + 1);


   if (p->pCsr == NULL) // no Cursor allocated for read-trans ops
   {
      WT_CURSOR * pNewCursor = NULL;
      int ret = 0;
      ret = p->session->open_cursor(p->session, p->table_name, NULL, "overwrite=false", &pNewCursor);
      if (ret != 0)
      {
         // failed
         // Integrate with SQLite4/M diagnostics!
         printf("Failed read-only cursor creation in kvwtBegin() : : error : '%s', '%s'\n", wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
         rc = SQLITE4_ERROR;
      }
      else
      {
         // succeeded    
         //printf("Succeeded read-only cursor creation in kvwtBegin() : KVStore = %p, pNewCursor = %p\n", pKVStore, pNewCursor);
         rc = SQLITE4_OK;
      }
      if (rc == SQLITE4_OK)
      {
         int ret = 0;
         ret = pNewCursor->reset(pNewCursor);
         if (ret != 0)
         {
            // failed
            // Integrate with SQLite4/M diagnostics!
            printf("Failed kvwtBegin(...) : failed cursor->reset(...) : error : '%s', '%s'\n", wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
            rc = SQLITE4_ERROR;
         }
      }
      if (rc == SQLITE4_OK)
      {
         p->pCsr = pNewCursor;
      }
   }


   //if(rc == SQLITE4_OK && iLevel >= 2 && iLevel >= pKVStore->iTransLevel) // KVLsm uses iLevel >= pKVStore->iTransLevel
   if (rc == SQLITE4_OK && iLevel >= 2 && iLevel > pKVStore->iTransLevel) // we separately handle iLevel == pKVStore->iTransLevel
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
      // parent_of(p->pTxnCsr[iLevel]) == p->pTxnCsr[p->base.iTransLevel] ; 

      assert(iLevel <= SQLITE4_KV_BDB_MAX_TXN_DEPTH);
      //assert(p->pTxnCsr[iLevel] == NULL); or: if(p->pTxnCsr[iLevel] == NULL){...}
      if (p->pTxnCsr[iLevel] == NULL)
      {
         assert((p->base.iTransLevel == 0 && p->pTxnCsr[p->base.iTransLevel] == 0)
            || (p->base.iTransLevel == 1 && p->pTxnCsr[p->base.iTransLevel] == 0)
            || (p->base.iTransLevel == 2 && p->pTxnCsr[p->base.iTransLevel] != 0));
         WT_CURSOR * pNewTxn = NULL;
         WT_CURSOR * pParentTxn = p->pTxnCsr[p->base.iTransLevel];
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

         //u_int32_t flags = DB_READ_COMMITTED;
         char cBufBegin[128];
         size_t nCounter = oCounter.fetch_add(1);
         sprintf(cBufBegin, "isolation=read-committed, read_timestamp=%ld", nCounter);
         ret = p->session->begin_transaction(p->session, cBufBegin);
         if (ret != 0)
         {
            // failed
            // Integrate with SQLite4/M diagnostics!
            printf("Failed kvwtBegin() : failed p->session->begin_transaction(...) [1]: error : '%s', '%s'\n", wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
            rc = SQLITE4_ERROR;
         }
         else
         {
            // succeeded    
            //printf("Succeeded begin_transaction(...) [1] in kvwtBegin() : KVStore = %p, pNewTxn = %d\n", pKVStore, pNewTxn);
            rc = SQLITE4_OK;
         }

         // Open a Cursor of class WT_CURSOR  to aasociate with current transaction
         ret = p->session->open_cursor(p->session, p->table_name, NULL, "overwrite=false", &pNewTxn);
         if (ret != 0)
         {
            // failed
            // Integrate with SQLite4/M diagnostics!
            printf("Failed kvwtBegin() : failed p->session->open_cursor(...) [3]: error : '%s', '%s'\n", wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
            rc = SQLITE4_ERROR;
         }
         else
         {
            // succeeded    
            //printf("Succeeded open_cursor(...) [3] in kvwtBegin() : KVStore = %p, pNewTxn = %d\n", pKVStore, pNewTxn);
            rc = SQLITE4_OK;
         }

         if (rc == SQLITE4_OK)
         {
            p->base.iTransLevel = iLevel;
            p->pTxnCsr[iLevel] = pNewTxn;
         }

      } // end of block : if(p->pTxnCsr[iLevel] == NULL){...}
      else if (rc == SQLITE4_OK && iLevel >= 2 && iLevel == pKVStore->iTransLevel) // we separately handle iLevel == pKVStore->iTransLevel  
      {
         // (till now OK ) && (WRITE TXN requested) && (requested Txn Level == curr Txn lLevel) 

         // Due to condition (iLevel == pKVStore->iTransLevel) 
         // we may or may not need to open a new DB_TXN.

         assert(iLevel <= SQLITE4_KV_BDB_MAX_TXN_DEPTH);
         //assert(p->pTxnCsr[iLevel] == NULL); //or: if(p->pTxnCsr[iLevel] == NULL){...}
         if (p->pTxnCsr[iLevel] == NULL)
         {
            assert((p->base.iTransLevel == 0 && p->pTxnCsr[p->base.iTransLevel] == 0)
               || (p->base.iTransLevel == 1 && p->pTxnCsr[p->base.iTransLevel] == 0)
               || (p->base.iTransLevel == 2 && p->pTxnCsr[p->base.iTransLevel] != 0));
            WT_CURSOR * pNewTxn = NULL;
            WT_CURSOR * pParentTxn = p->pTxnCsr[p->base.iTransLevel];
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

            //u_int32_t flags = DB_READ_COMMITTED;

            // Determine Parent Txn -- begin                   
            {
               int i = pKVStore->iTransLevel;
               for (; i >= 0; --i)
               {
                  pParentTxn = p->pTxnCsr[i];
                  if (pParentTxn != NULL)
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

            char cBufBegin[128];
            size_t nCounter = oCounter.fetch_add(1);
            sprintf(cBufBegin, "isolation=read-committed, read_timestamp=%ld", nCounter);
            ret = p->session->begin_transaction(p->session, cBufBegin);
            if (ret != 0)
            {
               // failed
               // Integrate with SQLite4/M diagnostics!
               printf("Failed kvwtBegin() : failed p->session->begin_transaction(...) [2] : error : '%s', '%s'\n", wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
               rc = SQLITE4_ERROR;
            }
            else
            {
               // succeeded    
               //printf("Succeeded begin_transaction(...) [2] in kvwtBegin() : KVStore = %p, pNewTxn = %d\n", pKVStore, pNewTxn);
               rc = SQLITE4_OK;
            }

            // Open a Cursor of class WT_CURSOR  to aasociate with current transaction
            ret = p->session->open_cursor(p->session, p->table_name, NULL, "overwrite=false", &pNewTxn);
            if (ret != 0)
            {
               // failed
               // Integrate with SQLite4/M diagnostics!
               printf("Failed kvwtBegin() : failed p->session->open_cursor(...) [4]: error : '%s', '%s'\n", wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
               rc = SQLITE4_ERROR;
            }
            else
            {
               // succeeded    
               //printf("Succeeded open_cursor(...) [4] in kvwtBegin() : KVStore = %p, pNewTxn = %d\n", pKVStore, pNewTxn);
               rc = SQLITE4_OK;
            }

            if (rc == SQLITE4_OK)
            {
               p->base.iTransLevel = iLevel;
               p->pTxnCsr[iLevel] = pNewTxn;
            }
         }
      }
   } // end of block : else if(rc == SQLITE4_OK && iLevel >= 2 && iLevel == pKVStore->iTransLevel){...}
   else
   {
      //printf("INFO : No DB_TXN Transaction [3] opened for pKVStore=%p, iLevel=%d\n", pKVStore, iLevel);
   }

   // Epilogue -- begin
   if (rc == SQLITE4_OK)
   {
      pKVStore->iTransLevel = SQLITE4_MAX(iLevel, pKVStore->iTransLevel);
   }
   else
   {
      // not SQLITE4_OK -- some error occurred 
      if (pKVStore->iTransLevel == 0)
      {
         // "initial" transaction level
         if (p->pCsr != NULL)
         {
            // if a Cursor 
            // for read-only txn ops exists, 
            // then close it.
            WT_CURSOR * pcsr = p->pCsr;
            pcsr->close(pcsr);
            p->pCsr = NULL;
         }
      }
   }
   // Epilogue -- end

   return rc;
} // end of kvwtBegin(...){...}


int kvwtCommitPhaseOne(KVStore *pKVStore, int iLevel) {
   //printf("-----> kvwtCommitPhaseOne(%p,%d)\n", pKVStore, iLevel);  

   int rc = SQLITE4_OK;

   KVWT * p = (KVWT*)pKVStore;

   if (pKVStore->iTransLevel > iLevel)
   {
      if (pKVStore->iTransLevel >= 2)
      {
         // Not committed writes present.
         // Find a candidate-BerkeleyDB-transaction to prepare.
         WT_CURSOR * pTxnPrepareCandidate = p->pTxnCsr[iLevel + 1]; // ?
         if (pTxnPrepareCandidate != NULL)
         {
            // Non-NULL candidate can be prepared, 
            // if no parental-txn exists, 
            // i.e. if the candidate itself 
            // is a parental txn.
            WT_CURSOR * pParentTxn = NULL;
            int i = 0;
            for (i = iLevel; i >= 0; --i)
            {
               pParentTxn = p->pTxnCsr[i];
               if (pParentTxn != NULL)
               {
                  break;
               }
            } // end loop : for( i = iLevel; i >= 0; --i ){...}
            if (pParentTxn != NULL)
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
               // Prepare a GID/timestamp for the txn to be prepared.
               char cBufPrepare[128];
               size_t nCounter = oCounter.fetch_add(1);;
               sprintf(cBufPrepare, "prepare_timestamp=%lld", nCounter);

               int ret = 0;
               ret = p->session->prepare_transaction(p->session, cBufPrepare);
               switch (ret)
               {
               case 0:
                  //printf("kvwtCommitPhaseOne() succeeded : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnPrepareCandidate = %p\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnPrepareCandidate); 
                  rc = SQLITE4_OK;
                  break;
               case WT_ROLLBACK:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtCommitPhaseOne() deadlock_resolved / needs_rollback_and_then_can_be_retried : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnPrepareCandidate = %p : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnPrepareCandidate, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : ""); 
                  rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
                  break;
               case WT_PREPARE_CONFLICT:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtCommitPhaseOne() prepare conflict : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnPrepareCandidate = %p : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnPrepareCandidate, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
                  rc = SQLITE4_ERROR; // or: SQLITE4_BUSY
                  break;
               case WT_CACHE_FULL:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtCommitPhaseOne() no more cache memory : current txn may be rolled-back and retried, and failed after exhaustion on attempt number limit: pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnPrepareCandidate = %p : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnPrepareCandidate, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
                  //rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY // ???
                  // rc = SQLITE4_FULL; // ???
                  rc = SQLITE4_NOMEM; // ???
                  break;
               case EINVAL:  // If the cursor is already closed; or if an invalid flag value or parameter was specified. 
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvbdbCommitPhaseOne() failed : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnPrepareCandidate = %p : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnPrepareCandidate, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : ""); 
                  //rc = SQLITE4_ERROR; // ?
                  rc = SQLITE4_MISUSE; // Library used incorrectly (!!!)
                  break;
               default:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvbdbCommitPhaseOne() FAILED : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnPrepareCandidate = %p : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnPrepareCandidate, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : ""); 
                  rc = SQLITE4_ERROR; // ?
                  break;
               }
               // CAUTION:
               // During kvbdbCommitPhaseOTwo() or kvbdbRollback(), 
               // all pTxnCsr[j]'s above iLevel should be (re-)set to NULL.
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
} // end of : kvwtCommitPhaseOne(...){...}



int kvwtCommitPhaseOneXID(KVStore *pKVStore, int iLevel, void * xid) {
   //printf("-----> kvwtCommitPhaseOneXID(%p,%d)\n", pKVStore, iLevel);  

   int rc = SQLITE4_OK;

   KVWT * p = (KVWT*)pKVStore;

   if (pKVStore->iTransLevel > iLevel)
   {
      if (pKVStore->iTransLevel >= 2)
      {
         // Not committed writes present.
         // Find a candidate-BerkeleyDB-transaction to prepare.
         WT_CURSOR * pTxnPrepareCandidate = p->pTxnCsr[iLevel + 1]; // ?
         if (pTxnPrepareCandidate != NULL)
         {
            // Non-NULL candidate can be prepared, 
            // if no parental-txn exists, 
            // i.e. if the candidate itself 
            // is a parental txn.
            WT_CURSOR * pParentTxn = NULL;
            int i = 0;
            for (i = iLevel; i >= 0; --i)
            {
               pParentTxn = p->pTxnCsr[i];
               if (pParentTxn != NULL)
               {
                  break;
               }
            } // end loop : for( i = iLevel; i >= 0; --i ){...}
            if (pParentTxn != NULL)
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
               // Prepare a GID/timestamp for the txn to be prepared.
               char cBufPrepare[128];
               size_t nCounter = oCounter.fetch_add(1);;
               sprintf(cBufPrepare, "prepare_timestamp=%lld", nCounter);

               int ret = 0;
               ret = p->session->prepare_transaction(p->session, cBufPrepare);
               switch (ret)
               {
               case 0:
                  //printf("kvwtCommitPhaseOne() succeeded : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnPrepareCandidate = %d\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnPrepareCandidate); 
                  rc = SQLITE4_OK;
                  break;
               case WT_ROLLBACK:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtCommitPhaseOneXID() deadlock_resolved / needs_rollback_and_then_can_be_retried : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnPrepareCandidate = %p : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnPrepareCandidate, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
                  rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
                  break;
               case WT_PREPARE_CONFLICT:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtCommitPhaseOneXID() prepare conflict : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnPrepareCandidate = %p : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnPrepareCandidate, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
                  rc = SQLITE4_ERROR; // or: SQLITE4_BUSY
                  break;
               case WT_CACHE_FULL:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtCommitPhaseOneXID() no more cache memory : current txn may be rolled-back and retried, and failed after exhaustion on attempt number limit: pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnPrepareCandidate = %p : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnPrepareCandidate, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
                  //rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY // ???
                  // rc = SQLITE4_FULL; // ???
                  rc = SQLITE4_NOMEM; // ???
                  break;
               case EINVAL:  // If the cursor is already closed; or if an invalid flag value or parameter was specified. 
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtCommitPhaseOneXID() failed : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnPrepareCandidate = %p : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnPrepareCandidate, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
                  //rc = SQLITE4_ERROR; // ?
                  rc = SQLITE4_MISUSE; // Library used incorrectly (!!!)
                  break;
               default:
                  // Integrate with SQLite4/M diagnostics!
                  printf("kvwtCommitPhaseOneXID() FAILED : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnPrepareCandidate = %p : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnPrepareCandidate, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : ""); 
                  rc = SQLITE4_ERROR; // ?
                  break;
               }
               // CAUTION:
               // During kvwtCommitPhaseOTwo() or kvbdbRollback(), 
               // all pTxnCsr[j]'s above iLevel should be (re-)set to NULL/ZERO.
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
}


int kvwtCommitPhaseTwo(KVStore *pKVStore, int iLevel) {
   //printf("-----> kvwtCommitPhaseTwo(%p,%d)\n", pKVStore, iLevel);  

   int rc = SQLITE4_OK;

   KVWT * p = (KVWT*)pKVStore;

   if (pKVStore->iTransLevel > iLevel)
   {
      if (pKVStore->iTransLevel >= 2)
      {
         // Not committed writes present.
         // Find a candidate-BerkeleyDB-transaction to commit.
         // According to "BerkeleyDB C Reference", 
         // we don't need to commit all child-txn's separately.
         // I understand it so, that I don't need to iterate 
         // between iTransLevel and iLevel+1, 
         // when I [prepare() or] commit() at iLevel+1.
         // I assume, that, what I need after successful commit() at iLevel+1, 
         // is to re-set to NULL all pTxnCsr[j]'s, 
         // for j between iTransLevel and iLevel+1, inclusively.

         WT_CURSOR * pTxnCommitCandidate = p->pTxnCsr[iLevel + 1]; // ?
         int nTxnCommitCandidateLevel = iLevel + 1;
         if (pTxnCommitCandidate != NULL)
         {
            char cBufCommit[128];
            size_t nCounter = oCounter.fetch_add(1);;
            sprintf(cBufCommit, "commit_timestamp=%lld", nCounter);

            int ret = p->session->commit_transaction(p->session, cBufCommit);
            switch (ret)
            {
            case 0:
               //printf("kvwtCommitPhaseTwo() succeeded : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnCommitCandidate = %d\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnCommitCandidate); 
               rc = SQLITE4_OK;
               break;
            case WT_ROLLBACK:
               // Integrate with SQLite4/M diagnostics!
               printf("kvwtCommitPhaseTwo() deadlock_resolved / needs_rollback_and_then_can_be_retried : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnCommitCandidate = %p : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnCommitCandidate, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
               rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY
               break;
            case WT_PREPARE_CONFLICT:
               // Integrate with SQLite4/M diagnostics!
               printf("kvwtCommitPhaseTwo() prepare conflict : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnCommitCandidate = %p : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnCommitCandidate, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
               rc = SQLITE4_ERROR; // or: SQLITE4_BUSY
               break;
            case WT_CACHE_FULL:
               // Integrate with SQLite4/M diagnostics!
               printf("kvwtCommitPhaseTwo() no more cache memory : current txn may be rolled-back and retried, and failed after exhaustion on attempt number limit: pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnCommitCandidate = %p : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnCommitCandidate, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
               //rc = SQLITE4_LOCKED; // or: SQLITE4_BUSY // ???
               // rc = SQLITE4_FULL; // ???
               rc = SQLITE4_NOMEM; // ???
               break;
            case EINVAL:  // If the cursor is already closed; or if an invalid flag value or parameter was specified. 
                          // Integrate with SQLite4/M diagnostics!
               printf("kvwtCommitPhaseTwo() failed : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnCommitCandidate = %p : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnCommitCandidate, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
               //rc = SQLITE4_ERROR; // ?
               rc = SQLITE4_MISUSE; // Library used incorrectly (!!!)
               break;
            default:
               // Integrate with SQLite4/M diagnostics!
               printf("kvwtCommitPhaseOneTwo() FAILED : pKVStore = %p, iLevel = %d. pKVStore->iTransLevel = %d, pTxnCommitCandidate = %p : error : '%s'\n", pKVStore, iLevel, pKVStore->iTransLevel, pTxnCommitCandidate, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
               rc = SQLITE4_ERROR; // ?
               break;
            }
            // CAUTION:
            // During kvbdbCommitPhaseOTwo() or kvbdbRollback(), 
            // all pTxnCsr[j]'s above iLevel should be (re-)set to NULL.
            if (rc == SQLITE4_OK)
            {
               int i = 0;
               for (i = pKVStore->iTransLevel; i > iLevel; --i)
               {
                  //p->pTxnCsr[i] = NULL;
                  WT_CURSOR * pCurrCsr = p->pTxnCsr[i];
                  pCurrCsr->close(pCurrCsr); // ???
                  p->pTxnCsr[i] = NULL;
               }
            }

            // At iLevel == 0 close a read-only Cursor, 
            // if any present
            if (iLevel == 0)
            {
               WT_CURSOR * pCsr = p->pCsr;
               if (pCsr != NULL)
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
     // all pTxnCsr[j]'s above iLevel should be (re-)set to NULL.
     // QUESTION:
     // Should we do this clean-up here, 
     // i.e. also for commit-canditate-txn's == NULL, 
     // i.e. when actual commit() has not been executed?
     //if(rc == SQLITE4_OK)
     //{
     //	int i = 0;
     //	for(i = pKVStore->iTransLevel; i > iLevel; --i)
     //	{
     //	    p->pTxnCsr[i] = NULL; 
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
   if (rc == SQLITE4_OK)
   {
      pKVStore->iTransLevel = iLevel;
   }

   return rc;
}




int kvwtRollback(KVStore *pKVStore, int iLevel)
{
   //printf("-----> kvwtRollback(%p,%d)\n", pKVStore, iLevel);   

   int rc = SQLITE4_OK;
   int ret = 0;

   KVWT *p = (KVWT*)pKVStore;

   assert(p->iMagicKVWTBase == SQLITE4_KVWTBASE_MAGIC);
   assert(iLevel >= 0);

   if (pKVStore->iTransLevel >= iLevel)
   {
      if (pKVStore->iTransLevel >= 2)
      {
         // WRITE ops may already be present, 
         // thus we may have something-to-rollback

         // */ ROLLBACK, i.e. abort(), all levels above iLevel 
         // */ cleanup appropriate ->pTxnCsr[i]'s for those levels
         // */ then, _perhaps_ :
         //    **/ abort() iLevel itself
         //    **/ cleanup ->pTxnCsr[iLevel] 		

         //// ROLLBACK/abort() & cleanup all levels above iLevel -- begin
         // ROLLBACK/abort() & cleanup all levels above iLevel and iLevel itself -- begin
         {
            int i = pKVStore->iTransLevel;
            //for(; i > iLevel && i >= 0; --i)
            for (; i >= iLevel && i >= 0; --i) // ... and iLevel itself ...
            {
               WT_CURSOR * pCurrTxn = p->pTxnCsr[i];
               if (pCurrTxn != NULL)
               {
                  // WiredTiger transaction exists, 
                  // ROLLBACK it using abort()
                  // and clean-up p->pTxnCsr[i]

                  char cBufRollback[128];
                  size_t nCounter = oCounter.fetch_add(1);;
                  sprintf(cBufRollback, "rollback_timestamp=%lld", nCounter);

                  ret = p->session->rollback_transaction(p->session, cBufRollback);

                  pCurrTxn->close(pCurrTxn); // clean-up
                  p->pTxnCsr[i] = NULL; // clean-up
                  
                  
                  if (ret != 0)
                  {
                     // ROLLBACK failed
                     if (rc == SQLITE4_OK)
                     {
                        rc = SQLITE4_ERROR;
                     }
                     // Integrate with SQLite4/M diagnostics!
                     printf("kvwtRollback() : abort() failed : pKVStore=%p, pKVStore->iTransLevel=%d, iLevel=%d, i=%d, pCurrTxn=%d, error='%s'\n", pKVStore, pKVStore->iTransLevel, iLevel, i, pCurrTxn, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
                  }
                  else
                  {
                     // ROLLBACK succeeded
                     //printf("kvbdbRollback() : abort() succeeded : pKVStore=%p, pKVStore->iTransLevel=%d, iLevel=%d, i=%d, pCurrTxn=%d\n", pKVStore, pKVStore->iTransLevel, iLevel, i, pCurrTxn);
                  }
               }
            }
         }
         // ROLLBACK/abort() & cleanup all levels above iLevel and iLevel itself -- end
         //// ROLLBACK/abort() & cleanup all levels above iLevel -- end

         //		// _possibly_ : ROLLBACK/abort() & cleanup level iLevel itself -- begin
         //		{
         //		   int pCurrTxn = p->pTxnCsr[iLevel];
         //		   if(pCurrTxn != NULL)
         //		   {
         //		      // BerkeleyDB transaction exists, 
         //			  // ROLLBACK it using abort()
         //			  // and clean-up p->pTxnCsr[iLevel]
         //         char cBufRollback[128];
         //         size_t nCounter = oCounter.fetch_add(1);;
         //         sprintf(cBufRollback, "rollback_timestamp=%lld", nCounter);
         //         ret = p->session->rollback_transaction(p->session, cBufRollback);
         //			  p->pTxnCsr[iLevel] = 0; // clean-up
         //			  if(ret != 0)
         //			  {
         //			    // ROLLBACK failed
         //				if(rc == SQLITE4_OK)
         //				{
         //				   rc = SQLITE4_ERROR;
         //				}
         //				//printf("kvwtRollback() : abort() failed : pKVStore=%p, pKVStore->iTransLevel=%d, iLevel=%d, i=%d, pCurrTxn=%d, error='%s'\n", pKVStore, pKVStore->iTransLevel, iLevel, i, pCurrTxn, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
         //			 }
         //			 else
         //			 {
         //			    // ROLLBACK succeeded
         //           //printf("kvbdbRollback() : abort() succeeded : pKVStore=%p, pKVStore->iTransLevel=%d, iLevel=%d\n", pKVStore, pKVStore->iTransLevel, iLevel);
         //			 }
         //		   }
         //		}
         //		// _possibly_ : ROLLBACK/abort() & cleanup level iLevel itself -- end

      } // end of block: if(pKVStore->iTransLevel >= 2){...}

      if (iLevel == 0)
      {
         if (p->pCsr != NULL)
         {
            // Cursor for read-only ops exists -- 
            // -- close it and clean it up!
            ret = p->pCsr->close(p->pCsr); // closing
            p->pCsr = NULL; // cleaning up
            if (ret != 0)
            {
               if (rc == SQLITE4_OK)
               {
                  rc = SQLITE4_ERROR;
               }
               // Integrate with SQLite4/M diagnostics!
               printf("kvwtRollback() : failed closing Cursor while iLevel==0 : pKVStore=%p, pKVStore->iTransLevel=%d. Error message: '%s'\n", pKVStore, pKVStore->iTransLevel, wiredtiger_strerror(ret), (p->session) ? p->session->strerror(p->session, ret) : "");
            }
            else
            {
               //printf("kvwtRollback() : succeeded closing Cursor while iLevel==0 : pKVStore=%p, pKVStore->iTransLevel=%d.\n", pKVStore, pKVStore->iTransLevel);
            }
         } // end of block : if(p->pCsr != NULL){...}
      } // end of block : if(iLevel == 0){...}

      if (rc == SQLITE4_OK)
      {
         //pKVStore->iTransLevel = iLevel;
         pKVStore->iTransLevel = iLevel - 1;// ... and iLevel itself ...
      }

      // Restart at iLevel to restore a SAVEPOINT -- begin
      // QUESTIONS:
      // Q1/ Is this restart unconditional?
      // Q2/ Or should we use, e.g.: if(iLevel > 0) {rc = kvbdbBegin(pKVStore, iLevel);} ?
      rc = kvwtBegin(pKVStore, iLevel);
      // Restart at iLevel to restore a SAVEPOINT -- end

   } // end of block: if(pKVStore->iTransLevel >= iLevel){...}

   return rc;
}



int kvwtRevert(KVStore *pKVStore, int iLevel) {
   //printf("-----> kvwtRevert(%p,%d)\n", pKVStore, iLevel); 
   // */ kvbdbRevert(..) will, actually, 
   //    be similar to kvwtRollback(...).
   // */ Perhaps, within kvwtRollback(...), 
   //    we should COMMIT at level "iLevel" ?
   // */ Or, perhaps, compared to kvwtRollback(...), 
   //    within kvwtRevert(...) we should 
   //    ROLLBACK/abort(...) at level "iLevel" ?
   // */ Test will, probably, ( :-) ), 
   //    show the proper answer, :-) !
   int rc = kvwtRollback(pKVStore, iLevel - 1);
   if (rc == SQLITE4_OK) {
      rc = kvwtBegin(pKVStore, iLevel);
   }
   return rc;
}

int kvwtClose(sqlite4_kvstore * pkvstore)
{
   //printf("-----> kvwtClose()");

   assert(pkvstore);
   KVWT * pKVWT = (KVWT *)pkvstore;
   assert(pKVWT);

   if (pKVWT == NULL)
   {
      return SQLITE4_ERROR;
   }

   assert(pKVWT->iMagicKVWTBase == SQLITE4_KVWTBASE_MAGIC);
   if (pKVWT->iMagicKVWTBase != SQLITE4_KVWTBASE_MAGIC)
   {
      //return SQLITE4_ERROR; // ???
      return SQLITE4_MISUSE; // ???
   }

   sqlite4_kvstore * pKVBase = &(pKVWT->base);
   assert(pKVBase);

   sqlite4_env * pEnv = pKVBase->pEnv;
   assert(pEnv);

   char * name = pKVWT->name;
   assert(name);
   assert(strlen(name));

   //printf(" : pkvstore=%p, pEnv=%p, name='%s'\n",
   //   pkvstore,
   //   pEnv,
   //   name);

   ///* Close a session handle for the database. */
   //int ret = pKVWT->session->close(pKVWT->session, NULL);
   //if (ret != 0)
   //{
   //   printf("Failed kvwtClose() : session->close(...) : error : '%s'\n", wiredtiger_strerror(ret));
   //   exit(-1);
   //}

   ///* Close a connection to the database. */
   //ret = pKVWT->conn->close(pKVWT->conn, NULL);
   //if (ret != 0)
   //{
   //   printf("Failed kvwtClose() : conn->close(...) : error : '%s'\n", wiredtiger_strerror(ret));
   //   exit(-1);
   //}

   //free(pkvstore);

   // DTOR of KVWT should clean cursor(s) and a session
   delete pKVWT; // ???

   return SQLITE4_OK;
}

int kvwtControl(sqlite4_kvstore * pkvstore, int n, void * arg)
{
   //printf("-----> kvwtControl()\n");

   //return SQLITE4_OK;
   return SQLITE4_NOTFOUND; // similar to what kvbdbControl(...) does
}

int kvwtGetMeta(sqlite4_kvstore * pkvstore, unsigned int * piVal)
{
   //printf("-----> kvwtGetMeta()\n");

   KVWT *p = (KVWT*)pkvstore;
   *piVal = p->iMeta;

   return SQLITE4_OK;
}

int kvwtPutMeta(sqlite4_kvstore * pkvstore, unsigned int iVal)
{
   //printf("-----> kvwtPutMeta()\n");

   KVWT *p = (KVWT*)pkvstore;
   p->iMeta = iVal;
   return SQLITE4_OK;
}

int kvwtGetMethod(sqlite4_kvstore * pkvstore, const char * cname, void **ppArg,
   void(**pxFunc)(sqlite4_context *, int, sqlite4_value **),
   void(**pxDestroy)(void *))
{
   //printf("-----> kvwtGetMethod()\n");

   return SQLITE4_OK;
}


static const sqlite4_kv_methods kvwtMethods = {
   1,                            /* iVersion */
   sizeof(sqlite4_kv_methods),   /* szSelf */
   kvwtReplace,                  /* xReplace */
   kvwtOpenCursor,               /* xOpenCursor */
   kvwtSeek,                     /* xSeek */
   kvwtNext,                     /* xNext */
   kvwtPrev,                     /* xPrev */
   kvwtDelete,                   /* xDelete */
   kvwtKey,                      /* xKey */
   kvwtData,                     /* xData */
   kvwtReset,                    /* xReset */
   kvwtCloseCursor,              /* xCloseCursor */
   kvwtBegin,                    /* xBegin */
   kvwtCommitPhaseOne,           /* xCommitPhaseOne */
   kvwtCommitPhaseOneXID,        /* xCommitPhaseOneXID */
   kvwtCommitPhaseTwo,           /* xCommitPhaseTwo */
   kvwtRollback,                 /* xRollback */
   kvwtRevert,                   /* xRevert */
   kvwtClose,                    /* xClose */
   kvwtControl,                  /* xControl */
   kvwtGetMeta,                  /* xGetMeta */
   kvwtPutMeta                   /* xPutMeta */
};




