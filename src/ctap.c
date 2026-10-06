#include "ctap.h"
#include "cbor.h"
#include "crypto.h"
#include "store.h"
#include "platform.h"
#include "pin.h"
#include <string.h>
static const uint8_t aaguid[16]={0x6d,0x69,0x6e,0x69,0x66,0x32,0x40,0x01,0x80,0,0,0,0,0,0,1};
static uint8_t pending_ids[STORE_CAPACITY][16],pending_rp[32],pending_hash[32];
static unsigned pending_count,pending_next;static int pending_up,pending_uv;static uint32_t pending_since;
void ctap_reset_state(void){pending_count=pending_next=0;}
static int blob(cv m,int k,unsigned type,const uint8_t **p,size_t *n){cv v;return cb_get(m,k,0,&v)==1&&cb_bytes(v,type,p,n);}
static int opt(cv m,const char *key,int fallback){cv v;int found=cb_get(m,0,key,&v);if(found<0)return -1;if(!found)return fallback;if(v.n!=1||(v.p[0]!=0xf4&&v.p[0]!=0xf5))return -1;return v.p[0]==0xf5;}
static void key(cw *w,int k){cb_put(w,k<0?1:0,k<0?-k-1:k);}
static int presence(void){board_ui(1);return board_presence();}
static size_t failure(uint8_t *out,unsigned code){out[0]=code;board_ui(4);return 1;}
static void count(uint8_t *a,uint32_t c){a[33]=c>>24;a[34]=c>>16;a[35]=c>>8;a[36]=c;}
static int list_match(cv list,const uint8_t rp[32],credential *c){cv item,id,type;const uint8_t *p;size_t n;for(unsigned i=0;cb_at(list,i,&item);i++)if(cb_get(item,0,"type",&type)==1&&cb_equal(type,"public-key")&&cb_get(item,0,"id",&id)==1&&cb_bytes(id,2,&p,&n)&&n==16&&store_find(p,rp,c))return 1;return 0;}
static void user_text(cv user,const char *field,char target[65]){cv v;const uint8_t *p;size_t n;if(cb_get(user,0,field,&v)==1&&cb_bytes(v,3,&p,&n)){if(n>64){n=64;while(n&&(p[n]&0xc0)==0x80)n--;}memcpy(target,p,n);target[n]=0;}}
static size_t sign_assertion(credential *c,const uint8_t rp[32],const uint8_t challenge[32],int up,int uv,unsigned total,uint8_t *out,size_t cap){
 uint8_t auth[37]={0},input[69],hash[32],sig[72];cw w={out,1,cap,0};out[0]=0;board_ui(2);memcpy(auth,rp,32);auth[32]=(up?1:0)|(uv?4:0);if(c->counter==UINT32_MAX)return failure(out,0x7f);c->counter++;if(!store_save(c))return failure(out,0x28);count(auth,c->counter);memcpy(input,auth,37);memcpy(input+37,challenge,32);sha(input,69,hash);int sn=crypto_sign(c->key,hash,sig);if(!sn||operation_cancelled)return failure(out,operation_cancelled?0x2d:0x7f);
 cb_put(&w,5,3+(c->resident==1)+(total>1));key(&w,1);cb_put(&w,5,2);cb_text(&w,"id");cb_blob(&w,c->id,16);cb_text(&w,"type");cb_text(&w,"public-key");key(&w,2);cb_blob(&w,auth,37);key(&w,3);cb_blob(&w,sig,sn);
 if(c->resident==1){key(&w,4);cb_put(&w,5,1+(uv&&c->name[0])+(uv&&c->display[0]));cb_text(&w,"id");cb_blob(&w,c->user,c->user_len);if(uv&&c->name[0]){cb_text(&w,"name");cb_text(&w,c->name);}if(uv&&c->display[0]){cb_text(&w,"displayName");cb_text(&w,c->display);}}
 if(total>1){key(&w,5);cb_put(&w,0,total);}board_ui(3);return w.error?failure(out,0x7f):w.n;
}
size_t ctap_request(const uint8_t *req,size_t n,uint8_t *out,size_t cap){
 if(!n||cap<512)return failure(out,0x12);uint8_t command=req[0];cv m={req+1,n-1},v;cw w={out,1,cap,0};out[0]=0;
 if(command==8){if(n!=1)return failure(out,0x12);if(!pending_count||pending_next>=pending_count||(uint32_t)(board_millis()-pending_since)>30000)return failure(out,0x30);credential c;if(!store_find(pending_ids[pending_next++],pending_rp,&c))return failure(out,0x2e);size_t result=sign_assertion(&c,pending_rp,pending_hash,pending_up,pending_uv,1,out,cap);memset(&c,0,sizeof(c));if(out[0])ctap_reset_state();return result;}
 ctap_reset_state();
 if(command==4){if(n!=1)return failure(out,0x12);cb_put(&w,5,8);key(&w,1);cb_put(&w,4,1);cb_text(&w,"FIDO_2_0");key(&w,3);cb_blob(&w,aaguid,16);key(&w,4);cb_put(&w,5,4);cb_text(&w,"rk");cb_bool(&w,1);cb_text(&w,"up");cb_bool(&w,1);cb_text(&w,"plat");cb_bool(&w,0);cb_text(&w,"clientPin");cb_bool(&w,pin_is_set());key(&w,5);cb_put(&w,0,CTAP_MAX);key(&w,6);cb_put(&w,4,1);cb_put(&w,0,1);key(&w,7);cb_put(&w,0,8);key(&w,8);cb_put(&w,0,16);key(&w,9);cb_put(&w,4,1);cb_text(&w,"usb");return w.error?failure(out,0x7f):w.n;}
 if(command==7){if(n!=1)return failure(out,0x12);if(board_millis()>10000)return failure(out,0x30);if(!presence())return failure(out,operation_cancelled?0x2d:0x2f);board_ui(2);if(!store_reset())return failure(out,0x7f);pin_init();board_ui(3);return 1;}
 if(command!=1&&command!=2&&command!=6)return failure(out,1);if(n>CTAP_MAX||!cb_valid(m))return failure(out,0x12);unsigned ty;uint32_t val;size_t head;if(!cb_head(m,&ty,&val,&head)||ty!=5)return failure(out,0x12);
 if(command==6)return pin_request(m,out,cap);
 const uint8_t *challenge,*rp;size_t cn,rn;cv options={0};int found=cb_get(m,command==1?7:5,0,&options);if(found<0)return failure(out,0x12);if(found&&(!cb_head(options,&ty,&val,&head)||ty!=5))return failure(out,0x12);
 int want_uv=found?opt(options,"uv",0):0,up=command==1?1:(found?opt(options,"up",1):1),rk=found?opt(options,"rk",0):0;if(want_uv<0||up<0||rk<0)return failure(out,0x12);
 if(!blob(m,command==1?1:2,2,&challenge,&cn)||cn!=32)return failure(out,0x14);int verified=0;int authorization=pin_authorize(m,command==1?8:6,command==1?9:7,challenge,&verified);if(authorization)return failure(out,authorization);if(want_uv&&!verified)return failure(out,0x2c);if(command==1&&pin_is_set()&&!verified)return failure(out,0x36);
 if(command==1){if(cb_get(m,2,0,&v)!=1||cb_get(v,0,"id",&v)!=1||!cb_bytes(v,3,&rp,&rn))return failure(out,0x14);}else if(!blob(m,1,3,&rp,&rn))return failure(out,0x14);if(!rn||rn>253)return failure(out,0x12);uint8_t rph[32];sha(rp,rn,rph);credential c={0};uint8_t pub[64];size_t result;
 if(command==1){cv user,uid;if(cb_get(m,3,0,&user)!=1||cb_get(user,0,"id",&uid)!=1)return failure(out,0x14);const uint8_t *userp;size_t un;if(!cb_bytes(uid,2,&userp,&un)||!un||un>64)return failure(out,0x12);
  cv params,item,alg,type;int supported=0,a;if(cb_get(m,4,0,&params)!=1)return failure(out,0x14);for(unsigned i=0;cb_at(params,i,&item);i++)if(cb_get(item,0,"alg",&alg)==1&&cb_int(alg,&a)&&a==-7&&cb_get(item,0,"type",&type)==1&&cb_equal(type,"public-key"))supported=1;if(!supported)return failure(out,0x26);
  cv exclude;int excluded=cb_get(m,5,0,&exclude)==1&&list_match(exclude,rph,&c);if(!presence())return failure(out,operation_cancelled?0x2d:0x2f);if(excluded)return failure(out,0x19);board_ui(2);memset(&c,0,sizeof(c));if(rk){c.resident=1;c.user_len=un;memcpy(c.user,userp,un);user_text(user,"name",c.name);user_text(user,"displayName",c.display);}
  if(!store_create(rph,&c,pub))return failure(out,0x28);if(operation_cancelled){memset(&c,0,sizeof(c));return failure(out,0x2d);}
  uint8_t auth[256]={0};memcpy(auth,rph,32);auth[32]=0x41|(verified?4:0);memcpy(auth+37,aaguid,16);auth[54]=16;memcpy(auth+55,c.id,16);cw aw={auth,71,sizeof(auth),0};cb_put(&aw,5,5);key(&aw,1);cb_put(&aw,0,2);key(&aw,3);cb_put(&aw,1,6);key(&aw,-1);cb_put(&aw,0,1);key(&aw,-2);cb_blob(&aw,pub,32);key(&aw,-3);cb_blob(&aw,pub+32,32);
  cb_put(&w,5,3);key(&w,1);cb_text(&w,"none");key(&w,2);cb_blob(&w,auth,aw.n);key(&w,3);cb_put(&w,5,0);result=w.error?failure(out,0x7f):w.n;
 }else{cv allow;int listed=cb_get(m,3,0,&allow);if(listed<0)return failure(out,0x12);int has_allow=0;if(listed){if(!cb_head(allow,&ty,&val,&head)||ty!=4)return failure(out,0x12);has_allow=val!=0;}
  unsigned total=1;if(has_allow){if(!list_match(allow,rph,&c))return failure(out,0x2e);}else{if(!up&&!verified)return failure(out,0x36);for(unsigned i=0;i<store_count();i++)if(store_at(i,&c)&&c.resident==1&&crypto_equal(c.rp,rph,32))memcpy(pending_ids[pending_count++],c.id,16);if(!pending_count)return failure(out,0x2e);total=pending_count;if(!store_find(pending_ids[0],rph,&c))return failure(out,0x2e);}
  if(up&&!presence()){ctap_reset_state();memset(&c,0,sizeof(c));return failure(out,operation_cancelled?0x2d:0x2f);}result=sign_assertion(&c,rph,challenge,up,verified,total,out,cap);
  if(!out[0]&&pending_count>1){memcpy(pending_rp,rph,32);memcpy(pending_hash,challenge,32);pending_up=up;pending_uv=verified;pending_next=1;pending_since=board_millis();}else ctap_reset_state();
 }memset(&c,0,sizeof(c));if(!out[0])board_ui(3);return result;
}
