/*
  Copyright (c) 2020 Damjan Marion

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include <vlib/vlib.h>
#include <vppinfra/time.h>
#include <vppinfra/cache.h>
#include <vppinfra/error.h>
#include <sys/resource.h>
#include <stdio.h>
#include <pthread.h>

#include <openssl/md5.h>

// #include <vppinfra/bihash_8_8.h>
#include <vppinfra/bihash_8_8_stats.h>
#include <vppinfra/bihash_template.h>

#include <vppinfra/bihash_template.c>


int generate_hash_by_key();
int tune_bihash_v4_search(BVT (clib_bihash) * h);
int add_collisions( BVT (clib_bihash) * h);


#define clib_bihash_search_batch_v5(h,kvs,kv_sz,valuep) \
do{\
 int i;\
 for(i=0;i<kv_sz;i++){\
  if (BV (clib_bihash_search) (h, &kvs[i], &valuep[i]) < 0){}\
 }\
  \
}while(0)


#define reset_keys(kvs,kv_sz,key_val) \
do{\
 int i;\
 for(i=0;i<kv_sz;i++){ \
    kvs[i].key = key_val+i; \
 }\
}while(0)

#if 1
#define shift_keys(kvs,kv_sz,shift_nm) \
do{\
 int i;\
 for(i=0;i<kv_sz;i++){\
    kvs[i].key += shift_nm; \
 }\
}while(0)

#define shift_one_key(kv,shift_nm) \
do{\
kv.key+=shift_nm;\
}while(0)

#else
#define shift_keys(kvs,kv_sz,shift_nm) 
#define shift_one_key(kv,shift_nm)
#endif

#define statistic_perf(test_no,if_no,loops_num,options,cycles) \
do{\
  char *prt_format ="---[item%d]|API V%d|---Des:searching %d elments---|Cycles/Option:%d|cycles:%ld|options:%ld\n"; \
  fformat (stdout,prt_format, \
      test_no,\
      if_no,\
      loops_num,\
      cycles/options,\
      cycles,\
      options \
      ); \
}while(0)

#define perf_test_0(test_no,if_no,loops_num,options,cycles,if_fn,h,kv,result) \
do{ \
  u64 _loop_cnt = loops_num;\
  u64 _loops_num = loops_num;\
  options = 0;\
  u64 start ; \
  /* BVT (clib_bihash_kv) _kv=kv[0] */ ; \
  start = clib_cpu_time_now();\
  do{\
\
    for(i=0;i<8;i++){\
      \
      if (BV (clib_bihash_search) (h, &kv, &kv) < 0){\
      }\
      shift_one_key(kv,1);\
      /* options++ */ ; \
    }\
    options+=8 ;\
\
  }while(_loop_cnt--);\
  cycles = clib_cpu_time_now() - start ;  \
  statistic_perf(test_no,if_no,_loops_num,options,cycles);\
}while(0)

#define perf_test_1(test_no,if_no,loops_num,options,cycles,if_fn,h,kv,result) \
do{ \
  u64 _loop_cnt = loops_num;\
  u64 _loops_num = loops_num;\
  options = 0;\
  u64 start; \
  u8 key_mask = 8;\
  u8 valid_key_idx = 0; \
  reset_keys(kv,8,0);\
  start = clib_cpu_time_now();\
  do{\
\
    if (if_fn(h, kv, key_mask,result,&valid_key_idx) < 0){\
      }\
    shift_keys(kv,8,8);\
    options+=8; \
\
  }while(_loop_cnt--);\
  cycles = clib_cpu_time_now() - start ;  \
  statistic_perf(test_no,if_no,_loops_num,options,cycles);\
}while(0)

#define perf_test_2(test_no,if_no,loops_num,options,cycles,if_fn,h,kv,result) \
do{ \
  u64 _loop_cnt = loops_num;\
  u64 _loops_num = loops_num;\
  options = 0;\
  u64 start; \
  reset_keys(kv,8,0);\
  start = clib_cpu_time_now();\
  \
    do{\
\
    clib_bihash_search_batch_v5(h,kv,8,kv);\
    shift_keys(kv,8,0);\
    options+=8; \
\
  }while(_loop_cnt--);\
  \
  cycles = clib_cpu_time_now() - start ;  \
  statistic_perf(test_no,if_no,_loops_num,options,cycles);\
}while(0)

