#include "kvwt_common.h"

#include <map>

//uint32_t nGlobalDefaultInitialCursorKeyBufferCapacity = 16384; // 16 k
//uint32_t nGlobalDefaultInitialCursorDataBufferCapacity = 16384; // 16 k

typedef struct KVWTEnv KVWTEnv;

struct KVWTEnv {
   std::mutex mutex;
   WT_CONNECTION * conn;
   char db_name[128];
   char table_name[128];
   uint32_t n_ref;

   KVWTEnv()
      : conn(nullptr)
      , n_ref(0)
   {
      memset(db_name, 0, 128);
      memset(table_name, 0, 128);
   }

   ~KVWTEnv()
   {
      if (conn)
      {
         conn->close(conn, nullptr);
         conn = nullptr;
      }
      n_ref = 0;
      memset(db_name, 0, 128);
      memset(table_name, 0, 128);
   }
};

std::mutex gKVWTEnvMapMutex;
std::map<std::string, KVWTEnv*> gKVWTEnvMap;

KVWTEnv * acquireKVWTEnv(const char * zName)
{
   //printf("-----> KVWT::acquireKVWTEnv(...)\n");

   std::lock_guard<std::mutex> oLock(gKVWTEnvMapMutex);
   KVWTEnv * pKVWTEnv = nullptr;

   auto it = gKVWTEnvMap.find(zName);
   if (it != gKVWTEnvMap.end())
   {
      // found
      pKVWTEnv = it->second;
   }
   else
   {
      // not found
      try
      {
         pKVWTEnv = new KVWTEnv;
         sprintf(pKVWTEnv->db_name, "%s", zName);
         //sprintf(pKVWTEnv->table_name, "table:sqlite4"); // see "test_015.cpp", l. 129
         //sprintf(pKVWTEnv->table_name, "sqlite4:%s", zName);
         sprintf(pKVWTEnv->table_name, "table:%s", zName);
         int ret = 0;
         ret = wiredtiger_open(NULL, NULL, "create, in_memory=true, cache_size=2147483648, cache_overhead=8, checkpoint={log_size=2147483648, wait=0}, checkpoint_sync=false, log={file_max=2147483648}", &(pKVWTEnv->conn));
         if (ret != 0)
         {
            // error

            // How to integrate with SQLite4 diagnostics?
            printf("\nFailed KWVT::kVStoreOpen() : failed wiredtiger_open(...) : error : '%s'\n", wiredtiger_strerror(ret));
            delete pKVWTEnv;
            pKVWTEnv = nullptr;
         }
         else
         {
            // success

            gKVWTEnvMap.insert(std::pair<std::string, KVWTEnv*>(zName, pKVWTEnv));
         }
      }
      catch (...)
      {
         if (pKVWTEnv)
         {
            delete pKVWTEnv;
            pKVWTEnv = nullptr;
         }
      }
   }
   return pKVWTEnv;
} // end of acquireKVWTEnv(...){...}

int acquireWTResources(const char * zName, KVWT * pKVWT)
{
   //printf("-----> KVWT::acquireWTResources(...)\n");

   int rc = SQLITE4_OK;
   KVWTEnv * pKVWTEnv = acquireKVWTEnv(zName);
   
   assert(pKVWTEnv);
   if (!pKVWTEnv)
   {
      rc = SQLITE4_ERROR;
      return SQLITE4_ERROR;
   }

   std::lock_guard<std::mutex> oLock(pKVWTEnv->mutex);

   assert(pKVWTEnv->conn);
   if (!pKVWTEnv->conn)
   {
      rc = SQLITE4_ERROR;
      return SQLITE4_ERROR;
   }

   assert(!pKVWT->session);
   if (pKVWT->session)
   {
      rc = SQLITE4_ERROR;
      return SQLITE4_ERROR;
   }

   int ret = pKVWTEnv->conn->open_session(pKVWTEnv->conn, NULL, NULL, &(pKVWT->session));
   if (ret != 0)
   {
      // error

      // How to integrate with SQLite4 diagnostics?
      printf("\nFailed KWVT::kVStoreOpen() : failed wiredtiger_open(...) : error : '%s'\n", wiredtiger_strerror(ret));
      rc = SQLITE4_ERROR;
      return SQLITE4_ERROR;
   }

   // Create a table for SQLite4/M database
   ret = pKVWT->session->create(pKVWT->session, pKVWTEnv->table_name, "access_pattern_hint=sequential, cache_resident=true, ignore_in_memory_cache_size=true, key_format=u,value_format=u");
   if (ret != 0)
   {
      // Integrate with SQLite4/M diagnostics!
      printf("Failed session->create(...) : error : '%s', '%s'\n", wiredtiger_strerror(ret), (pKVWT->session) ? pKVWT->session->strerror(pKVWT->session, ret) : "");
      rc = SQLITE4_ERROR;
      return SQLITE4_ERROR;
   }

   // success
   // 
   // copy conn from pKVWTEnv to pKVWT
   pKVWT->conn = pKVWTEnv->conn;
   //
   // set name(s)
   //pKVWT->dbname = const_cast<char *>(zName); // moved to CTOR
   // strcpy(pKVWT->name, pKVWTEnv->db_name); // moved to CTOR
   strcpy(pKVWT->table_name, pKVWTEnv->table_name);
   //
   //// set initial buffer sizes for Cursor's Key and Value -- moved to CTOR
   //pKVWT->nInitialCursorKeyBufferCapacity = nGlobalDefaultInitialCursorKeyBufferCapacity;
   //pKVWT->nInitialCursorDataBufferCapacity = nGlobalDefaultInitialCursorDataBufferCapacity;

   return rc;
} // end of acquireWTResources(...){...}


int kVStoreOpen(
   sqlite4_env *pEnv,              /* Runtime environment */
   sqlite4_kvstore **ppKVStore,    /* OUT: Write the new sqlite4_kvstore here */
   const char *zName,              /* Name of BerkeleyDB storage unit */
   unsigned openFlags              /* Flags */
   )
{
   //printf("-----> KVWT::kVStoreOpen() ");
   assert(pEnv);

   //KVWT * pNewKVWT = (KVWT*)malloc(sizeof(KVWT));
   KVWT * pNewKVWT = nullptr;
   try
   {
      pNewKVWT = new KVWT(zName);
   }
   catch (...)
   {
      printf("\nFailed KWVT::kVStoreOpen() : failed creation of the new KVWT.\n");
      return SQLITE4_ERROR;
   }

   assert(pNewKVWT);
   if(!pNewKVWT)
   {
      printf("\nFailed KWVT::kVStoreOpen() : failed creation of the new KVWT.\n");
      return SQLITE4_ERROR;
   }

   int ret = 0;
   ret = acquireWTResources(zName, pNewKVWT);
   assert(ret == SQLITE4_OK);
   if (ret != SQLITE4_OK)
   {
      printf("\nFailed KWVT::kVStoreOpen() : failed acquireWTResources(...)\n");
      return SQLITE4_ERROR;
   }
   //
   // SUCCESS - fill the remaining fields/members
   pNewKVWT->base.pStoreVfunc = &kvwtMethods;
   pNewKVWT->base.pEnv = pEnv;
   pNewKVWT->base.iTransLevel = 0;

   //printf(" : pEnv=%p, zName=%s, pNewKVWT=%p, conn=%p, session=%p\n",
   //   pEnv,
   //   zName,
   //   pNewKVWT,
   //   pNewKVWT->conn,
   //   pNewKVWT->session);

   *ppKVStore = (sqlite4_kvstore*)pNewKVWT;

   return (0);
}