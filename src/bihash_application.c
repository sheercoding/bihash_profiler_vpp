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
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <openssl/md5.h>

#define BIHASH_USING_8_8_STATS  (0)

#if BIHASH_USING_8_8_STATS
#include <vppinfra/bihash_8_8_stats.h> //#defined BIHASH_ENABLE_STATS
#else 
#include <vppinfra/bihash_8_8.h>
#endif

#include <vppinfra/bihash_template.h>

#include <vppinfra/bihash_template.c>


#if BIHASH_ENABLE_STATS
typedef struct
{
  u64 alloc_add;
  u64 add;
  u64 split_add;
  u64 replace;
  u64 update;
  u64 del;
  u64 del_free;
  u64 linear;
  u64 resplit;
  u64 working_copy_lost;
  u64 *splits;
} bihash_stats_t;

bihash_stats_t stats;

u8 * format_bihash_stats (u8 * s, va_list * args)
{
  BVT (clib_bihash) * h = va_arg (*args, BVT (clib_bihash) *);
  int verbose = va_arg (*args, int);
  int i;
  bihash_stats_t *sp = h->inc_stats_context;

#define _(a) s = format (s, "%20s: %lld\n", #a, sp->a);
  foreach_bihash_stat;
#undef _
  for (i = 0; i < vec_len (sp->splits); i++)
    {
      if (sp->splits[i] > 0 || verbose)
	s = format (s, "    splits[%d]: %lld\n", 1 << i, sp->splits[i]);
    }
  return s;
}

static void
inc_stats_callback (BVT (clib_bihash) * h, int stat_id, u64 count)
{
  uword *statp = h->inc_stats_context;
  bihash_stats_t *for_splits;

  if (PREDICT_TRUE (stat_id * sizeof (u64)
		    < STRUCT_OFFSET_OF (bihash_stats_t, splits)))
    {
      statp[stat_id] += count;
      return;
    }

  for_splits = h->inc_stats_context;
  vec_validate (for_splits->splits, count);
  for_splits->splits[count] += 1;
}

#endif


int add_collisions( BVT (clib_bihash) * h);

#define USER_BIT_SET(a,b) ((a) |= (1ULL<<(b)))

#define bihash_search_batch_v5(h,kvs,kv_sz,valuep) \
do{\
    int i;\
    for(i=0;i<kv_sz;i++){\
      if (BV (clib_bihash_search) (h, &kvs[i], &valuep[i]) < 0){}\
    }\
    \
}while(0)

#define bihash_search_batch_v5_with_couter(h,kvs,kv_sz,valuep,cnt,bitmap) \
do{\
    int i;\
    for(i=0;i<kv_sz;i++){\
      if (BV (clib_bihash_search) (h, &kvs[i], &valuep[i]) < 0){\
      }else{\
        USER_BIT_SET(bitmap,i);\
        cnt++;\
      }\
    }\
    \
}while(0)

int BV (clib_bihash_search_batch_v5)
  (BVT (clib_bihash) * h,
   BVT (clib_bihash_kv) * search_key, u8 key_mask,
   BVT (clib_bihash_kv) * valuep,u8 * valid_key_idx)
{
  int ret = 0;
  int kvs_cnt = _mm_popcnt_u32 (key_mask);
  u8 bitmap=0;
  bihash_search_batch_v5_with_couter(h,search_key,kvs_cnt,search_key,ret,bitmap);
  *valid_key_idx = bitmap;
  return ret;
}

int BV (clib_bihash_search_batch_v5x32)
  (BVT (clib_bihash) * h,
   BVT (clib_bihash_kv) * search_key, u32 key_mask,
   BVT (clib_bihash_kv) * valuep,u32 * valid_key_idx)
{
  int ret = 0;
  int kvs_cnt = _mm_popcnt_u32 (key_mask);
  int i;
  u32 bitmap=0;
  for(i=0;i<kvs_cnt;i++){
    if (BV (clib_bihash_search) (h, &search_key[i], &valuep[i]) < 0){
    }else{
      USER_BIT_SET(bitmap,i);
      ret++;
    }
  }
  
  *valid_key_idx = bitmap;
  return ret;
}

int BV (clib_bihash_search_batch_v4)
  (BVT (clib_bihash) * h,
   BVT (clib_bihash_kv) * search_key, u8 key_mask,
   BVT (clib_bihash_kv) * valuep,u8 * valid_key_idx)
{
  return BV (clib_bihash_search_inline_2_batch)(h,search_key,key_mask,valuep,valid_key_idx);
}

#define reset_keys(kvs,kv_sz,key_val) \
do{\
    int i;\
    for(i=0;i<kv_sz;i++){ \
        kvs[i].key = key_val+i; \
    }\
}while(0)

#define reset_one_key(kv,key_val) \
do{\
   kv.key = key_val;\
}while(0)

