#define _GNU_SOURCE
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <xcb/xcb.h>
#include <execinfo.h>
struct state {void*c;unsigned generation;int live;};
static struct state connections[512];static volatile int lock;
static void enter(){while(__sync_lock_test_and_set(&lock,1)){} }
static void leave(){__sync_lock_release(&lock);}
__attribute__((noinline,visibility("default"))) void audit_dead_connection(void*c){void*frames[32];int count=backtrace(frames,32);backtrace_symbols_fd(frames,count,2);}
static void event(const char*name,void*c,int action){
 unsigned generation=0;int live=-1;enter();unsigned i;
 for(i=0;i<512;i++)if(connections[i].c==c)break;
 if(i==512&&action==1)for(i=0;i<512;i++)if(!connections[i].c)break;
 if(i<512){if(action==1){connections[i].c=c;connections[i].generation++;connections[i].live=1;}
 if(action==-1)connections[i].live=0;generation=connections[i].generation;live=connections[i].live;}leave();
 if(live==0&&(!strcmp(name,"xcb_generate_id")||!strcmp(name,"xcb_present_select_input")||!strcmp(name,"xcb_get_maximum_request_length")))audit_dead_connection(c);
 char s[240];int n=snprintf(s,sizeof(s),"wsi_audit tid=%ld event=%s connection=%p generation=%u live=%d\n",syscall(SYS_gettid),name,c,generation,live);if(n>0)write(2,s,n);
}
static xcb_connection_t*(*real_xcb_connect)(const char*,int*);
static xcb_connection_t*wrap_xcb_connect(const char*n,int*s){xcb_connection_t*c=real_xcb_connect(n,s);event("xcb_connect",c,1);return c;}
static xcb_connection_t*(*real_xcb_connect_to_display_with_auth_info)(const char*,xcb_auth_info_t*,int*);
static xcb_connection_t*wrap_xcb_connect_to_display_with_auth_info(const char*n,xcb_auth_info_t*a,int*s){xcb_connection_t*c=real_xcb_connect_to_display_with_auth_info(n,a,s);event("xcb_connect_auth",c,1);return c;}
static xcb_connection_t*(*real_xcb_connect_to_fd)(int,xcb_auth_info_t*);
static xcb_connection_t*wrap_xcb_connect_to_fd(int f,xcb_auth_info_t*a){xcb_connection_t*c=real_xcb_connect_to_fd(f,a);event("xcb_connect_fd",c,1);return c;}
static void(*real_xcb_disconnect)(xcb_connection_t*);
static void wrap_xcb_disconnect(xcb_connection_t*c){event("xcb_disconnect_enter",c,0);real_xcb_disconnect(c);event("xcb_disconnect_return",c,-1);}
static uint32_t(*real_xcb_get_maximum_request_length)(xcb_connection_t*);
static uint32_t wrap_xcb_get_maximum_request_length(xcb_connection_t*c){event("xcb_get_maximum_request_length",c,0);return real_xcb_get_maximum_request_length(c);}
static uint32_t(*real_xcb_generate_id)(xcb_connection_t*);
static uint32_t wrap_xcb_generate_id(xcb_connection_t*c){event("xcb_generate_id",c,0);return real_xcb_generate_id(c);}
#define PRESENT(NAME) static xcb_void_cookie_t(*real_##NAME)(xcb_connection_t*,uint32_t,uint32_t,uint32_t);static xcb_void_cookie_t wrap_##NAME(xcb_connection_t*c,uint32_t e,uint32_t w,uint32_t m){event(#NAME,c,0);return real_##NAME(c,e,w,m);}
PRESENT(xcb_present_select_input) PRESENT(xcb_present_select_input_checked)
static void*(*real_XGetXCBConnection)(void*);
static void*wrap_XGetXCBConnection(void*d){void*c=real_XGetXCBConnection(d);event("XGetXCBConnection",c,0);return c;}
#define EGL_BOOL(NAME,ARGS,CALL) static unsigned(*real_##NAME)ARGS;static unsigned wrap_##NAME ARGS {event(#NAME,d,0);return real_##NAME CALL;}
EGL_BOOL(eglInitialize,(void*d,int*a,int*b),(d,a,b))
EGL_BOOL(eglTerminate,(void*d),(d))
static void*(*real_eglGetDisplay)(void*);
static void*wrap_eglGetDisplay(void*d){void*r=real_eglGetDisplay(d);event("eglGetDisplay_native",d,0);event("eglGetDisplay_result",r,0);return r;}
__attribute__((visibility("default"))) unsigned la_version(unsigned v){return v<LAV_CURRENT?v:LAV_CURRENT;}
__attribute__((visibility("default"))) unsigned la_objopen(struct link_map*m,Lmid_t n,uintptr_t*c){(void)m;(void)n;(void)c;return LA_FLG_BINDTO|LA_FLG_BINDFROM;}
__attribute__((visibility("default"))) uintptr_t la_symbind64(Elf64_Sym*s,unsigned i,uintptr_t*r,uintptr_t*d,unsigned*f,const char*n){(void)i;(void)r;(void)d;(void)f;
#define BIND(NAME) if(!strcmp(n,#NAME)){real_##NAME=(__typeof__(real_##NAME))(uintptr_t)s->st_value;return(uintptr_t)wrap_##NAME;}
 BIND(xcb_connect) BIND(xcb_connect_to_display_with_auth_info) BIND(xcb_connect_to_fd) BIND(xcb_disconnect) BIND(xcb_generate_id) BIND(xcb_get_maximum_request_length) BIND(xcb_present_select_input) BIND(xcb_present_select_input_checked) BIND(XGetXCBConnection) BIND(eglInitialize) BIND(eglTerminate) BIND(eglGetDisplay)
 return s->st_value;
}
