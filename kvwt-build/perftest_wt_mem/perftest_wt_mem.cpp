// perftest_wt_mem.cpp : 
// */ An attempt to achieve scalability.
// */ IN-MEMORY Perf-Test of INSERTing into WiredTiger w/o kvBdb, SQLIte4. etc.
// */ DEADLOCK-detection && RE-DO added.
// */ rpmalloc added in hope to improve scalability
// */ table with binary keys and binary values:
//    ... key_format=u,value_format=u ...  

//

#include <thread>

#include <vector>

#include <time.h>

extern "C"
{
#include "stdlib.h"
#include "stdio.h"
}

#include "wiredtiger.h"

#include <atomic>

#include "rpmalloc.h"



class MyTestTask 
{
public:
   MyTestTask(int a_n_my_num,
      WT_CONNECTION *conn,
      char * a_zName,
      int a_numrows_total,
      int a_numrows_per_txn,
      int a_numthreads, 
      std::atomic<size_t> &a_counter)
      : m_n_my_num(a_n_my_num)
      , m_conn(conn)
      , m_session(NULL)
      , m_zName(a_zName)
      , m_numrows_total(a_numrows_total)
      , m_numrows_per_txn(a_numrows_per_txn)
      , m_numthreads(a_numthreads)
      , m_counter(a_counter)
   {
      createThreadName(a_n_my_num);
   }

   ~MyTestTask()
   {
   }

public:
   virtual int do_work()
   {
      rpmalloc_thread_initialize();

      openDB();

      int n_num_iter = m_numrows_total / m_numrows_per_txn / m_numthreads;
      for (size_t i = 0; i < n_num_iter; ++i)
      {
         int redo = 0;
         do
         {
            redo = 0;

            beginTxn();

            for (size_t j = 0; j < m_numrows_per_txn; ++j)
            {
               //printf("     Thread '%s' Txn#'%d' INSERT#%d\n", thread_name, i, j);

               int nPK = i * m_numthreads * m_numrows_per_txn
                  + m_n_my_num * m_numrows_per_txn
                  + j;

               int ret = insertData(nPK);
               
               if (ret == WT_ROLLBACK)
               {
                  redo = 1;
                  break;
               }
            }
            if (redo)
            {
               rollbackTxn();
            }
            else
            {
               prepareTxn();
               commitTxn();
            }
         } while (redo);
      }

      closeDB();

      rpmalloc_thread_finalize();

      return -1;
   }
private:
   void openDB()
   {
      /* Open a session handle for the database. */
      int ret = m_conn->open_session(m_conn, NULL, NULL, &m_session);
      if (ret != 0)
      {
         printf("Failed openDB(...) : failed m_conn->open_session(...) : error : '%s', '%s'\n", wiredtiger_strerror(ret), (m_session) ? m_session->strerror(m_session, ret) : "");
         exit(-1);
      }

      /* Open a cursor reusable among transactions */
      /*
      *          If overwrite is false, 
      *          WT_CURSOR::insert fails with WT_DUPLICATE_KEY if the record exists, 
      *          WT_CURSOR::update and WT_CURSOR::remove fail with WT_NOTFOUND if the record does not exist.
      */
      ret = m_session->open_cursor(m_session, "table:sqlite4", NULL, "overwrite=false", &m_cursor);
      if (ret != 0)
      {
         printf("Failed openDB(...) : failed m_session->open_cursor(...) : error : '%s', '%s'\n", wiredtiger_strerror(ret), (m_session) ? m_session->strerror(m_session, ret) : "");
         exit(-1);
      }
   }

   void closeDB()
   {
      /* Close a cursor */
      int ret = m_cursor->close(m_cursor);
      m_cursor = 0;
      if (ret != 0)
      {
         printf("Failed closeDB(...) : failed m_cursor->close(...) : error : '%s', '%s'\n", wiredtiger_strerror(ret), (m_session) ? m_session->strerror(m_session, ret) : "");
         exit(-1);
      }

      /* Close a session handle for the database. */
      ret = m_session->close(m_session, NULL);
      m_session = 0;
      if (ret != 0)
      {
         printf("Failed closeDB(...) : failed m_session->close(...) : error : '%s'\n", wiredtiger_strerror(ret));
         exit(-1);
      }

      
   }

