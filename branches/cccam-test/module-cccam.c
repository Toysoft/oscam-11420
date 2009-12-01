#include "globals.h"

/******************************** */
/* LINKED LIST CODE - IF IT'S USEFUL ELSEWHERE, IT SHOULD BE SPLIT OFF INTO linkedlist.h/.c */
/******************************** */

// Simple, doubly linked
// This is thread-safe, so requires pthread. Also expect locking if iterators are not destroyed.

#include <pthread.h>

struct llist_node {
  void *obj;
  struct llist_node *prv;
  struct llist_node *nxt;
};

typedef struct llist {
  struct llist_node *first;
  struct llist_node *last;
  int items;
  pthread_mutex_t lock;
} LLIST;

typedef struct llist_itr {
  LLIST *l;
  struct llist_node *cur;
} LLIST_ITR;

LLIST *llist_create(void);                  // init linked list
void llist_destroy(LLIST *l);               // de-init linked list - frees all objects on the list

void *llist_append(LLIST *l, void *o);       // append object onto bottom of list, returns ptr to obj

void *llist_itr_init(LLIST *l, LLIST_ITR *itr);       // linked list iterator, returns ptr to first obj
void llist_itr_release(LLIST_ITR *itr);               // release iterator
void *llist_itr_next(LLIST_ITR *itr);                 // iterates, returns ptr to next obj

void *llist_itr_insert(LLIST_ITR *itr, void *o);  // insert object at itr point, iterates to and returns ptr to new obj
void *llist_itr_remove(LLIST_ITR *itr);           // remove obj at itr, iterates to and returns ptr to next obj

int llist_count(LLIST *l);    // returns number of obj in list

/******************************** */

#include <string.h>
#include <stdlib.h>

LLIST *llist_create(void)
{
  LLIST *l = malloc(sizeof(LLIST));
  bzero(l, sizeof(LLIST));

  pthread_mutex_init(&l->lock, NULL);

  l->items = 0;

  return l;
}

void llist_destroy(LLIST *l)
{
  LLIST_ITR itr;
  void *o = llist_itr_init(l, &itr);
  while (o) {
    free(o);
    o = llist_itr_remove(&itr);
  }
  llist_itr_release(&itr);
}

void *llist_append(LLIST *l, void *o)
{
  pthread_mutex_lock(&l->lock);
  if (o) {
    struct llist_node *ln = malloc(sizeof(struct llist_node));

    bzero(ln, sizeof(struct llist_node));
    ln->obj = o;

    if (l->last) {
      ln->prv = l->last;
      ln->prv->nxt = ln;
    } else {
      l->first = ln;
    }
    l->last = ln;

    l->items++;
  }
  pthread_mutex_unlock(&l->lock);

  return o;
}

void *llist_itr_init(LLIST *l, LLIST_ITR *itr)
{
  pthread_mutex_lock(&l->lock);
  if (l->first) {

    bzero(itr, sizeof(LLIST_ITR));
    itr->cur = l->first;
    itr->l = l;

    return itr->cur->obj;
  }

  return NULL;
}

void llist_itr_release(LLIST_ITR *itr)
{
  pthread_mutex_unlock(&itr->l->lock);
}

void *llist_itr_next(LLIST_ITR *itr)
{
  if (itr->cur->nxt) {
    itr->cur = itr->cur->nxt;
    return itr->cur->obj;
  }

  return NULL;
}

void *llist_itr_remove(LLIST_ITR *itr)  // this needs cleaning - I was lazy
{
  itr->l->items--;
  if ((itr->cur == itr->l->first) && (itr->cur == itr->l->last)) {
 //   cs_log("DEBUG %d: first&last", llist_count(itr->l));
    free(itr->cur);
    itr->l->first = NULL;
    itr->l->last = NULL;
    return NULL;
  } else if (itr->cur == itr->l->first) {
  //  cs_log("DEBUG %d: first", llist_count(itr->l));
    struct llist_node *nxt = itr->cur->nxt;
    free(itr->cur);
    nxt->prv = NULL;
    itr->l->first = nxt;
  } else if (itr->cur == itr->l->last) {
  //  cs_log("DEBUG %d: last", llist_count(itr->l));
    itr->l->last = itr->cur->prv;
    itr->l->last->nxt = NULL;
    free(itr->cur);
    return NULL;
  } else {
   // cs_log("DEBUG %d: free middle", llist_count(itr->l));
    struct llist_node *nxt = itr->cur->nxt;
    itr->cur->prv->nxt = itr->cur->nxt;
    itr->cur->nxt->prv = itr->cur->prv;
    free(itr->cur);
    itr->cur = nxt;
  }

  return itr->cur->obj;
}

