// perftest_kvwtmem.cpp : 
// */ IN-MEMORY Perf-Test of INSERTing various datatypes  w/ Parameter Binding.
//

#include <thread>

#include <vector>

#include <time.h>

extern "C"
{
#include "stdlib.h"
#include "stdio.h"
#include "sqlite4.h"
}

#include "rpmalloc.h"

void execute_select_or_exit(sqlite4 * db, const char * sSql, sqlite4_stmt ** ppStmt)
{
   int rc = SQLITE4_OK;

   rc = sqlite4_prepare(db, "SELECT SQLITE_VERSION()", -1, ppStmt, 0);

   if (rc != SQLITE4_OK) {

      printf( "Failed to fetch data: %s\n", sqlite4_errmsg(db));

      sqlite4_finalize(*ppStmt);
      sqlite4_close(db, 0);

      exit(-1);
   }

   rc = sqlite4_step(*ppStmt);

   if (rc != SQLITE4_ROW) {
      printf( "Not a RowSet\n");

      sqlite4_finalize(*ppStmt);
      sqlite4_close(db, 0);

      exit(-1);
   }
}

const char * column_text_or_exit(sqlite4 * db, sqlite4_stmt * stmt, int nCol)
{
   int nBytes = 0;
   const char * retval = sqlite4_column_text(stmt, nCol, &nBytes);
   return retval;
}


void execute_or_exit(sqlite4 * db, const char * sSql)
{
   printf("%s\n", sSql);

   sqlite4_stmt * pStmt = 0;
   int rc = SQLITE4_OK;

   rc = sqlite4_prepare(db, sSql, -1, &pStmt, 0);

   if (rc != SQLITE4_OK) {

      printf( "Failed to execute stmt [prepare]: %s\n", sqlite4_errmsg(db));

      sqlite4_finalize(pStmt);
      sqlite4_close(db, 0);

      exit(-1);
   }

   rc = sqlite4_step(pStmt);

   //if (rc != SQLITE4_OK) {

   //   printf( "Failed to execute stmt [step]: %s\n", sqlite4_errmsg(db));

   //   sqlite4_finalize(pStmt);
   //   sqlite4_close(db, 0);

   //   exit(-1);
   //}

   sqlite4_finalize(pStmt);
}

void count_rows_in_table_and_print_or_exit(sqlite4 * db, const char * sTable)
{
   char sSql[128];
   sprintf_s(sSql, "SELECT COUNT(*) from %s", sTable);

   printf("%s\n", sSql);

   sqlite4_stmt * pStmt = 0;

   int rc = SQLITE4_OK;

   rc = sqlite4_prepare(db, sSql, -1, &pStmt, 0);

   if (rc != SQLITE4_OK) {

      printf( "Failed to fetch data: %s\n", sqlite4_errmsg(db));

      sqlite4_finalize(pStmt);
      sqlite4_close(db, 0);

      exit(-1);
   }

   rc = sqlite4_step(pStmt);

   if (rc != SQLITE4_ROW) {
      printf( "Not a RowSet\n");

      sqlite4_finalize(pStmt);
      sqlite4_close(db, 0);

      exit(-1);
   }

   int retval = sqlite4_column_int(pStmt, 0);

   sqlite4_finalize(pStmt);

   printf("%d\n", retval);
}

void drop_table(sqlite4 * db)
{
   execute_or_exit(db, "drop table table06");
}

void count_count_rows_in_table(sqlite4 * db)
{
   count_rows_in_table_and_print_or_exit(db, "table06");
}

void create_table(sqlite4 * a_pDB)
{
   execute_or_exit(a_pDB, "create table if not exists table06 (c_int integer PRIMARY KEY, c_num number, c_datetime text, c_char char(20), c_varchar varchar(20))");
}


void prepare_insert(sqlite4 * a_pDB, sqlite4_stmt ** ppStmtInsert)
{
   char * sSQL = "insert into table06 (c_int, c_num, c_datetime, c_char, c_varchar) values (:p_int, :p_num, :p_datetime, :p_char, :p_varchar)";

   int rc = SQLITE4_OK;

   rc = sqlite4_prepare(a_pDB, sSQL, -1, ppStmtInsert, 0);

   if (rc != SQLITE4_OK) {

      printf( "Failed to execute INSERT stmt [prepare]: %s\n", sqlite4_errmsg(a_pDB));

      sqlite4_finalize(*ppStmtInsert);
      sqlite4_close(a_pDB, 0);

      exit(-1);
   }
}

