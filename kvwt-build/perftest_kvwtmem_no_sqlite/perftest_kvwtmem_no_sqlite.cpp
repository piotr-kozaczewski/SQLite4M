// perftest_kvwtmem_no_sqlite.cpp : 
// */ IN-MEMORY Perf-Test of INSERTing various datatypes  
// */ pure kvwtmem test, not using SQLite4 at all
// */ main purpose of this test is to estimate overhead 
//    of kvwtmem compared to pure WiredTiger
// */ another purpose is to estimate cost of missing 
//    re-use of KVWTCursor's and WT_CURSOR's
//    Context of this follows:
//    **/ TTY trace have shown, that , when called fro SQLite4, 
//        each kvwtReplace() is surrounded 
//        by kvwtOpenCursor() / kvwtCloseCursor()
//    **/ SQLite4+kvwtmem is ca. 2x slower than pure WiredTiger 
//        (as of 14-01-2019).
//    **/ profiling of the Release version have revealed malloc()'s 
//        on the HotPath
//    **/ hypothesis is, that 
//        ***/ current "perftest_kvwtmem_no_sqlite" 
//             is slower than pure WiredTiger, 
//             but faster than SQLite4+kvwtmem
//             i.e. "perftest_kvwtmem"
//        ***/ "perftest_kvwtmem_no_sqlite_surrounding_cursor" 
//             is slower than "perftest_kvwtmem_no_sqlite" 
//             and not much faster than SQLite4+kvwtmem
//             i.e. than "perftest_kvwtmem"
//    **/ should the hypothesis be true, 
//        aggressive caching of KVWTCursor's and WT_CURSORS 
//        makes much sense and would be expected to close/minimize 
//        performance gap between pure WiredTiger and SQLite4+kvwtmem.
//

#include <thread>

#include <vector>

#include <time.h>

extern "C"
{
#include "stdlib.h"
#include "stdio.h"
#include "kvwt.h"
}

#include "rpmalloc.h"



class MyTestTask 
{
public:
   MyTestTask(int a_n_my_num,
      sqlite4_env * a_pEnv,
      char * a_zName,
      int a_numrows_total,
      int a_numrows_per_txn,
      int a_numthreads,
      bool a_lazy_init)
      :  m_n_my_num(a_n_my_num)
      , m_pEnv(a_pEnv)
      , m_zName(a_zName)
      , m_numrows_total(a_numrows_total)
      , m_numrows_per_txn(a_numrows_per_txn)
      , m_numthreads(a_numthreads)
      , m_lazy_init(a_lazy_init)
   {
      createThreadName(a_n_my_num);

      if (!m_lazy_init)
      {
         initialize();
      }
   }

   ~MyTestTask()
   {
      finalize();
   }

public:
   virtual int do_work()
   {
      rpmalloc_thread_initialize();

      if (m_lazy_init)
      {
         initialize();
      }
      int n_num_iter = m_numrows_total / m_numrows_per_txn / m_numthreads;
      for (size_t i = 0; i < n_num_iter; ++i)
      {
         //printf("Thread '%s' Txn#'%d'\n", thread_name, i);

         beginTxn();

         for (size_t j = 0; j < m_numrows_per_txn; ++j)
         {
            //printf("     Thread '%s' Txn#'%d' INSERT#%d\n", thread_name, i, j);

            //insertData();

            int nPK = i * m_numthreads * m_numrows_per_txn
               + m_n_my_num * m_numrows_per_txn
               + j;

            insertData(nPK);
         }

         commitTxn();
      }

      rpmalloc_thread_finalize();

      return -1;
   }
private:
   void initialize()
   {
      //printf("Thread '%s' : initialize()\n", thread_name);

      // open db / session
      int rc = kVStoreOpen(m_pEnv, &m_pKVStore, m_zName, 0);
      if (rc != SQLITE4_OK)
      {
         printf("Cannot open kVStoreOpen(...): %s\n", sqlite4_errmsg(0));
         kvwtClose(m_pKVStore);

         exit( -1);
      }
   }

   void finalize()
   {
      //printf("Thread '%s' : finalize()\n", thread_name);

      kvwtClose(m_pKVStore);
   }
   // 
   void beginTxn()
   {
      //printf("Thread '%s' : beginTxn()\n", thread_name);

      int rc = kvwtBegin(m_pKVStore, 2);
      if (rc != SQLITE4_OK) 
      {
         printf( "Failed kvwtBegin(...)\n", sqlite4_errmsg(0));
         kvwtClose(m_pKVStore);
         exit(-1);
      }
   }

   void commitTxn()
   {
      //printf("Thread '%s' : commitTxn()\n", thread_name);

      int rc = kvwtCommitPhaseOne(m_pKVStore, 2);
      if (rc != SQLITE4_OK) 
      {
         printf("Failed kvwtCommitPhaseOne(...)\n", sqlite4_errmsg(0));
         kvwtClose(m_pKVStore);
         exit(-1);
      }
      rc = kvwtCommitPhaseTwo(m_pKVStore, 2);
      if (rc != SQLITE4_OK)
      {
         printf("Failed kvwtCommitPhaseTwo(...)\n", sqlite4_errmsg(0));
         kvwtClose(m_pKVStore);
         exit(-1);
      }
   }

