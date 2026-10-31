/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wasm_host.h"
#include "wasm3.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
/* Self-authored minimal ()->() infinite branch loop, no host imports or effects.
 * Run this executable under an outer hard timeout in addition to intrinsic gas. */
static const unsigned char loop[]={0,97,115,109,1,0,0,0,1,4,1,96,0,0,3,2,1,0,7,8,1,4,'l','o','o','p',0,0,10,9,1,7,0,3,64,12,0,11,11};
int main(int argc,char **argv) {
    IM3Environment env=m3_NewEnvironment(); IM3Runtime rt=m3_NewRuntime(env,65536,0); IM3Module mod=0; IM3Function fn=0;
    m3_SetGasLimit(rt,1.0); m3_SetValidation(rt,1);
    assert(!m3_ParseModule(env,&mod,loop,sizeof loop));assert(!m3_LoadModule(rt,mod));assert(!m3_FindFunction(&fn,rt,"loop"));
    M3Result e=m3_CallV(fn);assert(e && !strcmp(e,m3Err_trapOutOfGas));
    m3_FreeRuntime(rt);m3_FreeEnvironment(env);puts("intrinsic gas loop trap PASS");
    if(argc!=2)return 2;
    if(!iaf_wasm_open(argv[1])) {fprintf(stderr,"open: %s\n",iaf_wasm_error());return 1;}
    uint32_t h;assert(iaf_wasm_create(PH_INBOUND,&h));
    iaf_module_request q={0};iaf_module_response r;
    q.op=IAF_M_CLAMP;q.args[0]=20;q.args[1]=0;q.args[2]=10;
    assert(iaf_wasm_dispatch(&q,&r));assert(r.values[0]==10);
    q.op=IAF_M_GET;q.handle=h;assert(iaf_wasm_dispatch(&q,&r));assert(r.state.kin.yaw==IAF_FD_YAW_UNSET);
    q.op=IAF_M_LINE_UP;q.args[0]=0.5;assert(iaf_wasm_dispatch(&q,&r));assert(r.state.kin.yaw==0.5);
    q.op=IAF_M_GET;assert(iaf_wasm_dispatch(&q,&r));assert(r.state.kin.yaw==0.5);
    /* Differential vectors: the compiled interpreter executes production FLY, not a
     * second native domain implementation in the plugin. Native reference is test-only. */
    iaf_fd_kin reference=r.state.kin;
    for(int i=0;i<1000;i++) {
        q.op=IAF_M_FLY;q.args[0]=50+i*0.01;q.args[1]=1.0/30;q.args[2]=0.04;q.args[3]=80;
        q.args[4]=100;q.args[5]=-0.1;q.args[6]=8;q.args[7]=0;
        iaf_fd_cmd cmd;iaf_fd_plane_fly(&reference,q.args[0],q.args[1],q.args[2],q.args[3],q.args[4],q.args[5],q.args[6],q.args[7],&cmd);
        assert(iaf_wasm_dispatch(&q,&r));
        const double *a=(const double*)&reference,*b=(const double*)&r.state.kin;
        for(unsigned j=0;j<sizeof reference/sizeof(double);j++)assert(fabs(a[j]-b[j])<1e-10);
        a=(const double*)&cmd;b=(const double*)&r.command;
        for(unsigned j=0;j<sizeof cmd/sizeof(double);j++)assert(fabs(a[j]-b[j])<1e-10);
    }
    /* A count2 unsafe FINAL yields staged terminal ABORT3; never SET_STATE3.
     * GET must still return live count2, then DROP invalidates this generation. */
    q.op=IAF_M_GET;assert(iaf_wasm_dispatch(&q,&r));
    q.state=r.state;q.state.landing.phase=PH_FINAL;q.state.landing.go_arounds=2;
    q.op=IAF_M_SET_STATE;assert(iaf_wasm_dispatch(&q,&r));
    memset(&q.landing,0,sizeof q.landing);
    q.landing.runway.az=1;q.landing.runway.len=1000;
    q.landing.pos[0]=1000;q.landing.pos[1]=50;q.landing.pos[2]=-100;
    q.landing.slope=0.1;q.landing.h0=10;q.landing.cruise=100;
    q.op=IAF_M_LANDING;assert(iaf_wasm_dispatch(&q,&r));
    assert(r.landing.action==IAF_LAND_ABORT);assert(r.state.landing.go_arounds==3);
    q.op=IAF_M_GET;assert(iaf_wasm_dispatch(&q,&r));assert(r.state.landing.go_arounds==2);
    assert(iaf_wasm_drop(h));
    uint32_t replacement;assert(iaf_wasm_create(PH_INBOUND,&replacement));assert(replacement!=h);
    q.handle=replacement;assert(iaf_wasm_dispatch(&q,&r));assert(r.state.landing.go_arounds==0);
    q.handle=h;assert(!iaf_wasm_dispatch(&q,&r));assert(!iaf_wasm_ready());
    iaf_wasm_close();puts("actual production wasm persistent slots and fail-closed PASS");return 0;
}
