#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/string.h>
//Net
#include<linux/socket.h>
#include<linux/in.h>
#include<linux/net.h>
#include <linux/syscalls.h>
#include <asm/uaccess.h>
#include <net/sock.h>
// Time
#include <linux/time.h>
// IO
#include <linux/blkdev.h>
#include <linux/fs.h>
// Queue
#include <linux/kfifo.h>

#include <linux/slab.h>

#include "dysk_bdd.h"
#include "dysk_utils.h"
#include "az.h"

#define AZ_SLAB_NAME "dysk_az_reqs"

// Http Response processing
#define AZ_RESPONSE_OK            206 // As returned from GET
#define AZ_RESPONSE_CREATED       201 // as returned fro PUT
#define AZ_RESPONSE_ERR_ACCESS    403 // Access denied key is invalid or has changed
#define AZ_RESPONSE_ERR_LEASE     412 // Lease broke
#define AZ_RESPONSE_ERR_NOT_FOUND 404 // Page blob deleted
#define AZ_RESPONSE_ERR_THROTTLE  503 // We are being throttling
#define AZ_RESPONSE_ERR_TIME_OUT  500 // Throttle but the server side is misbehaving
#define AZ_RESPONSE_CONFLICT      429 // Conflict (shouldn't really happen: unless reqing during transnient states)
#define AZ_RESPONSE_BAD_RANGE     416 // Bad range (disk resized?)

#define az_is_catastrophe(azstatuscode) ((azstatuscode == AZ_RESPONSE_ERR_ACCESS || azstatuscode == AZ_RESPONSE_ERR_LEASE || azstatuscode == AZ_RESPONSE_ERR_NOT_FOUND || azstatuscode == AZ_RESPONSE_BAD_RANGE) ? 1 : 0)
#define az_is_throttle(azstatuscode) ((azstatuscode == AZ_RESPONSE_ERR_THROTTLE || azstatuscode == AZ_RESPONSE_ERR_TIME_OUT || azstatuscode == AZ_RESPONSE_CONFLICT) ? 1 : 0)
#define az_is_done(azstatuscode) ((azstatuscode == AZ_RESPONSE_OK || azstatuscode == AZ_RESPONSE_CREATED) ? 1 : 0)


// Http request header processing
#define DATE_LENGTH            32   // Date buffer
#define SIGN_STRING_LENGTH     1024 // StringToSign (Processed)
#define AUTH_TOKEN_LENGTH      1024 // HMAC(SHA256(StringToSign))
#define HEADER_LENGTH          1024 // Upstream message header, all headers + token
#define RESPONSE_HEADER_LENGTH 1024 // Response Header Length // Azure sends on average 592 bytes

// PUT REQUEST HEADER
//PATH/Sas/HOST/Sas/Lease/ContentLength/Range-Start/Range-End/Date/AccountName/AuthToken
static const char *put_request_head = "PUT %s?comp=page&%s HTTP/1.1\r\n"
                                      "Host: %s\r\n"
                                      "x-ms-lease-id: %s\r\n"
                                      "Content-Length: %lu\r\n"
                                      "x-ms-page-write: update\r\n"
                                      "x-ms-range: bytes=%lu-%lu\r\n"
                                      "x-ms-date: %s\r\n"
                                      "UserAgent: dysk/0.0.1\r\n"
                                      "x-ms-version: 2017-04-17\r\n\r\n";

// GET REQUEST HEADER
//PATH/Sas/HOST/Lease/ContentLength/Range-Start/Range-End/Date/AccountName
static const char *get_request_head = "GET %s?%s HTTP/1.1\r\n"
                                      "Host: %s\r\n"
                                      "x-ms-lease-id: %s\r\n"
                                      "Content-Length: %d\r\n"
                                      "x-ms-range: bytes=%lu-%lu\r\n"
                                      "x-ms-date: %s\r\n"
                                      "UserAgent: dysk/0.0.1\r\n"
                                      "x-ms-version: 2017-04-17\r\n\r\n";

// GET REQUEST HEADER (No Lease)
// Used by readonly disks
//PATH/Sas/HOST/ContentLength/Range-Start/Range-End/Date/AccountName
static const char *get_request_head_no_lease = "GET %s?%s HTTP/1.1\r\n"
                                      "Host: %s\r\n"
                                      "Content-Length: %d\r\n"
                                      "x-ms-range: bytes=%lu-%lu\r\n"
                                      "x-ms-date: %s\r\n"
                                      "UserAgent: dysk/0.0.1\r\n"
                                      "x-ms-version: 2017-04-17\r\n\r\n";

/*
  Notes on mem alloc:
  ===================
  Each request is represented by __reqstate __resstate both handle
  upstream and downstream data. These objects are allocated on
  a single slab az_slab with GFP_NOIO. Objects they reference
  all allocated normally via kmalloc + GFP_KERNEL.
*/

// -----------------------------
// Module global State
// ----------------------------
// for __reqstate __resstate allocation for *all dysks*.
struct kmem_cache *az_slab;

