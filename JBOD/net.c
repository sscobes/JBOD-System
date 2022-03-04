#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;

/* attempts to read n bytes from fd; returns true on success and false on
 * failure */
static bool nread(int fd, int len, uint8_t *buf) {
  int n = 0;
  //attempt to read bytes from fd until successful, return false if failure
  while(n < len){
    int r = read(fd, buf+n, len-n);
    if(r < 0){
      return false;
    }
    n += r;
  }
  return true;
}

/* attempts to write n bytes to fd; returns true on success and false on
 * failure */
static bool nwrite(int fd, int len, uint8_t *buf) {
  int n = 0;
  //attempt to write bytes into fd until successful, return false if failure
  while(n < len){
    int r = write(fd, buf+n, len-n);
    if(r < 0){
      return false;
    }
    n += r;
  }
  return true;
}

/* attempts to receive a packet from fd; returns true on success and false on
 * failure */
static bool recv_packet(int fd, uint32_t *op, uint16_t *ret, uint8_t *block) {
  uint16_t len;
  uint8_t header[HEADER_LEN];

  //return false if packet gives reading error
  if(!nread(fd, HEADER_LEN, header)){
    return false;
  }

  //copy contents of packet to host variables
  int offset = 0;
  memcpy(&len, header + offset, sizeof(len));
  offset += sizeof(len);
  memcpy(op, header + offset, sizeof(*op));
  offset += sizeof(*op);
  memcpy(ret, header + offset, sizeof(*ret));

  len = ntohs(len);
  *op = ntohl(*op);
  *ret = ntohs(*ret);

  //read data from block if len == 264
  if(len == 264){
    if(!nread(fd, JBOD_BLOCK_SIZE, block)){
      return false;
    }
  }
  return true;
}

/* attempts to send a packet to sd; returns true on success and false on
 * failure */
static bool send_packet(int sd, uint32_t op, uint8_t *block) {
  uint16_t len = 8;
  uint8_t header[HEADER_LEN+JBOD_BLOCK_SIZE];

  //modify len for write operations
  if(op >> 26 == JBOD_WRITE_BLOCK){
    len += 256;
  }

  //translate op and len to network bytes and copy contents to the header
  uint16_t nlen = htons(len);
  op = htonl(op);

  int offset = 0;
  memcpy(header+offset, &nlen, sizeof(nlen));
  offset += sizeof(nlen);
  memcpy(header+offset, &op, sizeof(op));
  offset += sizeof(op) + sizeof(uint16_t);

  //append contents of block to header if len == 264
  if(len == 264){
    memcpy(header+offset, block, JBOD_BLOCK_SIZE);
  }

  //return written packet
  return nwrite(sd, len, header);
}

/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. */
bool jbod_connect(const char *ip, uint16_t port) {
  struct sockaddr_in saddr;

  //translate port and IP to network bytes
  saddr.sin_family = AF_INET;
  saddr.sin_port = htons(JBOD_PORT);
  if(inet_aton(JBOD_SERVER, &saddr.sin_addr) == 0){
    return false;
  }
  
  //create socket
  cli_sd = socket(AF_INET, SOCK_STREAM, 0);
  if(!cli_sd){
    return false;
  }

  //open connection
  if(connect(cli_sd, (const struct sockaddr *)&saddr, sizeof(saddr)) == -1){
    return false;
  }

  return true;
}

/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void) {
  //close connection to socket
  close(cli_sd);
  cli_sd = -1;
}

/* sends the JBOD operation to the server and receives and processes the
 * response. */
int jbod_client_operation(uint32_t op, uint8_t *block) {
  uint16_t ret;
  uint32_t rop;

  //call send/recv packet functions so users can communicate with jbod server, return the ret value
  send_packet(cli_sd, op, block);
  recv_packet(cli_sd, &rop, &ret, block);

  return ret;
}
