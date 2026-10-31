/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wasm_host.h"
#include "wasm3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* No WASI/libc/native imports are linked. Beta runtime: trusted developer artifact,
 * not an assertion of hostile-mod sandbox safety. All lazy compilation is metered. */
static struct {
    IM3Environment env; IM3Runtime rt; IM3Module module;
    IM3Function query, req, res, create, drop, reset, dispatch, init;
    unsigned char *bytes; uint32_t reqoff, resoff;
    int ready, failed; char error[256];
} H;
static int fail(const char *why) {
    H.ready=0; H.failed=1;
    snprintf(H.error,sizeof H.error,"%s",why ? why : "wasm host fault"); return 0;
}
const char *iaf_wasm_error(void) { return H.error; }
int iaf_wasm_ready(void) { return H.ready && !H.failed; }
void iaf_wasm_close(void) {
    if(H.rt) m3_FreeRuntime(H.rt);
    else if(H.module) m3_FreeModule(H.module);
    if(H.env) m3_FreeEnvironment(H.env);
    free(H.bytes); memset(&H,0,sizeof H);
}
static int leb(const uint8_t **p,const uint8_t *end,uint32_t *out) {
    uint32_t v=0;
    for(unsigned s=0;s<35;s+=7) {
        if(*p==end) return 0;
        uint8_t b=*(*p)++; if(s==28 && (b&0xf0)) return 0;
        v|=(uint32_t)(b&127)<<s;
        if(!(b&128)) {*out=v;return 1;}
    } return 0;
}
/* Reject capability imports, start functions (before ABI validation), and any memory
 * declaration other than one bounded, nonshared 128KiB wasm32 memory. */
