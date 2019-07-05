#ifndef _KVBDB_COMMON_H_
#define _KVBDB_COMMON_H_

#if defined(KVBDB_CREATE_DLL)
#  define kvbdb_export __declspec(dllexport)
#  define kvbdb_export_stl(key,arg) key __declspec(dllexport) arg
#  define kvbdb_extern
#elif defined(DECLSPEC_DLL_HORROR)
#  define kvbdb_export __declspec(dllimport)
#  define kvbdb_export_stl(key,arg) key __declspec(dllimport) arg
#  define kvbdb_extern extern
#else
#  define kvbdb_export
#  define kvbdb_export_stl(key,arg)
#  define kvbdb_extern
#endif

#endif // _KVBDB_COMMON_H_