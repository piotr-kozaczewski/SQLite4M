/*
** 2007 August 14
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This file contains the C functions that implement mutexes for win32
*/
#include "sqliteInt.h"

/*
** The code in this file is only used if we are compiling multithreaded
** on a win32 system.
*/
#ifdef SQLITE4_MUTEX_W32

//static int winMutexInit(void);
static int winMutexInit(void * pfp); // pfp == pro forma parameter/pointer
//static int winMutexEnd(void);
static int winMutexEnd(void * pfp); // pfp == pro forma parameter/pointer
//static sqlite4_mutex *winMutexAlloc(int iType);
static sqlite4_mutex *winMutexAlloc(void *pMutexEnv, int iType);
static void winMutexFree(sqlite4_mutex *p);
static void winMutexEnter(sqlite4_mutex *p);
static int winMutexTry(sqlite4_mutex *p);
static void winMutexLeave(sqlite4_mutex *p);
static int winMutexHeld(sqlite4_mutex *p);
static int winMutexNotheld(sqlite4_mutex *p);

static const sqlite4_mutex_methods sMutexMethods = {
   winMutexInit,
   winMutexEnd,
   winMutexAlloc,
   winMutexFree,
   winMutexEnter,
   winMutexTry,
   winMutexLeave,
#ifdef SQLITE4_DEBUG
   winMutexHeld,
   winMutexNotheld,
#else
   0,
   0,
#endif
   0 // void *pMutexEnv, see "sqlite.h.in", l. 3437
};

/*
** Each recursive mutex is an instance of the following structure.
*/
typedef struct sqlite4Win32Mutex sqlite4Win32Mutex;

struct sqlite4Win32Mutex {
  sqlite4_mutex base;         /* a base struct member */
  CRITICAL_SECTION mutex;    /* Mutex controlling the lock */
  int id;                    /* Mutex type */
#ifdef SQLITE4_DEBUG
  volatile int nRef;         /* Number of enterances */
  volatile DWORD owner;      /* Thread holding this mutex */
  int trace;                 /* True to trace changes */
#endif
};
#define SQLITE4_W32_MUTEX_INITIALIZER { 0 }
#ifdef SQLITE4_DEBUG
#define SQLITE4_MUTEX_INITIALIZER { {&sMutexMethods}, SQLITE4_W32_MUTEX_INITIALIZER, 0, 0L, (DWORD)0, 0 }
#else
#define SQLITE4_MUTEX_INITIALIZER { {&sMutexMethods}, SQLITE4_W32_MUTEX_INITIALIZER, 0 }
#endif

/*
** Return true (non-zero) if we are running under WinNT, Win2K, WinXP,
** or WinCE.  Return false (zero) for Win95, Win98, or WinME.
**
** Here is an interesting observation:  Win95, Win98, and WinME lack
** the LockFileEx() API.  But we can still statically link against that
** API as long as we don't call it win running Win95/98/ME.  A call to
** this routine is used to determine if the host is Win95/98/ME or
** WinNT/2K/XP so that we will know whether or not we can safely call
** the LockFileEx() API.
**
** mutexIsNT() is only used for the TryEnterCriticalSection() API call,
** which is only available if your application was compiled with 
** _WIN32_WINNT defined to a value >= 0x0400.  Currently, the only
** call to TryEnterCriticalSection() is #ifdef'ed out, so #ifdef 
** this out as well.
*/
#if 0
#if SQLITE4_OS_WINCE
# define mutexIsNT()  (1)
#else
  static int mutexIsNT(void){
    static int osType = 0;
    if( osType==0 ){
      OSVERSIONINFO sInfo;
      sInfo.dwOSVersionInfoSize = sizeof(sInfo);
      GetVersionEx(&sInfo);
      osType = sInfo.dwPlatformId==VER_PLATFORM_WIN32_NT ? 2 : 1;
    }
    return osType==2;
  }
#endif /* SQLITE4_OS_WINCE */
#endif

#ifdef SQLITE4_DEBUG
/*
** The sqlite4_mutex_held() and sqlite4_mutex_notheld() routine are
** intended for use only inside assert() statements.
*/
static int winMutexHeld(sqlite4_mutex *p){
   sqlite4Win32Mutex * pWin32 = (sqlite4Win32Mutex *)p;
  return pWin32->nRef!=0 && pWin32->owner==GetCurrentThreadId();
}
static int winMutexNotheld2(sqlite4_mutex *p, DWORD tid){
   sqlite4Win32Mutex * pWin32 = (sqlite4Win32Mutex *)p;
  return pWin32->nRef==0 || pWin32->owner!=tid;
}
static int winMutexNotheld(sqlite4_mutex *p){
  DWORD tid = GetCurrentThreadId(); 
  return winMutexNotheld2(p, tid);
}
#endif