int llist_count(LLIST *l)
{
  return l->items;
}

/******************************** */

#define CC_MAXMSGSIZE 512
#define CC_MAX_PROV   16

#define SWAPC(X, Y) do { char p; p = *X; *X = *Y; *Y = p; } while(0)
#define B16(X) ( (X)[0] << 8 | (X)[1] )
#define B24(X) ( (X)[0] << 16 | (X)[1] << 8 | (X)[2] )
#define B32(X) ( (X)[0] << 24 | (X)[1] << 16 |  (X)[2] << 8 | (X)[3] )
#define B64(X) ( (X)[0] << 56 | (X)[1] << 48 | (X)[2] << 40 | (X)[3] << 32 | (X)[4] << 24 | (X)[5] << 16 |  (X)[6] << 8 | (X)[7] )
#define X_FREE(X) do { if (X) { free(X); X = NULL; } } while(0)

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef unsigned long long uint64;

typedef enum {
  DECRYPT,
  ENCRYPT
} cc_crypt_mode_t;

typedef enum
{
  MSG_CLI_DATA,
  MSG_CW,
  MSG_ECM = 1,
  MSG_CARD_REMOVED = 4,
  MSG_DCW_SOMETHING,            // this still needs to be worked out
  MSG_PING,
  MSG_NEW_CARD,
  MSG_SRV_DATA,
  MSG_CW_NOK1 = 0xfe,
  MSG_CW_NOK2 = 0xff,
  MSG_NO_HEADER = 0xffff
} cc_msg_type_t;

struct cc_crypt_block
{
  uint8 keytable[256];
  uint8 state;
  uint8 counter;
  uint8 sum;
};

struct cc_card {
    uint32 id;        // cccam card (share) id
    uint16 caid;
    uint8 hop;
    uint8 key[8];     // card serial (for au)
    LLIST *provs;     // providers
    LLIST *badsids;   // sids that have failed to decode
};

struct cc_data {
  struct cc_crypt_block block[2];    // crypto state blocks

  uint8 node_id[8],           // client node id
        server_node_id[8],    // server node id
        dcw[16];              // control words

  struct cc_card *cur_card;   // ptr to selected card
  LLIST *cards;               // cards list

  uint32 count;
};

static unsigned int seed;
static uchar fast_rnd()
{
  unsigned int offset = 12923;
  unsigned int multiplier = 4079;

  seed = seed * multiplier + offset;
  return (uchar)(seed % 0xFF);
}


static void cc_init_crypt(struct cc_crypt_block *block, uint8 *key, int len)
{
  int i = 0 ;
  uint8 j = 0;

  for (i=0; i<256; i++) {
    block->keytable[i] = i;
  }

  for (i=0; i<256; i++) {
    j += key[i % len] + block->keytable[i];
    SWAPC(&block->keytable[i], &block->keytable[j]);
  }

  block->state = *key;
  block->counter=0;
  block->sum=0;
}

static void cc_crypt(struct cc_crypt_block *block, uint8 *data, int len, cc_crypt_mode_t mode)
{
  int i;
  uint8 z;

  for (i = 0; i < len; i++) {
    block->counter++;
    block->sum += block->keytable[block->counter];
    SWAPC(&block->keytable[block->counter], &block->keytable[block->sum]);
    z = data[i];
    data[i] = z ^ block->keytable[(block->keytable[block->counter] + block->keytable[block->sum]) & 0xff] ^ block->state;
    if (!mode) z = data[i];
    block->state = block->state ^ z;
  }
}

static void cc_xor(uint8 *buf)
{
  const char cccam[] = "CCcam";
  uint8 i;

  for ( i = 0; i < 8; i++ ) {
    buf[8 + i] = i * buf[i];
    if ( i <= 5 ) {
      buf[i] ^= cccam[i];
    }
  }
}

