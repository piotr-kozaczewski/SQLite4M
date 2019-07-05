#ifndef _SQLITE4_KVWT_H_
#define _SQLITE4_KVWT_H_

#include "common.h"

#include "sqlite4.h"

#include <stdio.h>

#ifdef WIN32
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

//kvwt_export int KVStoreOpen(
//   KVEnv *pEnv,               /* IN  : The environment to use */
//   sqlite4_kvstore **ppKVStore,       /* OUT : New KV store returned here */
//   const char *zFilename      /* IN  : Name of database file to open */
//   );

 kvwt_export   int kVStoreOpen(
      sqlite4_env *pEnv,              /* Runtime environment */
      sqlite4_kvstore **ppKVStore,            /* OUT: Write the new sqlite4_kvstore here */
      const char *zName,              /* Name of BerkeleyDB storage unit */
      unsigned openFlags              /* Flags */
      );

kvwt_export int kvwtReplace(
   sqlite4_kvstore*,
   const unsigned char *pKey, sqlite4_kvsize nKey,
   const unsigned char *pData, sqlite4_kvsize nData);
kvwt_export int kvwtOpenCursor(sqlite4_kvstore*, sqlite4_kvcursor**);
kvwt_export int kvwtSeek(sqlite4_kvcursor*,
   const unsigned char *pKey, sqlite4_kvsize nKey, int dir);
kvwt_export int kvwtNext(sqlite4_kvcursor*);
kvwt_export int kvwtPrev(sqlite4_kvcursor*);
kvwt_export int kvwtDelete(sqlite4_kvcursor*);
kvwt_export int kvwtKey(sqlite4_kvcursor*,
   const unsigned char **ppKey, sqlite4_kvsize *pnKey);
kvwt_export int kvwtData(sqlite4_kvcursor*, sqlite4_kvsize ofst, sqlite4_kvsize n,
   const unsigned char **ppData, sqlite4_kvsize *pnData);
kvwt_export int kvwtReset(sqlite4_kvcursor*);
kvwt_export int kvwtCloseCursor(sqlite4_kvcursor*);
kvwt_export int kvwtBegin(sqlite4_kvstore*, int);
kvwt_export int kvwtCommitPhaseOne(sqlite4_kvstore*, int);
kvwt_export int kvwtCommitPhaseOneXID(sqlite4_kvstore*, int, void*);
kvwt_export int kvwtCommitPhaseTwo(sqlite4_kvstore*, int);
kvwt_export int kvwtRollback(sqlite4_kvstore*, int);
kvwt_export int kvwtRevert(sqlite4_kvstore*, int);
kvwt_export int kvwtClose(sqlite4_kvstore*);
kvwt_export int kvwtControl(sqlite4_kvstore*, int, void*);
kvwt_export int kvwtGetMeta(sqlite4_kvstore*, unsigned int *);
kvwt_export int kvwtPutMeta(sqlite4_kvstore*, unsigned int);
kvwt_export int kvwtGetMethod(sqlite4_kvstore*, const char *, void **ppArg,
   void(**pxFunc)(sqlite4_context *, int, sqlite4_value **),
   void(**pxDestroy)(void *));

#ifdef __cplusplus
}
#endif

#endif /* end of #ifndef _SQLITE4_KVWT_H_ */