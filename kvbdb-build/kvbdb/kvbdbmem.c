#include "kvbdb_common.h"



int kVStoreOpen(
   sqlite4_env *pEnv,              /* Runtime environment */
   KVStore **ppKVStore,            /* OUT: Write the new KVStore here */
   const char *zName,              /* Name of BerkeleyDB storage unit */
   unsigned openFlags              /* Flags */
) {
   //printf("-----> KVBdbMem::kVStoreOpen(), ZName=%s, pEnv=%p\n", zName, pEnv);

   DB_ENV * envp = NULL;
   u_int32_t env_flags = 0;

   DB     * dbp = NULL;
   u_int32_t extra_flags = 0;
   u_int32_t open_flags = 0;

   int ret;

   init_global_bdb_dict();

   bdb_dict_node_t * pDictNode_zName = global_acquire_locked_dict_node((char*)zName);
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

   // set initial buffers size(s) for Cursor's Key and Data Buffer(s) -- begin
   pNew->nInitialCursorKeyBufferCapacity = nGlobalDefaultInitialCursorKeyBufferCapacity;
   pNew->nInitialCursorDataBufferCapacity = nGlobalDefaultInitialCursorDataBufferCapacity;
   // set initial buffers size(s) for Cursor's Key and Data Buffer(s) -- end

   *ppKVStore = (KVStore*)pNew;

   //printf("'%s' : new KVBdb=%p\n", zName, pNew);

   unlock_locked_dict_node(pDictNode_zName);

   return SQLITE4_OK;
}