#define dump_md5(if_name,out0) do{\
\
int n; \
fformat (stdout,"["#if_name"]\n\t md5sum:");\
for(n=0; n<MD5_DIGEST_LENGTH; n++) \
        fformat (stdout,"%02x", out0[n]);\
fformat (stdout,"\n");\
}while(0)


always_inline u64 bitmap_first_set(u64 ai)
{
  u64 i;
  u64 base = 1;
  for(i=0;i<64;i++){
     if((ai & (base<<i))){
      return i;
    }
  }
  return ~0;
}

always_inline u64 bitmap_next_set(u64 ai,u64 ei)
{
  u64 i;
  u64 base = 1;
  for(i=ei; i<64; i++){
     if((ai & (base<<i))){
      return i;
    }
  }
  return ~0;
}

#define bit_foreach(i,ai) \
  if(ai) \
    for(i = bitmap_first_set(ai); \
        i != ~0; \
        i = bitmap_next_set(ai,i+1))

#define judge_match_result(if_fn0,if_fn1,md5sum0,md5sum1,ret) \
do{\
  ret = memcmp(out0,out1,MD5_DIGEST_LENGTH) ?1:0; \
  if(!ret){\
    fformat (stdout,#if_fn0" IS MATCH "#if_fn1"\n",if_fn0,if_fn1);\
  }else{\
    dump_md5(if_fn0,out0);dump_md5(if_fn1,out1);\
  }\
}while(0)

#define consistency_test_0(test_no,if_no1,loops_num,h,kv0,kv1,if_fn0,if_fn1) \
do{\
  u64 _loop_cnt = loops_num;\
  MD5_CTX c[2];\
  char buf[256];\
  unsigned char out0[MD5_DIGEST_LENGTH];\
  unsigned char out1[MD5_DIGEST_LENGTH];\
  u8 key_mask = 0xFF;\
  u8 valid_key_idx = 0; \
  int ret; \
  u64 cnts[2]={0,0};\
  kv0.key = 0;\
  reset_keys(kv1,8,0);\
  MD5_Init(&c[0]);\
  MD5_Init(&c[1]);\
  \
  do{\
  \
    for(i=0;i<8;i++){\
      if (if_fn0 (h, &kv0, &kv0) == 0){\
      cnts[0]++;\
        sprintf(buf,"%ld",kv0.value);\
        MD5_Update(&c[0], buf, strlen(buf));\
      }\
      shift_one_key(kv0,1);\
      /* options++ */ ; \
    }\
    \
    if (if_fn1(h, kv1, key_mask,kv1,&valid_key_idx) > 0){\
       bit_foreach(i,valid_key_idx){\
       cnts[1]++;\
          sprintf(buf,"%ld",kv1[i].value);\
          MD5_Update(&c[1], buf, strlen(buf));\
        }\
    }\
    shift_keys(kv1,8,8);\
  }while(_loop_cnt--);\
  \
  MD5_Final(out0, &c[0]);\
  MD5_Final(out1, &c[1]);\
  judge_match_result(if_fn0,if_fn1,out0,out1,ret);\
 \
}while(0)


#define consistency_test_1(test_no,if_no0,if_no1,loops_num,h,kv0,kv1,if_fn0,if_fn1) \
do{\
  u64 _loop_cnt = loops_num;\
  MD5_CTX c[2];\
  char buf[256];\
  unsigned char out0[MD5_DIGEST_LENGTH];\
  unsigned char out1[MD5_DIGEST_LENGTH];\
  u8 key_mask = 0xFF;\
  u8 valid_key_idx = 0; \
  int ret; \
  reset_keys(kv0,8,0);\
  reset_keys(kv1,8,0);\
  MD5_Init(&c[0]);\
  MD5_Init(&c[1]);\
  \
  do{\
  \
    if (if_fn0(h, kv0, key_mask,kv0,&valid_key_idx) > 0){\
        bit_foreach(i,valid_key_idx){\
          sprintf(buf,"%ld",kv0[i].value);\
          MD5_Update(&c[0], buf, strlen(buf));\
        }\
    }\
    \
    if (if_fn1(h, kv1, key_mask,kv1,&valid_key_idx) > 0){\
       bit_foreach(i,valid_key_idx){\
          sprintf(buf,"%ld",kv1[i].value);\
          MD5_Update(&c[1], buf, strlen(buf));\
        }\
    }\
    shift_keys(kv0,8,8);\
    shift_keys(kv1,8,8);\
  }while(_loop_cnt--);\
  \
  MD5_Final(out0, &c[0]);\
  MD5_Final(out1, &c[1]);\
  judge_match_result(if_fn0,if_fn1,out0,out1,ret);\
\
}while(0)

int test_md5()
{
  MD5_CTX c[32];
  unsigned char out[MD5_DIGEST_LENGTH];
  char buf[256];
  int i,n;
  int bytes = 4;
  for(i=0;i<32;i++)
     MD5_Init(&c[i]);
  

    // while(bytes > 0)
    // {
    for(i=0;i<32;i++){
      sprintf(buf,"%d",i);
      MD5_Update(&c[i], "abcd", bytes);
      MD5_Update(&c[i], buf, 2);

    }
     
        // bytes=read(STDIN_FILENO, buf, 512);
    // }
    for(i=0;i<32;i++){
      MD5_Final(out, &c[i]);
      printf("--item[%0.2d]:",i);
      for(n=0; n<MD5_DIGEST_LENGTH; n++)
          printf("%02x", out[n]);
      printf("\n");
    }


  return 0;
}
//
// 3/23/2022 todo: create new interface to test the consistency on searching results.
// #define perf_consistency_test() 

int main(int argc,char *argv[])
{

  BVT (clib_bihash_kv) kv;
  int i, j;

  // u64 nbuckets = 32000;
  u64 cycles[32];
  u64 options[32];
  BVT (clib_bihash_kv) kv0_8[32];
  BVT (clib_bihash_kv) kv1_8[32];
  BVT (clib_bihash_kv) kv2_8[32];
  BVT (clib_bihash_kv) kv3_8[32];
  BVT (clib_bihash_kv) kv4_8[32];
  BVT (clib_bihash_kv) kv5_8[32];
  BVT (clib_bihash) * h;
  u32 user_buckets = 1228800;
  u32 user_memory_size = 209715200;

  BVT (clib_bihash) hash={0};
  // BVT (clib_bihash) hash2={0};
  h = &hash;
  // clib_mem_init_with_page_size (1ULL << 30, CLIB_MEM_PAGE_SZ_1G);
  clib_mem_init (0, 1ULL << 30);

  BV (clib_bihash_init) (h, "test", user_buckets, user_memory_size);
  // BV (clib_bihash_init) (&hash2, "test", user_buckets, user_memory_size);
  
  for(i=0;i<32;i++){
    cycles[i] = 0;
    options[i] = 1;
  }
  int is_which ;

#if 0
  #define LOOP_CNT  (12374)
   for (j = 0; j < 100; j++)
    {
      for (i = 1; i <= j * 1000 + 1; i++)
      {
        kv.key = i;
        kv.value = i+1+0x7FFFFFFFFFFF;

        BV (clib_bihash_add_del) (h, &kv, 1 /* is_add */ );
      }    
    }
#endif


  // 
#if 1
  #define LOOP_CNT  (1e6)
   for (j = 0; j < LOOP_CNT; j++)
    {
     
        kv.key = j;
        kv.value = j;

        BV (clib_bihash_add_del) (h, &kv, 1 /* is_add */ );
        // BV (clib_bihash_add_del) (&hash2, &kv, 1 /* is_add */ );
        
    }
#endif

#if 0

  // #define LOOP_CNT  (1)
  // #define LOOP_CNT  (2e7)
#endif


  // u8 key_mask = 8;
  // u8 valid_key_idx = 0;



  clib_bitmap_t *bm = NULL ;
  bm = clib_bitmap_set (bm, 0, 1);
  // u64 loop_cnt = LOOP_CNT;

  kv.key = 100;
  for(i=0;i<32;i++){
    kv0_8[i].key = kv.key;
    kv1_8[i].key = kv.key;
    kv2_8[i].key = kv.key;
    kv3_8[i].key = kv.key;
    kv4_8[i].key = kv.key;
    kv5_8[i].key = kv.key;

    kv0_8[i].value = 0;
    kv2_8[i].value = 0;
    kv3_8[i].value = 0;
    kv4_8[i].value = 0;
    
  }

#if 0
// the key is out the table, so it will failed on searching.
  kv0_8[3].key = 1000*1000;
  kv2_8[3].key = 1000*1000;
  kv3_8[3].key = 1000*1000;
  kv4_8[3].key = 1000*1000;
#endif

#if 0
  add_collisions(h);
  kv.key       = 0x69c603;
  kv0_8[3].key = 0x69c603;
  kv1_8[3].key = 0x69c603;
  kv2_8[3].key = 0x69c603;
  kv3_8[3].key = 0x69c603;
  kv4_8[3].key = 0x69c603;
#endif

  // return generate_hash_by_key();
  // return tune_bihash_v4_search(h); // debug

//   clib_bihash_search_batch_v5(h,kv0_8,8,kv0_8);
// for(i=0;i<8;i++){
     
//       fformat (stdout, "kv0_8[%d].key:0x%lx,kv0_8[%d].value:0x%lx \n",i,kv0_8[i].key,i,kv0_8[i].value);
//             // fformat (stdout, "kv0_8[%d].key:0x%lx,kv0_8[%d].value:0x%lx \n",i,kv.key,i,kv.value);

    
//     }

    // return 0;
#if 0
  u64 start;
  u8 valid_key_idx = 0;
  u64 loop_cnt = LOOP_CNT;  
  options[0] = 0;
  start = clib_cpu_time_now();
  while(loop_cnt--){

    
    for(i=0;i<8;i++){ 
      // if (BV (clib_bihash_search) (h, &kv0_8[i], &kv0_8[i]) < 0){
       kv.key++;   
      if (BV (clib_bihash_search) (h, &kv, &kv) < 0){
      }
   

      options[0]++;
    }
 
  }
  cycles[0] = clib_cpu_time_now() - start ;  
  fformat (stdout,"---[item0]---searching 1 elments V0 valid_key_idx:%d--- options:%ld,cycles:%ld\n",
      valid_key_idx,options[0],cycles[0]); 
 
    for(i=0;i<8;i++){
     
      // fformat (stdout, "kv0_8[%d].key:0x%lx,kv0_8[%d].value:0x%lx \n",i,kv0_8[i].key,i,kv0_8[i].value);
      fformat (stdout, "kv0_8[%d].key:0x%lx,kv0_8[%d].value:0x%lx \n",i,kv.key,i,kv.value);

    
    }
    
#endif

    #if 0


  //statistic start
  loop_cnt = LOOP_CNT;
  options[1] = 0;
  start = clib_cpu_time_now();
  while(loop_cnt--){

    shift_keys(kv1_8,8,8);


    if (BV (clib_bihash_search_batch_v1) (h, kv1_8, key_mask, kv1_8, &valid_key_idx) < 0){
        //warning...
            // clib_warning("[%d] search for key %lld failed unexpectedly\n", 0,kv2_8[7].key);
            
    }else{
      
    }
    options[1]+=8;

  }
  cycles[1] = clib_cpu_time_now() - start ;  
  fformat (stdout,"---[item1]---searching 1 elments V1 valid_key_idx:%d---options:%ld, cycles:%ld\n",
      valid_key_idx,options[1],cycles[1]);
  //statistic end

    for(i=0;i<8;i++){

    fformat (stdout, "kv1_8[%d].key:0x%lx,kv1_8[%d].value:0x%lx \n",
      i,kv1_8[i].key,i,kv1_8[i].value);
 
  }
  
  
  #endif

  #if 0
  //statistic start
  loop_cnt = LOOP_CNT;
  options[2] = 0;
  start = clib_cpu_time_now();
  while(loop_cnt--){

    shift_keys(kv2_8,8,8);

    if (BV (clib_bihash_search_batch_v2) (h, kv2_8, key_mask, kv2_8, &valid_key_idx) < 0){
        //warning...
            // clib_warning("[%d] search for key %lld failed unexpectedly\n", 0,kv2_8[7].key);
            
    }else{
      
    }
    options[2]+=8;

  }
  cycles[2] = clib_cpu_time_now() - start ;  
  fformat (stdout,"---[item2]---searching 1 elments V2 valid_key_idx:%d---options:%ld, cycles:%ld\n",
      valid_key_idx,options[2],cycles[2]);
  //statistic end

    for(i=0;i<8;i++){

    fformat (stdout, "kv2_8[%d].key:0x%lx,kv2_8[%d].value:0x%lx \n",
      i,kv2_8[i].key,i,kv2_8[i].value);
 
  }


  #endif
  is_which = 0xFF;
  int is_consistency = 0xFF;
  if(argc > 1){
    is_which = atoi(argv[1]);
  }

  
  // test_md5();
  if(is_which == 0xFF){
    fformat (stdout,"perf_test[ALL]...\n");
      perf_test_0(0,0,LOOP_CNT,options[0],cycles[0],NULL,h,kv,kv);
      perf_test_1(1,1,LOOP_CNT,options[1],cycles[1],BV (clib_bihash_search_batch_v1),h,kv1_8,kv1_8);
      perf_test_1(2,2,LOOP_CNT,options[2],cycles[2],BV (clib_bihash_search_batch_v2),h,kv2_8,kv2_8);
      // perf_test_1(3,3,LOOP_CNT,options[3],cycles[3],BV (clib_bihash_search_batch_v3),h,kv3_8,kv3_8);
      perf_test_1(4,4,LOOP_CNT,options[4],cycles[4],BV (clib_bihash_search_batch_v4),h,kv4_8,kv4_8);
      perf_test_2(5,5,LOOP_CNT,options[5],cycles[5],NULL,h,kv5_8,kv5_8);
  }else if(is_which == 0x0 ){
      fformat (stdout,"perf_test[0]...\n");
      perf_test_0(0,0,LOOP_CNT,options[0],cycles[0],NULL,h,kv,kv);
  }else if(is_which == 0x1){
      fformat (stdout,"perf_test[1]...\n");
      perf_test_1(1,1,LOOP_CNT,options[1],cycles[1],BV (clib_bihash_search_batch_v1),h,kv1_8,kv1_8);
  }else if(is_which == 0x2){
      fformat (stdout,"perf_test[2]...\n");
      perf_test_1(2,2,LOOP_CNT,options[2],cycles[2],BV (clib_bihash_search_batch_v2),h,kv2_8,kv2_8);
  } else if(is_which == 0x3){
      fformat (stdout,"perf_test[3]...\n");
      // perf_test_1(3,3,LOOP_CNT,options[3],cycles[3],BV (clib_bihash_search_batch_v3),h,kv3_8,kv3_8);
  }else if(is_which == 0x4){
      fformat (stdout,"perf_test[4]...\n");
      perf_test_1(4,4,LOOP_CNT,options[4],cycles[4],BV (clib_bihash_search_batch_v4),h,kv4_8,kv4_8);
  }else if(is_which == 0x5){
      fformat (stdout,"perf_test[5]...\n");
      perf_test_2(5,5,LOOP_CNT,options[5],cycles[5],NULL,h,kv5_8,kv5_8);
  }

  if(argc >2){
    is_consistency = atoi(argv[2]);
  }
 
  if(is_consistency == 0xFF){
      fformat (stdout,"consistency_test[ALL]...\n");
      consistency_test_0( 0,
                          4,
                          LOOP_CNT,
                          h,
                          kv,
                          kv4_8,
                          BV (clib_bihash_search),
                          BV (clib_bihash_search_batch_v4));
      consistency_test_1( 1,
                          1,4,
                          LOOP_CNT,
                          h,
                          kv1_8,kv4_8,
                          BV (clib_bihash_search_batch_v1),
                          BV (clib_bihash_search_batch_v4));
  }else if(is_consistency == 0){
      fformat (stdout,"consistency_test[0]...\n");
      consistency_test_0( 0,
                          4,
                          LOOP_CNT,
                          h,
                          kv,
                          kv4_8,
                          BV (clib_bihash_search),
                          BV (clib_bihash_search_batch_v4));
  }else if(is_consistency == 1){
      fformat (stdout,"consistency_test[1]...\n");
      consistency_test_1( 1,
                          1,4,
                          LOOP_CNT,
                          h,
                          kv1_8,kv4_8,
                          BV (clib_bihash_search_batch_v1),
                          BV (clib_bihash_search_batch_v4));
  }
  



 #if 0
  loop_cnt = LOOP_CNT;
  options[3] = 0;
  start = clib_cpu_time_now();
  while(loop_cnt--){

    shift_keys(kv3_8,8,8);

    if (BV (clib_bihash_search_batch_v3) (h, kv3_8, key_mask, kv3_8, bm) < 0){
        //warning...
            clib_warning
          ("[%d] search for key %lld failed unexpectedly\n", 0,
            kv3_8[7].key);
    }else{
       
    }
    options[3]+=8;
  }
  cycles[3] = clib_cpu_time_now() - start ;  
  // fformat (stdout, "set Bitmap..%U \n",format_bitmap_hex,bm);
  // clib_bitmap_set (bm,7,1);
  // uword ei;
  // clib_bitmap_foreach (ei, bm)  {
  //   if (clib_bitmap_get(bm, ei)) {
  //     fformat (stdout, "kv3_8[%d].key:0x%x,kv3_8[%d].value:0x%lx \n",
  //       ei,kv3_8[ei].key,ei,kv3_8[ei].value);
  //   }
  // }
  
  fformat (stdout,"---[item3]---searching 2 elments V3 fnd_cnts:%d---options:%ld, cycles:%ld\n",
    valid_key_idx,options[3],cycles[3]);
#endif

#if 0
//statistic start
  loop_cnt = LOOP_CNT;
  options[4] = 0;
  key_mask = 0xFF;
  start = clib_cpu_time_now();
  while(loop_cnt--){

    shift_keys(kv4_8,8,8);

    if (BV (clib_bihash_search_batch_v4) (h, kv4_8, key_mask, kv4_8, &valid_key_idx) < 0){
        //warning...
    }
    options[4]+=8;
  }
  cycles[4] = clib_cpu_time_now() - start ;  
  fformat (stdout,"---[item4]---searching 4 elments V4 valid_key_idx:0x%x---options:%ld, cycles:%ld\n",
      valid_key_idx,options[4],cycles[4]);
  //statistic end
    for(i=0;i<8;i++){
      if(valid_key_idx & (1<<i))
      fformat (stdout, "kv4_8[%d].key:0x%lx,kv4_8[%d].value:0x%lx \n",
        i,kv4_8[i].key,i,kv4_8[i].value);
      else
      fformat (stdout, "kv4_8[%d].key:0x%lx,kv4_8[%d].value:N/A \n",
        i,kv4_8[i].key,i);
    }
  #endif

  #if 0
  //statistic start
  loop_cnt = LOOP_CNT;
  options[5] = 0;
  start = clib_cpu_time_now();
  while(loop_cnt--){

      shift_keys(kv5_8,8,8);

      clib_bihash_search_batch_v5(h,kv5_8,8,kv5_8);
      // clib_bihash_search_batch_v5(&hash2,kv5_8,8,kv5_8);
      options[5]+=8;
  }
  cycles[5] = clib_cpu_time_now() - start ;  
  fformat (stdout,"---[item5]---searching 1 elments V5 valid_key_idx:%d--- options:%ld,cycles:%ld\n",
      valid_key_idx,options[5],cycles[5]); 
 
    for(i=0;i<8;i++){
      fformat (stdout, "kv5_8[%d].key:0x%lx,kv5_8[%d].value:0x%lx \n",
        i,kv5_8[i].key,i,kv5_8[i].value);
    }
    
#endif

  
  fformat(stdout,"Summary:@%ld options \n\t"
                  "[items]----|Cycles/options| \n\t"
                  "[item0]:     %d \n\t"
                  "[item1]:     %d \n\t"
                  "[item2]:     %d \n\t"
                  "[item3]:     %d \n\t"
                  "[item4]:     %d \n\t"
                  "[item5]:     %d \n",
             options[0],
             cycles[0]/options[0],cycles[1]/options[1],
             cycles[2]/options[2],cycles[3]/options[3],
             cycles[4]/options[4],cycles[5]/options[5]);

  #define inc_per(b,t) ( (b)>0 ? (t)*100/(b): 0)

  f64 base = cycles[0]/options[0];
  fformat(stdout,"perf percentage:\t\n "
                  "---|V0 as the benchmark:%0.2f| "
                  "[item1]:%.2f%% |"
                  "[item2]:%.2f%% |"
                  "[item3]:%.2f%% |"
                  "[item4]:%.2f%% |"
                  "[item5]:%.2f%%  \n",
            base,
            inc_per(base,cycles[1]/options[1]),
            inc_per(base,cycles[2]/options[2]),
            inc_per(base,cycles[3]/options[3]),
            inc_per(base,cycles[4]/options[4]),
            inc_per(base,cycles[5]/options[5])
            );


  return 0;
}

int generate_hash_by_key()
{
  BVT (clib_bihash_kv) kv;
   //generate hash list from keys

    int i;
    u64 key;
    u64 hash;
    u64 mask;
    mask = 0x1fffff;
    u64 nitems;
    FILE *fp;
    char filename[32]="hash_list_s.txt";
    fp = fopen(filename,"w");
    fprintf(fp,"index|key|hash|mask|search_key\n");

    nitems = 1<<25;
    for(i=0;i<nitems;i++){
      // key = i+100;
      key = 0x1e7dc13 +i;
      kv.key = key;
      hash = BV(clib_bihash_hash)(&kv);
      // fformat (stdout,"key:0x%.4x,hash:0x%.8x,mask:0x%x,search:0x%.6x\n",key,hash,mask,mask&hash);
      if( (mask&hash) == 0x1d2cc6){
          printf("%d|0x%lx|0x%lx|0x%lx|0x%lx\n",i,key,hash,mask,mask&hash);
      }

      fprintf(fp,"%d|0x%lx|0x%lx|0x%lx|0x%lx\n",i,key,hash,mask,mask&hash);

    }
   
    fclose(fp);
  return 0;
}

/**
 * 
 * index|key|hash|mask|search_key
 * --------------------------------------------------
 * 0|0x64|0x361d2cc6|0x1fffff|0x1d2cc6
 * 2687876|0x2903e8|0xd3fd2cc6|0x1fffff|0x1d2cc6
 * 4244779|0x40c58f|0xc05d2cc6|0x1fffff|0x1d2cc6
 * 6931871|0x69c603|0x25bd2cc6|0x1fffff|0x1d2cc6
 * 9652445|0x934941|0xe71d2cc6|0x1fffff|0x1d2cc6
 * 12208745|0xba4acd|0x2fd2cc6|0x1fffff|0x1d2cc6
 * 13864006|0xd38caa|0x115d2cc6|0x1fffff|0x1d2cc6
 * 16420546|0xfa8f26|0xf4bd2cc6|0x1fffff|0x1d2cc6
 * 18698989|0x11d5351|0x4c7d2cc6|0x1fffff|0x1d2cc6
 * 20205689|0x13450dd|0xa99d2cc6|0x1fffff|0x1d2cc6
 * 22910550|0x15d96ba|0xba3d2cc6|0x1fffff|0x1d2cc6
 * 24417490|0x1749536|0x5fdd2cc6|0x1fffff|0x1d2cc6
 * 26090000|0x18e1a74|0x9d7d2cc6|0x1fffff|0x1d2cc6
 * 27728276|0x1a719f8|0x789d2cc6|0x1fffff|0x1d2cc6
 * 30334779|0x1cedf9f|0x6b3d2cc6|0x1fffff|0x1d2cc6
 * 31972271|0x1e7dc13|0x8edd2cc6|0x1fffff|0x1d2cc6
 * 
 * 2869615|0x213a582|0x273d2cc6|0x1fffff|0x1d2cc6
 * 5425659|0x23aa60e|0xc2dd2cc6|0x1fffff|0x1d2cc6
 * 7046230|0x2536069|0xd17d2cc6|0x1fffff|0x1d2cc6
 * 9603026|0x27a63e5|0x349d2cc6|0x1fffff|0x1d2cc6
 * 10031252|0x280eca7|0xf63d2cc6|0x1fffff|0x1d2cc6
 * 12718872|0x2a9ef2b|0x13dd2cc6|0x1fffff|0x1d2cc6
 * 14175545|0x2c0294c|0x7d2cc6|0x1fffff|0x1d2cc6
 * 16862893|0x2e92ac0|0xe59d2cc6|0x1fffff|0x1d2cc6
 * 19339940|0x30ef6b7|0x5d5d2cc6|0x1fffff|0x1d2cc6
 * 20977960|0x327f53b|0xb8bd2cc6|0x1fffff|0x1d2cc6
 * 23484233|0x34e335c|0xab1d2cc6|0x1fffff|0x1d2cc6
 * 25121981|0x36730d0|0x4efd2cc6|0x1fffff|0x1d2cc6
 * 28697471|0x39dbf92|0x8c5d2cc6|0x1fffff|0x1d2cc6
 * 30203915|0x3b4bc1e|0x69bd2cc6|0x1fffff|0x1d2cc6
 * 32874086|0x3dd7a79|0x7a1d2cc6|0x1fffff|0x1d2cc6
 * 
 * 
 * 
 * 
 */
int add_collisions( BVT (clib_bihash) * h)
{
    BVT (clib_bihash_kv) kv;
    u64 key_table[]={
          0x64,0x2903e8,0x40c58f,0x69c603,          // cnt=4,log2_page = 0 on BIHASH_KVP_PER_PAGE=4
          0x934941,0xba4acd,0xd38caa,0xfa8f26,     //  cnt=8,log2_page = 1 on BIHASH_KVP_PER_PAGE=4
          0x11d5351,0x13450dd,0x15d96ba,0x1749536,  // cnt=12,log2_page = 2 on BIHASH_KVP_PER_PAGE=4
          0x18e1a74,0x1a719f8,0x1cedf9f,0x1e7dc13,  // cnt=16,log2_page = 2 on BIHASH_KVP_PER_PAGE=4
          0x213a582,0x23aa60e,0x2536069,0x27a63e5}; // cnt=20,log2_page = 3 on BIHASH_KVP_PER_PAGE=4
    int cnt = sizeof(key_table)/sizeof(key_table[0]);
    int i;
  
    /*
    *  
    *  Add more than BIHASH_KVP_PER_PAGE elements to make split_rehash happen.
    *  but it's hardly to emit enough keys to entry split_rehash_liear, 
    *  it depend on two failures of split_rehash, what mean the keys after splited,and 
    *  rehash by the address of V, should hit the same KVS over than BIHASH_KVP_PER_PAGE times.
    *  so, it could be happen in theory, hard to find the key.
    */
    for(i=0;i<cnt;i++){
      kv.key = key_table[i];
      kv.value =  key_table[i]+0x7FFFFFFFFFFF;

      BV (clib_bihash_add_del) (h, &kv, 1 /* is_add */ );
    }
   

    return 0;
}


int BV (clib_bihash_foreach_key_value_pair_cb1) (BVT (clib_bihash_kv) *kv,
						     void *arg)
{

  fformat (stdout,"key:0x%lx,value:0x%lx\n",kv->key,kv->value);
  // if(kv->key == 0x1e7dc13)
  //   return BIHASH_WALK_STOP;
  return BIHASH_WALK_CONTINUE;//BIHASH_WALK_CONTINUE or BIHASH_WALK_STOP

}


int tune_bihash_v4_search(BVT (clib_bihash) * h)
{

  int loop_cnt;
  BVT (clib_bihash_kv) kv4_8[32];
  u8 key_mask;
  u8 valid_key_idx;
  u64 start,cycles;
  int i;
  u64 options;

  loop_cnt = LOOP_CNT;
  options = 0;
  key_mask = 0xFF;

  for(i=0;i<32;i++){
    kv4_8[i].key = 100+i;
    kv4_8[i].value = 0;
  }

  kv4_8[3].key = 0x69c603;

  start = clib_cpu_time_now();
  while(loop_cnt--){
    if (BV (clib_bihash_search_batch_v4) (h, kv4_8, key_mask, kv4_8, &valid_key_idx) < 0){
        //warning...
            clib_warning
          ("[%d] search for key %lld failed unexpectedly\n", 0,
            kv4_8[7].key);
            
    }else{
      options+=8;
    }

  }
  cycles = clib_cpu_time_now() - start ;  
  fformat (stdout,"---[item3]---searching 4 elments V4 valid_key_idx:0x%x---options:%ld, cycles:%ld\n",
      valid_key_idx,options,cycles);
  //statistic end
    for(i=0;i<8;i++){
      if(valid_key_idx & (1<<i))
      fformat (stdout, "kv4_8[%d].key:0x%lx,kv4_8[%d].value:0x%lx \n",
        i,kv4_8[i].key,i,kv4_8[i].value);
      else
      fformat (stdout, "kv4_8[%d].key:0x%lx,kv4_8[%d].value:N/A \n",
        i,kv4_8[i].key,i);
    }

  return 0;

    BV(clib_bihash_foreach_key_value_pair)(h,BV (clib_bihash_foreach_key_value_pair_cb1),NULL);
    return 0;

}