static int preflight(const uint8_t *p,size_t n) {
    const uint8_t magic[8]={0,97,115,109,1,0,0,0};
    if(n<8 || memcmp(p,magic,8)) return 0;
    const uint8_t *end=p+n; p+=8; unsigned mem=0;
    while(p<end) {
        uint8_t id=*p++; uint32_t len,count,flags,min,max;
        if(!leb(&p,end,&len) || len>(size_t)(end-p)) return 0;
        const uint8_t *next=p+len;
        if(id==8) return 0;
        if(id==2 && (!leb(&p,next,&count) || count)) return 0;
        if(id==5) {
            if(mem++ || !leb(&p,next,&count)||count!=1 || !leb(&p,next,&flags)||flags!=1
               ||!leb(&p,next,&min)||!leb(&p,next,&max)||min!=2||max!=2||p!=next) return 0;
        }
        p=next;
    } return mem==1;
}
static int lookup(IM3Function *fn,const char *name,int nargs) {
    M3Result e=m3_FindFunction(fn,H.rt,name); if(e) return fail(e);
    if(m3_GetArgCount(*fn)!=(unsigned)nargs || m3_GetRetCount(*fn)!=(unsigned)(nargs>=0 && strcmp(name,"_initialize")!=0)) return fail("export signature mismatch");
    for(int i=0;i<nargs;i++) if(m3_GetArgType(*fn,i)!=c_m3Type_i32) return fail("non-i32 argument");
    if(strcmp(name,"_initialize") && m3_GetRetType(*fn,0)!=c_m3Type_i32) return fail("non-i32 result");
    return 1;
}
static int call(IM3Function fn,int arg,uint32_t value,uint32_t *result) {
    m3_SetGasLimit(H.rt,1000000.0); /* reset call budget, including initialization */
    M3Result e=arg ? m3_CallV(fn,value) : m3_CallV(fn);
    if(e) return fail(e);
    if(result && (e=m3_GetResultsV(fn,result))) return fail(e);
    return 1;
}
static uint8_t *range(uint32_t off,size_t size) {
    size_t n=0; uint8_t *p=m3_GetMemory(H.module,&n,0);
    if(!p || n!=131072 || off>n || size>n-off) {fail("linear memory range violation");return NULL;}
    return p+off;
}
int iaf_wasm_open(const char *path) {
    if(H.ready||H.failed||H.rt) return 0;
    const uint16_t endian=1;
    if(*(const uint8_t*)&endian!=1) return fail("wire ABI requires little-endian host");
    FILE *f=fopen(path,"rb"); if(!f) return fail("cannot open packaged cc2x_island_airfields_core.wasm");
    if(fseek(f,0,SEEK_END)) {fclose(f);return fail("module seek failed");}
    long size=ftell(f);
    if(size<8||size>65536||fseek(f,0,SEEK_SET)) {fclose(f);return fail("module size rejected");}
    H.bytes=malloc((size_t)size);
    if(!H.bytes) {fclose(f);return fail("module allocation failed");}
    size_t got=fread(H.bytes,1,(size_t)size,f); int bad=ferror(f); fclose(f);
    if(bad||got!=(size_t)size||!preflight(H.bytes,got)) return fail("module capability/memory envelope rejected");
    H.env=m3_NewEnvironment(); if(!H.env) return fail("environment allocation failed");
    H.rt=m3_NewRuntime(H.env,65536,NULL); if(!H.rt) return fail("runtime allocation failed");
    m3_SetGasLimit(H.rt,1000000.0); m3_SetValidation(H.rt,true);
    M3Result e=m3_ParseModule(H.env,&H.module,H.bytes,(uint32_t)got);
    if(e) return fail(e);
    e=m3_LoadModule(H.rt,H.module);
    if(e) {m3_FreeModule(H.module);H.module=NULL;return fail(e);}
    if(!lookup(&H.query,"iaf_module_query",1)||!lookup(&H.req,"iaf_module_request_offset",0)
       ||!lookup(&H.res,"iaf_module_response_offset",0)||!lookup(&H.create,"iaf_module_create",1)
       ||!lookup(&H.drop,"iaf_module_drop",1)||!lookup(&H.reset,"iaf_module_reset",0)
       ||!lookup(&H.dispatch,"iaf_module_dispatch",1)||!lookup(&H.init,"_initialize",0)) return 0;
    if(!call(H.init,0,0,NULL)) return 0;
    const uint32_t expected[]={IAF_MODULE_ABI,IAF_MODULE_CAPACITY,sizeof(iaf_module_request),sizeof(iaf_module_response),sizeof(iaf_module_state),
        offsetof(iaf_module_request,state),offsetof(iaf_module_request,landing),offsetof(iaf_module_request,a),offsetof(iaf_module_request,b),offsetof(iaf_module_request,args),offsetof(iaf_module_request,now),offsetof(iaf_module_request,flag),offsetof(iaf_module_response,state),offsetof(iaf_module_response,command),offsetof(iaf_module_response,landing),offsetof(iaf_module_response,runway),offsetof(iaf_module_response,values)};
    for(unsigned i=0;i<sizeof expected/sizeof expected[0];i++) {uint32_t v;if(!call(H.query,1,i,&v)) return 0;if(v!=expected[i]) return fail("module ABI identity/layout mismatch");}
    if(!call(H.req,0,0,&H.reqoff)||!call(H.res,0,0,&H.resoff)) return 0;
    if(!range(H.reqoff,sizeof(iaf_module_request))||!range(H.resoff,sizeof(iaf_module_response))) return 0;
    if((H.reqoff< H.resoff+sizeof(iaf_module_response)) && (H.resoff<H.reqoff+sizeof(iaf_module_request))) return fail("module buffers overlap");
    H.ready=1;return 1;
}
int iaf_wasm_create(uint32_t phase,uint32_t *handle) {
    if(!iaf_wasm_ready()||!handle||!call(H.create,1,phase,handle)) return 0;
    return *handle ? 1 : fail("module slot unavailable");
}
int iaf_wasm_drop(uint32_t handle) {
    uint32_t v;if(!iaf_wasm_ready()||!call(H.drop,1,handle,&v)) return 0;
    return v==IAF_M_OK ? 1 : fail("module drop failed");
}
int iaf_wasm_reset(void) {
    uint32_t v;if(!iaf_wasm_ready()||!call(H.reset,0,0,&v)) return 0;
    return v==IAF_M_OK ? 1 : fail("module reset failed");
}
int iaf_wasm_dispatch(iaf_module_request *q,iaf_module_response *r) {
    if(!iaf_wasm_ready()) return 0;
    q->abi=IAF_MODULE_ABI;q->size=sizeof *q;
    uint8_t *p=range(H.reqoff,sizeof *q);if(!p)return 0;memcpy(p,q,sizeof *q);
    uint32_t v;if(!call(H.dispatch,1,sizeof *q,&v))return 0;
    p=range(H.resoff,sizeof *r);if(!p)return 0;memcpy(r,p,sizeof *r);
    if(v!=IAF_M_OK||r->status!=IAF_M_OK||r->abi!=IAF_MODULE_ABI||r->size!=sizeof *r) return fail("dispatch response rejected");
    return 1;
}
