#include "cbor.h"
#include <string.h>
#include <limits.h>
int cb_head(cv v,unsigned *t,uint32_t *x,size_t *h){
 if(!v.n)return 0; *t=v.p[0]>>5; unsigned a=v.p[0]&31; *h=1; *x=a;
 if(a<24)return 1; if(a>26)return 0; unsigned n=1u<<(a-24);
 if(v.n<1+n)return 0; *x=0; for(unsigned i=0;i<n;i++)*x=(*x<<8)|v.p[1+i]; *h=1+n; return 1;
}
static int skip(cv v,size_t *used,unsigned depth){
 unsigned t; uint32_t x; size_t h; if(depth>8||!cb_head(v,&t,&x,&h))return 0;
 if(t==2||t==3){if(x>v.n-h)return 0;*used=h+x;return 1;}
 if(t==4||t==5){if(x>128)return 0; size_t pos=h; unsigned count=t==5?x*2:x;
  for(unsigned i=0;i<count;i++){size_t u;if(!skip((cv){v.p+pos,v.n-pos},&u,depth+1))return 0;pos+=u;}*used=pos;return 1;}
 if(t==0||t==1||(t==7&&(x==20||x==21||x==22))){*used=h;return 1;}return 0;
}
int cb_valid(cv v){size_t u;return skip(v,&u,0)&&u==v.n;}
int cb_int(cv v,int *out){unsigned t;uint32_t x;size_t h;if(!cb_head(v,&t,&x,&h)||t>1||x>INT_MAX)return 0;*out=t?-(int)x-1:(int)x;return 1;}
int cb_bytes(cv v,unsigned type,const uint8_t **p,size_t *n){unsigned t;uint32_t x;size_t h;if(!cb_head(v,&t,&x,&h)||t!=type||x>v.n-h)return 0;*p=v.p+h;*n=x;return 1;}
int cb_equal(cv v,const char *s){const uint8_t *p;size_t n;return cb_bytes(v,3,&p,&n)&&n==strlen(s)&&!memcmp(p,s,n);}
int cb_get(cv v,int key,const char *text,cv *out){
 unsigned t;uint32_t x;size_t pos;if(!cb_head(v,&t,&x,&pos)||t!=5||x>128)return -1; int found=0;
 for(unsigned i=0;i<x;i++){size_t k,l;cv kv={v.p+pos,v.n-pos};if(!skip(kv,&k,0))return -1;pos+=k;
  cv value={v.p+pos,v.n-pos};if(!skip(value,&l,0))return -1;int num;int match=text?cb_equal(kv,text):(cb_int(kv,&num)&&num==key);
  if(match){if(found)return -1;*out=(cv){value.p,l};found=1;}pos+=l;
 }return found;
}
int cb_at(cv v,unsigned index,cv *out){unsigned t;uint32_t x;size_t pos;if(!cb_head(v,&t,&x,&pos)||t!=4||x>128||index>=x)return 0;
 for(unsigned i=0;i<=index;i++){size_t n;if(!skip((cv){v.p+pos,v.n-pos},&n,0))return 0;if(i==index){*out=(cv){v.p+pos,n};return 1;}pos+=n;}return 0;}
void cb_raw(cw *w,const void *p,size_t n){if(w->error||n>w->cap-w->n){w->error=1;return;}memcpy(w->p+w->n,p,n);w->n+=n;}
void cb_put(cw *w,unsigned t,uint32_t x){uint8_t b[5];size_t n=1;if(x<24)b[0]=(t<<5)|x;else{unsigned bytes=x<=255?1:x<=65535?2:4;b[0]=(t<<5)|(bytes==1?24:bytes==2?25:26);for(unsigned i=0;i<bytes;i++)b[1+i]=x>>(8*(bytes-1-i));n+=bytes;}cb_raw(w,b,n);}
void cb_blob(cw *w,const void *p,size_t n){cb_put(w,2,n);cb_raw(w,p,n);}
void cb_text(cw *w,const char *s){cb_put(w,3,strlen(s));cb_raw(w,s,strlen(s));}
void cb_bool(cw *w,int v){cb_put(w,7,v?21:20);}