static void cc_cw_decrypt(uint8 *cws)
{
  struct cc_data *cc = reader[ridx].cc;

  uint32 cur_card = cc->cur_card->id;
  uint32 node_id_1 = B32(cc->node_id);
  uint32 node_id_2 = B32(cc->node_id + 4);
  uint32 tmp;
  int i;

  for (i = 0; i < 16; i++) {
    tmp = cws[i] ^ node_id_2;
    if (i & 1) {
      tmp = ~tmp;
    }
    cws[i] = cur_card ^ tmp;
    node_id_2 = (node_id_2 >> 4) | (node_id_1 << 28);
    node_id_1 >>= 4;
    cur_card >>= 2;
  }
}

static int connect_nonb(int sockfd, const struct sockaddr *saptr, socklen_t salen, int nsec)
{
  int             flags, n, error;
  socklen_t       len;
  fd_set          rset, wset;
  struct timeval  tval;

  flags = fcntl(sockfd, F_GETFL, 0);
  fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

  error = 0;
  cs_debug("cccam: conn_nb 1 (fd=%d)", sockfd);

  if ( (n = connect(sockfd, saptr, salen)) < 0) {
    if( errno==EALREADY ) {
      cs_debug("cccam: conn_nb in progress, errno=%d", errno);
      return(-1);
    }
    else if( errno==EISCONN ) {
      cs_debug("cccam: conn_nb already connected, errno=%d", errno);
      goto done;
    }
    cs_debug("cccam: conn_nb 2 (fd=%d)", sockfd);
    if (errno != EINPROGRESS) {
      cs_debug("cccam: conn_nb 3 (fd=%d)", sockfd);
      return(-1);
    }
  }

  cs_debug("cccam: n = %d\n", n);

  /* Do whatever we want while the connect is taking place. */
  if (n == 0)
    goto done;  /* connect completed immediately */

  FD_ZERO(&rset);
  FD_SET(sockfd, &rset);
  wset = rset;
  tval.tv_sec = nsec;
  tval.tv_usec = 0;

  if ( (n = select(sockfd+1, &rset, &wset, 0, nsec ? &tval : 0)) == 0) {
      //close(sockfd);    // timeout
    cs_debug("cccam: conn_nb 4 (fd=%d)", sockfd);
      errno = ETIMEDOUT;
      return(-1);
  }

  cs_debug("cccam: conn_nb 5 (fd=%d)", sockfd);

  if (FD_ISSET(sockfd, &rset) || FD_ISSET(sockfd, &wset)) {
    cs_debug("cccam: conn_nb 6 (fd=%d)", sockfd);
    len = sizeof(error);
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
      cs_debug("cccam: conn_nb 7 (fd=%d)", sockfd);
      return(-1);     // Solaris pending error
    }
  } else {
    cs_debug("cccam: conn_nb 8 (fd=%d)", sockfd);
    return -2;
  }

done:
cs_debug("cccam: conn_nb 9 (fd=%d)", sockfd);
  fcntl(sockfd, F_SETFL, flags);  /* restore file status flags */

  if (error) {
    cs_debug("cccam: conn_nb 10 (fd=%d)", sockfd);
    //close(sockfd);    /* just in case */
    errno = error;
    return(-1);
  }
  return(0);
}

static int network_tcp_connection_open(uint8 *hostname, uint16 port)
{

  int flags;
  if( connect_nonb(client[cs_idx].udp_fd,
      (struct sockaddr *)&client[cs_idx].udp_sa,
      sizeof(client[cs_idx].udp_sa), 5) < 0)
  {
    cs_log("cccam: connect(fd=%d) failed: (errno=%d)", client[cs_idx].udp_fd, errno);
    return -1;
  }
  flags = fcntl(client[cs_idx].udp_fd, F_GETFL, 0);
  flags &=~ O_NONBLOCK;
  fcntl(client[cs_idx].udp_fd, F_SETFL, flags );

  return client[cs_idx].udp_fd;
}