#define MAX_CONNECTIONS       64  // Max concurrent conenctions
#define ERR_FAILED_CONNECTION -999 // Used to signal inability to connection to server
#define MAX_TRY_CONNECT       3    // Defines the max # of attempt to connect, will signal catastrohpe after

// Reason why the connection is returning to pool
typedef enum put_connection_reason  put_connection_reason;
// Manages a pool of connections (sockets)
typedef struct connection_pool connection_pool;
// Represents a socket.
typedef struct connection connection;
// entire module state attached to each dysk
typedef struct az_state az_state;

// Request Mgmt
typedef struct __reqstate __reqstate;       // Send state (Task)
typedef struct __resstate __resstate;       // receive state (Task)
typedef struct http_response http_response; // Response Formatting - This struct does not own ref to external mem.


// Forward declaration for request/response processing
task_result __send_az_req(w_task *this_task);
task_result __receive_az_response(w_task *this_task);
void __clean_receive_az_response(w_task *this_task, task_clean_reason clean_reason);
void __clean_send_az_req(w_task *this_task, task_clean_reason clean_reason);
enum put_connection_reason {
  connection_failed = 1 << 0,
  connection_ok     = 1 << 1
};
struct connection_pool {
  // Used to maintain # of active of connections
  struct kfifo connection_queue;
  // Address used by all sockets
  struct sockaddr_in *server;
  // count of connection
  unsigned int count;
  // State
  az_state *azstate;
};

struct connection {
  // Actual socket
  struct socket *sockt;
};

struct az_state {
  // Connection pool used by this dysk
  connection_pool *pool;
  // this dysk
  dysk *d;
};

// ---------------------------
// Request Mgmt
// ---------------------------
// States are put into worker when queue, worker passes then upon execution and cleaning
// State help request reentrancy
struct __reqstate {
  // Caller set state //
  az_state *azstate;    // module state
  struct request *req;  // current request

  // reentrancy state  //
  connection *c;        // connection used for request
  __resstate *resstate; // response state
  int try_new_request;  // flagged when we retry from the top

  // Header message
  struct msghdr *header_msg;
  struct iovec *header_iov;
  char *header_buffer;  // request header buffer
  int header_sent;

  //body message allocated only for put request
  char *body_buffer;
  struct msghdr *body_msg;
  struct iovec *body_iov;
  int body_sent;
};

struct __resstate {
  // Caller set state
  az_state *azstate;    // module state
  struct request *req;  // current request
  connection *c;        // conenction used in the send part

  // reentrancy state //
  char *response_buffer;        // Response buffer, can be potentially large (4megs + 1024)
  http_response *httpresponse;  // http response translated into meaninful object
  struct iovec *iov;            // io vector used in the receive message
  struct msghdr *msg;           // Message used to receive
  __reqstate *reqstate;         // if we are retrying we will need to issue new request with this
  int try_new_request;          // flag will be set if connection failed, retryable/throttle request
};

struct http_response {
  // Status Code
  int status_code;
  // Content Length (Value of:Content-Length)
  int content_length; // we don't expect anything > 4 megs
  //actual lenth of total payload
  size_t bytes_received;
  // Body Mark in a given response buffer
  char *body;
  // processing water mark
  int idx;
  // Status Description
  char status[256];
};

//  Connection Pool Mgmt
//  -------------------------
// closes a connection
static void connection_teardown(connection *c)
{
  if (c) {
    if (c->sockt) {
      c->sockt->ops->release(c->sockt);

      if (c->sockt) sock_release(c->sockt);
    }

    kfree(c);
  }
}
// Creates a connection
static int connection_create(connection_pool *pool, connection **c)
{
  struct socket *sockt   = NULL;
  connection *newcon     = NULL;
  int success            = -ENOMEM;
  int connection_attempt = 1;
  newcon = kmalloc(sizeof(connection), GFP_KERNEL);

  if (!newcon) goto failed;

  if (0 != sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, &sockt)) goto failed;

  while (connection_attempt <= MAX_TRY_CONNECT) {
    if (0 != (success =  sockt->ops->connect(sockt, (struct sockaddr *)pool->server, sizeof(struct sockaddr_in), 0))) {
      if (-ENOMEM == success) goto failed; // if no memory try later

      // Anything else is subject to max try connection
      if (MAX_TRY_CONNECT == connection_attempt) {
        printk(KERN_INFO "Failed to connect:%d", success);
        success = ERR_FAILED_CONNECTION;
        goto failed;
      }

      /* allow the machine to gracefully lose the connection for few seconds */
      set_current_state(TASK_INTERRUPTIBLE);
      schedule_timeout(2 * HZ);
      connection_attempt++;
    }

    break; // connected
  }

  newcon->sockt = sockt;
  *c            = newcon;
  return success;
failed:
  connection_teardown(newcon);
  return success;
}

// How many connections are in pool
static unsigned int connection_pool_count(connection_pool *pool)
{
  unsigned int sizebytes = kfifo_len(&pool->connection_queue);
  unsigned int actual = sizebytes / sizeof(connection *);
  return actual;
}

