#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mdadm.h"
#include "jbod.h"
#include "net.h"

int mounted = 0;

uint32_t op_code(jbod_cmd_t cmmnd, int disk_num, int block_num){
  return ((cmmnd << 26) | (disk_num << 22) | block_num);
}

int mdadm_mount(void) {
  if(mounted){
    return -1;
  }
  uint32_t op = op_code(JBOD_MOUNT,0,0);
  jbod_client_operation(op, NULL);
  mounted = 1;
  return 1;
}

int mdadm_unmount(void) {
  if(!mounted){
    return -1;
  }
  uint32_t op = op_code(JBOD_UNMOUNT, 0, 0);
  jbod_client_operation(op, NULL);
  mounted = 0;
  return 1;
}

int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf) {
  if(!mounted){
    return -1;
  }
  if(len > 1024){
    return -1;
  }
  if(addr+len > 1048576){
    return -1;
  }
  if(buf == NULL && len > 0){
    return -1;
  }
  
  int current_addr = addr; // set addr as starter point
  int blocks_read = 0; // keep track of blocks read to determine whether we are in first or last block
  int buf_offset = 0; // buf offset to keep track where we read into buf
  while (current_addr < addr+len){
    // computer disk number, block number, and offset within block that we seek to
    int disk_num = current_addr / JBOD_DISK_SIZE;
    int block_num = current_addr % JBOD_DISK_SIZE / JBOD_BLOCK_SIZE;
    int block_offset = current_addr % JBOD_BLOCK_SIZE;
    uint8_t cachetemp[JBOD_BLOCK_SIZE]; // temp cache buf

    // check if block is in cache
    if(cache_enabled() == true && cache_lookup(disk_num, block_num, cachetemp) == 1){
        // reading from single block that starts at the beginning of the block
        if(blocks_read == 0 && block_offset == 0 && len < JBOD_BLOCK_SIZE){
          memcpy(buf+buf_offset, cachetemp, len);
          current_addr += JBOD_BLOCK_SIZE;
          blocks_read += 1;
        }
        // read from first block that read doesn't start from beginning of block
        else if(blocks_read == 0){
          memcpy(buf+buf_offset, cachetemp+block_offset, JBOD_BLOCK_SIZE-block_offset);
          buf_offset += JBOD_BLOCK_SIZE-block_offset;
          blocks_read += 1;
          current_addr += JBOD_BLOCK_SIZE-block_offset;
     
        }
        // check that block we are reading from is the last block in the read
        else if(blocks_read > 0 && 0 < ((addr+len)-current_addr) && (((addr+len)-current_addr) < JBOD_BLOCK_SIZE)){
          int remaining = (addr+len) - current_addr;
          memcpy(buf+buf_offset, cachetemp, remaining);
          current_addr += JBOD_BLOCK_SIZE;
        }
        // read whole blocks 
        else{
          memcpy(buf+buf_offset, cachetemp, JBOD_BLOCK_SIZE);
          buf_offset += JBOD_BLOCK_SIZE;
          blocks_read += 1;
          current_addr += JBOD_BLOCK_SIZE;
        }
    }
    else{ 


    // seek into correct disk then correct block
    jbod_client_operation(op_code(JBOD_SEEK_TO_DISK, disk_num,0), NULL);
    jbod_client_operation(op_code(JBOD_SEEK_TO_BLOCK, 0,block_num), NULL);

    // read from block
    uint8_t temp[JBOD_BLOCK_SIZE];
    jbod_client_operation(op_code(JBOD_READ_BLOCK, 0,0), temp);

  

    // reading from single block that starts at the beginning of the block
    if(blocks_read == 0 && block_offset == 0 && len < JBOD_BLOCK_SIZE){
        memcpy(buf+buf_offset, temp, len);
        current_addr += JBOD_BLOCK_SIZE;
        blocks_read += 1;
    }
    // read from first block that read doesn't start from beginning of block
    else if(blocks_read == 0){
      memcpy(buf+buf_offset, temp+block_offset, JBOD_BLOCK_SIZE-block_offset);
      buf_offset += JBOD_BLOCK_SIZE-block_offset;
      blocks_read += 1;
      current_addr += JBOD_BLOCK_SIZE-block_offset;
     
    }
    // check that block we are reading from is the last block in the read
    else if(blocks_read > 0 && 0 < ((addr+len)-current_addr) && (((addr+len)-current_addr) < JBOD_BLOCK_SIZE)){
      int remaining = (addr+len) - current_addr;
      memcpy(buf+buf_offset, temp, remaining);
      current_addr += JBOD_BLOCK_SIZE;
    }
    // read whole blocks 
    else{
      memcpy(buf+buf_offset, temp, JBOD_BLOCK_SIZE);
      buf_offset += JBOD_BLOCK_SIZE;
      blocks_read += 1;
      current_addr += JBOD_BLOCK_SIZE;
    }
    cache_insert(disk_num, block_num, temp);
  }
  
  }
  return len;
}