static int cc_msg_recv(uint8 *buf, int l)
{
  int len, flags;
  uint8 netbuf[CC_MAXMSGSIZE];

  struct cc_data *cc = reader[ridx].cc;
  int handle = client[cs_idx].udp_fd;

  if (handle < 0) return -1;

  len = recv(handle, netbuf, 4, MSG_WAITALL);

  if (!len) return 0;

  if (len != 4) { // invalid header length read
    cs_log("cccam: invalid header length");
    return -1;
  }

  cc_crypt(&cc->block[DECRYPT], netbuf, 4, DECRYPT);
  //cs_ddump(netbuf, 4, "cccam: decrypted header:");

  flags = netbuf[0];

  if (((netbuf[2] << 8) | netbuf[3]) != 0) {  // check if any data is expected in msg
    if (((netbuf[2] << 8) | netbuf[3]) > CC_MAXMSGSIZE - 2) {
      cs_log("cccam: message too big");
      return -1;
    }

    len = recv(handle, netbuf+4, (netbuf[2] << 8) | netbuf[3], MSG_WAITALL);  // read rest of msg

    if (len != ((netbuf[2] << 8) | netbuf[3])) {
      cs_log("cccam: invalid message length read");
      return -1;
    }

    cc_crypt(&cc->block[DECRYPT], netbuf+4, len, DECRYPT);
    len += 4;
  }

  //cs_ddump(netbuf, len, "cccam: full decrypted msg, len=%d:", len);

  memcpy(buf, netbuf, len);
  return len;
}

static int cc_cmd_send(uint8 *buf, int len, cc_msg_type_t cmd)
{
  int n;
  uint8 *netbuf = malloc(len+4);
  struct cc_data *cc = reader[ridx].cc;

  bzero(netbuf, len+4);

  if (cmd == MSG_NO_HEADER) {
    memcpy(netbuf, buf, len);
  } else {
    // build command message
    netbuf[0] = 0;   // flags??
    netbuf[1] = cmd & 0xff;
    netbuf[2] = len >> 8;
    netbuf[3] = len & 0xff;
    if (buf) memcpy(netbuf+4, buf, len);
    len += 4;
  }

  cc_crypt(&cc->block[ENCRYPT], netbuf, len, ENCRYPT);

  cs_ddump(buf, len, "cccam: send:");
  //cs_ddump(netbuf, len, "cccam: send enc:");
  n = send(client[cs_idx].udp_fd, netbuf, len, 0);

  X_FREE(netbuf);

  return n;
}

static int cc_send_cli_data()
{
  int i;
  struct cc_data *cc = reader[ridx].cc;

  cs_debug("cccam: send client data");

  seed = (unsigned int) time((time_t*)0);
  for( i=0; i<8; i++ ) cc->node_id[i]=fast_rnd();

  uint8 buf[CC_MAXMSGSIZE];
  bzero(buf, CC_MAXMSGSIZE);

  memcpy(buf, reader[ridx].r_usr, strlen(reader[ridx].r_usr) );
  memcpy(buf + 20, cc->node_id, 8 );
  memcpy(buf + 29, reader[ridx].cc_version, strlen(reader[ridx].cc_version));   // cccam version (ascii)
  memcpy(buf + 61, reader[ridx].cc_build, strlen(reader[ridx].cc_build));       // build number (ascii)

  return cc_cmd_send(buf, 20 + 8 + 6 + 26 + 4 + 28 + 1, MSG_CLI_DATA);
}

