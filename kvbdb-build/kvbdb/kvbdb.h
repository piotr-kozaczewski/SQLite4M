#ifndef _SQLITE4_KVBDB_H_
#define _SQLITE4_KVBDB_H_

#include "common.h"

#include "sqlite4.h"
#include <db.h>
#include "sqliteInt.h"
#include <stdio.h>



#ifdef __cplusplus
extern "C" {
#endif

   // ---------------- API functions -- begin --------------------------

   kvbdb_export int kVStoreOpen(
      sqlite4_env *pEnv,              /* Runtime environment */
      KVStore **ppKVStore,            /* OUT: Write the new KVStore here */
      const char *zName,              /* Name of BerkeleyDB storage unit */
      unsigned openFlags              /* Flags */
   );

   kvbdb_export int kvbdbReplace(
      KVStore *pKVStore,
      const KVByteArray *aKey, KVSize nKey,
      const KVByteArray *aData, KVSize nData);

   kvbdb_export int kvbdbOpenCursor(KVStore *pKVStore, KVCursor **ppKVCursor);

   kvbdb_export int kvbdbSeek(
      KVCursor *pKVCursor,
      const KVByteArray *aKey,
      KVSize nKey,
      int direction);

   kvbdb_export int kvbdbNextEntry(KVCursor *pKVCursor);

   kvbdb_export int kvbdbPrevEntry(KVCursor *pKVCursor);

   kvbdb_export int kvbdbDelete(KVCursor *pKVCursor);

   kvbdb_export int kvbdbKey(
      KVCursor *pKVCursor,         /* The cursor whose key is desired */
      const KVByteArray **paKey,   /* Make this point to the key */
      KVSize *pN                   /* Make this point to the size of the key */
   );

   kvbdb_export int kvbdbData(
      KVCursor *pKVCursor,         /* The cursor from which to take the data */
      KVSize ofst,                 /* Offset into the data to begin reading */
      KVSize n,                    /* Number of bytes requested */
      const KVByteArray **paData,  /* Pointer to the data written here */
      KVSize *pNData               /* Number of bytes delivered */
   );

   kvbdb_export int kvbdbReset(KVCursor *pKVCursor);

   kvbdb_export int kvbdbCloseCursor(KVCursor *pKVCursor);

   kvbdb_export int kvbdbBegin(KVStore *pKVStore, int iLevel);

   kvbdb_export int kvbdbCommitPhaseOne(KVStore *pKVStore, int iLevel);

   kvbdb_export int kvbdbCommitPhaseOneXID(KVStore *pKVStore, int iLevel, void * xid);

   kvbdb_export int kvbdbCommitPhaseTwo(KVStore *pKVStore, int iLevel);

   kvbdb_export int kvbdbRollback(KVStore *pKVStore, int iLevel);

   kvbdb_export int kvbdbRevert(KVStore *pKVStore, int iLevel);

   kvbdb_export int kvbdbClose(KVStore *pKVStore);

   kvbdb_export int kvbdbControl(KVStore *pKVStore, int op, void *pArg);

   kvbdb_export int kvbdbGetMeta(KVStore *pKVStore, unsigned int *piVal);

   kvbdb_export int kvbdbPutMeta(KVStore *pKVStore, unsigned int iVal);

   // ---------------- API functions -- end   --------------------------
#ifdef __cplusplus
}
#endif
#endif /* end of #ifndef _SQLITE4_KVBDB_H_ */