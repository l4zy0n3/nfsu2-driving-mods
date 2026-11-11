// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#include <math.h>
#include <string.h>

struct panel { float x,y,w,h,u0,u1; };
static int panel_layout(int width,int height,int tw,int th,int flip,struct panel p[2])
{
    if (width<320 || height<200 || tw<2 || th<1) return 0;
    /* U2 stores its panoramic rear-view image in a square render texture. */
    float aspect=2.5f,w=width*0.16f,h=w/aspect;
    if (h>height*0.18f) {h=height*0.18f;w=h*aspect;}
    p[0]=(struct panel){width*0.02f,height*0.35f,w,h,flip?1.0f:0.0f,0.5f};
    p[1]=(struct panel){width*0.98f-w,height*0.35f,w,h,0.5f,flip?0.0f:1.0f};
    return 1;
}
#ifdef SELF_TEST
#include <assert.h>
int main(void)
{
    struct panel p[2];
    assert(!panel_layout(0,0,512,128,1,p));
    for (int s=1;s<=3;s++) for (int flip=0;flip<2;flip++) {
        assert(panel_layout(1280*s,720*s,512,128,flip,p));
        assert(p[0].x>=0 && p[0].x+p[0].w<p[1].x);
        assert(p[1].x+p[1].w<=1280*s && p[1].y+p[1].h<720*s);
        assert(p[0].u1==0.5f && p[1].u0==0.5f);
        assert(fabsf(p[0].w/p[0].h-2.5f)<0.0001f);
        assert((flip && p[0].u0==1 && p[1].u1==0) ||
               (!flip && p[0].u0==0 && p[1].u1==1));
    }
    return 0;
}
#else
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9core.h>
#include <stdint.h>
#include <stdio.h>

typedef HRESULT (WINAPI *EndSceneFn)(IDirect3DDevice9 *);
typedef HRESULT (WINAPI *ResetFn)(IDirect3DDevice9 *,D3DPRESENT_PARAMETERS *);
static EndSceneFn old_end;
static ResetFn old_reset;
static IDirect3DStateBlock9 *state;
static ID3DXFont *font;
typedef const char *(__cdecl *StatusFn)(void);
static StatusFn realism_status;
static int flip=1;
static char log_path[MAX_PATH];
static DWORD last_log;
static unsigned frames;

