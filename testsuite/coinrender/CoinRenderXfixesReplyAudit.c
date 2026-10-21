#define _GNU_SOURCE
#include <link.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>
#include <sys/uio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Test-only audit module. No request is generated, suppressed or reordered.
 * Track actual XFIXES QueryVersion cookies and edit only their returned version.
 * The passive mode (unset COIN_TEST_XFIXES_REPLY) records the same replies.
 * LD_AUDIT observes dynamic symbol bindings, including bindings via dlsym.
 */
struct pending { xcb_connection_t*c; uint64_t seq; unsigned kind; uint32_t major,minor; };
struct extension { xcb_connection_t*c; unsigned opcode; };
static struct pending pending[256];
static struct extension extensions[64];
static volatile int lock;
static unsigned mutations,observations;
static void enter(void){while(__sync_lock_test_and_set(&lock,1)){} }
static void leave(void){__sync_lock_release(&lock);}
static int mode(void){const char*s=getenv("COIN_TEST_XFIXES_REPLY");
  return s&&!strcmp(s,"1.0")?1:s&&!strcmp(s,"6.1")?2:0;}
static void fail(const char*s){write(2,s,strlen(s));_exit(87);}
static void remember(xcb_connection_t*c,uint64_t seq,unsigned kind,uint32_t major,uint32_t minor){
  if(!kind||!seq)return;enter();unsigned i;
  for(i=0;i<256;++i)if(!pending[i].c||(pending[i].c==c&&pending[i].seq==seq))break;
  if(i==256){leave();fail("xfixes_audit pending capacity exhausted\n");}
  pending[i]=(struct pending){c,seq,kind,major,minor};leave();
}
static unsigned classify(xcb_connection_t*c,struct iovec*v,const xcb_protocol_request_t*r,uint32_t*major,uint32_t*minor){
  if(!r||!v||!r->count||!v[0].iov_base)return 0;
  if(!r->ext&&r->opcode==98){for(size_t i=1;i<r->count;++i)
    if(v[i].iov_len==6&&v[i].iov_base&&!memcmp(v[i].iov_base,"XFIXES",6))return 2;}
  const unsigned char*bytes=v[0].iov_base;
  int fixes=r->ext&&r->ext->name&&!strcmp(r->ext->name,"XFIXES");
  unsigned minorOpcode=r->opcode;
  if(!r->ext){enter();for(unsigned i=0;i<64;++i)if(extensions[i].c==c&&
    extensions[i].opcode&&(extensions[i].opcode==r->opcode||extensions[i].opcode==bytes[0])){fixes=1;break;}leave();minorOpcode=bytes[1];}
  if(!fixes||minorOpcode!=0||v[0].iov_len<12)return 0;
  memcpy(major,bytes+4,4);memcpy(minor,bytes+8,4);return 1;
}
static void report(const char*route,uint32_t requestedMajor,uint32_t requestedMinor,uint32_t originalMajor,uint32_t originalMinor,uint32_t major,uint32_t minor){
  __sync_fetch_and_add(&observations,1);if(mode())__sync_fetch_and_add(&mutations,1);
  char line[256];int n=snprintf(line,sizeof(line),"xfixes_audit route=%s requested=%u.%u original=%u.%u returned=%u.%u injected=%d\n",
    route,requestedMajor,requestedMinor,originalMajor,originalMinor,major,minor,mode()!=0);
  if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
}
static void reply(xcb_connection_t*c,uint64_t seq,void*data){
  if(!data)return;struct pending found={0};enter();
  for(unsigned i=0;i<256;++i)if(pending[i].c==c&&pending[i].seq==seq){found=pending[i];pending[i].c=0;break;}
  if(found.kind==2){xcb_query_extension_reply_t*r=data;if(r->present){unsigned i;
    for(i=0;i<64;++i)if(!extensions[i].c||extensions[i].c==c)break;
    if(i==64){leave();fail("xfixes_audit extension capacity exhausted\n");}
    extensions[i]=(struct extension){c,r->major_opcode};}leave();return;}
  leave();if(found.kind!=1||((unsigned char*)data)[0]!=1)return;
  uint32_t originalMajor,originalMinor,major,minor;
  memcpy(&originalMajor,(char*)data+8,4);memcpy(&originalMinor,(char*)data+12,4);
  major=mode()==1?1:mode()==2?6:originalMajor;minor=mode()==1?0:mode()==2?1:originalMinor;
  memcpy((char*)data+8,&major,4);memcpy((char*)data+12,&minor,4);
  report("xcb-reply",found.major,found.minor,originalMajor,originalMinor,major,minor);
}
/* Public extension discovery must also be observed: libxcb can call its
 * internal send/wait implementation without crossing an ELF binding. */
#define EXTENSION_WRAPPER(NAME) \
static xcb_query_extension_cookie_t(*real_##NAME)(xcb_connection_t*,uint16_t,const char*); \
static xcb_query_extension_cookie_t wrap_##NAME(xcb_connection_t*c,uint16_t length,const char*name){ \
  xcb_query_extension_cookie_t cookie=real_##NAME(c,length,name); \
  if(length==6&&name&&!memcmp(name,"XFIXES",6))remember(c,cookie.sequence,2,0,0);return cookie;}