// Put a connection back to pool
void connection_pool_put(connection_pool *pool, connection **c, put_connection_reason reason)
{
  if (connection_failed == reason) {
    // This connection has failed tear it down and don't enqueue it
    connection_teardown(*c);
    *c = NULL;
    pool->count--;
  } else {
    // put it back in queue
    kfifo_in(&pool->connection_queue, c, sizeof(connection *));
  }
}

//gets a connection from queue or NULL if all busy
int connection_pool_get(connection_pool *pool, connection **c)
{
  int success   = -ENOMEM;

  if (0 <  connection_pool_count(pool)) { // we have connection in pool
#if NEW_KERNEL
    kfifo_out(&pool->connection_queue, c, sizeof(connection *));
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
    //https://lkml.org/lkml/2010/10/20/397
    kfifo_out(&pool->connection_queue, c, sizeof(connection *));
#pragma GCC diagnostic pop
#endif
    return 0;
  }

  // are at max?
  if (MAX_CONNECTIONS <= pool->count) goto failed;

  // Create new
  if (0 != (success = connection_create(pool, c))) goto failed;

  pool->count++;
  return success;
failed:
  return success;
}

// Creates a pool
static int connection_pool_init(connection_pool *pool)
{
  char *ip = pool->azstate->d->def->ip;
  int port = 80;
  int success = -ENOMEM;
  struct sockaddr_in *server = NULL;
  // Allocate address for all sockets
  server =  kmalloc(sizeof(struct sockaddr_in), GFP_KERNEL);
  if (!server) goto fail;

  memset(server, 0, sizeof(struct sockaddr_in));
  server->sin_family      = AF_INET;
  server->sin_addr.s_addr = inet_addr(ip);
  server->sin_port        = htons(port);

  if (0 != (success = kfifo_alloc(&pool->connection_queue, sizeof(connection *) * MAX_CONNECTIONS, GFP_KERNEL))) {
    printk(KERN_INFO  "dysk failed to create connection pool with error %d", success);
    goto fail;
  }

  pool->server = server;
  return success;
fail:
  if (server) kfree(server);

  return success;
}

// Destroy a pool
static void connection_pool_teardown(connection_pool *pool)
{
  connection *c = NULL;

  // Close and destroy all the connections
  while (0 < connection_pool_count(pool)) {
#if NEW_KERNEL
    kfifo_out(&pool->connection_queue, &c, sizeof(connection *));
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
    //https://lkml.org/lkml/2010/10/20/397
    kfifo_out(&pool->connection_queue, &c, sizeof(connection *));
#pragma GCC diagnostic pop
#endif
    connection_teardown(c);
    //kfree(c);
    c = NULL;
  }

  if (pool->server) kfree(pool->server); // free server

  kfifo_free(&pool->connection_queue); // free the queue
}

// ---------------------------
// Worker Utility Functions
// ---------------------------
static int http_response_completed(http_response *res, char *buffer)
{
  /* for any status other than 201 we will get Content-Length */
  const char *chunked_body_mark = "0\r\n\r\n";

  // fail any?
  if (0 == res->status_code || NULL == res->status || NULL == res->body) return 0;

  // Put Reqs (with or without content length)
  if (-1 == res->content_length && AZ_RESPONSE_CREATED == res->status_code && NULL != res->body && 0 == strncmp(res->body, chunked_body_mark, strlen(chunked_body_mark)))
    return 1;

  // Get Req + Put req with content length
  if (res->content_length == (res->bytes_received - (res->body - buffer))) return 1;

  return 0;
}
// Convert response to something meaninful
static int process_response(char *response, size_t response_length, http_response *res, size_t bytes_received)
{
  const char *content_length = "Content-Length";
  int cut          = 0;
  char buffer[512] = {0};
  const char *rn   = "\r\n";
  // sscanf limitation for status line process, check below
  char temp[32]      = {0};
  char *idx_of_first = NULL;
  int has_content_length = 0;
  size_t scan_for_content_length = 0;
  // append bytes received
  res->bytes_received += bytes_received;

  // status line
  if (0 == res->status_code) {
    cut = get_until(response,  rn, (char *) &buffer, 512);

    if (0 >= cut) {
      printk(KERN_INFO "STATUS LINE FAILED:%s\n", response);
      return 0; // Can move if we cant do status line
    }

    sscanf(buffer, "%*s %d %s", &res->status_code, (char *) &temp);
    // Kernel space does not implement %[a-zA-Z0-9. ] format specifier.
    idx_of_first = strnstr(buffer, temp, 512);
    memcpy(&res->status,  idx_of_first, strnlen(idx_of_first, 510));
    res->idx += cut + strlen(rn);
  }

  // Standard SKU: on 201 Created, a chunk encoding with no content length
  // Premium SKU: on 201 created, a content length:- will be provided
  scan_for_content_length = res->bytes_received > 1024 ? 1024 : res->bytes_received;
  has_content_length = NULL == strnstr(response, content_length, scan_for_content_length) ? 0 : 1;

  if (0 != res->status_code && 1 == has_content_length) {
    //headers, we are only intersted in Content-Length
    while (res->idx < response_length) {
      memset(&buffer, 0, 512);
      cut = get_until(response + res->idx,  rn, (char *) &buffer, 510);

      // Content Length
      if (NULL != strstr(buffer, content_length))
        sscanf(buffer, "%*s %d", &res->content_length);

      res->idx += cut + strlen(rn);

      if (-1 != res->content_length) break; // found it
    }
  }

  // Its kinna ugly but we need to handle incomplete response
  if (0 != res->status_code && (-1 != res->content_length || AZ_RESPONSE_CREATED == res->status_code)  && NULL == res->body) {
    while (res->idx < response_length) {
      memset(&buffer, 0, 512);
      cut = get_until(response + res->idx,  rn, (char *) &buffer, 510);

      if (0 == cut) {
        res->idx += strlen(rn);
        res->body = response + res->idx;
        break;
      }

      res->idx += cut + strlen(rn);
    }
  }

  return http_response_completed(res, response);
}