static int cc_send_ecm(ECM_REQUEST *er, uchar *buf)
{
  int n, h = 0;
  struct cc_data *cc = reader[ridx].cc;
  struct cc_card *card;
  LLIST_ITR itr;

  memcpy(buf, er->ecm, er->l);

  cc->cur_card = NULL;

  card = llist_itr_init(cc->cards, &itr);

  while (card) {
    if (card->caid == er->caid) {   // caid matches
      LLIST_ITR pitr;
      char *prov = llist_itr_init(card->provs, &pitr);
      while (prov && !h) {
        if (B24(prov) == er->prid) {  // provid matches
      //    if ((card->hop < h) || !h) {  // card is closer
          if (card->hop == 0) {// test
            cc->cur_card = card;
            h = card->hop;
          }
        }
        prov = llist_itr_next(&pitr);
      }
      llist_itr_release(&pitr);
    }
    card = llist_itr_next(&itr);
  }
  llist_itr_release(&itr);

  if (cc->cur_card) {
    uint8 *ecmbuf = malloc(er->l+13);
    bzero(ecmbuf, er->l+13);

    // build ecm message
    ecmbuf[0] = cc->cur_card->caid >> 8;
    ecmbuf[1] = cc->cur_card->caid & 0xff;
    ecmbuf[2] = er->prid >> 24;
    ecmbuf[3] = er->prid >> 16;
    ecmbuf[4] = er->prid >> 8;
    ecmbuf[5] = er->prid & 0xff;
    ecmbuf[6] = cc->cur_card->id >> 24;
    ecmbuf[7] = cc->cur_card->id >> 16;
    ecmbuf[8] = cc->cur_card->id >> 8;
    ecmbuf[9] = cc->cur_card->id & 0xff;
    ecmbuf[10] = er->srvid >> 8;
    ecmbuf[11] = er->srvid & 0xff;
    ecmbuf[12] = er->l & 0xff;
    memcpy(ecmbuf+13, buf, er->l);

    cc->count = er->idx;

    cs_log("cccam: sending ecm for sid %04x to card %08x, hop %d", er->srvid, cc->cur_card->id, cc->cur_card->hop);
    n = cc_cmd_send(ecmbuf, er->l+13, MSG_ECM);      // send ecm

    X_FREE(ecmbuf);
  } else {
    n = -1;
    cs_log("cccam: no suitable card on server");
  }

  if (n) return 0;
  else return -1;
}

static cc_msg_type_t cc_parse_msg(uint8 *buf, int l)
{
  struct cc_data *cc = reader[ridx].cc;

  switch (buf[1]) {
  case MSG_CLI_DATA:
    cs_debug("cccam: client data ack");
    break;
  case MSG_SRV_DATA:
    memcpy(cc->server_node_id, buf+4, 8);
    cs_log("cccam: srv %s running v%s (%s)", cs_hexdump(0, cc->server_node_id, 8), buf+12, buf+44);
    break;
  case MSG_NEW_CARD:
    if (B16(buf+12) == reader[ridx].ctab.caid[0]) { // only add cards with relevant caid (for now)
      int i;
      struct cc_card *card = malloc(sizeof(struct cc_card));

      bzero(card, sizeof(struct cc_card));

      card->provs = llist_create();
      card->id = B32(buf+4);
      card->caid = B16(buf+12);
      card->hop = buf[14];
      memcpy(card->key, buf+16, 8);

      cs_log("cccam: card %08x added, caid %04x, hop %d, key %s",
          card->id, card->caid, card->hop, cs_hexdump(0, card->key, 8));

      for (i = 0; i < buf[24]; i++) {  // providers
        uint8 *prov = malloc(3);

        memcpy(prov, buf+25+(7*i), 3);
        cs_debug("      prov %d, %06x", i+1, B24(prov));

        llist_append(card->provs, prov);
      }

      llist_append(cc->cards, card);
      if (!cc->cur_card) cc->cur_card = card;
    }
    break;
  case MSG_CARD_REMOVED:
  {
    struct cc_card *card;
    LLIST_ITR itr;

    card = llist_itr_init(cc->cards, &itr);
    while (card) {
      if (card->id == B32(buf+4)) {
        llist_destroy(card->provs);

        cs_log("cccam: card %08x removed, caid %04x", card->id, card->caid);
        card = llist_itr_remove(&itr);
        free(card);
        break;
      } else {
        card = llist_itr_next(&itr);
      }
    }
    llist_itr_release(&itr);
  }
    break;
  case MSG_CW_NOK1:
  case MSG_CW_NOK2:
    cs_log("cccam: cw nok");
    break;
  case MSG_CW:
    cc_cw_decrypt(buf+4);
    memcpy(cc->dcw, buf+4, 16);
    cs_debug("cccam: cws: %s", cs_hexdump(0, cc->dcw, 16));
    cc_crypt(&cc->block[DECRYPT], buf+4, l-4, ENCRYPT); // additional crypto step
    return 0;
    break;
  case MSG_PING:
    cc_cmd_send(NULL, 0, MSG_PING);
    break;
  default:
    break;
  }

  return buf[1];
}