/*
** Initialize and deinitialize the mutex subsystem.
*/
static sqlite4Win32Mutex winMutex_staticMutexes[6] = {
  SQLITE4_MUTEX_INITIALIZER,
  SQLITE4_MUTEX_INITIALIZER,
  SQLITE4_MUTEX_INITIALIZER,
  SQLITE4_MUTEX_INITIALIZER,
  SQLITE4_MUTEX_INITIALIZER,
  SQLITE4_MUTEX_INITIALIZER
};
static int winMutex_isInit = 0;
/* As winMutexInit() and winMutexEnd() are called as part
** of the sqlite4_initialize and sqlite4_shutdown()
** processing, the "interlocked" magic is probably not
** strictly necessary.
*/
static long winMutex_lock = 0;

//static int winMutexInit(void )
static int winMutexInit(void *pfp)
{ 
  /* The first to increment to 1 does actual initialization */
  if( InterlockedCompareExchange(&winMutex_lock, 1, 0)==0 ){
    int i;
    for(i=0; i<ArraySize(winMutex_staticMutexes); i++){
      InitializeCriticalSection(&winMutex_staticMutexes[i].mutex);
    }
    winMutex_isInit = 1;
  }else{
    /* Someone else is in the process of initing the static mutexes */
    while( !winMutex_isInit ){
      Sleep(1);
    }
  }
  return SQLITE4_OK; 
}

//static int winMutexEnd(void)
static int winMutexEnd(void * pfp)
{ 
  /* The first to decrement to 0 does actual shutdown 
  ** (which should be the last to shutdown.) */
  if( InterlockedCompareExchange(&winMutex_lock, 0, 1)==1 ){
    if( winMutex_isInit==1 ){
      int i;
      for(i=0; i<ArraySize(winMutex_staticMutexes); i++){
        DeleteCriticalSection(&winMutex_staticMutexes[i].mutex);
      }
      winMutex_isInit = 0;
    }
  }
  return SQLITE4_OK; 
}

/*
** The sqlite4_mutex_alloc() routine allocates a new
** mutex and returns a pointer to it.  If it returns NULL
** that means that a mutex could not be allocated.  SQLite
** will unwind its stack and return an error.  The argument
** to sqlite4_mutex_alloc() is one of these integer constants:
**
** <ul>
** <li>  SQLITE4_MUTEX_FAST
** <li>  SQLITE4_MUTEX_RECURSIVE
** <li>  SQLITE4_MUTEX_STATIC_MASTER
** <li>  SQLITE4_MUTEX_STATIC_MEM
** <li>  SQLITE4_MUTEX_STATIC_MEM2
** <li>  SQLITE4_MUTEX_STATIC_PRNG
** <li>  SQLITE4_MUTEX_STATIC_LRU
** <li>  SQLITE4_MUTEX_STATIC_PMEM
** </ul>
**
** The first two constants cause sqlite4_mutex_alloc() to create
** a new mutex.  The new mutex is recursive when SQLITE4_MUTEX_RECURSIVE
** is used but not necessarily so when SQLITE4_MUTEX_FAST is used.
** The mutex implementation does not need to make a distinction
** between SQLITE4_MUTEX_RECURSIVE and SQLITE4_MUTEX_FAST if it does
** not want to.  But SQLite will only request a recursive mutex in
** cases where it really needs one.  If a faster non-recursive mutex
** implementation is available on the host platform, the mutex subsystem
** might return such a mutex in response to SQLITE4_MUTEX_FAST.
**
** The other allowed parameters to sqlite4_mutex_alloc() each return
** a pointer to a static preexisting mutex.  Six static mutexes are
** used by the current version of SQLite.  Future versions of SQLite
** may add additional static mutexes.  Static mutexes are for internal
** use by SQLite only.  Applications that use SQLite mutexes should
** use only the dynamic mutexes returned by SQLITE4_MUTEX_FAST or
** SQLITE4_MUTEX_RECURSIVE.
**
** Note that if one of the dynamic mutex parameters (SQLITE4_MUTEX_FAST
** or SQLITE4_MUTEX_RECURSIVE) is used then sqlite4_mutex_alloc()
** returns a different mutex on every call.  But for the static 
** mutex types, the same mutex is returned on every call that has
** the same type number.
*/
//static sqlite4_mutex *winMutexAlloc(int iType)
static sqlite4_mutex *winMutexAlloc(void *pMutexEnv, int iType)
{
   sqlite4Win32Mutex * p = 0;

  switch( iType ){
    case SQLITE4_MUTEX_FAST:
    case SQLITE4_MUTEX_RECURSIVE: {
      p = sqlite4MallocZero(0, sizeof(*p) );
      if( p ){  
#ifdef SQLITE4_DEBUG
        p->id = iType;
#endif
        p->base.pMutexMethods = &sMutexMethods;
        InitializeCriticalSection(&p->mutex);
      }
      break;
    }
    default: {
      assert( winMutex_isInit==1 );
      assert( iType-2 >= 0 );
      assert( iType-2 < ArraySize(winMutex_staticMutexes) );
      p = &winMutex_staticMutexes[iType-2];
#ifdef SQLITE4_DEBUG
      p->id = iType;
#endif
      break;
    }
  }
  return &(p->base);
}