   void rollbackTxn()
   {
      //printf("Thread '%s' : rollbackTxn()\n", thread_name);

      int rc = kvwtRollback(m_pKVStore, 1);
      if (rc != SQLITE4_OK)
      {
         printf("Failed kvwtRollback(...)\n", sqlite4_errmsg(0));
         kvwtClose(m_pKVStore);
         exit(-1);
      }
   }


   void insertData(int a_nPK)
   {
      //printf("Thread '%s' : insertData(), a_nPK=%d\n", thread_name, a_nPK);
      
      char cKeyBuf[64];
      sprintf(cKeyBuf, "%d", a_nPK);

      //char cDataBuf[256];
      //sprintf(cDataBuf, "%d, '123.456', '2018-11-13 08:52:56.803', 'qazwsx', 'edcrfv'", a_nPK);
      char cDataBuf[128];
      sprintf(cDataBuf, "sqlite4 : %d : 1234567890.1234567 : 17-11-2018 13:03:23.123 : 'qazwsx' : edcrfvtgbyhn' ", a_nPK);

      int rc = kvwtReplace(
         m_pKVStore,
         (const unsigned char *)cKeyBuf, (sqlite4_kvsize)strlen(cKeyBuf),
         (const unsigned char *)cDataBuf, (sqlite4_kvsize)strlen(cDataBuf));
   }

private:
   void createThreadName(int a_nMyNum)
   {
      sprintf(thread_name, "MyTestTask#%d", a_nMyNum);
   }

private:
   int m_n_my_num = 0;

   sqlite4_env * m_pEnv = NULL;
   char * m_zName = NULL;
   sqlite4_kvstore * m_pKVStore = nullptr;

   int m_numrows_total = 0;
   int m_numrows_per_txn = 0;
   int m_numthreads = 0;
   bool m_lazy_init = true;

   
   char m_cBuf[128];

   char thread_name[128];
}; // end of class MyTestTask

int main(int argc, char* argv[])
{
   rpmalloc_initialize();

   char c;

   //std::cout << "Wpisz: ";
   //std::cin >> c;

   clock_t start_t, end_t;
   double total_t;

   sqlite4_env * pEnv = NULL;
   
   char * zName = "perftest_kvwtmem.db";
   sqlite4_kvstore * pKVStore = nullptr;

   int rc = SQLITE4_OK;

   int numrows_total   = 0;
   int numrows_per_txn = 0;
   int numthreads      = 0;
   bool lazy_init = true;

   std::vector<MyTestTask*> oMyTestTaskVector;
   std::vector<std::thread*> oMyTestTaskThreadsVector;

   if (argc == 5)
   {
      numrows_total = atoi(argv[1]);
      numrows_per_txn = atoi(argv[2]);
      numthreads = atoi(argv[3]);
      if (!strcmp(argv[4], "lazy"))
      {
         lazy_init = true;
      }
      else if (!strcmp(argv[4], "eager"))
      {
         lazy_init = false;
      }
      else
      {
         printf("Usage:\nperftest_kvwtmem_no_sqlite numrows_total numrows_per_txn numthreads lazy|eager\n");
         return -1;
      }
   }
   else
   {
      printf("Usage:\nperftest_kvwtmem_no_sqlite numrows_total numrows_per_txn numthreads lazy|eager\n");
      return -1;
   }

   printf("nperftest_kvwtmem_no_sqlite   numrows_total=%d numrows_per_txn=%d numthreads=%d lazy_init=%s \n", 
      numrows_total, numrows_per_txn, numthreads, lazy_init?"true":"false");

   //// initialize db / open session etc. -- begin
   //rc = sqlite4_load_kvstore_plugin(0, "kvwtmem.dll", "kvwtmem");
   //if (rc != SQLITE4_OK)
   //{
   //   printf("Failed sqlite4_load_kvstore_plugin(...) for kvbdb.dll\n");
   //   exit(-1);
   //}

   pEnv = sqlite4_env_default();
   if (pEnv == NULL) 
   {
      printf( "Cannot acquire default environment\n");
      return -1;
   }


   rc = kVStoreOpen(pEnv, &pKVStore, zName, 0);
   if (rc != SQLITE4_OK) 
   {
      printf( "Cannot open kVStoreOpen(...): %s\n", sqlite4_errmsg(0));
      kvwtClose(pKVStore);

      return -1;
   }

   if (pKVStore == NULL)
   {
      printf("pKVStore == NULL\n");
      kvwtClose(pKVStore);

      return -1;
   }
   // initialize db / open session etc. -- end

   start_t = clock();

   // workers -- begin ----------------------------------
      // create 
   for (int i = 0; i < numthreads; ++i)
   {
      MyTestTask * pNewMyTestTask = new MyTestTask(i,
         pEnv,
         zName,
         numrows_total,
         numrows_per_txn,
         numthreads,
         lazy_init);

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
   

   // close KVStore
   kvwtClose(pKVStore);

   printf("\n\n#Rows: %d [-]\n", numrows_total);

   total_t = ((double)(end_t - start_t)) / CLOCKS_PER_SEC;
   printf("Time: %f [s]\n", total_t);

   double speed = ((double)(numrows_total*1.0)) / total_t;
   printf("Speed: %f [rows/s]\n", speed);

   rpmalloc_finalize();

   return 0;
}
   
   