static int cc_recv_chk(uchar *dcw, int *rc, uchar *buf, int n)
{
  struct cc_data *cc = reader[ridx].cc;

  if (buf && n) {
    if (buf[1] == MSG_CW) {
      memcpy(dcw, cc->dcw, 16);
      cs_debug("cccam: recv chk - MSG_CW %d - %s", cc->count, cs_hexdump(0, dcw, 16));
      *rc = 1;
      return(cc->count);
    }
  }

  return -1;
}

int cc_recv(uchar *buf, int l)
{
  int n = -1;
  uint8 *cbuf = malloc(l);

  memcpy(cbuf, buf, l);

  if (!is_server) {
    if (!client[cs_idx].udp_fd) return(-1);
    if (reader[ridx].cc) n = cc_msg_recv(cbuf, l);  // recv and decrypt msg
  } else {
    return -2;
  }

  cs_ddump(buf, n, "cccam: received %d bytes from %s", n, remote_txt());
  client[cs_idx].last = time((time_t *) 0);

  if (n < 4) {
    cs_log("cccam: packet to small (%d bytes)", n);
    n = -1;
  } else if (n == 0) {
    cs_log("cccam: Connection closed to %s", remote_txt());
    n = -1;
  }

  cc_parse_msg(cbuf, n);

  X_FREE(cbuf);

  return(n);
}

static int cc_cli_connect(void)
{
  int handle;
  uint8 data[20];
  uint8 hash[SHA1_DIGEST_SIZE];
  uint8 buf[CC_MAXMSGSIZE];
  struct cc_data *cc;

  // init internals data struct
  cc = malloc(sizeof(struct cc_data));
  reader[ridx].cc = cc;
  bzero(reader[ridx].cc, sizeof(struct cc_data));
  cc->cards = llist_create();

  // check cred config
  if(reader[ridx].device[0] == 0 || reader[ridx].r_pwd[0] == 0 ||
     reader[ridx].r_usr[0] == 0 || reader[ridx].r_port == 0)
    return -5;

  // connect
  handle = network_tcp_connection_open((uint8 *)reader[ridx].device, reader[ridx].r_port);
  if(handle < 0) return -1;

  // get init seed
  if(read(handle, data, 16) != 16) {
    cs_log("cccam: server does not return 16 bytes");
    network_tcp_connection_close(handle);
    return -2;
  }
  cs_ddump(data, 16, "cccam: server init seed:");

  cc_xor(data);  // XOR init bytes with 'CCcam'

  SHA1_CTX ctx;
  SHA1_Init(&ctx);
  SHA1_Update(&ctx, data, 16);
  SHA1_Final(&ctx, hash);

  cs_ddump(hash, sizeof(hash), "cccam: sha1 hash:");

  //initialisate crypto states
  cc_init_crypt(&cc->block[DECRYPT], hash, 20);
  cc_crypt(&cc->block[DECRYPT], data, 16, DECRYPT);
  cc_init_crypt(&cc->block[ENCRYPT], data, 16);
  cc_crypt(&cc->block[ENCRYPT], hash, 20, DECRYPT);

  cc_cmd_send(hash, 20, MSG_NO_HEADER);   // send crypted hash to server

  bzero(buf, sizeof(buf));

  memcpy(buf, reader[ridx].r_usr, strlen(reader[ridx].r_usr));
  cs_ddump(buf, 20, "cccam: username '%s':", buf);
  cc_cmd_send(buf, 20, MSG_NO_HEADER);    // send usr '0' padded -> 20 bytes

  bzero(buf, sizeof(buf));

  cs_debug("cccam: 'CCcam' xor");
  memcpy(buf, "CCcam", 5);
  cc_crypt(&cc->block[ENCRYPT], (uint8 *)reader[ridx].r_pwd, strlen(reader[ridx].r_pwd), ENCRYPT);     // modify encryption state w/ pwd
  cc_cmd_send(buf, 6, MSG_NO_HEADER); // send 'CCcam' xor w/ pwd

  if (read(handle, data, 20) != 20) {
    cs_log("ccam: login failed, pwd ack not received");
    return -2;
  }
  cc_crypt(&cc->block[DECRYPT], data, 20, DECRYPT);
  cs_ddump(data, 20, "cccam: pwd ack received:");

  if (memcmp(data, buf, 5)) {  // check server response
    cs_log("cccam: login failed, usr/pwd invalid");
    return -2;
  } else {
    cs_debug("cccam: login succeeded");
  }

  reader[ridx].tcp_connected = 1;
  reader[ridx].last_g = reader[ridx].last_s = time((time_t *)0);

  cs_debug("cccam: last_s=%d, last_g=%d", reader[ridx].last_s, reader[ridx].last_g);

  pfd=client[cs_idx].udp_fd;

  if (cc_send_cli_data(cc)<=0) {
    cs_log("cccam: login failed, could not send client data");
    return -3;
  }
  return 0;
}