void bind_params_and_execute_insert(sqlite4 * a_pDB, sqlite4_stmt * pStmtInsert, char * p_1, char * p_2, char * p_3, char * p_4, char * p_5)
{
   int rc = SQLITE4_OK;

   // 1st Param
   rc = sqlite4_bind_text(pStmtInsert, 1, p_1, strlen(p_1), SQLITE4_STATIC, NULL);
   if (rc != SQLITE4_OK) {

      printf( "Failed to bind the 1st Parameter: %s\n", sqlite4_errmsg(a_pDB));

      sqlite4_finalize(pStmtInsert);
      sqlite4_close(a_pDB, 0);

      exit(-1);
   }

   // 2nd Param
   rc = sqlite4_bind_text(pStmtInsert, 2, p_2, strlen(p_2), SQLITE4_STATIC, NULL);
   if (rc != SQLITE4_OK) {

      printf( "Failed to bind the 2nd Parameter: %s\n", sqlite4_errmsg(a_pDB));

      sqlite4_finalize(pStmtInsert);
      sqlite4_close(a_pDB, 0);

      exit(-1);
   }

   // 3rd Param
   rc = sqlite4_bind_text(pStmtInsert, 3, p_3, strlen(p_3), SQLITE4_STATIC, NULL);
   if (rc != SQLITE4_OK) {

      printf( "Failed to bind the 3rd Parameter: %s\n", sqlite4_errmsg(a_pDB));

      sqlite4_finalize(pStmtInsert);
      sqlite4_close(a_pDB, 0);

      exit(-1);
   }

   // 4th Param
   rc = sqlite4_bind_text(pStmtInsert, 4, p_4, strlen(p_4), SQLITE4_STATIC, NULL);
   if (rc != SQLITE4_OK) {

      printf( "Failed to bind the 4th Parameter: %s\n", sqlite4_errmsg(a_pDB));

      sqlite4_finalize(pStmtInsert);
      sqlite4_close(a_pDB, 0);

      exit(-1);
   }

   // 5th Param
   rc = sqlite4_bind_text(pStmtInsert, 5, p_5, strlen(p_5), SQLITE4_STATIC, NULL);
   if (rc != SQLITE4_OK) {

      printf( "Failed to bind the 5th Parameter: %s\n", sqlite4_errmsg(a_pDB));

      sqlite4_finalize(pStmtInsert);
      sqlite4_close(a_pDB, 0);

      exit(-1);
   }

   // step
   rc = sqlite4_step(pStmtInsert);
   if (rc != SQLITE4_DONE) {

      printf( "Failed to step/execute the INSERT statement: %s\n", sqlite4_errmsg(a_pDB));

      sqlite4_finalize(pStmtInsert);
      sqlite4_close(a_pDB, 0);

      exit(-1);
   }

   // reset
   sqlite4_reset(pStmtInsert);

   // clear bindings
   sqlite4_clear_bindings(pStmtInsert);
}