#define insert_key_to_kvs(kvs,pos,newkey) \
do{\
  int kvs_sz;\
  kvs_sz = sizeof(kvs)/sizeof(kvs[0]);\
    if(pos < kvs_sz){\
       kvs[pos].key = newkey; \
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
    kv.key += shift_nm;\
}while(0)

#define random_keys(kvs,kv_sz,ops) \
do{\
    int i;\
    if(ops>0) srandom(ops);\
    for(i=0;i<kv_sz;i++){\
        kvs[i].key = random(); \
        /* fformat(stdout,"key:%x \n",kvs[i].key); */ \
    }\
}while(0)

#define random_one_key(kv,ops) \
do{\
  if(ops>0) srandom(ops);\
    kv.key = random();\
}while(0)

#else
#define shift_keys(kvs,kv_sz,shift_nm) 
#define shift_one_key(kv,shift_nm)
#endif

#define statistic_perf(test_no,if_no,num_of_elm,options,cycles) \
do{\
  char *prt_format ="---[item%d]|API V%d|---Dec:searching %d elments---"\
                    "|Cycles/Option:%d|cycles:%ld|options:%ld\n"; \
\
  fformat (stdout,prt_format, \
      test_no,\
      if_no,\
      num_of_elm,\
      cycles/options,\
      cycles,\
      options \
      ); \
}while(0)

#define perf_test_0_random(test_no,if_no,loops_num,options,cycles,if_fn,h,kv,result) \
do{ \
  u64 _elm_cnt = loops_num/8;\
  u64 num_of_elm = loops_num;\
  options = 0;\
  u64 start ; \
  reset_one_key(kv,0); \
  start = clib_cpu_time_now();\
  do{\
\
    for(i=0;i<8;i++){\
      \
      if (BV (clib_bihash_search) (h, &kv, &kv) < 0){\
      }\
      random_one_key(kv,0);\
    }\
    options+=8 ;\
\
  }while(--_elm_cnt);\
  cycles = clib_cpu_time_now() - start ;  \
  statistic_perf(test_no,if_no,num_of_elm,options,cycles);\
}while(0)

#define perf_test_once_vars(test_no,if_no,loops_num,options,cycles,elm_cnt_once,key_ops_step,ops_flag,if_fn,h,kv,result) \
do{ \
  u64 _elm_cnt = loops_num/elm_cnt_once;\
  u64 div_cnt = loops_num%elm_cnt_once;\
  u64 num_of_elm = loops_num;\
  options = 0;\
  u64 start ; \
  reset_one_key(kv,0); \
  start = clib_cpu_time_now();\
  do{\
\
    for(i=0;i<elm_cnt_once;i++){\
      \
      if (BV (clib_bihash_search) (h, &kv, &kv) < 0){\
      }\
      key_ops_step(kv,ops_flag);\
    }\
    options += elm_cnt_once ;\
\
  }while(--_elm_cnt);\
  if(div_cnt){\
\
  for(i=0;i<div_cnt;i++){\
      \
      if (BV (clib_bihash_search) (h, &kv, &kv) < 0){\
      }\
      key_ops_step(kv,ops_flag);\
    }\
    options += div_cnt ;\
  }\
  cycles = clib_cpu_time_now() - start ;  \
  statistic_perf(test_no,if_no,num_of_elm,options,cycles);\
}while(0)


#define perf_test_0_linear(test_no,if_no,loops_num,options,cycles,if_fn,h,kv,result) \
do{\
  perf_test_once_vars(test_no,if_no,loops_num,options,cycles,8,shift_one_key,1,if_fn,h,kv,result);\
}while(0)

#define perf_test_0_linear_x16(test_no,if_no,loops_num,options,cycles,if_fn,h,kv,result) \
do{\
  perf_test_once_vars(test_no,if_no,loops_num,options,cycles,16,shift_one_key,1,if_fn,h,kv,result);\
}while(0)

#if 0
#define perf_test_0_random(test_no,if_no,loops_num,options,cycles,if_fn,h,kv,result) \
do{\
  perf_test_once_vars(test_no,if_no,loops_num,options,cycles,8,random_one_key,0,if_fn,h,kv,result);\
}while(0)
#endif

#define perf_test_0_random_x16(test_no,if_no,loops_num,options,cycles,if_fn,h,kv,result) \
do{\
  perf_test_once_vars(test_no,if_no,loops_num,options,cycles,16,random_one_key,0,if_fn,h,kv,result);\
}while(0)


#define perf_test_1_linear(test_no,if_no,loops_num,options,cycles,if_fn,h,kv,result) \
do{ \
  u64 _elm_cnt = loops_num/8;\
  u64 num_of_elm = loops_num;\
  options = 0;\
  u64 start; \
  u8 key_mask = 0xFF;\
  u8 valid_key_idx = 0; \
  reset_keys(kv,8,0);\
  start = clib_cpu_time_now();\
  do{\
\
    if (if_fn(h, kv, key_mask,result,&valid_key_idx) < 0){\
    }\
    shift_keys(kv,8,8);\
    if(is_which_profile == 59)insert_key_to_kvs(kv,3,1e6+1000);\
    options+=8; \
\
  }while(--_elm_cnt);\
  cycles = clib_cpu_time_now() - start ;  \
  statistic_perf(test_no,if_no,num_of_elm,options,cycles);\
}while(0)

#define perf_test_batch_vars(test_no,if_no,loops_num,options,cycles,elm_cnt_once,key_ops_step,ops_flag,if_fn,h,kv,result) \
do{ \
  u64 _elm_cnt = loops_num/elm_cnt_once;\
  u64 div_cnt = loops_num%elm_cnt_once; \
  u64 ngrp = elm_cnt_once/8; \
  int i; \
  u64 num_of_elm = loops_num;\
  options = 0;\
  u64 start; \
  u8 key_mask = 0xFF;\
  u8 valid_key_idx = 0; \
  reset_keys(kv,elm_cnt_once,0);\
  start = clib_cpu_time_now();\
  do{\
\
  for(i=0;i<ngrp;i++){\
  if (if_fn(h, kv+8*i, key_mask,result+8*i,&valid_key_idx) < 0){\
      }\
  }\
    key_ops_step(kv,elm_cnt_once,ops_flag);\
    if(is_which_profile == 59)insert_key_to_kvs(kv,3,1e6+1000);\
    options+=elm_cnt_once; \
\
  }while(--_elm_cnt);\
  if(div_cnt){\
  \
    for(i=0;i<div_cnt;i++){\
      \
      if (BV (clib_bihash_search) (h, &kv[0], &kv[0]) < 0){\
      }\
      kv[0].key+=1;\
    }\
    options+=div_cnt ;\
  }\
  cycles = clib_cpu_time_now() - start ;  \
  statistic_perf(test_no,if_no,num_of_elm,options,cycles);\
}while(0)

#define perf_test_1_linear_x16(test_no,if_no,loops_num,options,cycles,if_fn,h,kv,result) \
do{\
perf_test_batch_vars(test_no,if_no,loops_num,options,cycles,16,shift_keys,16,if_fn,h,kv,result);\
\
}while(0)

#define perf_test_1_random_x16(test_no,if_no,loops_num,options,cycles,if_fn,h,kv,result) \
do{\
perf_test_batch_vars(test_no,if_no,loops_num,options,cycles,16,random_keys,0,if_fn,h,kv,result);\
\
}while(0)

#define perf_test_1_random_x32(test_no,if_no,loops_num,options,cycles,if_fn,h,kv,result) \
do{\
perf_test_batch_vars(test_no,if_no,loops_num,options,cycles,32,random_keys,0,if_fn,h,kv,result);\
\
}while(0)

#define perf_test_1_random_x64(test_no,if_no,loops_num,options,cycles,if_fn,h,kv,result) \
do{\
perf_test_batch_vars(test_no,if_no,loops_num,options,cycles,64,random_keys,0,if_fn,h,kv,result);\
\
}while(0)

#define perf_test_1_random(test_no,if_no,loops_num,options,cycles,if_fn,h,kv,result) \
do{ \
  u64 _elm_cnt = loops_num/8;\
  /* u64 div_cnt = loops_num%8 */ ; \
  u64 num_of_elm = loops_num;\
  options = 0;\
  u64 start; \
  u8 key_mask = 0xFF;\
  u8 valid_key_idx = 0; \
  reset_keys(kv,8,0);\
  start = clib_cpu_time_now();\
  do{\
\
    if (if_fn(h, kv, key_mask,result,&valid_key_idx) < 0){\
    }\
    random_keys(kv,8,0);\
    if(is_which_profile == 59)insert_key_to_kvs(kv,3,1e6+1000);\
    options+=8; \
\
  }while(--_elm_cnt);\
  cycles = clib_cpu_time_now() - start ;  \
  statistic_perf(test_no,if_no,num_of_elm,options,cycles);\
}while(0)

#define perf_test_2(test_no,if_no,loops_num,options,cycles,if_fn,h,kv,result) \
do{ \
  u64 _elm_cnt = loops_num/8;\
  u64 num_of_elm = loops_num;\
  options = 0;\
  u64 start; \
  reset_keys(kv,8,0);\
  start = clib_cpu_time_now();\
  \
    do{\
\
    bihash_search_batch_v5(h,kv,8,kv);\
    shift_keys(kv,8,8);\
    if(is_which_profile == 59)insert_key_to_kvs(kv,3,1e6+1000);\
    options+=8; \
\
  }while(--_elm_cnt);\
  \
  cycles = clib_cpu_time_now() - start ;  \
  statistic_perf(test_no,if_no,num_of_elm,options,cycles);\
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
    fformat (stdout,#if_fn0"|-> MATCH <-|"#if_fn1" ---[PASS]\n",if_fn0,if_fn1);\
  }else{\
    fformat (stdout,#if_fn0"|-> MATCH <-|"#if_fn1" ---[FAILED]\n",if_fn0,if_fn1);\
    dump_md5(if_fn0,out0);dump_md5(if_fn1,out1);\
  }\
}while(0)

#define consistency_test_0(test_no,if_no1,loops_num,h,kv0,kv1,if_fn0,if_fn1) \
do{\
  u64 _elm_cnt = loops_num;\
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
  }while(--_elm_cnt);\
  \
  MD5_Final(out0, &c[0]);\
  MD5_Final(out1, &c[1]);\
  judge_match_result(if_fn0,if_fn1,out0,out1,ret);\
 \
}while(0)


#define consistency_test_1(test_no,if_no0,if_no1,loops_num,h,kv0,kv1,if_fn0,if_fn1) \
do{\
  u64 _elm_cnt = loops_num;\
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
    shift_keys(kv0,8,8);/* proof the diffrence, change 'shift_keys(...)' to 'random_keys(...)'; */\
    shift_keys(kv1,8,8);\
  }while(--_elm_cnt);\
  \
  MD5_Final(out0, &c[0]);\
  MD5_Final(out1, &c[1]);\
  judge_match_result(if_fn0,if_fn1,out0,out1,ret);\
\
}while(0)