   void beginTxn()
   {
      //printf("Thread '%s' : beginTxn()\n", thread_name);

      int ret = m_session->begin_transaction(m_session, "isolation=read-committed");
      if (ret != 0)
      {
         printf("Failed beginTxn(...) : failed m_session->begin_transaction(...) : error : '%s', '%s'\n", wiredtiger_strerror(ret), (m_session) ? m_session->strerror(m_session, ret) : "");
         exit(-1);
      }
   }

   void prepareTxn()
   {
      //printf("Thread '%s' : prepareTxn()\n", thread_name);

      size_t vCounter = m_counter.fetch_add(1);
      sprintf(m_cBufPrepare, "prepare_timestamp=%ld", vCounter);

      int ret = m_session->prepare_transaction(m_session, m_cBufPrepare);
      if (ret != 0)
      {
         printf("Failed prepareTxn(...) : failed m_session->prepare_transaction(...) : error : '%s', '%s'\n", wiredtiger_strerror(ret), (m_session) ? m_session->strerror(m_session, ret) : "");
         exit(-1);
      }
   }

   void commitTxn()
   {
      //printf("Thread '%s' : commitTxn()\n", thread_name);

      size_t vCounter = m_counter.fetch_add(1);
      sprintf(m_cBufCommit, "commit_timestamp=%ld", vCounter);

      int ret = m_session->commit_transaction(m_session, m_cBufCommit);
      if (ret != 0)
      {
         printf("Failed commitTxn(...) : failed m_session->commit_transaction(...) : error : '%s', '%s'\n", wiredtiger_strerror(ret), (m_session) ? m_session->strerror(m_session, ret) : "");
         exit(-1);
      }
   }

   void rollbackTxn()
   {
      //printf("Thread '%s' : rollbackTxn()\n", thread_name);

      int ret = m_session->rollback_transaction(m_session, NULL);
      if (ret != 0)
      {
         printf("Failed rollbackTxn(...) : failed m_session->rollback_transaction(...) : error : '%s', '%s'\n", wiredtiger_strerror(ret), (m_session) ? m_session->strerror(m_session, ret) : "");
         exit(-1);
      }
   }

   /*
   *          If overwrite is false,
   *          WT_CURSOR::insert fails with WT_DUPLICATE_KEY if the record exists,
   *          WT_CURSOR::update and WT_CURSOR::remove fail with WT_NOTFOUND if the record does not exist.
   */
   //int insertData(int a_nPK)
   //{
   //   //printf("Thread '%s' : insertData(), a_nPK=%d\n", thread_name, a_nPK);
   //   
   //   char cKey[64];
   //   sprintf(cKey, "sqlite4 : %d", a_nPK);

   //   char cData[128];
   //   sprintf(cData, "sqlite4 : %d : 1234567890.1234567 : 17-11-2018 13:03:23.123 : 'qazwsx' : edcrfvtgbyhn' ", a_nPK);

   //   int ret = 0;
   //   m_cursor->set_key(m_cursor, cKey);
   //   m_cursor->set_value(m_cursor, cData);
   //   ret = m_cursor->insert(m_cursor);
   //   if (ret != 0 && ret != WT_ROLLBACK)
   //   {
   //      printf("Failed insertData(...) : failed m_cursor->insert(...) : error : '%s', '%s'\n", wiredtiger_strerror(ret), (m_session) ? m_session->strerror(m_session, ret) : "");
   //      exit(-1);
   //   }
   //   return ret;
   //}

   int insertData(int a_nPK)
   {
      //printf("Thread '%s' : insertData(), a_nPK=%d\n", thread_name, a_nPK);

      //char cKey[64];
      //sprintf(cKey, "sqlite4 : %10.10d", a_nPK);
      
      WT_ITEM oKey;
      oKey.data = &a_nPK;
      oKey.size = sizeof(int);

      char cData[128];
      sprintf(cData, "sqlite4 : %d : 1234567890.1234567 : 17-11-2018 13:03:23.123 : 'qazwsx' : edcrfvtgbyhn' ", a_nPK);

      WT_ITEM oData;
      oData.data = cData;
      oData.size = strlen(cData);

      int ret = 0;
      m_cursor->set_key(m_cursor, &oKey);
      m_cursor->set_value(m_cursor, &oData);
      ret = m_cursor->insert(m_cursor);
      if (ret != 0 && ret != WT_ROLLBACK)
      {
         printf("Failed insertData(...) : failed m_cursor->insert(...) : error : '%s', '%s'\n", wiredtiger_strerror(ret), (m_session) ? m_session->strerror(m_session, ret) : "");
         exit(-1);
      }
      return ret;
   }

private:
   void createThreadName(int a_nMyNum)
   {
      sprintf(thread_name, "MyTestTask#%d", a_nMyNum);
   }

private:
   int m_n_my_num = 0;

