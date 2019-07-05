// perftest_bdb_api_mem.cpp : 
// */ An attempt to achieve scalability.
// */ FIX for read access violation at l. 31 in __os_fsync(...), 
//    replicated by test_008.cpp, 
//    fixed by test_010.cpp    
// */ IN-MEMORY Perf-Test of INSERTing into BerkeleyDB w/o kvBdv, SQLIte4. etc.

#include <thread>

#include <vector>

#include <time.h>

extern "C"
{
#include "stdlib.h"
#include "stdio.h"
#include "db.h"
}



class MyTestTask 
{
public:
   MyTestTask(int a_n_my_num,
      DB_ENV * a_pEnv,
      char * a_zName,
      int a_numrows_total,
      int a_numrows_per_txn,
      int a_numthreads)
      : m_n_my_num(a_n_my_num)
      , m_pEnv(a_pEnv)
      , m_pDb(NULL)
      , m_zName(a_zName)
      , m_numrows_total(a_numrows_total)
      , m_numrows_per_txn(a_numrows_per_txn)
      , m_numthreads(a_numthreads)
   {
      createThreadName(a_n_my_num);
   }

   MyTestTask(const MyTestTask & obj)
      : m_n_my_num(obj.m_n_my_num)
      , m_pEnv(obj.m_pEnv)
      , m_pDb(obj.m_pDb)
      , m_zName(obj.m_zName)
      , m_numrows_total(obj.m_numrows_total)
      , m_numrows_per_txn(obj.m_numrows_per_txn)
      , m_numthreads(obj.m_numthreads)
   {
      createThreadName(m_n_my_num);
   }

   ~MyTestTask()
   {
   }

public:
   virtual int do_work()
   {
      openDB();

      int n_num_iter = m_numrows_total / m_numrows_per_txn / m_numthreads;
      for (size_t i = 0; i < n_num_iter; ++i)
      {
         int redo = 0;
         do
         {
            redo = 0;

            DB_TXN *  pTxn = NULL;
            beginTxn(&pTxn);

            for (size_t j = 0; j < m_numrows_per_txn; ++j)
            {
               //printf("     Thread '%s' Txn#'%d' INSERT#%d\n", thread_name, i, j);

               //insertData();

               int nPK = i * m_numthreads * m_numrows_per_txn
                  + m_n_my_num * m_numrows_per_txn
                  + j;

               int ret = insertData(pTxn, nPK);
               
               if (ret == DB_LOCK_DEADLOCK)
               {
                  redo = 1;
                  break;
               }
            }
            if (redo)
            {
               rollbackTxn(pTxn);
            }
            else
            {
               prepareTxn(pTxn);
               commitTxn(pTxn);
            }
         } while (redo);
      }

      closeDB();

      return -1;
   }
private:
   void openDB()
   {
      u_int32_t extra_flags = 0;
      u_int32_t open_flags = 0;
      int ret = 0;

      /*
      * create the database
      */
      ret = db_create(&m_pDb, m_pEnv, 0);
      if (ret != 0)
      {
         printf("Error creating database: %s\n", db_strerror(ret));
         exit(-1);
      }

      /*
      * Keep Temporary Overflow Pages in Memory
      */
      DB_MPOOLFILE * mpf = m_pDb->get_mpf(m_pDb);
      ret = mpf->set_flags(mpf, DB_MPOOL_NOFILE, 1);
      if (ret != 0)
      {
         printf("Attempt failed to configure for no backing of temp files: %s\n", db_strerror(ret));
         exit(-1);
      }

      /* set extra flags before open, if any needed */
      if (extra_flags != 0)
      {
         ret = m_pDb->set_flags(m_pDb, extra_flags);
         if (ret != 0)
         {
            printf("Error setting extra flags: %s\n", db_strerror(ret));
            exit(-1);
         }
      }

      /* prepare open flags */
      open_flags = DB_CREATE | /* Allow database creation */
         DB_THREAD |
         DB_AUTO_COMMIT;    /* Allow autocommit */

                            /*
                            * Now open the database
                            */
      ret = m_pDb->open(m_pDb, /* Pointer to the database */
         NULL,          /* Txn pointer */
         NULL,         /* File name */
         m_zName,      /* Logical db name */
         DB_BTREE,      /* Database type (using btree) */
         open_flags,    /* Open flags */
         0);
      if (ret != 0)
      {
         printf("Error opening database '%s'. Error message: '%s'\n", m_zName, db_strerror(ret));
         exit(-1);
      }
   }

   void closeDB()
   {
      int ret = 0;

      /*
      * Close a database
      */
      ret = m_pDb->close(m_pDb, 0);
      if (ret != 0)
      {
         printf("Failed to close a database , error='%s'\n", db_strerror(ret));
         exit(-1);
      }
   }

   void beginTxn(DB_TXN ** a_ppTxn)
   {
      //printf("Thread '%s' : beginTxn()\n", thread_name);

      u_int32_t flags = DB_READ_COMMITTED;

      int ret = 0;

      ret = m_pEnv->txn_begin(m_pEnv, NULL, a_ppTxn, flags);
      if (ret != 0)
      {
         // failed
         printf("Failed DB_TXN Transaction creation in beginTxn() : error='%s'\n", db_strerror(ret));
         exit(-1);
      }
   }