static void log_status(const char *message)
{
    FILE *f=fopen(log_path,"a"); if (f) {fprintf(f,"%s\n",message);fclose(f);}
}
static int readable(const void *p,size_t size)
{
    MEMORY_BASIC_INFORMATION m;
    return p && VirtualQuery(p,&m,sizeof(m)) && m.State==MEM_COMMIT &&
        !(m.Protect&(PAGE_NOACCESS|PAGE_GUARD)) &&
        (uintptr_t)p+size<=(uintptr_t)m.BaseAddress+m.RegionSize;
}
struct vertex {float x,y,z,rhw;DWORD color;float u,v;};
static HRESULT quad(IDirect3DDevice9 *d,float x,float y,float w,float h,float u0,float u1,DWORD color)
{
    struct vertex v[4]={{x-.5f,y-.5f,0,1,color,u0,0},{x+w-.5f,y-.5f,0,1,color,u1,0},
        {x-.5f,y+h-.5f,0,1,color,u0,1},{x+w-.5f,y+h-.5f,0,1,color,u1,1}};
    return IDirect3DDevice9_DrawPrimitiveUP(d,D3DPT_TRIANGLESTRIP,2,v,sizeof(v[0]));
}
static void draw_panels(IDirect3DDevice9 *d)
{
    unsigned char *view=(unsigned char *)(0x832de0+3*0x70);
    if (!view[8] || !*(unsigned char *)0x83aa18) return;
    void *target=*(void **)(view+0x58);
    if (!readable(target,0x20)) return;
    IDirect3DSurface9 *mirror=*(IDirect3DSurface9 **)((char *)target+8);
    if (!readable(mirror,4)) return;
    IDirect3DSurface9 *back=NULL,*current=NULL;
    IDirect3DTexture9 *texture=NULL;
    if (FAILED(IDirect3DDevice9_GetBackBuffer(d,0,0,D3DBACKBUFFER_TYPE_MONO,&back))) return;
    if (FAILED(IDirect3DDevice9_GetRenderTarget(d,0,&current))) goto done;
    if (back!=current || mirror==back) goto done;
    D3DSURFACE_DESC bd,md;
    if (FAILED(IDirect3DSurface9_GetDesc(back,&bd)) ||
        FAILED(IDirect3DSurface9_GetDesc(mirror,&md)) ||
        FAILED(IDirect3DSurface9_GetContainer(mirror,&IID_IDirect3DTexture9,(void **)&texture))) goto done;
    struct panel p[2];
    if (!panel_layout(bd.Width,bd.Height,md.Width,md.Height,flip,p)) goto done;
    if (!state && FAILED(IDirect3DDevice9_CreateStateBlock(d,D3DSBT_ALL,&state))) goto done;
    if (FAILED(IDirect3DStateBlock9_Capture(state))) goto done;
    D3DVIEWPORT9 vp={0,0,bd.Width,bd.Height,0,1};
    IDirect3DDevice9_SetViewport(d,&vp);
    IDirect3DDevice9_SetVertexShader(d,NULL);IDirect3DDevice9_SetPixelShader(d,NULL);
    IDirect3DDevice9_SetFVF(d,D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);
    IDirect3DDevice9_SetRenderState(d,D3DRS_ZENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(d,D3DRS_ZWRITEENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(d,D3DRS_ALPHABLENDENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(d,D3DRS_ALPHATESTENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(d,D3DRS_STENCILENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(d,D3DRS_SCISSORTESTENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(d,D3DRS_FOGENABLE,FALSE);
    IDirect3DDevice9_SetRenderState(d,D3DRS_CULLMODE,D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(d,D3DRS_FILLMODE,D3DFILL_SOLID);
    IDirect3DDevice9_SetRenderState(d,D3DRS_COLORWRITEENABLE,15);
    IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
    IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_TEXCOORDINDEX,0);
    IDirect3DDevice9_SetTextureStageState(d,1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    IDirect3DDevice9_SetTexture(d,0,NULL);
    for(int i=0;i<2;i++) quad(d,p[i].x-3,p[i].y-3,p[i].w+6,p[i].h+6,0,1,0xff181818);
    IDirect3DDevice9_SetTexture(d,0,(IDirect3DBaseTexture9 *)texture);
    IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
    IDirect3DDevice9_SetSamplerState(d,0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
    IDirect3DDevice9_SetSamplerState(d,0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    IDirect3DDevice9_SetSamplerState(d,0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(d,0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    HRESULT a=quad(d,p[0].x,p[0].y,p[0].w,p[0].h,p[0].u0,p[0].u1,0xffffffff);
    HRESULT b=quad(d,p[1].x,p[1].y,p[1].w,p[1].h,p[1].u0,p[1].u1,0xffffffff);
    IDirect3DStateBlock9_Apply(state);
    if (SUCCEEDED(a)&&SUCCEEDED(b)) frames++;
    if (GetTickCount()-last_log>5000) {
        char msg[180];snprintf(msg,sizeof(msg),"panels_drawn=%u rearview_texture=%ux%u backbuffer=%ux%u flip=%d",frames,md.Width,md.Height,bd.Width,bd.Height,flip);
        log_status(msg);last_log=GetTickCount();
    }
done:
    if(texture) IDirect3DTexture9_Release(texture);
    if(current) IDirect3DSurface9_Release(current);
    if(back) IDirect3DSurface9_Release(back);
}
static void draw_starter_status(IDirect3DDevice9 *d)
{
    if(!realism_status) {
        HMODULE module=GetModuleHandleA("NFSU2.AnalogClutch.asi");
        if(module)realism_status=(StatusFn)(void *)GetProcAddress(module,"NFSU2RealismStatus");
    }
    const char *text=realism_status?realism_status():NULL;
    if(!text)return;
    IDirect3DSurface9 *back=NULL,*current=NULL;
    if(FAILED(IDirect3DDevice9_GetBackBuffer(d,0,0,D3DBACKBUFFER_TYPE_MONO,&back)))return;
    if(FAILED(IDirect3DDevice9_GetRenderTarget(d,0,&current)))goto done;
    D3DSURFACE_DESC bd;
    if(back!=current||FAILED(IDirect3DSurface9_GetDesc(back,&bd)))goto done;
    if(!state&&FAILED(IDirect3DDevice9_CreateStateBlock(d,D3DSBT_ALL,&state)))goto done;
    if(FAILED(IDirect3DStateBlock9_Capture(state)))goto done;
    if(!font) {
        int size=bd.Height/40;if(size<16)size=16;
        if(FAILED(D3DXCreateFontA(d,size,0,FW_BOLD,1,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,"Arial",&font))) {
            log_status("starter_status_font_failed=1");
            IDirect3DStateBlock9_Apply(state);goto done;
        }
    }
    D3DVIEWPORT9 vp={0,0,bd.Width,bd.Height,0,1};
    IDirect3DDevice9_SetViewport(d,&vp);
    IDirect3DDevice9_SetRenderState(d,D3DRS_SCISSORTESTENABLE,FALSE);
    RECT shadow={2,(LONG)(bd.Height*.88f)+2,(LONG)bd.Width+2,(LONG)bd.Height};
    RECT position={0,(LONG)(bd.Height*.88f),(LONG)bd.Width,(LONG)bd.Height};
    ID3DXFont_DrawTextA(font,NULL,text,-1,&shadow,DT_CENTER|DT_NOCLIP,0xff000000);
    int drawn=ID3DXFont_DrawTextA(font,NULL,text,-1,&position,DT_CENTER|DT_NOCLIP,
        strstr(text,"ENGINE RUNNING")?0xffaaffaa:0xffffcc66);
    IDirect3DStateBlock9_Apply(state);
    static const char *last_status;
    if(drawn&&text!=last_status){log_status(text);last_status=text;}
done:
    if(current)IDirect3DSurface9_Release(current);
    if(back)IDirect3DSurface9_Release(back);
}
static HRESULT WINAPI end_scene(IDirect3DDevice9 *d)
{draw_panels(d);draw_starter_status(d);return old_end(d);}
static HRESULT WINAPI reset(IDirect3DDevice9 *d,D3DPRESENT_PARAMETERS *p)
{
    if(font){ID3DXFont_Release(font);font=NULL;}
    if(state){IDirect3DStateBlock9_Release(state);state=NULL;}
    return old_reset(d,p);
}
static DWORD WINAPI install(void *unused)
{
    (void)unused;
    for(int i=0;i<600;i++) {
        IDirect3DDevice9 *d=*(IDirect3DDevice9 **)0x870974;
        if (readable(d,4) && readable(d->lpVtbl,sizeof(*d->lpVtbl))) {
            void **v=(void **)d->lpVtbl;DWORD protect,ignored;
            if(VirtualProtect(v+16,27*sizeof(void *),PAGE_READWRITE,&protect)) {
                old_reset=(ResetFn)v[16];old_end=(EndSceneFn)v[42];
                v[16]=(void *)reset;v[42]=(void *)end_scene;
                VirtualProtect(v+16,27*sizeof(void *),protect,&ignored);
                log_status("device_hook_installed=1 source=rearview_left_right_crops");return 0;
            }
        }
        Sleep(100);
    }
    log_status("device_hook_installed=0");return 0;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID unused)
{
    (void)unused;if(reason!=DLL_PROCESS_ATTACH)return TRUE;
    DisableThreadLibraryCalls(module);
    char path[MAX_PATH];DWORD n=GetModuleFileNameA(NULL,path,sizeof(path));
    char *slash=n&&n<sizeof(path)?strrchr(path,'\\'):NULL;
    if(!slash || slash-path+50>=MAX_PATH)return TRUE;
    strcpy(slash,"\\scripts\\NFSU2.MirrorPanels.log");strcpy(log_path,path);
    FILE *f=fopen(log_path,"w");if(f)fclose(f);
    unsigned char *base=(unsigned char *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt=(IMAGE_NT_HEADERS *)(base+((IMAGE_DOS_HEADER *)base)->e_lfanew);
    const unsigned char expected[]={0x74,0x05,0x83,0xf8,0x01,0x75,0x06};
    if ((uintptr_t)base!=0x400000 || nt->OptionalHeader.AddressOfEntryPoint!=0x35b8d1 ||
        memcmp((void *)0x6000f0,expected,sizeof(expected))) {log_status("build_guard_failed=1");return TRUE;}
    DWORD old,ignored;
    if(!VirtualProtect((void *)0x6000f0,1,PAGE_EXECUTE_READWRITE,&old))return TRUE;
    *(unsigned char *)0x6000f0=0xeb;
    FlushInstructionCache(GetCurrentProcess(),(void *)0x6000f0,1);
    VirtualProtect((void *)0x6000f0,1,old,&ignored);
    *(unsigned char *)0x83aa18=1;
    log_status("mirror_camera_gate_patched=1");
    HANDLE t=CreateThread(NULL,0,install,NULL,0,NULL);if(t)CloseHandle(t);
    return TRUE;
}
#endif