   WT_CONNECTION *m_conn;
   WT_SESSION *m_session;
   WT_CURSOR *m_cursor;

   char * m_zName = NULL;

   int m_numrows_total = 0;
   int m_numrows_per_txn = 0;
   int m_numthreads = 0;

   std::atomic<size_t> &m_counter;

   char m_cBuf[128];

   char thread_name[128];

   char m_cBufPrepare[128];
   char m_cBufCommit[128];
}; // end of class MyTestTask

int main(int argc, char* argv[])
{
   rpmalloc_initialize();

   std::atomic<size_t> oCounter(1); // transactions' counter, zero (0) not permitted as txn counter/timestamp!

   WT_CONNECTION *conn;
   WT_SESSION *session;

   clock_t start_t, end_t;
   double total_t;

   int ret = 0;

   char * zName = "perftest_wt_mem.db";

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
      printf("Usage:\nperftest_wt_mem numrows_total numrows_per_txn numthreads \n");
      return -1;
   }

   printf("perftest_wt_me m  numrows_total=%d numrows_per_txn=%d numthreads=%d \n", 
      numrows_total, numrows_per_txn, numthreads );

   // ==================================================================================
   /* Open a connection to the database, creating it if necessary. */
   ret = wiredtiger_open(NULL, NULL, "create, in_memory=true, cache_size=2147483648, cache_overhead=8, checkpoint={log_size=2147483648, wait=0}, checkpoint_sync=false, log={file_max=2147483648}", &conn);
   if (ret != 0)
   {
      printf("Failed wiredtiger_open(...) : error : '%s'\n", wiredtiger_strerror(ret));
      exit(-1);
   }

   /* Open a session handle for the database. */
   ret = conn->open_session(conn, NULL, NULL, &session);
   if (ret != 0)
   {
      printf("Failed conn->open_session(...) : error : '%s', '%s'\n", wiredtiger_strerror(ret), (session)?session->strerror(session, ret):"");
      exit(-1);
   }

   /* Create a table for SQLite4 */
   ret = session->create(session, "table:sqlite4", "access_pattern_hint=sequential, cache_resident=true, ignore_in_memory_cache_size=true, key_format=u,value_format=u");
   if (ret != 0)
   {
      printf("Failed session->create(...) : error : '%s', '%s'\n", wiredtiger_strerror(ret), (session) ? session->strerror(session, ret) : "");
      exit(-1);
   }
   // ==================================================================================


   // ==================================================================================

   start_t = clock();

   // workers -- begin ----------------------------------
      // create 
   for (int i = 0; i < numthreads; ++i)
   {
      MyTestTask * pNewMyTestTask = new MyTestTask(i,
         conn,
         zName,
         numrows_total,
         numrows_per_txn,
         numthreads,
         oCounter);

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
   


   /* Drop a table for SQLite4 */
   ret = session->drop(session, "table:sqlite4", NULL);
   if (ret != 0)
   {
      printf("Failed session->drop(...) : error : '%s', '%s'\n", wiredtiger_strerror(ret), (session) ? session->strerror(session, ret) : "");
      exit(-1);
   }

   /* Close a session handle for the database. */
   ret = session->close(session, NULL);
   if (ret != 0)
   {
      printf("Failed session->close(...) : error : '%s'\n", wiredtiger_strerror(ret));
      exit(-1);
   }

   /* Close a connection to the database. */
   ret = conn->close(conn, NULL);
   if (ret != 0)
   {
      printf("Failed conn->close(...) : error : '%s'\n", wiredtiger_strerror(ret));
      exit(-1);
   }
   // ==================================================================================

   printf("\n\n#Rows: %d [-]\n", numrows_total);

   total_t = ((double)(end_t - start_t)) / CLOCKS_PER_SEC;
   printf("Time: %f [s]\n", total_t);

   double speed = ((double)(numrows_total*1.0)) / total_t;
   printf("Speed: %f [rows/s]\n", speed);

   rpmalloc_finalize();

   return 0;
}
