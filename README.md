
# Introduction 
    These tool is using to measure performance of VPP's searching API.
Help users to get better know when and who have the better performance.

# Measure performance

## test cases schema 

```bash
  key-val schema

        is_which_profile
                |
    table |--- ID ---|----Description-----|-------------------supplementary-------|
    row0  | 0        |  99000 cnts        |
    row1  | 1        |  general case      |
    row2  | 2        |  log2_page case    |
    row3  | 3        |  exception case    | some key didn't exist in hash table   |
    row4  | 11       |  2E7 cnts          |
    row5  | 20       |  1E6 cnts          |
    row6  | 21       |  linear case       |
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
./bin/bihash_application.icl 20 255 255

Stats:
           alloc_add: 1000000
                 add: 0
           split_add: 0
             replace: 0
              update: 0
                 del: 0
            del_free: 0
              linear: 0
             resplit: 0
   working_copy_lost: 0
              splits: 0
perf_test[ALL]...
---[item0]|API V0|---Des:searching 1000000 elments---|Cycles/Option:39|cycles:315335072|options:8000000
---[item4]|API V4|---Des:searching 1000000 elments---|Cycles/Option:21|cycles:175360856|options:8000000
---[item5]|API V5|---Des:searching 1000000 elments---|Cycles/Option:25|cycles:200267274|options:8000000
Summary:@8000000 options
        [items]----|Cycles/options|---|V0 as the benchmark:39.00|
        [item0]:     39                      100.00%
        [item4]:     21                      53.85%
        [item5]:     25                      64.10%
consistency_test[ALL]...
clib_bihash_search_8_8_stats|-> MATCH <-|clib_bihash_search_batch_v4_8_8_stats ---[PASS]

```

# Statistic perfs

![image](https://user-images.githubusercontent.com/94589984/160145887-f103e667-c840-4424-a60b-99a4a2ad99e9.png)

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