   void prepareTxn(DB_TXN * a_pTxn)
   {
      //printf("Thread '%s' : prepareTxn()\n", thread_name);

      DB_TXN * pTxnPrepareCandidate = a_pTxn; // for naming conformance with "kvbdb.c"

      // Prepare a GID for the txn to be prepared.
      u_int8_t gid[DB_GID_SIZE]; // DB_GID_SIZE == 128 bytes
      memset(gid, 0, DB_GID_SIZE);
      time_t currTime;
      currTime = time(NULL); // think of finer granularity or, e.g.  <thread, per-thread counter>
      sprintf((char*)gid, "%p:%ld", (void*)pTxnPrepareCandidate, currTime);

      int ret = 0;
      ret = pTxnPrepareCandidate->prepare(pTxnPrepareCandidate, gid);
      if (ret != 0)
      {
         // failed
         printf("Failed DB_TXN Transaction preparation in prepareTxn() : error='%s'\n", db_strerror(ret));
         exit(-1);
      }
   }

   void commitTxn(DB_TXN * a_pTxn)
   {
      //printf("Thread '%s' : commitTxn()\n", thread_name);

      DB_TXN * pTxnCommitCandidate = a_pTxn; // for naming conformance with "kvbdb.c"

      /*
      // kvbdbBegin() flags were:
      u_int32_t flags = DB_READ_COMMITTED
                       | DB_TXN_BULK
                       | DB_TXN_NOSYNC
                       | DB_TXN_WRITE_NOSYNC
                          //| DB_TXN_NOWAIT
                       | DB_TXN_WAIT
                       ;
      */
      int ret = 0;
      ret = pTxnCommitCandidate->commit(pTxnCommitCandidate, 0);
      if (ret != 0)
      {
         // failed
         printf("Failed DB_TXN Transaction commit in commitTxn() : error='%s'\n", db_strerror(ret));
         exit(-1);
      }
   }

   void rollbackTxn(DB_TXN * a_pTxn)
   {
      //printf("Thread '%s' : rollbackTxn()\n", thread_name);

      DB_TXN * pTxnRollbackCandidate = a_pTxn; // for naming conformance with "kvbdb.c"

      int ret = 0;
      ret = pTxnRollbackCandidate->abort(pTxnRollbackCandidate);
      if (ret != 0)
      {
         // failed
         printf("Failed DB_TXN Transaction abort in rollbackTxn() : error='%s'\n", db_strerror(ret));
         exit(-1);
      }
   }

   int insertData(DB_TXN *  a_pTxn, int a_nPK)
   {
      //printf("Thread '%s' : insertData(), a_nPK=%d\n", thread_name, a_nPK);

      DB_TXN * pCurrTxn = a_pTxn; // for naming conformance with "kvbdb.c"
      
      char cKey[64];
      sprintf(cKey, "table008 : %d", a_nPK);

      char cData[128];
      sprintf(cData, "table008 : %d : 1234567890.1234567 : 17-11-2018 13:03:23.123 : 'qazwsx' : edcrfvtgbyhn' ", a_nPK);

      // Prepare DBT's for Key and Data -- begin
      DBT keyDBT;
      DBT dataDBT;
      //
      memset(&keyDBT, 0, sizeof(DBT));
      memset(&dataDBT, 0, sizeof(DBT));
      //
      keyDBT.data = (void*)cKey;
      keyDBT.size = (u_int32_t)strlen(cKey);
      dataDBT.data = (void*)cData;
      dataDBT.size = (u_int32_t)strlen(cData);
      // Prepare DBT's for Key and Data -- end

      int ret = 0;

      ret = m_pDb->put(m_pDb, pCurrTxn, &keyDBT, &dataDBT, 0); // flag==0 taken from file "txn_guide.c", line 256. Is this flag OK?
      if (ret == 0)
      {
         // success
         return 0;
      }
      else if (ret == DB_LOCK_DEADLOCK)
      {
         // deadlock - try to redo
         printf("%s\n", db_strerror(ret));
         return ret;
      }
      else if (ret != 0)
      {
         // failed
         printf("Failed db->put(...) in insertData(...) : error='%s'\n", db_strerror(ret));
         exit(-1);
      }
   }

private:
   void createThreadName(int a_nMyNum)
   {
      sprintf(thread_name, "MyTestTask#%d", a_nMyNum);
   }

private:
   int m_n_my_num = 0;

   DB_ENV * m_pEnv;
   DB * m_pDb;

   char * m_zName = NULL;

   int m_numrows_total = 0;
   int m_numrows_per_txn = 0;
   int m_numthreads = 0;

   char m_cBuf[128];

   char thread_name[128];
}; // end of class MyTestTask

