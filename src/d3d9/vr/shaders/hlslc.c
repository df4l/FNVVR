// Small Windows program for compile.sh: compiles a shader with d3dcompiler_47.
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
typedef struct Blob { const struct BlobVtbl* v; } Blob;
struct BlobVtbl { void* q; void* a; ULONG (__stdcall* rel)(Blob*); void* (__stdcall* ptr)(Blob*); SIZE_T (__stdcall* size)(Blob*); };
typedef HRESULT (WINAPI *CompileFn)(const void*, SIZE_T, const char*, const void*, void*, const char*, const char*, UINT, UINT, Blob**, Blob**);
int main(int argc, char** argv) {
  FILE* f = fopen(argv[1], "rb"); fseek(f,0,SEEK_END); long n=ftell(f); rewind(f); char* s=malloc(n+1); fread(s,1,n,f); s[n]=0; fclose(f);
  HMODULE m = LoadLibraryA("d3dcompiler_47.dll"); if(!m){printf("no dll\n");return 2;}
  CompileFn c=(CompileFn)GetProcAddress(m,"D3DCompile"); Blob* code=0,*err=0;
  HRESULT hr=c(s,n,argv[1],0,0,argv[2],argv[3],0,0,&code,&err);
  if(err) printf("%s\n",(char*)err->v->ptr(err));
  printf("hr %08lx\n",(unsigned long)hr); if(FAILED(hr)) return 1;
  f=fopen(argv[4],"wb"); fwrite(code->v->ptr(code),1,code->v->size(code),f); fclose(f);
  printf("%lu bytes\n",(unsigned long)code->v->size(code)); return 0; }
