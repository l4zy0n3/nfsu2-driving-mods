// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#ifndef U2_WHEELFIX_NATIVE_H
#define U2_WHEELFIX_NATIVE_H
#include <wincrypt.h>
#include "steering-guard-core.h"
static int steering_checked,steering_supported;
static int steering_retryable;
static char steering_expected_path[MAX_PATH];
/* A basename may identify Wine's system DirectInput instead of the game's proxy. */
static unsigned char *steering_find_module(void)
{
 char actual[MAX_PATH]={0},basename_path[MAX_PATH]={0};
 if(!steering_expected_path[0]){
  DWORD n=GetModuleFileNameA(NULL,steering_expected_path,sizeof(steering_expected_path));
  char *slash=n&&n<sizeof(steering_expected_path)?strrchr(steering_expected_path,'\\'):NULL;
  if(!slash||slash-steering_expected_path+13>=MAX_PATH){steering_expected_path[0]=0;return NULL;}
  strcpy(slash,"\\dinput8.dll");
 }
 HMODULE basename=GetModuleHandleA("dinput8.dll"),selected=GetModuleHandleA(steering_expected_path);
 if(basename)GetModuleFileNameA(basename,basename_path,sizeof(basename_path));
 if(selected){
  DWORD n=GetModuleFileNameA(selected,actual,sizeof(actual));
  if(!n||n>=sizeof(actual)||_stricmp(actual,steering_expected_path))selected=NULL;
 }
 if(!selected){
  HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
  if(snapshot!=INVALID_HANDLE_VALUE){
   MODULEENTRY32 entry={0};entry.dwSize=sizeof(entry);
   if(Module32First(snapshot,&entry))do {
    DWORD n=GetModuleFileNameA(entry.hModule,actual,sizeof(actual));
    if(n&&n<sizeof(actual)&&!_stricmp(actual,steering_expected_path)){selected=entry.hModule;break;}
   }while(Module32Next(snapshot,&entry));
   CloseHandle(snapshot);
  }
 }
 if(selected){
  GetModuleFileNameA(selected,actual,sizeof(actual));
  char message[896];snprintf(message,sizeof(message),"wheelfix_module_resolved expected=%s selected=%p path=%s basename=%p basename_path=%s",
   steering_expected_path,(void *)selected,actual,(void *)basename,basename_path);log_status(message);
 }
 return (unsigned char *)selected;
}
static int steering_identity(void)
{
 static const BYTE expected[32]={0x9f,0x48,0xdc,0x0a,0x49,0x8f,0x9c,0x3e,0x17,0xbf,0x73,0x45,0x19,0xd8,0x3a,0xbd,0x6f,0xbb,0x93,0xac,0xdd,0xac,0x89,0x95,0x96,0xab,0x4a,0x1e,0x75,0x14,0x82,0xde};
 if(steering_checked)return steering_supported;
 char path[MAX_PATH]={0};HCRYPTPROV provider=0;HCRYPTHASH hash=0;HANDLE file=INVALID_HANDLE_VALUE;int ok=0;
 DWORD error=0;MEMORY_BASIC_INFORMATION region={0};SIZE_T queried=0;
 steering_retryable=0;
 const char *stage="module_path";
 DWORD n=GetModuleFileNameA((HMODULE)wheel,path,sizeof(path));
 if(!n||n>=sizeof(path)){error=n?ERROR_INSUFFICIENT_BUFFER:GetLastError();steering_retryable=!n;goto done;}
 if(_stricmp(path,steering_expected_path)){error=ERROR_INVALID_DLL;goto done;}
 stage="module_memory";
 if(!wheel||!readable(wheel,0x1000)){steering_retryable=1;goto done;}
 IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER *)wheel;
 stage="dos_header";
 if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0||dos->e_lfanew>0x800){error=ERROR_BAD_EXE_FORMAT;goto done;}
 IMAGE_NT_HEADERS *pe=(IMAGE_NT_HEADERS *)(wheel+dos->e_lfanew);
 stage="pe_identity";
 if(pe->Signature!=IMAGE_NT_SIGNATURE||pe->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||pe->FileHeader.TimeDateStamp!=0x6acd150d||pe->OptionalHeader.SizeOfImage!=0xb0000||pe->OptionalHeader.AddressOfEntryPoint!=0x13b0){error=ERROR_BAD_EXE_FORMAT;goto done;}
 /* Runtime relocation-aware gain operand check, in addition to on-disk SHA. */
 stage="gain_operand";
 if(!readable(wheel+0xa4b7,6)||wheel[0xa4b7]!=0xdc||wheel[0xa4b8]!=0x0d||*(uint32_t *)(wheel+0xa4b9)!=(uintptr_t)(wheel+0x431c8)){error=ERROR_INVALID_DATA;goto done;}
 stage="file_identity";
 file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
 if(file==INVALID_HANDLE_VALUE){error=GetLastError();goto done;}
 if(GetFileSize(file,NULL)!=1037198){error=ERROR_INVALID_DATA;goto done;}
 stage="crypto_provider";
 if(!CryptAcquireContextA(&provider,NULL,NULL,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)){error=GetLastError();goto done;}
 stage="crypto_hash";if(!CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)){error=GetLastError();goto done;}
 BYTE buffer[16384],digest[32];DWORD count;
 stage="hash_file";for(;;){if(!ReadFile(file,buffer,sizeof(buffer),&count,NULL)){error=GetLastError();goto done;}if(!count)break;if(!CryptHashData(hash,buffer,count,0)){error=GetLastError();goto done;}}
 stage="sha256";
 count=sizeof(digest);if(!CryptGetHashParam(hash,HP_HASHVAL,digest,&count,0)){error=GetLastError();goto done;}
 if(count!=32||memcmp(digest,expected,32)){error=ERROR_INVALID_DATA;goto done;}
 stage="gain_region";
 queried=VirtualQuery(wheel+0x431c8,&region,sizeof(region));
 if(!queried){error=GetLastError();steering_retryable=1;goto done;}
 if(region.AllocationBase!=wheel||region.Type!=MEM_IMAGE){error=ERROR_INVALID_ADDRESS;goto done;}
 if(region.State!=MEM_COMMIT||(region.Protect&(PAGE_GUARD|PAGE_NOACCESS))||
    !((region.Protect&0xff)==PAGE_READWRITE||(region.Protect&0xff)==PAGE_WRITECOPY||
      (region.Protect&0xff)==PAGE_EXECUTE_READWRITE||(region.Protect&0xff)==PAGE_EXECUTE_WRITECOPY)||
    (uintptr_t)(wheel+0x431d0)>(uintptr_t)region.BaseAddress+region.RegionSize){steering_retryable=1;goto done;}
 ok=1;
 done:
 if(!ok){char message[1024];snprintf(message,sizeof(message),"steering_identity_failed stage=%s error=%lu retryable=%d module=%p expected=%s path=%s query_bytes=%lu region_base=%p allocation_base=%p region_bytes=%lu state=0x%lx protect=0x%lx type=0x%lx",
  stage,(unsigned long)error,steering_retryable,(void *)wheel,steering_expected_path,path,(unsigned long)queried,region.BaseAddress,region.AllocationBase,
  (unsigned long)region.RegionSize,(unsigned long)region.State,(unsigned long)region.Protect,(unsigned long)region.Type);log_status(message);}
 if(hash)CryptDestroyHash(hash);
 if(provider)CryptReleaseContext(provider,0);
 if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
 steering_checked=ok||!steering_retryable;steering_supported=ok;
 if(ok)log_status("steering_build_guard=verified sha256=9f48dc0a498f9c3e17bf734519d83abd6fbb93acddac899596ab4a1e751482de");
 else if(!steering_retryable)log_status("clutch_dependency=disabled reason=unsupported_wheelfix");
 return ok;
}
#endif
