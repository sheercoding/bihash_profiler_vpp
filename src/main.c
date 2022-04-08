

#include "bihash_application.c"

int main(int argc,char *argv[])
{

  int is_which_profile;
  int is_which_cmp;
  int is_consistency;
  int start_mod;

  is_which_profile = 0;
  if(argc>1){
    is_which_profile = atoi(argv[1]);
  }

  is_which_cmp = 255;
  if(argc > 2){
    is_which_cmp = atoi(argv[2]);
  }

  is_consistency = 255;
  if(argc > 3) {
    is_consistency = atoi(argv[3]);
  }

  start_mod = 1;

  // clib_mem_init_with_page_size (1ULL << 30, CLIB_MEM_PAGE_SZ_1G);
  clib_mem_init (0, 1ULL << 31);
  
  return perf_cmp_body(is_which_profile,start_mod,is_which_cmp, is_consistency);
}