/*
** This routine deallocates a previously
** allocated mutex.  SQLite is careful to deallocate every
** mutex that it allocates.
*/
static void winMutexFree(sqlite4_mutex *p){
   sqlite4Win32Mutex * pWin32;
  assert( p );
  pWin32 = (sqlite4Win32Mutex *)p;
#ifdef SQLITE4_DEBUG
  assert(pWin32->nRef==0 && pWin32->owner==0 );
#endif
  assert(pWin32->id==SQLITE4_MUTEX_FAST || pWin32->id==SQLITE4_MUTEX_RECURSIVE );
  DeleteCriticalSection(&pWin32->mutex);
  sqlite4_free(0, p);
}

/*
** The sqlite4_mutex_enter() and sqlite4_mutex_try() routines attempt
** to enter a mutex.  If another thread is already within the mutex,
** sqlite4_mutex_enter() will block and sqlite4_mutex_try() will return
** SQLITE4_BUSY.  The sqlite4_mutex_try() interface returns SQLITE4_OK
** upon successful entry.  Mutexes created using SQLITE4_MUTEX_RECURSIVE can
** be entered multiple times by the same thread.  In such cases the,
** mutex must be exited an equal number of times before another thread
** can enter.  If the same thread tries to enter any other kind of mutex
** more than once, the behavior is undefined.
*/
static void winMutexEnter(sqlite4_mutex *p){
   sqlite4Win32Mutex * pWin32 = 0;
   assert(p);
   pWin32 = (sqlite4Win32Mutex *)p;
#ifdef SQLITE4_DEBUG
  DWORD tid = GetCurrentThreadId(); 
  assert(pWin32->id==SQLITE4_MUTEX_RECURSIVE || winMutexNotheld2(p, tid) );
#endif
  EnterCriticalSection(&pWin32->mutex);
#ifdef SQLITE4_DEBUG
  assert(pWin32->nRef>0 || pWin32->owner==0 );
  pWin32->owner = tid;
  pWin32->nRef++;
  if(pWin32->trace ){
    printf("enter mutex %p (%d) with nRef=%d\n", p, pWin32->trace, pWin32->nRef);
  }
#endif
}
static int winMutexTry(sqlite4_mutex *p){
   sqlite4Win32Mutex * pWin32 = 0;
   assert(p);
   pWin32 = (sqlite4Win32Mutex *)p;
#ifndef NDEBUG
  DWORD tid = GetCurrentThreadId(); 
#endif
  int rc = SQLITE4_BUSY;
  assert(pWin32->id==SQLITE4_MUTEX_RECURSIVE || winMutexNotheld2(p, tid) );
  /*
  ** The sqlite4_mutex_try() routine is very rarely used, and when it
  ** is used it is merely an optimization.  So it is OK for it to always
  ** fail.  
  **
  ** The TryEnterCriticalSection() interface is only available on WinNT.
  ** And some windows compilers complain if you try to use it without
  ** first doing some #defines that prevent SQLite from building on Win98.
  ** For that reason, we will omit this optimization for now.  See
  ** ticket #2685.
  */
#if 0
  if( mutexIsNT() && TryEnterCriticalSection(&pWin32->mutex) ){
     pWin32->owner = tid;
     pWin32->nRef++;
     rc = SQLITE4_OK;
  }
#else
  UNUSED_PARAMETER(p);
#endif
#ifdef SQLITE4_DEBUG
  if( rc==SQLITE4_OK && pWin32->trace ){
    printf("try mutex %p (%d) with nRef=%d\n", p, pWin32->trace, pWin32->nRef);
  }
#endif
  return rc;
}

/*
** The sqlite4_mutex_leave() routine exits a mutex that was
** previously entered by the same thread.  The behavior
** is undefined if the mutex is not currently entered or
** is not currently allocated.  SQLite will never do either.
*/
static void winMutexLeave(sqlite4_mutex *p){
   sqlite4Win32Mutex * pWin32 = 0;
   assert(p);
   pWin32 = (sqlite4Win32Mutex *)p;
#ifndef NDEBUG
  DWORD tid = GetCurrentThreadId();
  assert(pWin32->nRef>0 );
  assert(pWin32->owner==tid );
  pWin32->nRef--;
  if(pWin32->nRef==0 ) pWin32->owner = 0;
  assert(pWin32->nRef==0 || pWin32->id==SQLITE4_MUTEX_RECURSIVE );
#endif
  LeaveCriticalSection(&pWin32->mutex);
#ifdef SQLITE4_DEBUG
  if( p->trace ){
    printf("leave mutex %p (%d) with nRef=%d\n", p, pWin32->trace, pWin32->nRef);
  }
#endif
}

sqlite4_mutex_methods const *sqlite4DefaultMutex(void){
  return &sMutexMethods;
}
#endif /* SQLITE4_MUTEX_W32 */