typedef enum {
  PROFILE_TYPE_I,
  PROFILE_TYPE_II,
  PROFILE_TYPE_III,
  PROFILE_TYPE_IV,
  PROFILE_TYPE_V
}keyInitType;

typedef struct profile_type_table
{
  int id;
  keyInitType type;
  u64 element_cnt;
  u64 nbuckets;
  u64 active;
  f64 bucket_usage_rate;
  char info[32];
}profile_type_table;

profile_type_table g_p_table[] = {

  /**
   * general case, scale: 1e3 
   * 
   */
  {1,PROFILE_TYPE_I,1000,1048576,1000,0.001,"profile_id 01"},

  /**
   * general case, scale: 1e4 
   * 
   */
  {2,PROFILE_TYPE_I,10000,1048576,10000,0.0095,"profile_id 02"},

  /**
   * general case, scale: 1e5 
   * 
   */
  {3,PROFILE_TYPE_I,100000,1048576,100000,0.0954,"profile_id 03"},

  /**
   * general case, scale: 1e6 
   * 
   */
  {4,PROFILE_TYPE_I,1000000,1048576,100000,0.9537,"profile_id 04"},

  /**
   * general case, scale: 1e7 
   * 
   */
  {5,PROFILE_TYPE_I,10000000,1048576,1000000,1.0,"profile_id 05"},
  
  /**
   * general case, scale: 3e6 
   * 
   */
  {6,PROFILE_TYPE_I,3000000,1048576,1000000,1.0,"profile_id 06"},

   /**
   * general case, scale: 4e6 
   * 
   */
  {7,PROFILE_TYPE_I,4000000,1048576,1000000,1.0,"profile_id 07"},

   /**
   * general case, scale: 5e6 
   * 
   */
  {8,PROFILE_TYPE_I,5000000,1048576,1000000,1.0,"profile_id 08"},

     /**
   * general case, scale: 6e6 
   * 
   */
  {9,PROFILE_TYPE_I,6000000,1048576,1000000,1.0,"profile_id 09"},

  /**
   * general case, scale: 7e6 
   * 
   */
  {10,PROFILE_TYPE_I,7000000,1048576,1000,0.001,"profile_id 10"},

   /**
   * 
   * general case, scale: 1e3 
   * category II
   */
  {11,PROFILE_TYPE_II,1000,1048576,1000,0.001,"profile_id 11"},

  /**
   * 
   * general case, scale: 1e4
   * category II
   */
  {12,PROFILE_TYPE_II,10000,1048576,9978,0.0095,"profile_id 12"},

  /**
   * 
   * general case, scale: 1e5
   * category II
   */
  {13,PROFILE_TYPE_II,100000,1048576,96864,0.0924,"profile_id 13"},

  /**
   * 
   * general case, scale: 1e6
   * category II
   */
  {14,PROFILE_TYPE_II,1000000,1048576,665289,0.6345,"profile_id 14"},

  /**
   * 
   * general case, scale: 1e7
   * category II
   */
  {15,PROFILE_TYPE_II,10000000,1048576,1048576,1.0,"profile_id 15"},

  /**
   * linear case ,scale: 2e6 
   * linear: 22
   * category III
   */
  {20,PROFILE_TYPE_III,2000000,1048576,989261,0.9434,"profile_id 21"},

  /*
  * linear case ,scale: 3e6
  * linear: 109 
  * category III
  * */
  {21,PROFILE_TYPE_III,3000000,1048576,989261,0.9434,"profile_id 21"},

  /*
  * linear case ,scale: 4e6
  * linear: 338 
  * category III
  * */
  {22,PROFILE_TYPE_III,4000000,1048576,1025909,0.9784,"profile_id 22"},

  /*
  * linear case ,scale: 5e6
  * linear: 724 
  * category III
  * */
  {23,PROFILE_TYPE_III,5000000,1048576,1040006,0.9918,"profile_id 23"},

  /*
  * linear case ,scale: 6e6
  * linear: 1306 
  * category III
  * */
  {24,PROFILE_TYPE_III,6000000,1048576,1045319,0.9969,"profile_id 24"},

  /*
  * linear case ,scale: 7e6
  * linear: 2104 
  * category III
  * */
  {25,PROFILE_TYPE_III,7000000,1048576,1047308,0.9988,"profile_id 25"},
  
  /*
  * linear case ,scale: 1e7
  * linear: 5031 
  * category III
  * */
  {29,PROFILE_TYPE_III,7000000,1048576,1047308,0.9988,"profile_id 29"},

  /**
   * 
   * random case, scale: 1e3
   * category IV
   */
  {31,PROFILE_TYPE_IV,1000,1048576,1000,0.001,"profile_id 31"},
  /**
   * 
   * random case, scale: 1e4
   * category IV
   */
  {32,PROFILE_TYPE_IV,10000,1048576,9949,0.0095,"profile_id 32"},

  /**
   * 
   * random case, scale: 1e5
   * category IV
   */
  {33,PROFILE_TYPE_IV,100000,1048576,95208,0.0908,"profile_id 33"},

  /**
   * 
   * random case, scale: 1e6
   * category IV
   */
  {34,PROFILE_TYPE_IV,1000000,1048576,644486,0.6146,"profile_id 34"},

  /**
   * 
   * random case, scale: 1e7
   * category IV
   */
  {35,PROFILE_TYPE_IV,10000000,1048576,1048500,0.9999,"profile_id 35"},

  /**
   * 
   * random case, scale: 3e6
   * category IV
   */
  {36,PROFILE_TYPE_IV,3000000,1048576,1048500,0.9999,"profile_id 36"},

  /**
   * 
   * random case, scale: 12e4
   * category V
   * bucket_usage_rate ~ 10%
   */
  {40,PROFILE_TYPE_V,120000,1048576,114195,0.01089,"profile_id 40"},

  /**
   * 
   * random case, scale: 25e4
   * category V
   * bucket_usage_rate ~ 20%
   */
  {41,PROFILE_TYPE_V,250000,1048576,223931,0.2136,"profile_id 41"},

  /**
   * 
   * random case, scale: 36e4
   * category V
   * bucket_usage_rate ~ 30%
   */
  {42,PROFILE_TYPE_V,360000,1048576,305228,0.2911,"profile_id 42"},

  /**
   * 
   * random case, scale: 5e5
   * category V
   * bucket_usage_rate ~ 40%
   */
  {43,PROFILE_TYPE_V,500000,1048576,399339,0.3808,"profile_id 43"},

  /**
   * 
   * random case, scale: 7e5
   * category V
   * bucket_usage_rate ~ 50%
   */
  {44,PROFILE_TYPE_V,700000,1048576,510995,0.4873,"profile_id 44"},  

  /**
   * 
   * random case, scale: 10e5
   * category V
   * bucket_usage_rate ~ 60%
   */
  {45,PROFILE_TYPE_V,1000000,1048576,645734,0.6158,"profile_id 45"},

  /**
   * 
   * random case, scale: 13e5
   * category V
   * bucket_usage_rate ~ 70%
   */
  {46,PROFILE_TYPE_V,1300000,1048576,748349,0.7137,"profile_id 46"},

  /**
   * 
   * random case, scale: 19e5
   * category V
   * bucket_usage_rate ~ 80%
   */
  {47,PROFILE_TYPE_V,1900000,1048576,874700,0.8342,"profile_id 47"},

  /**
   * 
   * random case, scale: 25e5
   * category V
   * bucket_usage_rate ~  90%
   */
  {48,PROFILE_TYPE_V,2500000,1048576,953081,0.9089,"profile_id 48"},

  /**
   * 
   * random case, scale: 35e5
   * category V
   * bucket_usage_rate  ~ 95%
   */
  {49,PROFILE_TYPE_V,3500000,1048576,1009506,0.9327,"profile_id 49"},

  /**
   * general case, scale: 1e6
   * is_which_profile open switch,some key didn't exist in hash table.
   * @see the macro of perf_test_1
   */
  {59,PROFILE_TYPE_I,1000000,1048576,1000,0.001,"profile_id 59"}
  
};