int cc_cli_init(void)
{
  static struct sockaddr_in loc_sa;
  struct protoent *ptrp;
  int p_proto;

  pfd=0;
  if (reader[ridx].r_port<=0)
  {
    cs_log("cccam: invalid port %d for server %s", reader[ridx].r_port, reader[ridx].device);
    return(1);
  }
  if( (ptrp=getprotobyname("tcp")) )
    p_proto=ptrp->p_proto;
  else
    p_proto=6;

  client[cs_idx].ip=0;
  memset((char *)&loc_sa,0,sizeof(loc_sa));
  loc_sa.sin_family = AF_INET;
#ifdef LALL
  if (cfg->serverip[0])
    loc_sa.sin_addr.s_addr = inet_addr(cfg->serverip);
  else
#endif
    loc_sa.sin_addr.s_addr = INADDR_ANY;
  loc_sa.sin_port = htons(reader[ridx].l_port);

  if ((client[cs_idx].udp_fd=socket(PF_INET, SOCK_STREAM, p_proto))<0)
  {
    cs_log("cccam: Socket creation failed (errno=%d)", errno);
    cs_exit(1);
  }

#ifdef SO_PRIORITY
  if (cfg->netprio)
    setsockopt(client[cs_idx].udp_fd, SOL_SOCKET, SO_PRIORITY,
               (void *)&cfg->netprio, sizeof(ulong));
#endif
  if (!reader[ridx].tcp_ito) {
    ulong keep_alive = reader[ridx].tcp_ito?1:0;
    setsockopt(client[cs_idx].udp_fd, SOL_SOCKET, SO_KEEPALIVE,
    (void *)&keep_alive, sizeof(ulong));
  }

  memset((char *)&client[cs_idx].udp_sa,0,sizeof(client[cs_idx].udp_sa));
  client[cs_idx].udp_sa.sin_family = AF_INET;
  client[cs_idx].udp_sa.sin_port = htons((u_short)reader[ridx].r_port);

  struct hostent *server;
  server = gethostbyname(reader[ridx].device);
  bcopy((char *)server->h_addr, (char *)&client[cs_idx].udp_sa.sin_addr.s_addr, server->h_length);

  cs_log("cccam: proxy %s:%d cccam v%s (%s) (fd=%d)",
          reader[ridx].device, reader[ridx].r_port, reader[ridx].cc_version,
          reader[ridx].cc_build, client[cs_idx].udp_fd);

  cc_cli_connect();

  return(0);
}

void cc_cleanup(void)
{
  struct cc_data *cc = reader[ridx].cc;
  LLIST_ITR itr;
  struct cc_card *card = llist_itr_init(cc, &itr);

  cs_debug("cccam: cleanup");

  while (card) {
    cs_debug("card %x: cleanup", card->id);
    llist_destroy(card->provs);
    card = llist_itr_next(&itr);
  }
  llist_itr_release(&itr);

  llist_destroy(cc->cards);
  X_FREE(cc);
}

void module_cccam(struct s_module *ph)
{
  strcpy(ph->desc, "cccam");
  ph->type=MOD_CONN_TCP;
  ph->logtxt = ", crypted";
  ph->watchdog=1;
  ph->recv=cc_recv;
  ph->cleanup=cc_cleanup;
  ph->c_multi=1;
  ph->c_init=cc_cli_init;
  ph->c_recv_chk=cc_recv_chk;
  ph->c_send_ecm=cc_send_ecm;
}