int main(int argc, char* argv[])
{
   clock_t start_t, end_t;
   double total_t;

   DB_ENV * envp = NULL;
   u_int32_t env_flags = 0;

   //DB * dbp = NULL;
   //u_int32_t extra_flags = 0;
   //u_int32_t open_flags = 0;

   int ret = 0;

   char * zName = "perftest_bdb_api_mem.db";

   int numrows_total   = 0;
   int numrows_per_txn = 0;
   int numthreads      = 0;

   std::vector<MyTestTask*> oMyTestTaskVector;
   std::vector<std::thread*> oMyTestTaskThreadsVector;

   if (argc == 4)
   {
      numrows_total = atoi(argv[1]);
      numrows_per_txn = atoi(argv[2]);
      numthreads = atoi(argv[3]);
   }
   else
   {
      printf("Usage:\nperftest_bdb_api_mem numrows_total numrows_per_txn numthreads \n");
      return -1;
   }

   printf("perftest_bdb_api_mem   numrows_total=%d numrows_per_txn=%d numthreads=%d \n", 
      numrows_total, numrows_per_txn, numthreads );

   // ==================================================================================

   /* Create the environment */
   ret = db_env_create(&envp, 0);
   if (ret != 0)
   {
      printf("Error creating environment handle: %s\n", db_strerror(ret));
      exit(-1);
   }

   /*
   * Specify env flags
   */
   env_flags =
      DB_CREATE |  /* Create the environment if it does not exist */
      DB_INIT_LOCK |  /* Initialize the locking subsystem */
      DB_INIT_LOG |  /* Initialize the logging subsystem */
      DB_INIT_TXN |  /* Initialize the transactional subsystem. This
                     * also turns on logging. */
      DB_INIT_MPOOL |  /* Initialize the memory pool (in-memory cache) */
      DB_PRIVATE |  /* Region files are backed by heap memory.  */
      DB_THREAD;       /* Cause the environment to be free-threaded */

   /* 
   * Specify in-memory logging 
   */
   ret = envp->log_set_config(envp, DB_LOG_IN_MEMORY, 1);
   if (ret != 0)
   {
      printf("Error setting log subsystem to in-memory: %s\n", db_strerror(ret));
      exit(-1);
   }

   /*
   * Specify the size of the in-memory log buffer, 100 MB
   */
   ret = envp->set_lg_bsize(envp, 100 * 1024 * 1024 );
   if (ret != 0)
   {
      printf("Error increasing the log buffer size: %s\n", db_strerror(ret));
      exit(-1);
   }

   /*
   * Specify the size of the in-memory cache, 1 GB
   */
   ret = envp->set_cachesize(envp, 0, 10 * 1024 * 1024 * 1024, 1);
   if (ret != 0)
   {
      printf("Error increasing the cache size: %s\n", db_strerror(ret));
      exit(-1);
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
      exit(-1);
   }

   /* 
   * Now actually open the environment 
   */
   ret = envp->open(envp, NULL, env_flags, 0);
   if (ret != 0)
   {
      printf("Error opening environment: %s\n", db_strerror(ret));
      exit(-1);
   }

   // ==================================================================================

   start_t = clock();

   // workers -- begin ----------------------------------
      // create 
   for (int i = 0; i < numthreads; ++i)
   {
      MyTestTask * pNewMyTestTask = new MyTestTask(i,
         envp,
         zName,
         numrows_total,
         numrows_per_txn,
         numthreads);

      if (pNewMyTestTask)
      {
         oMyTestTaskVector.push_back(pNewMyTestTask);
      }
      else
      {
         printf("ERROR: pNewMyTestTask == nullptr\n");
         exit(-1);
      }
   }

   // run
   for (int i = 0; i < numthreads; ++i)
   {
      std::thread * pNewThread = new std::thread(&MyTestTask::do_work, oMyTestTaskVector[i]);
      oMyTestTaskThreadsVector.push_back(pNewThread);
   }

   // wait/join
   for (int i = 0; i < numthreads; ++i)
   {
      std::thread * pCurrThread = oMyTestTaskThreadsVector[i];
      pCurrThread->join();
      //delete pCurrThread; // ???
   }

   // destroy
   for (int i = 0; i < numthreads; ++i)
   {
      MyTestTask * pNewMyTestTask = oMyTestTaskVector[i];
      oMyTestTaskVector[i] = nullptr;
      delete pNewMyTestTask;
   }

   // cleanup
   oMyTestTaskThreadsVector.clear();
   oMyTestTaskVector.clear();
   // workers --   end ----------------------------------

   end_t = clock();
   
   // ==================================================================================
   
   /*
   * Close the environment - database(s)/session(s) have already been closed by worker threads
   */
   ret = envp->close(envp, 0);
   if (ret != 0)
   {
      printf("Failed to close the environment , error='%s'\n", db_strerror(ret));
      exit(-1);
   }
   // ==================================================================================

   printf("\n\n#Rows: %d [-]\n", numrows_total);

   total_t = ((double)(end_t - start_t)) / CLOCKS_PER_SEC;
   printf("Time: %f [s]\n", total_t);

   double speed = ((double)(numrows_total*1.0)) / total_t;
   printf("Speed: %f [rows/s]\n", speed);

   return 0;
}