// Makes request header
int make_header(__reqstate *reqstate, char *header_buffer, size_t header_buffer_len)
{
  struct request *req   = NULL;
  int dir               = 0;
  // Header Processing
  char *date            = NULL;
  dysk *d               = NULL;
  size_t range_start    = 0;
  size_t range_end      = 0;
  int res = -ENOMEM;
  req = reqstate->req;
  dir = rq_data_dir(req);
  d = reqstate->azstate->d;
  // Ranges
  range_start = ((u64) blk_rq_pos(req) << 9);
  range_end   = (range_start + blk_rq_bytes(req) - 1);
  // date
  date = kmalloc(DATE_LENGTH, GFP_KERNEL);

  if (!date) goto done;

  utc_RFC1123_date(date, DATE_LENGTH);
  if (READ == dir) {
    if(1 == d->def->readOnly){
    // readonly disks we ignore lease
     sprintf(header_buffer, get_request_head_no_lease,
            d->def->path,
            d->def->sas,
            d->def->host,
            0,
            range_start,
            range_end,
            date,
            d->def->accountName);
    }else{
    //PATH/Sas/HOST/ContentLength/Range-Start/Range-End/Date/AccountName/AuthToken
    sprintf(header_buffer, get_request_head,
            d->def->path,
            d->def->sas,
            d->def->host,
            d->def->lease_id,
            0,
            range_start,
            range_end,
            date,
            d->def->accountName);
    }
  } else {
    //PATH/Sas/HOST/ContentLength/Range-Start/Range-End/Date/AccountName/AuthToken
    sprintf(header_buffer, put_request_head,
            d->def->path,
            d->def->sas,
            d->def->host,
            d->def->lease_id,
            blk_rq_bytes(req),
            range_start,
            range_end,
            date,
            d->def->accountName);
  }

  res = 0;
done:
  if (date) kfree(date);
  return res;
}

// ---------------------------------
// WORKER FUNCS
// ---------------------------------
// Post receive cleanup
void __clean_receive_az_response(w_task *this_task, task_clean_reason clean_reason)
{
  int free_all = 0;
  __resstate *resstate  = (__resstate *) this_task->state;

  if (resstate->c) connection_pool_put(resstate->azstate->pool, &resstate->c, connection_ok);

  resstate->c = NULL;

  if (clean_done) {
    free_all = 1;

    if (0 == resstate->try_new_request) {
      if (resstate->reqstate) {
        kmem_cache_free(az_slab, resstate->reqstate);
        resstate->reqstate = NULL;
      }

      io_end_request(this_task->d, resstate->req, 0);
    } else {
      //DEBUG
      //printk(KERN_INFO "RECV TRY NEW REQUEST");
    }

    // else: another request has been queued with resstate->reqstate.. Leave it
  } else { // Timeout, deletion & catastrophe (Request was not requeued).
    if (resstate->reqstate) {
      kmem_cache_free(az_slab, resstate->reqstate);
      resstate->reqstate = NULL;
    }

    io_end_request(this_task->d, resstate->req, (clean_reason == clean_timeout) ? -EAGAIN  : -EIO);
    free_all = 1;
  }

  if (resstate->iov) kfree(resstate->iov);

  if (resstate->msg) kfree(resstate->msg);

  if (resstate->response_buffer) kfree(resstate->response_buffer);

  if (resstate->httpresponse) kfree(resstate->httpresponse);

  resstate->iov             = NULL;
  resstate->msg             = NULL;
  resstate->response_buffer = NULL;
  resstate->httpresponse    = NULL;

  if (1 == free_all) {
    kmem_cache_free(az_slab, resstate);
    resstate = NULL;
  }
}

