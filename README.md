
# Introduction 
    These tool is using to measure performance of bihash's searching interface.
Help users to get better know when and who have the better performance.


# Functionality
## 1.check data consitency out of the searching APIs
Abstract:
	
	KEYn-> V0 Api -> VALUPn =>	MD5_update-> .... -> MD_final-> md5sum_for_V0	
	KEYn-> V4 Api -> VALUPn =>	MD5_update-> .... -> MD_final-> md5sum_for_V4
	
	compare md5sum_for_V0 with md5sum_for_V4, judge the data consistency by check the md5sum.
## 2. Perf statistic
### Definition:
	  Cycles/Options: 
	  ===============
	  	Record start time(tm1) before execute searching, when finish workloads on the profiles define, 
	  	stop and retrieve the time(tm2), delte=tm2-tm1. @see aquire cycles from os rerfer to clib_cpu_time_now();
		
	  Options/Second: 
	  ===============
	 	First, get the constant freq on the platform (cycles_per_second = os_cpu_clock_frequency()), 
		the 'options' represent the wordloads, and record efforts of finish iterate all the elements in hashtable as Cycles/Option descripted. 
		
### formula:
	CPO = cycles/options
	OPS(PPS) = options/(cycles/cycles_per_second)
			
### for human readable:
	OPS(MPPS) = options/1e6*(cycles/cycles_per_second) 
			
		

# Measure performance

## test cases schema 

```bash
  key-val schema

        is_which_profile
                |
    table |--- ID ---|----Description-----|-------------------supplementary-------|
    row0  | 0x       |  99000 cnts        | generate case
    row1  | 1x       |  general cases      | Key generate algorithm 'category_I_init', and counts of elements is from 1e3 to 1e7  |
    row2  | 2x       |  log2_page cases    | category_II_init, others is refer to group1.
    row3  | 3x       |  linear cases      | category_III_init,others is refer to group1.  |
    row6  | 59       |  exception case    | some key didn't exist in hash table    |
    ---------------------------------------------------------------------------------

```

## Objective API 

```bash

    V0: clib_bihash_search,           benchmark
    V4: clib_bihash_search_batch_v4,  base V0, vectorize search dataflow on loading hash
                                        AVX512 intrinsic,judge condition simultaneously.
    V5: clib_bihash_search_batch_v5,  A macro wrap 8 original searching API, 
                                        no incremental AVX512 intrinsic in it.

```

## Features
```bash
    * buit-in more than 6 test sets.
    * get statistic for respetive performance, support to choose target.
    * support check API consistency.
```

## Usage 

```bash 

syntax: ./bin/bihash_application.icl [profile_idx] [perf_cmp_id] [consistency_check_msk] 

explains:   1st, select row by profile_idx on the schema, initial the hash table.
            2nd, select APIs from group{V0,V4,V5}, execute searching from previous initialized hash table,respectively,
            output perfs result and get the statistic of perfs.
            3td, check the data consistency for respective search result, here, pick up each value to subsequent,
            inject found value to MD5 alogrithm, get the digest md5sum. observe the md5sum, 
            if the md5sum are match,both of the compared API have the same outputs.
 
e.g., ./bin/bihash_application.icl 0 255 255
            choose the first shcema (99000 cnts) to initial hash table;
            mark APIs {V0,V4,V5} available, to test respective perfs;
            mark all the combination{V0 vs V4, V5 vs V4} available, check their concistency.
          
```

# Example
```bash
Stats:
           alloc_add: 1290609
                 add: 701890
           split_add: 6231
             replace: 1270
              update: 0
                 del: 0
            del_free: 0
              linear: 22
             resplit: 399
   working_copy_lost: 7
              splits: 140414067121184
    splits[1]: 6031
    splits[2]: 6804
    splits[4]: 25
    splits[8]: 1
perf_test[ALL]...profile_id[20]
---[item0]|API V0|---Des:searching 2000000 elments---|Cycles/Option:54|cycles:109085054|options:2000000
---[item4]|API V4|---Des:searching 2000000 elments---|Cycles/Option:49|cycles:99225908|options:2000000
---[item5]|API V5|---Des:searching 2000000 elments---|Cycles/Option:57|cycles:114506302|options:2000000
Summary:@2000000 options,V0 as the benchmark on the Ratio column
        [items]----|  CPO  |---| MOPS  |---|Ratio for OPS|---|  Cycles |---|  Options  |
        [item0]:     54.00       42.17      100.00%           109085054          2000000
        [item4]:     49.00       46.36      109.94%           99225908          2000000
        [item5]:     57.00       40.17      95.27%           114506302          2000000
        ...........|-------------------------------------------------------------------|
consistency_test[ALL]...
clib_bihash_search_8_8_stats|-> MATCH <-|clib_bihash_search_batch_v4_8_8_stats ---[PASS]
clib_bihash_search_batch_v5_8_8_stats|-> MATCH <-|clib_bihash_search_batch_v4_8_8_stats ---[PASS]
```

# Statistic perfs
## batch profile
refer to the profile batch script on profiles.

```bash
for a instance 
profile_cateI_batch.sh 
./bin/bihash_application.icl 1 255 5
./bin/bihash_application.icl 2 255 5
./bin/bihash_application.icl 3 255 5
./bin/bihash_application.icl 4 255 5
./bin/bihash_application.icl 5 255 5


Profile 	key initType	elements num
profile_id 01	category_I_init	1.00E+03
profile_id 02	category_I_init	1.00E+04
profile_id 03	category_I_init	1.00E+05
profile_id 04	category_I_init	1.00E+06
profile_id 05	category_I_init	1.00E+07
		
		
"#define category_I_init(h,kv,amount) do{\
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
}while(0)"		
```
# Gather raw-data 
collect each test results:
```bash
after run:
	./profiles/profile_cateI_batch.sh > test_log1.txt
	./profiles/profile_cateII_batch.sh > test_log2.txt
	./profiles/profile_cateIII_batch.sh > test_log3.txt
```

## Rendering 
### for Profile_typeI

![image](https://user-images.githubusercontent.com/94589984/160267726-333cd603-56b8-435d-98b7-87984f87e4db.png)

## for Profile_typeII

![image](https://user-images.githubusercontent.com/94589984/160267701-37cd29bb-96ea-4717-8de1-e124d9fb8dea.png)

## for Profile_typeIII

![image](https://user-images.githubusercontent.com/94589984/160267717-fc929679-195a-4cc3-bf1a-d0e185d7c073.png)


# Build instructions

## Clone and build vpp release

```bash
git clone https://git.fd.io/vpp
cd vpp
make install-deps
make build-release
cd ..

```

## Clone and build vpp-apps

``` bash
git clone https://github.com/sheercoding/testcases_for_vpp.git
cd testcases_for_vpp 
make build [VPP_DIR=vpp_dir]
```
binaries can be found in `./bin`