EXTENSION_WRAPPER(xcb_query_extension)
EXTENSION_WRAPPER(xcb_query_extension_unchecked)
static xcb_query_extension_reply_t*(*real_xcb_query_extension_reply)(xcb_connection_t*,xcb_query_extension_cookie_t,xcb_generic_error_t**);
static xcb_query_extension_reply_t*wrap_xcb_query_extension_reply(xcb_connection_t*c,xcb_query_extension_cookie_t cookie,xcb_generic_error_t**error){
  xcb_query_extension_reply_t*data=real_xcb_query_extension_reply(c,cookie,error);
  reply(c,cookie.sequence,data);return data;
}
#define SEND_WRAPPER(NAME,TYPE,EXTRA,ARGS) \
static TYPE (*real_##NAME)(xcb_connection_t*,int,struct iovec*,const xcb_protocol_request_t* EXTRA); \
static TYPE wrap_##NAME(xcb_connection_t*c,int f,struct iovec*v,const xcb_protocol_request_t*r EXTRA){ \
  uint32_t major=0,minor=0;unsigned kind=classify(c,v,r,&major,&minor); \
  TYPE seq=real_##NAME(c,f,v,r ARGS);remember(c,seq,kind,major,minor);return seq;}
#define NO_EXTRA
#define WITH_FDS ,unsigned int nfds,int*fds
#define FDS_ARGS ,nfds,fds
SEND_WRAPPER(xcb_send_request,unsigned int,NO_EXTRA,NO_EXTRA)
SEND_WRAPPER(xcb_send_request64,uint64_t,NO_EXTRA,NO_EXTRA)
SEND_WRAPPER(xcb_send_request_with_fds,unsigned int,WITH_FDS,FDS_ARGS)
SEND_WRAPPER(xcb_send_request_with_fds64,uint64_t,WITH_FDS,FDS_ARGS)
#define WAIT_WRAPPER(NAME,TYPE) \
static void*(*real_##NAME)(xcb_connection_t*,TYPE,xcb_generic_error_t**); \
static void*wrap_##NAME(xcb_connection_t*c,TYPE seq,xcb_generic_error_t**e){ \
  void*p=real_##NAME(c,seq,e);reply(c,seq,p);return p;}
WAIT_WRAPPER(xcb_wait_for_reply,unsigned int)
WAIT_WRAPPER(xcb_wait_for_reply64,uint64_t)
#define POLL_WRAPPER(NAME,TYPE) \
static int(*real_##NAME)(xcb_connection_t*,TYPE,void**,xcb_generic_error_t**); \
static int wrap_##NAME(xcb_connection_t*c,TYPE seq,void**p,xcb_generic_error_t**e){ \
  int ready=real_##NAME(c,seq,p,e);if(ready&&p)reply(c,seq,*p);return ready;}
POLL_WRAPPER(xcb_poll_for_reply,unsigned int)
POLL_WRAPPER(xcb_poll_for_reply64,uint64_t)
static void(*real_xcb_disconnect)(xcb_connection_t*);
static void wrap_xcb_disconnect(xcb_connection_t*c){
  real_xcb_disconnect(c);enter();for(unsigned i=0;i<256;++i)if(pending[i].c==c)pending[i].c=0;
  for(unsigned i=0;i<64;++i)if(extensions[i].c==c)extensions[i].c=0;leave();
}
/* A production implementation may use libXfixes rather than generated XCB.
 * Alter its public version result after the actual round trip, with no helper
 * or dependency on private product symbols. Raw Xlib protocol code that uses
 * neither API remains outside this auditor and fails the zero-injection gate.
 */
static int(*real_XFixesQueryVersion)(void*,int*,int*);
static int wrap_XFixesQueryVersion(void*d,int*major,int*minor){
  uint32_t requestedMajor=major?*major:0,requestedMinor=minor?*minor:0;
  int ok=real_XFixesQueryVersion(d,major,minor);if(!ok||!major||!minor)return ok;
  uint32_t originalMajor=*major,originalMinor=*minor;
  if(mode()==1){*major=1;*minor=0;}else if(mode()==2){*major=6;*minor=1;}
  report("XFixesQueryVersion",requestedMajor,requestedMinor,originalMajor,originalMinor,*major,*minor);return ok;
}
__attribute__((visibility("default"))) unsigned int la_version(unsigned int version){return version<LAV_CURRENT?version:LAV_CURRENT;}
__attribute__((visibility("default"))) unsigned int la_objopen(struct link_map*map,Lmid_t namespace,uintptr_t*cookie){(void)map;(void)namespace;(void)cookie;return LA_FLG_BINDTO|LA_FLG_BINDFROM;}
__attribute__((visibility("default"))) uintptr_t la_symbind64(Elf64_Sym*symbol,unsigned int index,uintptr_t*ref,uintptr_t*def,unsigned int*flags,const char*name){
  (void)index;(void)ref;(void)def;(void)flags;
#define BIND(NAME) if(!strcmp(name,#NAME)){real_##NAME=(__typeof__(real_##NAME))(uintptr_t)symbol->st_value;return(uintptr_t)wrap_##NAME;}
  BIND(xcb_send_request) BIND(xcb_send_request64) BIND(xcb_send_request_with_fds) BIND(xcb_send_request_with_fds64)
  BIND(xcb_wait_for_reply) BIND(xcb_wait_for_reply64) BIND(xcb_poll_for_reply) BIND(xcb_poll_for_reply64)
  BIND(xcb_query_extension) BIND(xcb_query_extension_unchecked) BIND(xcb_query_extension_reply)
  BIND(xcb_disconnect) BIND(XFixesQueryVersion)
#undef BIND
  return symbol->st_value;
}
__attribute__((destructor))static void finish(void){
  char line[128];int n=snprintf(line,sizeof(line),"xfixes_audit observations=%u mutations=%u\n",observations,mutations);
  if(n>0)write(2,line,(size_t)n);if(mode()&&!mutations)_exit(86);
}