int init_hash_table(
  profile_type_table *table,
  int is_which_profile,
  BVT (clib_bihash) * h, 
  u64* loops)
{
  BVT (clib_bihash_kv) kv;
  int table_cnt; 
  int i;
  int elm_cnt;
  keyInitType ntype;
  profile_type_table *ptbl = NULL;

  
#define category_I_init(h,kv,amount) do{\
int j=0;\
for (j = 0; j < amount; j++)\
    {\
     \
        kv.key = j;\
        kv.value = j+1+0x7FFFFFFFFFFF;\
\
        BV (clib_bihash_add_del) (h, &kv, 1 /* is_add */ );\
    \
    }\
}while(0)

#define category_II_init(h,kv,amount) do{\
int j=0;\
for (j = 0; j < amount; j++)\
    {\
     \
        kv.key = (j+1000000*j)%(12208745);\
        kv.value = j+1+0x7FFFFFFFFFFF;\
\
        BV (clib_bihash_add_del) (h, &kv, 1 /* is_add */ );\
    \
    }\
}while(0)

#define category_III_init(h,kv,amount) do{\
int j=0;\
for (j = 0; j < amount; j++)\
    {\
     \
        kv.key = j*j;\
        kv.value = j+1+0x7FFFFFFFFFFF;\
\
        BV (clib_bihash_add_del) (h, &kv, 1 /* is_add */ );\
    \
    }\
}while(0)