void select_data(sqlite4 * a_pDB, sqlite4_stmt ** ppStmtSelect)
{
   char * sSQL = "select c_int, c_num, c_datetime, c_char, c_varchar from table03";


   int rc = SQLITE4_OK;

   // prepare
   rc = sqlite4_prepare(a_pDB, sSQL, -1, ppStmtSelect, 0);
   if (rc != SQLITE4_OK) {

      printf( "Failed to execute SELECT stmt [prepare]: %s\n", sqlite4_errmsg(a_pDB));

      sqlite4_finalize(*ppStmtSelect);
      sqlite4_close(a_pDB, 0);

      exit(-1);
   }

   // step
   while ((rc = sqlite4_step(*ppStmtSelect)) != SQLITE4_DONE)
   {
      if (rc != SQLITE4_ROW) {

         printf( "Failed to step/execute the SELECT statement: %s\n", sqlite4_errmsg(a_pDB));

         sqlite4_finalize(*ppStmtSelect);
         sqlite4_close(a_pDB, 0);

         exit(-1);
      }
      char cMyBuf[128];
      int nColCount = sqlite4_column_count(*ppStmtSelect);
      for (int i = 0; i< nColCount; ++i)
      {
         const char * cBuf = 0;
         int nByte = 0;
         cBuf = sqlite4_column_text(*ppStmtSelect, i, &nByte);
         //memcpy(cMyBuf, cBuf, nByte);
         //cMyBuf[nByte] = 0;
         //printf("'%s' | ", cMyBuf);
         printf("'%s' | ", cBuf);
      }
      printf("\n");
   }
}

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
   void prepareStmtBeginTxn()
   {
      int rc = SQLITE4_OK;

      rc = sqlite4_prepare(m_pDb, "BEGIN TRANSACTION", -1, &m_pBeginTxn, 0);
      if (rc != SQLITE4_OK)
      {
         printf( "Failed to prepare 'BEGIN TRANSACTION' : %s\n", sqlite4_errmsg(m_pDb));
         sqlite4_finalize(m_pBeginTxn);
         sqlite4_close(m_pDb, 0);
         exit(-1);
      }
   }

   void prepareStmtCommitTxn()
   {
      int rc = SQLITE4_OK;

      rc = sqlite4_prepare(m_pDb, "COMMIT", -1, &m_pCommitTxn, 0);
      if (rc != SQLITE4_OK)
      {
         printf( "Failed to prepare 'COMMIT' : %s\n", sqlite4_errmsg(m_pDb));
         sqlite4_finalize(m_pCommitTxn);
         sqlite4_close(m_pDb, 0);
         exit(-1);
      }
   }

   void prepareStmtRollbackTxn()
   {
      int rc = SQLITE4_OK;

      rc = sqlite4_prepare(m_pDb, "ROLLBACK", -1, &m_pRollbackTxn, 0);
      if (rc != SQLITE4_OK)
      {
         printf( "Failed to prepare 'ROLLBACK' : %s\n", sqlite4_errmsg(m_pDb));
         sqlite4_finalize(m_pRollbackTxn);
         sqlite4_close(m_pDb, 0);
         exit(-1);
      }
   }

   void prepareStmtInsertData()
   {
      char * sSQL = "insert into table06 (c_int, c_num, c_datetime, c_char, c_varchar) values (:p_int, :p_num, :p_datetime, :p_char, :p_varchar)";

      int rc = SQLITE4_OK;

      rc = sqlite4_prepare(m_pDb, sSQL, -1, &m_pInsertData, 0);
      if (rc != SQLITE4_OK)
      {
         printf( "Failed to prepare 'INSERT' : %s\n", sqlite4_errmsg(m_pDb));
         sqlite4_finalize(m_pInsertData);
         sqlite4_close(m_pDb, 0);
         exit(-1);
      }
   }

   void finalizeStmtBeginTxn()
   {
      sqlite4_finalize(m_pBeginTxn);
      m_pBeginTxn = NULL;
   }

   void finalizeStmtCommitTxn()
   {
      sqlite4_finalize(m_pCommitTxn);
      m_pCommitTxn = NULL;
   }

   void finalizeStmtRollbackTxn()
   {
      sqlite4_finalize(m_pRollbackTxn);
      m_pRollbackTxn = NULL;
   }

   void finalizeStmtInsertData()
   {
      sqlite4_finalize(m_pInsertData);
      m_pInsertData = NULL;
   }

   void initialize()
   {
      //printf("Thread '%s' : initialize()\n", thread_name);

      // open db / session
      int rc = sqlite4_open(m_pEnv, m_zName, &m_pDb);

      if (rc != SQLITE4_OK) {

         printf( "Cannot open database: %s\n", sqlite4_errmsg(m_pDb));
         sqlite4_close(m_pDb, 0);

         exit(-1);
      }

      // prepare statements
      prepareStmtBeginTxn();
      prepareStmtCommitTxn();
      prepareStmtRollbackTxn();
      prepareStmtInsertData();
   }

   void finalize()
   {
      //printf("Thread '%s' : finalize()\n", thread_name);

      finalizeStmtBeginTxn();
      finalizeStmtCommitTxn();
      finalizeStmtRollbackTxn();
      finalizeStmtInsertData();

      sqlite4_close(m_pDb, 0);
   }
   // 
   void beginTxn()
   {
      //printf("Thread '%s' : beginTxn()\n", thread_name);

      // step
      int rc = sqlite4_step(m_pBeginTxn);
      if (rc != SQLITE4_DONE) {

         printf( "Failed to step/execute the 'BEGIN TRANSACTION' statement: %s\n", sqlite4_errmsg(m_pDb));

         sqlite4_finalize(m_pBeginTxn);
         sqlite4_close(m_pDb, 0);

         exit(-1);
      }

      // reset
      sqlite4_reset(m_pBeginTxn);
   }

   void commitTxn()
   {
      //printf("Thread '%s' : commitTxn()\n", thread_name);

      // step
      int rc = sqlite4_step(m_pCommitTxn);
      if (rc != SQLITE4_DONE) {

         printf( "Failed to step/execute the 'COMMIT' statement: %s\n", sqlite4_errmsg(m_pDb));

         sqlite4_finalize(m_pCommitTxn);
         sqlite4_close(m_pDb, 0);

         exit(-1);
      }

      // reset
      sqlite4_reset(m_pCommitTxn);
   }

   void rollbackTxn()
   {
      //printf("Thread '%s' : rollbackTxn()\n", thread_name);

      // step
      int rc = sqlite4_step(m_pRollbackTxn);
      if (rc != SQLITE4_DONE) {

         printf( "Failed to step/execute the 'COMMIT' statement: %s\n", sqlite4_errmsg(m_pDb));

         sqlite4_finalize(m_pRollbackTxn);
         sqlite4_close(m_pDb, 0);

         exit(-1);
      }

      // reset
      sqlite4_reset(m_pRollbackTxn);
   }
   // 
   void insertData()
   {
      printf("Thread '%s' : insertData()\n", thread_name);

      bind_params_and_execute_insert(m_pDb, m_pInsertData, "123", "123.456", "2018-11-13 08:52:56.803", "qazwsx", "edcrfv");
   }

   void insertData(int a_nPK)
   {
      //printf("Thread '%s' : insertData(), a_nPK=%d\n", thread_name, a_nPK);
      
      char cBuf[64];
      sprintf(cBuf, "%d", a_nPK);

      bind_params_and_execute_insert(m_pDb, m_pInsertData, cBuf, "123.456", "2018-11-13 08:52:56.803", "qazwsx", "edcrfv");
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
   sqlite4 * m_pDb = NULL;

   sqlite4_stmt * m_pBeginTxn = NULL;
   sqlite4_stmt * m_pCommitTxn = NULL;
   sqlite4_stmt * m_pRollbackTxn = NULL;

   sqlite4_stmt * m_pInsertData = NULL;

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
   char * zName = "file:perftest_kvwtmem.db?kv=kvwtmem";
   sqlite4 * pDb = NULL;
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
         printf("Usage:\nperftest_kvwtmem numrows_total numrows_per_txn numthreads lazy|eager\n");
         return -1;
      }
   }
   else
   {
      printf("Usage:\nperftest_kvwtmem numrows_total numrows_per_txn numthreads lazy|eager\n");
      return -1;
   }

   printf("perftest_kvwtmem   numrows_total=%d numrows_per_txn=%d numthreads=%d lazy_init=%s \n", 
      numrows_total, numrows_per_txn, numthreads, lazy_init?"true":"false");

   // initialize db / open session etc. -- begin
   rc = sqlite4_load_kvstore_plugin(0, "kvwtmem.dll", "kvwtmem");
   if (rc != SQLITE4_OK)
   {
      printf("Failed sqlite4_load_kvstore_plugin(...) for kvbdb.dll\n");
      exit(-1);
   }

   pEnv = sqlite4_env_default();
   if (pEnv == NULL) 
   {
      printf( "Cannot acquire default environment\n");
      return -1;
   }

   rc = sqlite4_open(pEnv, zName, &pDb);
   if (rc != SQLITE4_OK) 
   {
      printf( "Cannot open database: %s\n", sqlite4_errmsg(pDb));
      sqlite4_close(pDb, 0);

      return -1;
   }
   // initialize db / open session etc. -- end

   // create table
   create_table(pDb);

   // view count rows in table
   count_count_rows_in_table(pDb);

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
   

   // view count rows in table
   //count_count_rows_in_table(pDb);

   // drop table
   //drop_table(pDb);

   // close db
   sqlite4_close(pDb, 0);

   printf("\n\n#Rows: %d [-]\n", numrows_total);

   total_t = ((double)(end_t - start_t)) / CLOCKS_PER_SEC;
   printf("Time: %f [s]\n", total_t);

   double speed = ((double)(numrows_total*1.0)) / total_t;
   printf("Speed: %f [rows/s]\n", speed);

   rpmalloc_finalize();

   return 0;
}
   
   
