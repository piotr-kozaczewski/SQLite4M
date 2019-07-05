#ifndef _KVWT_COMMON_H_
#define _KVWT_COMMON_H_

#if defined(KVWT_CREATE_DLL)
#  define kvwt_export __declspec(dllexport)
#  define kvwt_export_stl(key,arg) key __declspec(dllexport) arg
#  define kvwt_extern
#elif defined(DECLSPEC_DLL_HORROR)
#  define kvwt_export __declspec(dllimport)
#  define kvwt_export_stl(key,arg) key __declspec(dllimport) arg
#  define kvwt_extern extern
#else
#  define kvwt_export
#  define kvwt_export_stl(key,arg)
#  define kvwt_extern
#endif

#endif // _KVWT_COMMON_H_