int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf) {

 if(!mounted){
    return -1;
  }
  if(len > 1024){
    return -1;
  }
  if(addr+len > 1048576){
    return -1;
  }
  if(buf == NULL && len > 0){
    return -1;
  }

  int current_addr = addr; // set addr as starter point
  int blocks_wrote = 0; // keep track of blocks written to to determine whether we are in first or last
  int buf_offset = 0; // buf offset to keep track which part of buf to write to the block
  while (current_addr < addr+len){
    // computer disk number, block number, and offset within block that we seek to
    int disk_num = current_addr / JBOD_DISK_SIZE;
    int block_num = current_addr % JBOD_DISK_SIZE / JBOD_BLOCK_SIZE;
    int block_offset = current_addr % JBOD_BLOCK_SIZE;
    uint8_t cachetemp[JBOD_BLOCK_SIZE]; // temp cache buf

    // look to see if block is in cache 
    if(cache_enabled() == true && cache_lookup(disk_num, block_num, cachetemp) == 1){
      // writing to single block that starts at the beginning of the block
      if(blocks_wrote == 0 && block_offset == 0 && len < JBOD_BLOCK_SIZE){
        memcpy(cachetemp+block_offset, buf+buf_offset, len);
        jbod_client_operation(op_code(JBOD_SEEK_TO_DISK, disk_num,0), NULL);
        jbod_client_operation(op_code(JBOD_SEEK_TO_BLOCK, 0,block_num), NULL);
        jbod_client_operation(op_code(JBOD_WRITE_BLOCK, 0, 0), cachetemp);
        current_addr += JBOD_BLOCK_SIZE;
        blocks_wrote += 1;
      }
      // write first block that doesn't write to beginning of block
      else if(blocks_wrote == 0 && block_offset > 0 && len > JBOD_BLOCK_SIZE-block_offset){
        memcpy(cachetemp+block_offset, buf+buf_offset, JBOD_BLOCK_SIZE-block_offset);
        jbod_client_operation(op_code(JBOD_SEEK_TO_DISK, disk_num,0), NULL);
        jbod_client_operation(op_code(JBOD_SEEK_TO_BLOCK, 0,block_num), NULL);
        jbod_client_operation(op_code(JBOD_WRITE_BLOCK, 0, 0), cachetemp);
        buf_offset += JBOD_BLOCK_SIZE-block_offset;
        blocks_wrote += 1;
        current_addr += JBOD_BLOCK_SIZE-block_offset;
      }
      // write that only happens within a single block that doesn't start at beginning of block
      else if(blocks_wrote == 0 && block_offset > 0 && len <= JBOD_BLOCK_SIZE-block_offset){
        memcpy(cachetemp+block_offset, buf+buf_offset, len);
        jbod_client_operation(op_code(JBOD_SEEK_TO_DISK, disk_num,0), NULL);
        jbod_client_operation(op_code(JBOD_SEEK_TO_BLOCK, 0,block_num), NULL);
        jbod_client_operation(op_code(JBOD_WRITE_BLOCK, 0, 0), cachetemp);
        buf_offset += len;
        blocks_wrote += 1;
        current_addr += JBOD_BLOCK_SIZE-block_offset;
      } 
      // check that block we are writing to is last block of the write
      else if(blocks_wrote > 0 && 0 < ((addr+len)-current_addr) && (((addr+len)-current_addr) < JBOD_BLOCK_SIZE)){
        int remaining = (addr+len) - current_addr;
        memcpy(cachetemp, buf+buf_offset, remaining);
        jbod_client_operation(op_code(JBOD_SEEK_TO_DISK, disk_num,0), NULL);
        jbod_client_operation(op_code(JBOD_SEEK_TO_BLOCK, 0,block_num), NULL);
        jbod_client_operation(op_code(JBOD_WRITE_BLOCK, 0, 0), cachetemp);
        current_addr += JBOD_BLOCK_SIZE;
      }
      // write whole blocks 
      else{
        memcpy(cachetemp, buf+buf_offset, JBOD_BLOCK_SIZE);
        jbod_client_operation(op_code(JBOD_SEEK_TO_DISK, disk_num,0), NULL);
        jbod_client_operation(op_code(JBOD_SEEK_TO_BLOCK, 0,block_num), NULL);
        jbod_client_operation(op_code(JBOD_WRITE_BLOCK, 0, 0), cachetemp);
        buf_offset += JBOD_BLOCK_SIZE;
        blocks_wrote += 1;
        current_addr += JBOD_BLOCK_SIZE;
      } 
      cache_update(disk_num, block_num, cachetemp);
    }
    else{
    // seek into correct disk then correct block
    jbod_client_operation(op_code(JBOD_SEEK_TO_DISK, disk_num,0), NULL);
    jbod_client_operation(op_code(JBOD_SEEK_TO_BLOCK, 0,block_num), NULL);

    // read buf
    uint8_t temp[JBOD_BLOCK_SIZE];
    jbod_client_operation(op_code(JBOD_READ_BLOCK, 0,0), temp);


    // writing to single block that starts at the beginning of the block
    if(blocks_wrote == 0 && block_offset == 0 && len < JBOD_BLOCK_SIZE){
        memcpy(temp+block_offset, buf+buf_offset, len);
        jbod_client_operation(op_code(JBOD_SEEK_TO_DISK, disk_num,0), NULL);
        jbod_client_operation(op_code(JBOD_SEEK_TO_BLOCK, 0,block_num), NULL);
        jbod_client_operation(op_code(JBOD_WRITE_BLOCK, 0, 0), temp);
        current_addr += JBOD_BLOCK_SIZE;
        blocks_wrote += 1;
    }
    // write first block that doesn't write to beginning of block
    else if(blocks_wrote == 0 && block_offset > 0 && len > JBOD_BLOCK_SIZE-block_offset){
      memcpy(temp+block_offset, buf+buf_offset, JBOD_BLOCK_SIZE-block_offset);
      jbod_client_operation(op_code(JBOD_SEEK_TO_DISK, disk_num,0), NULL);
      jbod_client_operation(op_code(JBOD_SEEK_TO_BLOCK, 0,block_num), NULL);
      jbod_client_operation(op_code(JBOD_WRITE_BLOCK, 0, 0), temp);
      buf_offset += JBOD_BLOCK_SIZE-block_offset;
      blocks_wrote += 1;
      current_addr += JBOD_BLOCK_SIZE-block_offset;
     
    }
    // write that only happens within a single block that doesn't start at beginning of block
    else if(blocks_wrote == 0 && block_offset > 0 && len <= JBOD_BLOCK_SIZE-block_offset){
        memcpy(temp+block_offset, buf+buf_offset, len);
        jbod_client_operation(op_code(JBOD_SEEK_TO_DISK, disk_num,0), NULL);
        jbod_client_operation(op_code(JBOD_SEEK_TO_BLOCK, 0,block_num), NULL);
        jbod_client_operation(op_code(JBOD_WRITE_BLOCK, 0, 0), temp);
        buf_offset += len;
        blocks_wrote += 1;
        current_addr += JBOD_BLOCK_SIZE-block_offset;
    } 
    // check that block we are writing to is last block of the write
    else if(blocks_wrote > 0 && 0 < ((addr+len)-current_addr) && (((addr+len)-current_addr) < JBOD_BLOCK_SIZE)){
      int remaining = (addr+len) - current_addr;
      memcpy(temp, buf+buf_offset, remaining);
      jbod_client_operation(op_code(JBOD_SEEK_TO_DISK, disk_num,0), NULL);
      jbod_client_operation(op_code(JBOD_SEEK_TO_BLOCK, 0,block_num), NULL);
      jbod_client_operation(op_code(JBOD_WRITE_BLOCK, 0, 0), temp);
      current_addr += JBOD_BLOCK_SIZE;
    }
    // write whole blocks 
    else{
      memcpy(temp, buf+buf_offset, JBOD_BLOCK_SIZE);
      jbod_client_operation(op_code(JBOD_SEEK_TO_DISK, disk_num,0), NULL);
      jbod_client_operation(op_code(JBOD_SEEK_TO_BLOCK, 0,block_num), NULL);
      jbod_client_operation(op_code(JBOD_WRITE_BLOCK, 0, 0), temp);
      buf_offset += JBOD_BLOCK_SIZE;
      blocks_wrote += 1;
      current_addr += JBOD_BLOCK_SIZE;
    }
    cache_insert(disk_num, block_num, temp);
  }
  }
  return len;
}
