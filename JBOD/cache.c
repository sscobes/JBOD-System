#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cache.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;

int cache_create(int num_entries) {
  // ignore all invalid parameters 
  if(cache || num_entries < 2 || num_entries > 4096){
    return -1;
  }
  // set cache size to num of entries specified
  if(cache_size == 0){
    cache_size = num_entries;
  }
  // allocate memory for cache
  cache = malloc(cache_size*sizeof(cache_entry_t));
  return 1;
}

int cache_destroy(void) {
  // check if cache is NULL, if not free cache space and set cache to NULL and size to 0
  if(!cache){
    return -1;
  }
  free(cache);
  cache = NULL;
  cache_size = 0;
  return 1;
}

int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
  // ignore invalid parameters
  if(!cache || !buf){
    return -1;
  }
  num_queries += 1; // increment num of queries per call of lookup
  int i = 0;
  // loop to find cache entry with the corresponding block num and disk num with a valid tag and copy block data into buf and increment access time
  for(i = 0; i < cache_size; i++){
    if(cache[i].block_num == block_num && cache[i].disk_num == disk_num && cache[i].valid == true){
      num_hits += 1;
      memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE);
      cache[i].access_time = clock;
      clock += 1;
      return 1;
    }
  }
  return -1;
}

void cache_update(int disk_num, int block_num, const uint8_t *buf) {
  // loop to find corresponding cache entry to update and increment access time of the cache 
  for(int i = 0; i < cache_size; i++){
    if(cache[i].disk_num == disk_num && cache[i].block_num == block_num && cache[i].valid == true){
      cache[i].access_time = clock;
      memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
      clock += 1;
    }
  }
}

int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
  // ignore all invalid parameters
  if(!cache || !buf || disk_num < 0 || block_num < 0 || disk_num > 15 || block_num > 255){
    return -1;
  }
  int LRU = 0;
  int ins = -1;
  int i = 0;
  // loop to find empty space in cache or if cache is full find the LRU cache entry by comparing access time of each entry
  for(i=0; i < cache_size; i++){
    if(!cache[i].valid){
      ins = i;
      break;
    }
    else if(cache[i].block_num == block_num && cache[i].disk_num == disk_num && cache[i].valid == true){
      return -1;
    }
    else if(cache[i].access_time < cache[LRU].access_time){
      LRU = i;
    }
  }
  // set ins var to LRU if loop completes and cache was full
  if(ins == -1){
    ins = LRU;
  }
  // copy data into cache
  memcpy(cache[ins].block, buf, JBOD_BLOCK_SIZE);
  cache[ins].block_num = block_num;
  cache[ins].disk_num = disk_num;
  cache[ins].access_time = clock;
  cache[ins].valid = true;
  clock += 1;
  return 1;
}

bool cache_enabled(void) {
  // false if cache is NULL true if enabled
  if(!cache){
    return false;
  }
  return true;
}

void cache_print_hit_rate(void) {
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}