// Process Response + Copy buffers as needed
task_result __receive_az_response(w_task *this_task)
{
  __resstate *resstate  = NULL; // receive state
  struct request *req   = NULL; // ref'ed from state
  connection *c         = NULL; // ref'ed from  state
  connection_pool *pool = NULL; // ref'ed out of state -- module state
  // Calculated
  size_t range_start    = 0;
  size_t range_end      = 0;
  size_t response_size  = 0;
  int success           = 0;
  int dir               = 0;
  task_result res = done;
  mm_segment_t oldfs;
  // Extract state
  resstate = (__resstate *) this_task->state;
  pool     = resstate->azstate->pool;
  req      = resstate->req;
  c        = resstate->c;

  // if we failed to enqueue a request the last timne
  if (1 == resstate->try_new_request) goto retry_new_request;

  // Ranges
  range_start   = ((u64) blk_rq_pos(req) << 9);
  range_end     = range_start + blk_rq_bytes(req) - 1;
  response_size = (READ == rq_data_dir(req)) ?  blk_rq_bytes(req) + RESPONSE_HEADER_LENGTH : RESPONSE_HEADER_LENGTH;

  // allocate response buffer
  if (!resstate->response_buffer) {
    resstate->response_buffer = (char *) kmalloc(response_size, GFP_KERNEL);

    if (!resstate->response_buffer) return retry_later;

    memset(resstate->response_buffer, 0, response_size);
  }

  // allocate http response object
  if (!resstate->httpresponse) {
    // allocate http response buffers
    resstate->httpresponse = kmalloc(sizeof(http_response), GFP_KERNEL);

    if (!resstate->httpresponse) return retry_later;

    memset(resstate->httpresponse, 0, sizeof(http_response));
    resstate->httpresponse->content_length = -1;
  }

  // Allocate request state in case we needed to retry
  if (!resstate->reqstate) {
    resstate->reqstate = kmem_cache_alloc(az_slab, GFP_NOIO);

    if (!resstate->reqstate) return retry_later;

    memset(resstate->reqstate, 0, sizeof(__reqstate));
  }

  // recieve message
  if (!resstate->msg) {
    resstate->msg = kmalloc(sizeof(struct msghdr), GFP_KERNEL);

    if (!resstate->msg) return retry_later;

    memset(resstate->msg, 0, sizeof(struct msghdr));
    resstate->msg->msg_name       = NULL; //pool->server;
    resstate->msg->msg_namelen    = 0;    //sizeof(pool->server);
    resstate->msg->msg_control    = NULL;
    resstate->msg->msg_controllen = 0;
    resstate->msg->msg_flags      = 0;
  }

  // iterator
  if (!resstate->iov) {
    resstate->iov = kmalloc(sizeof(struct iovec), GFP_KERNEL);

    if (!resstate->iov) return retry_later;

    memset(resstate->iov, 0, sizeof(struct iovec));
    resstate->iov->iov_base = (void *) resstate->response_buffer;
    resstate->iov->iov_len = response_size;
#if NEW_KERNEL
      iov_iter_init(&resstate->msg->msg_iter, READ, resstate->iov, 1, response_size);
#else
      resstate->msg->msg_iov    = resstate->iov;
      resstate->msg->msg_iovlen = 1;
#endif
  }

  if (0 == http_response_completed(resstate->httpresponse, resstate->response_buffer)) {
    // receive ite
    while (0 == http_response_completed(resstate->httpresponse, resstate->response_buffer)) {
      oldfs = get_fs();
      set_fs(KERNEL_DS);
#if NEW_KERNEL
      success = sock_recvmsg(c->sockt, resstate->msg, MSG_DONTWAIT);
#else
      // forward message pointer
      resstate->iov->iov_base = (resstate->response_buffer + resstate->httpresponse->bytes_received);
      success = sock_recvmsg(c->sockt, resstate->msg, (response_size - resstate->httpresponse->bytes_received), MSG_DONTWAIT);
#endif
      set_fs(oldfs);

      if (0 >= success) {
        if (-EAGAIN == success || success == -EWOULDBLOCK) {
          return retry_later;
        } else {
          //drop the connection to the pool.. now
          //DEBUG
          //printk(KERN_INFO "RCV CONNECTION CLOSE!");
          connection_pool_put(pool, &c, connection_failed);
          resstate->c = NULL;
          goto retry_new_request;
        }
      }

      if (1 == process_response(resstate->response_buffer, strlen(resstate->response_buffer), resstate->httpresponse, success))
        break;
    }
  }

  if (1 == http_response_completed(resstate->httpresponse, resstate->response_buffer)) {
    if (1 == az_is_catastrophe(resstate->httpresponse->status_code)) {
      printk(KERN_ERR "dysk:[%s] entered catastrophe mode because http response was:%d-%s", this_task->d->def->deviceName, resstate->httpresponse->status_code, resstate->httpresponse->status);
      return catastrophe;
    }

    if (1 == az_is_throttle(resstate->httpresponse->status_code)) goto retry_throttle;

    if (1 != az_is_done(resstate->httpresponse->status_code)) {
      printk(KERN_ERR "** dysk az module got an expected status code %d and will go into catastrophe mode for [%s] - response is:%s", resstate->httpresponse->status_code, this_task->d->def->deviceName, resstate->httpresponse->body);
      return catastrophe;
    }

    // We are done, done.
    // If this was a read request, copy data to request vector
    // No reentrancy handling needed for this part
    dir = rq_data_dir(req);

    if (READ == dir) {
      // Response iterator
      struct req_iterator iter;
      struct bio_vec bvec;
      size_t mark = 0;
      void *target_buffer;
      size_t len;
      //write: Response in request bio
#if NEW_KERNEL
      rq_for_each_segment(bvec, req, iter) {
#else
      struct bio_vec *_bvec;
      rq_for_each_segment(_bvec, req, iter) {
      memcpy(&bvec, _bvec, sizeof(struct bio_vec));
#endif
        len =  bvec.bv_len;
        target_buffer = kmap_atomic(bvec.bv_page);
        memcpy(target_buffer + bvec.bv_offset, resstate->httpresponse->body + mark, len);
        kunmap_atomic(target_buffer);
        mark += len;
      }
    }

    return done;
  }

  printk(KERN_ERR "** dysk az module got unexpected response and will fail :%s", resstate->response_buffer);
  /* we shouldn't be here */
retry_throttle:
  res = throttle_dysk;
retry_new_request:
  //set that we are trying with new request
  resstate->try_new_request = 1;
  //create new request
  resstate->reqstate->req     = req;
  resstate->reqstate->azstate = resstate->azstate;

  if (0 != queue_w_task(this_task, this_task->d, &__send_az_req, &__clean_send_az_req, normal, resstate->reqstate))
    return retry_now;

  return res; // we have failed to get response now, but will try with new request
}

// post send clean up
void __clean_send_az_req(w_task *this_task, task_clean_reason clean_reason)
{
  int free_all = 0;
  __reqstate *reqstate  = (__reqstate *) this_task->state;

  if (clean_done) {
    if (0 == reqstate->try_new_request) {
      free_all = 1;
    } else {
      if (reqstate->c) connection_pool_put(reqstate->azstate->pool, &reqstate->c, connection_ok);

      reqstate->c = NULL;
      reqstate->header_sent     = 0;
      reqstate->body_sent       = 0;
      reqstate->try_new_request = 0;

      if (reqstate->resstate) {
        kmem_cache_free(az_slab, reqstate->resstate);
        reqstate->resstate = NULL;
      }
    }
  } else { // Timeout, deletion & catastrophe (Request was not requeued).
    if (reqstate->resstate) {
      kmem_cache_free(az_slab, reqstate->resstate);
      reqstate->resstate = NULL;
    }

    if (reqstate->c) connection_pool_put(reqstate->azstate->pool, &reqstate->c, connection_ok);

    reqstate->c = NULL;
    io_end_request(this_task->d, reqstate->req, (clean_reason == clean_timeout) ? -EAGAIN  : -EIO);
    free_all = 1;
  }

  if (reqstate->header_buffer) kfree(reqstate->header_buffer); // header_buffer

  if (reqstate->body_buffer) kfree(reqstate->body_buffer);    // body buffer

  // messages and iovs
  if (reqstate->header_iov) kfree(reqstate->header_iov);

  if (reqstate->body_iov)kfree(reqstate->body_iov);

  if (reqstate->header_msg) kfree(reqstate->header_msg);

  if (reqstate->body_msg) kfree(reqstate->body_msg);

  reqstate->header_buffer = NULL;
  reqstate->body_buffer   = NULL;
  reqstate->header_iov    = NULL;
  reqstate->body_iov      = NULL;
  reqstate->header_msg    = NULL;
  reqstate->body_msg      = NULL;

  if (1 == free_all) {
    kmem_cache_free(az_slab, reqstate); // root object
    reqstate = NULL;
  }
}

// Request send function
task_result __send_az_req(w_task *this_task)
{
  struct request *req   = NULL; // ref'ed out of task state
  connection_pool *pool = NULL; // ref'ed out of task state (xfer  state)
  __reqstate *reqstate  = NULL; // ref'ed out of task state
  size_t range_start    = 0;
  size_t range_end      = 0;
  int dir               = 0;
  int success           = 0;
  mm_segment_t oldfs;
  // Extract state - created by created or task
  reqstate = (__reqstate *) this_task->state;
  pool     = reqstate->azstate->pool;
  req      = reqstate->req;
  dir      = rq_data_dir(req);
  // Calculate Ranges
  range_start = ((u64) blk_rq_pos(req) << 9);
  range_end   = (range_start +  blk_rq_bytes(req) - 1);

  if (1 == reqstate->try_new_request) goto retry_new_request;

  if (1 == reqstate->header_sent && 1 == reqstate->body_sent) goto message_sent;

  // upstream header
  if (!reqstate->header_buffer) {
    //allocate
    reqstate->header_buffer = kmalloc(HEADER_LENGTH, GFP_KERNEL);

    if (!reqstate->header_buffer) return retry_now;

    memset(reqstate->header_buffer, 0, HEADER_LENGTH);
    //create or retry
    success = make_header(reqstate, reqstate->header_buffer, HEADER_LENGTH);

    if (0 != success) return retry_now;
  }

  if (!reqstate->resstate) {
    // response state object
    reqstate->resstate = kmem_cache_alloc(az_slab, GFP_NOIO);

    if (!reqstate->resstate) return retry_now;

    memset(reqstate->resstate, 0, sizeof(__resstate));
  }

  // connection
  if (!reqstate->c) {
    if (0 != (success = connection_pool_get(pool, &reqstate->c))) {
      // signal catastrophe if needed
      if (success == ERR_FAILED_CONNECTION)
        return  catastrophe;

      return retry_later;
    }
  }

  if (!reqstate->header_msg) {
    reqstate->header_msg = kmalloc(sizeof(struct msghdr), GFP_KERNEL);

    if (!reqstate->header_msg) return retry_now;

    memset(reqstate->header_msg, 0, sizeof(struct msghdr));
    reqstate->header_msg->msg_control    = NULL;
    reqstate->header_msg->msg_controllen = 0;
    reqstate->header_msg->msg_name       = pool->server;
    reqstate->header_msg->msg_namelen    = sizeof(struct sockaddr_in);
    reqstate->header_msg->msg_flags      = MSG_DONTWAIT;
  }

  if (!reqstate->header_iov) {
    reqstate->header_iov = kmalloc(sizeof(struct iovec), GFP_KERNEL);

    if (!reqstate->header_iov) return retry_now;

    memset(reqstate->header_iov, 0, sizeof(struct iovec));
    reqstate->header_iov->iov_base = reqstate->header_buffer;
    reqstate->header_iov->iov_len  = strlen(reqstate->header_buffer);

#if NEW_KERNEL
    iov_iter_init(&reqstate->header_msg->msg_iter, WRITE, reqstate->header_iov, 1, strlen(reqstate->header_buffer));
#else
      reqstate->header_msg->msg_iov    = reqstate->header_iov;
      reqstate->header_msg->msg_iovlen = 1;
#endif

  }

  if (0 == reqstate->header_sent) {
    // Sending header
#if NEW_KERNEL
    while (msg_data_left(reqstate->header_msg)) {
#else
    size_t remaining = strlen(reqstate->header_buffer);
    while(remaining){
#endif
      oldfs = get_fs();
      set_fs(KERNEL_DS);
#if NEW_KERNEL
      success = sock_sendmsg(reqstate->c->sockt, reqstate->header_msg);
#else
      //forward buffer pointer
      reqstate->header_iov->iov_base = reqstate->header_buffer + (strlen(reqstate->header_buffer) - remaining);
      reqstate->header_iov->iov_len = remaining;
      success = sock_sendmsg(reqstate->c->sockt, reqstate->header_msg, remaining);
#endif
      set_fs(oldfs);

      if (0 >= success) {
        if (-EAGAIN == success || -EWOULDBLOCK == success) return retry_later;

        // drop connection here
        connection_pool_put(pool, &reqstate->c, connection_failed);
        reqstate->c = NULL;
        goto retry_new_request;
      }
#if !(NEW_KERNEL)
    remaining -= success;
#endif
    }
    reqstate->header_sent = 1;
  }

  if (WRITE == dir) {
    if (!reqstate->body_buffer) {
      struct req_iterator iter;
      struct bio_vec bvec;
#if !(NEW_KERNEL)
      struct bio_vec *_bvec;
#endif
      size_t mark = 0;
      void *target_buffer;
      size_t len;
      reqstate->body_buffer = kmalloc(blk_rq_bytes(req), GFP_KERNEL);

      if (!reqstate->body_buffer) return retry_now;

      memset(reqstate->body_buffer, 0, blk_rq_bytes(req));
      /* While i love to do scatter gather here but tracking
       * multiple messages with nonblock + reentrancy was a nightmare.
       * we fallback to 1 copy operation.
       */
      //copy the entire buffer
#if NEW_KERNEL
      rq_for_each_segment(bvec, req, iter) {
#else
      // this is ugly
      //  but rq_for_each_segment has changed between
      //  kernel v3.x and v4.x
      rq_for_each_segment(_bvec, req, iter) {
      memcpy(&bvec, _bvec, sizeof(struct bio_vec));
#endif
        len =  bvec.bv_len;
        target_buffer = kmap_atomic(bvec.bv_page);
        memcpy(reqstate->body_buffer + mark, target_buffer + bvec.bv_offset, len);
        kunmap_atomic(target_buffer);
        mark += len;
      }
    }

    if (!reqstate->body_msg) {
      reqstate->body_msg = kmalloc(sizeof(struct msghdr), GFP_KERNEL);

      if (!reqstate->body_msg) return retry_now;

      memset(reqstate->body_msg, 0, sizeof(struct msghdr));
      reqstate->body_msg->msg_control     = NULL;
      reqstate->body_msg->msg_controllen  = 0;
      reqstate->body_msg->msg_name        = pool->server;
      reqstate->body_msg->msg_namelen     = sizeof(struct sockaddr_in);
      reqstate->body_msg->msg_flags       = MSG_DONTWAIT;
    }

    if (!reqstate->body_iov) {
      reqstate->body_iov = kmalloc(sizeof(struct iovec), GFP_KERNEL);

      if (!reqstate->body_iov) return retry_now;

      memset(reqstate->body_iov, 0, sizeof(struct iovec));
      reqstate->body_iov->iov_base = reqstate->body_buffer;
      reqstate->body_iov->iov_len  = blk_rq_bytes(req);
#if NEW_KERNEL
      iov_iter_init(&reqstate->body_msg->msg_iter, WRITE, reqstate->body_iov, 1, blk_rq_bytes(req));
#else
      reqstate->body_msg->msg_iov    = reqstate->body_iov;
      reqstate->body_msg->msg_iovlen = 1;
#endif
    }

#if NEW_KERNEL
    // Send
    while (msg_data_left(reqstate->body_msg)) {
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"
    size_t remaining = blk_rq_bytes(req);
#pragma GCC diagnostic pop

    while (remaining) {
#endif
      oldfs = get_fs();
      set_fs(KERNEL_DS);
#if NEW_KERNEL
      success = sock_sendmsg(reqstate->c->sockt, reqstate->body_msg);
#else
      //forward message body pointer
      reqstate->body_iov->iov_base = reqstate->body_buffer + (blk_rq_bytes(req) - remaining);
      reqstate->body_iov->iov_len = remaining;
      success = sock_sendmsg(reqstate->c->sockt, reqstate->body_msg, remaining);
#endif
      set_fs(oldfs);

      if (0 >= success) {
        if (-EAGAIN == success || -EWOULDBLOCK == success) return retry_later;

        //DEBUG
        //printk("FAILED TO SEND PUT BODY MESSAGE: %d", success);
        //drop connection here
        connection_pool_put(pool, &reqstate->c, connection_failed);
        reqstate->c = NULL;
        goto retry_new_request;
      }
#if !(NEW_KERNEL)
      remaining -= success;
#endif
    }

    reqstate->body_sent = 1;
  } // if WRITE == dir

  reqstate->body_sent = 1;
message_sent:
  // -----------------------------------
  // Prepare receive state
  // -----------------------------------
  reqstate->resstate->azstate = reqstate->azstate;
  reqstate->resstate->req     = reqstate->req;
  reqstate->resstate->c       = reqstate->c;
  // Queue the receive part, fathering it with this task.
  success = queue_w_task(this_task, this_task->d, &__receive_az_response, __clean_receive_az_response,  no_throttle, reqstate->resstate);

  if (0 != success) return retry_now;

  return  done;
retry_new_request: // Failed to send the complete request. retry from the top
  reqstate->try_new_request = 1;
  success = queue_w_task(this_task, this_task->d, &__send_az_req, __clean_send_az_req, normal, reqstate);

  if (0 != success) return retry_now;

  return done;
}
// ---------------------------
// Main entry point for request handling
// ---------------------------
// places the request in queue.
int az_do_request(dysk *d, struct request *req)
{
  int success = 0;
  __reqstate *reqstate = NULL;
  reqstate = kmem_cache_alloc(az_slab, GFP_NOIO);

  if (!reqstate) return -ENOMEM;

  memset(reqstate, 0, sizeof(__reqstate));
  reqstate->req     = req;
  reqstate->azstate = (az_state *) d->xfer_state;
  success = queue_w_task(NULL, d, &__send_az_req, __clean_send_az_req, normal, reqstate);

  if (0 != success) {
    if (reqstate) kmem_cache_free(az_slab, reqstate);
  }

  return success;
}

// ---------------------------
// Dysk state management
// ---------------------------
int az_init_for_dysk(dysk *d)
{
  az_state *azstate     = NULL;
  connection_pool *pool = NULL;
  int success           = -1;
  azstate = kmalloc(sizeof(az_state), GFP_KERNEL);

  if (!azstate) goto free_all;

  memset(azstate, 0, sizeof(az_state));
  d->xfer_state = azstate;

  //connection pool
  pool = kmalloc(sizeof(connection_pool), GFP_KERNEL);
  if (!pool) goto free_all;
  memset(pool, 0, sizeof(connection_pool));

  pool->azstate = azstate;
  azstate->d = d;

  if (0 != (success = connection_pool_init(pool))) goto free_all;

  azstate->pool = pool;
  return success;
free_all:
  az_teardown_for_dysk(d);
  return success;
}

void az_teardown_for_dysk(dysk *d)
{
  az_state *azstate = NULL;
  azstate = (az_state *) d->xfer_state;

  if (!azstate) return; // already cleaned.

   if (azstate->pool) {
    connection_pool_teardown(azstate->pool);
    kfree(azstate->pool);
  }

  kfree(azstate);
}

// ---------------------------
// Az Global State
// ---------------------------
int az_init(void)
{
  int entry_size = 0;

  /* Our slab services req state and res state, the difference in size is minimal*/
  entry_size = sizeof(__reqstate) > sizeof(__resstate) ? sizeof(__reqstate) : sizeof(__resstate);
  az_slab = kmem_cache_create(AZ_SLAB_NAME,
                              entry_size,
                              0, /*no special behavior */
                              0, /* no alignment a cache miss is ok, for now */
                              NULL /*let kernel create pages */);

  if (!az_slab) return -1;

  return 0;
}

void az_teardown(void)
{
  if (az_slab) kmem_cache_destroy(az_slab);
}