#define category_IV_init(h,kv,amount) do{\
int j=0;\
for (j = 0; j < amount; j++)\
    {\
     \
        kv.key = random();\
        kv.value = j;\
\
        BV (clib_bihash_add_del) (h, &kv, 1 /* is_add */ );\
    \
    }\
}while(0)

#define category_V_init(h,kv,amount) do{\
int j=0;\
for (j = 0; j < amount; j++)\
    {\
     \
        kv.key = j*amount;\
        kv.value = j;\
\
        BV (clib_bihash_add_del) (h, &kv, 1 /* is_add */ );\
    \
    }\
}while(0)


  table_cnt = sizeof(g_p_table)/sizeof(g_p_table[0]);

  for(i=0;i<table_cnt;i++){
    //  fformat (stdout, "item:%s\n",table[i].info);
    if(table[i].id == is_which_profile){
      ptbl= &table[i];
      /**/
      fformat (stdout, "item:%s\n",ptbl->info);
      break;
    }
  }

  if(!ptbl)return -1;

  elm_cnt = ptbl->element_cnt;
  ntype = ptbl->type;
  switch (ntype)
  {
    case PROFILE_TYPE_I:
      /* code */
      category_I_init(h,kv,elm_cnt);
      break;
    case PROFILE_TYPE_II:
      /* code */
      category_II_init(h,kv,elm_cnt);
      break;
    case PROFILE_TYPE_III:
      /* code */
      category_III_init(h,kv,elm_cnt);
      break;
    case PROFILE_TYPE_IV:
      /* code */
      category_IV_init(h,kv,elm_cnt);
      break;
    case PROFILE_TYPE_V:
      /* code */
      category_V_init(h,kv,elm_cnt);
      break;
  default:
    category_I_init(h,kv,elm_cnt);
    break;
  }

  if(loops)
    *loops=elm_cnt;

  return 0;

}

/*
*
*
*/

int main(int argc,char *argv[])
{

  BVT (clib_bihash_kv) kv;
  int i, j;
  int ret;
  u64 elm_cnt = 0;
  u64 cycles[32];
  u64 options[32];

  BVT (clib_bihash_kv) kv1_8[8];
  BVT (clib_bihash_kv) kv4_8[8];
  BVT (clib_bihash_kv) kv5_8[8];
  BVT (clib_bihash_kv) kv14_8[16];
  BVT (clib_bihash_kv) kv24_8[32];
  BVT (clib_bihash_kv) kv34_8[64];

  BVT (clib_bihash) * h;

  // u32 user_buckets = 1228800;
    u32 user_buckets = 614400;
    // u32 user_buckets = 307200;

  u32 user_memory_size = 209715200;

  BVT (clib_bihash) hash={0};
  // BVT (clib_bihash) hash2={0};
  h = &hash;
  // clib_mem_init_with_page_size (1ULL << 30, CLIB_MEM_PAGE_SZ_1G);
  clib_mem_init (0, 1ULL << 30);

  BV (clib_bihash_init) (h, "bihash-profiler", user_buckets, user_memory_size);

  #if BIHASH_ENABLE_STATS
  BV (clib_bihash_set_stats_callback) (h, inc_stats_callback, &stats);
  #endif

  u32 fix_seed=0;

  // if(argc > 4){
    fix_seed = time(0);
  // }
  srandom(fix_seed);


  // BV (clib_bihash_init) (&hash2, "test", user_buckets, user_memory_size);
  i=j=0;
  kv.key = 0;

  int is_which ;
  int is_which_profile;
  is_which_profile = 0;

  if(argc>1){
    is_which_profile = atoi(argv[1]);
  }

  ret = init_hash_table(g_p_table,is_which_profile,h,&elm_cnt);
  if(ret < 0 ){
      fformat (stdout, "init_hash_table failed \n");
  }


#if BIHASH_ENABLE_STATS
  
  fformat (stdout, "Stats:\n%U", BV(format_bihash), h, 1 /* verbose */ );

#endif

#define DEBUG_BIHASH_IF (1)
#if DEBUG_BIHASH_IF

  fformat (stdout, "Stats:\n%U", BV(format_bihash), h, 0 /* verbose */ );

#if 0
  fformat (stdout, "nbuckets:%d,log2_nbuckets:%d \n", 
          h->nbuckets,h->log2_nbuckets );
  fformat (stdout, "buckets:%ld,bucket_usage_rate:%.2f%%,bucket_avx512_flag:%d \n", 
          h->active_buckets,100*h->bucket_usage_rate,h->bucket_avx512_flag );
#endif

#endif
  

  f64 base;
  f64 cycles_per_second;
  cycles_per_second = os_cpu_clock_frequency();
  #define ST_1e6(cycles,freq) ( ((f64)(cycles)) / (freq) )
  #define CPO(x,y) ( (f64)(cycles[(x)] / options[(y)]) )
  #define OPS(x,y) ( (f64)(options[(x)])  / ST_1e6(1e6*cycles[(y)],cycles_per_second)  ) 
  #define cpo_per(b,t) ( (b)>0 ? (t)*100/(b): 0)

#if 0
/**
 * Disable the row id, 
 * for the convenience of pasting to excel tables
 * 
 */
  #define table_head_line "[items]----|  CPO  |---| MOPS  |---|Ratio for OPS|---|  Cycles |---|  Options  | \n\t"
  #define new_perf_data_line "[item%d]:     %.2f       %.2f      %.2f%%           %ld          %ld \n\t"
  #define new_data_line(cycles_id,options_id)    cycles_id,CPO(cycles_id,options_id),OPS(options_id,cycles_id),cpo_per(base,OPS(options_id,cycles_id)),cycles[cycles_id],options[options_id]
  #define table_end_line "...........|-------------------------------------------------------------------| \n"
#else
  #define table_head_line "CPO  |---| MOPS  |---|Ratio for OPS|---|  Cycles |---|  Options  | \n"
  #define new_perf_data_line "%.2f       %.2f      %.2f%%           %ld          %ld \n"
  #define new_data_line(cycles_id,options_id)    CPO(cycles_id,options_id),OPS(options_id,cycles_id),cpo_per(base,OPS(options_id,cycles_id)),cycles[cycles_id],options[options_id]
  #define table_end_line "-------------------------------------------------------------------| \n"

#endif

  #define format_prt_compared(base_cycles_id,base_options_id) \
  do{\
  base = OPS(base_cycles_id,base_options_id);\
  fformat(stdout,"Summary:@%ld options,V0 as the baseline \n"\
            table_head_line\
            new_perf_data_line\
            new_perf_data_line\
            new_perf_data_line\
            new_perf_data_line\
            new_perf_data_line\
            table_end_line,\
        options[0],\
        new_data_line(0,0),\
        new_data_line(1,1),\
        new_data_line(4,4),\
        new_data_line(5,5),\
        new_data_line(6,6)\
        );\
  }while(0)

  is_which = 0xFF;
  int is_consistency = 0xFF;
  if(argc > 2){
    is_which = atoi(argv[2]);
  }

 
  if(is_which == 0xFF){
     /**
     * 
     * V0 as baseline, compare V4 and V5 with increased searching keys.
     * Observe the 'ratio' metric.
     */
      fformat (stdout,"perf_test[ALL]...profile_id[%d]\n",is_which_profile);
      perf_test_0_linear(0,0,elm_cnt,options[0],cycles[0],NULL,h,kv,kv);
      perf_test_0_linear_x16(10,10,elm_cnt,options[1],cycles[1],NULL,h,kv,kv);
      perf_test_1_linear(4,4,elm_cnt,options[4],cycles[4],BV (clib_bihash_search_batch_v4),h,kv4_8,kv4_8);
      perf_test_1_linear(5,5,elm_cnt,options[5],cycles[5],BV (clib_bihash_search_batch_v5),h,kv5_8,kv5_8);
      perf_test_1_linear_x16(14,14,elm_cnt,options[6],cycles[6],BV (clib_bihash_search_batch_v4),h,kv14_8,kv14_8);

      // perf_test_2(5,5,elm_cnt,options[5],cycles[5],NULL,h,kv5_8,kv5_8);
    
      format_prt_compared(0,0);

  }else if(is_which == 0x0 ){
      fformat (stdout,"perf_test[0]...\n");
      perf_test_0_linear(0,0,elm_cnt,options[0],cycles[0],NULL,h,kv,kv);
  }else if(is_which == 0x4){
      fformat (stdout,"perf_test[4]...\n");
      perf_test_1_linear(4,4,elm_cnt,options[4],cycles[4],BV (clib_bihash_search_batch_v4),h,kv4_8,kv4_8);
  }else if(is_which == 0x5){
      fformat (stdout,"perf_test[5]...\n");
      // perf_test_2(5,5,elm_cnt,options[5],cycles[5],NULL,h,kv5_8,kv5_8);
      perf_test_1_linear(5,5,elm_cnt,options[5],cycles[5],BV (clib_bihash_search_batch_v5),h,kv5_8,kv5_8);

  }else if(is_which == 0x6){
    /**
     * 
     * V0 as baseline, compare V4 and V5 with random searching keys.
     * Observe the 'ratio' metric.
     */
    fformat (stdout,"perf_test[6]...profile_id[%d]\n",is_which_profile);

    perf_test_0_random(0,0,elm_cnt,options[0],cycles[0],NULL,h,kv,kv);
    perf_test_0_random_x16(10,10,elm_cnt,options[1],cycles[1],NULL,h,kv,kv);
    perf_test_1_random(4,4,elm_cnt,options[4],cycles[4],BV (clib_bihash_search_batch_v4),h,kv4_8,kv4_8);
    perf_test_1_random(5,5,elm_cnt,options[5],cycles[5],BV (clib_bihash_search_batch_v5),h,kv5_8,kv5_8);
    perf_test_1_random_x16(14,14,elm_cnt,options[6],cycles[6],BV (clib_bihash_search_batch_v4),h,kv14_8,kv14_8);


    format_prt_compared(0,0);

  }else if(is_which == 0x7){
     /**
     * 
     * V0 as baseline, compare V4 and V5 with random searching keys.
     * Observe the 'ratio' metric.
     */
    fformat (stdout,"perf_test[7]...profile_id[%d]\n",is_which_profile);

    perf_test_0_random(0,0,elm_cnt,options[0],cycles[0],NULL,h,kv,kv);
    perf_test_0_random_x16(1,1,elm_cnt,options[1],cycles[1],NULL,h,kv,kv);

    // perf_test_1_random(4,4,elm_cnt,options[1],cycles[1],BV (clib_bihash_search_batch_v4),h,kv4_8,kv4_8);
    perf_test_1_random_x16(16,16,elm_cnt,options[4],cycles[4],BV (clib_bihash_search_batch_v4),h,kv14_8,kv14_8);
    perf_test_1_random_x32(32,32,elm_cnt,options[5],cycles[5],BV (clib_bihash_search_batch_v4),h,kv24_8,kv24_8);
    perf_test_1_random_x64(64,64,elm_cnt,options[6],cycles[6],BV (clib_bihash_search_batch_v4),h,kv34_8,kv34_8);

    // perf_test_1_random(5,5,elm_cnt,options[5],cycles[5],BV (clib_bihash_search_batch_v5),h,kv5_8,kv5_8);

    format_prt_compared(0,0);
  }

  if(argc > 3) {
    is_consistency = atoi(argv[3]);
  }
 
  if(is_consistency == 0xFF){
      fformat (stdout,"consistency_test[ALL]...\n");
      consistency_test_0( 0,
                          4,
                          elm_cnt,
                          h,
                          kv,
                          kv4_8,
                          BV (clib_bihash_search),
                          BV (clib_bihash_search_batch_v4));
      consistency_test_1( 1,
                          1,4,
                          elm_cnt,
                          h,
                          kv1_8,kv4_8,
                          BV (clib_bihash_search_batch_v5),
                          BV (clib_bihash_search_batch_v4));
  }else if(is_consistency == 0){
      fformat (stdout,"consistency_test[0]...\n");
      consistency_test_0( 0,
                          4,
                          elm_cnt,
                          h,
                          kv,
                          kv4_8,
                          BV (clib_bihash_search),
                          BV (clib_bihash_search_batch_v4));
  }else if(is_consistency == 1){
      fformat (stdout,"consistency_test[1]...\n");
      consistency_test_1( 1,
                          1,4,
                          elm_cnt,
                          h,
                          kv1_8,kv4_8,
                          BV (clib_bihash_search_batch_v5),
                          BV (clib_bihash_search_batch_v4));
  }
  
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
