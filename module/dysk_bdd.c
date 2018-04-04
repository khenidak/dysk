#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/genhd.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/hdreg.h>
#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/bio.h>

#include <linux/version.h>

#include "dysk_utils.h"
#include "dysk_bdd.h"
#include "az.h"


/* avoid building against older kernel */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,0,0)
#error dysk is for 4.10.0++ kernel versions
#endif

// ---------------------------------
// Dysk Enpoint
// ---------------------------------
#define EP_DEVICE_NAME "dysk_ep" // We use a dummy char dev, we are only intersted in IOCTL

#define IOCTLMOUNTDYSK   9901
#define IOCTLUNMOUNTDYSK 9902
#define IOCTGETDYSK      9903
#define IOCTLISTDYYSKS   9904

static int ep_release(struct inode *, struct file *);
static ssize_t ep_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t ep_write(struct file *, const char __user *, size_t, loff_t *);
static long ep_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
struct file_operations ep_ops = {
  .owner          = THIS_MODULE,
  .llseek         = no_llseek,
  .read           = ep_read,
  .write          = ep_write,
  .open           = nonseekable_open,
  .release        = ep_release,
  .unlocked_ioctl = ep_ioctl,
};

// -----------------------------------
// Typical dysk instance operations
// -----------------------------------
#define DYSK_BD_NAME "dyskroot"
static int dysk_open(struct block_device *bd, fmode_t mode);
static void dysk_release(struct gendisk *gd, fmode_t mode);
static int dysk_revalidate(struct gendisk *gd);
static int dysk_ioctl(struct block_device *bd, fmode_t mode, unsigned int cmd, unsigned long arg);
static int dysk_getgeo(struct block_device *, struct hd_geometry *);

static struct block_device_operations dysk_ops = {
  .owner           = THIS_MODULE,
  .open            = dysk_open,
  .release         = dysk_release,
  .revalidate_disk = dysk_revalidate,
  .ioctl           = dysk_ioctl,
  .getgeo          = dysk_getgeo,
};

// --------------------------------
// Partitions etc..
// --------------------------------
/* cycle through an array of long (sized sa below). Flagging
 * the slots we use.
 */
#define DYSK_MINORS 8                // max partitions per disk
#define MAX_DYSKS 1024               // max attached dysk per node - if you think we need more file an issue
#define DYSK_TRACK_SIZE MAX_DYSKS/64 // long array used to track which pos allocated for dysk

// ---------------------------------
// Dysk Life Cycle Management
// ---------------------------------
typedef struct dyskslist dyskslist;
typedef struct __dyskdelstate __dyskdelstate; // Used for delete operations

struct __dyskdelstate {
  int counter;
  dysk *d;
};

struct dyskslist {
  spinlock_t lock;

  // # of dysks mounted
  int count;

  // slots used tracking
  unsigned int dysks_slots[DYSK_TRACK_SIZE];

  // list of dysks
  dysk head;
};


// --------------------------
// Global State
// --------------------------
static int endpoint_major = 0;
static int dysk_major     = -1;

// mknod
struct class *class;
struct device *device;
// List of current dysks
static dyskslist dysks;
// worker instance used by all dysks
dysk_worker default_worker;

// Endpoint contants
#define MAX_IN_OUT 2048
#define LINE_LENGTH 32

const char *dysk_ok = "OK\n";
const char *dysk_err = "ERR\n";
const char *n  = "\n";
const char *RW = "RW";

// Forward
int io_hook(dysk *d);
int io_unhook(dysk *d);

// finds and mark slot as busy
static inline int find_set_dysk_slots(void)
{
  int ret = -1;
  unsigned int pos;
  spin_lock(&dysks.lock);

  if (MAX_DYSKS == dysks.count)
    goto done;

  dysks.count++;
  pos = find_first_zero_bit((unsigned long *) &dysks.dysks_slots, MAX_DYSKS);
  test_and_set_bit(pos, (unsigned long *) &dysks.dysks_slots); // at this point, we are sure that this bit is free
  ret = pos;
done:
  spin_unlock(&dysks.lock);
  return ret;
}

// frees dysk slot
static inline void free_dysk_slot(unsigned int pos)
{
  spin_lock(&dysks.lock);
  test_and_clear_bit(pos, (unsigned long *) &dysks.dysks_slots);
  dysks.count --;
  spin_unlock(&dysks.lock);
  return;
}

// Finds a dysk in a list
static inline dysk *dysk_exist(char *name)
{
  dysk *existing;
  int found = 0;
  spin_lock(&dysks.lock);
  list_for_each_entry(existing, &dysks.head.list, list) {
    if (0 == strncmp(existing->def->deviceName, name, DEVICE_NAME_LEN)) {
      found = 1;
      break;
    }
  }
  spin_unlock(&dysks.lock);

  if (1 == found) return existing;

  return NULL;
}
/*
 Dysk delete operations are two parts.
 validation which happens synchronously
 and actual removal process which happens asynchronously
 via task in worker queue
*/
task_result __del_dysk_async(w_task *this_task)
{
  __dyskdelstate *dyskdelstate = (__dyskdelstate *) this_task->state;
  dyskdelstate->counter++;

  // We need the worker the do 2 passes to ensure
  // that all tasks has been canceled
  if (2 >= dyskdelstate->counter) return retry_later;

  // done, actual delete
  az_teardown_for_dysk(dyskdelstate->d); // tell azure library we are deleteing
  io_unhook(dyskdelstate->d); // unhook it from kernel scheduler

  if (dyskdelstate->d->def) kfree(dyskdelstate->d->def); // free def

  kfree(dyskdelstate->d); // destroy dysk
  return done;
}

// Sync part
static inline int dysk_del(char *name, char *error)
{
  const char *ERR_DYSK_DOES_NOT_EXIST = "Failed to unmount dysk, device with name:%s does not exists";
  const char *ERR_DYSK_DEL_NO_MEM = "No memory to delete dysk:%s";
  dysk *d = NULL;
  __dyskdelstate *dyskdelstate = NULL;
  dyskdelstate = kmalloc(sizeof(__dyskdelstate), GFP_KERNEL);

  if (!dyskdelstate) {
    sprintf(error, ERR_DYSK_DEL_NO_MEM, name);
    return -ENOMEM;
  }

  memset(dyskdelstate, 0, sizeof(__dyskdelstate));

  // Check Exists
  if (NULL == (d = dysk_exist(name))) {
    sprintf(error, ERR_DYSK_DOES_NOT_EXIST, name);
    kfree(dyskdelstate);
    return -1;
  }

  // set to delete
  d->status = DYSK_DELETING;
  del_gendisk(d->gd);
  // remove it from list
  spin_lock(&dysks.lock);
  list_del(&d->list);
  spin_unlock(&dysks.lock);
  // setup async part
  dyskdelstate->d = d;

  // we can not fail here, if no mem keep trying
  while (0 != queue_w_task(NULL,
                           &dysks.head /* we send head because it is always healthy dummy dysk */,
                           &__del_dysk_async,
                           NULL /* we depend on genearl purpose clean func */,
                           no_throttle,
                           dyskdelstate))
    ;

  return 0;
}
// Adds a dysk
static inline int dysk_add(dysk *d, char *error)
{
  const char *ERR_DYSK_EXISTS = "Failed to mount dysk, device with name:%s already exists";
  const char *ERR_DYSK_ADD    = "Failed to mount device:%s with errno:%d";
  int success;

  // Check Exists
  if (NULL != dysk_exist(d->def->deviceName)) {
    sprintf(error, ERR_DYSK_EXISTS, d->def->deviceName);
    return -1;
  }

  spin_lock_init(&d->lock);
  d->worker        = &default_worker;

  // init Dysk
  if (0 != (success = az_init_for_dysk(d))) {
    printk(KERN_ERR "Failed to az_init dysk:%s", d->def->deviceName);
    sprintf(error, ERR_DYSK_ADD, d->def->deviceName, success);
    //az_teardown_for_dysk(d);
    return -1;
  }

  if (0 != (success = io_hook(d))) {
    printk(KERN_ERR "Failed to hook dysk:%s", d->def->deviceName);
    sprintf(error, ERR_DYSK_ADD, d->def->deviceName, success);
    az_teardown_for_dysk(d);
    return -1;
  }

  spin_lock(&dysks.lock);
  list_add(&d->list, &dysks.head.list);
  spin_unlock(&dysks.lock);
  return 0;
}
// Dysk def to buffer for Endpoint IOCTL
void dysk_def_to_buffer(dysk_def *dd, char *buffer)
{
  //type-devicename-sectorcount-accountname-sas-path-host-ip-lease-major-minor
  const char *format = "%s\n%s\n%lu\n%s\n%s\n%s\n%s\n%s\n%s\n%d\n%d\n%d\n";
  sprintf(buffer, format,
          (0 == dd->readOnly) ? "RW" : "R",
          dd->deviceName,
          dd->sector_count,
          dd->accountName,
          dd->sas,
          dd->path,
          dd->host,
          dd->ip,
          dd->lease_id,
          dd->major,
          dd->minor,
          dd->is_vhd);
}
// Dysk def from buffer -- Endpoint IOCTL
int dysk_def_from_buffer(char *buffer, size_t len, dysk_def *dd, char *error)
{
  const char *ERR_RW           = "Can't determine read/write flag";
  const char *ERR_DEVICE_NAME  = "Can't determine deviceName";
  const char *ERR_SECTOR_COUNT = "Can't determine sector count";
  const char *ERR_ACCOUNT_NAME = "Can't determine account name";
  const char *ERR_ACCOUNT_KEY =  "Can't determine account key";
  const char *ERR_PATH         = "Can't determine path";
  const char *ERR_HOST         = "Can't determine host";
  const char *ERR_IP           = "Can't determine ip";
  const char *ERR_LEASE_ID     = "Can't determine lease";
  const char *ERR_VHD          = "Can't determine vhd";
  char line[LINE_LENGTH] = {0};
  int cut       = 0;
  int idx       = 0;
  // Read/Write
  cut = get_until(buffer, n, line, LINE_LENGTH);

  if (-1 == cut) {
    memcpy(error, ERR_RW, strlen(ERR_RW));
    return -1;
  }

  idx += cut + strlen(n);

  if (0 == strncmp(line, RW, strlen(RW)))
    dd->readOnly = 0;
  else
    dd->readOnly =  1;

  // Device Name
  cut = get_until(buffer + idx, n, dd->deviceName, DEVICE_NAME_LEN);

  if (-1 == cut) {
    memcpy(error, ERR_DEVICE_NAME, strlen(ERR_DEVICE_NAME));
    return -1;
  }

  idx += cut + strlen(n);
  // Sector Count
  memset(line, 0, LINE_LENGTH);
  cut = get_until(buffer + idx, n, line, LINE_LENGTH);

  if (-1 == cut) {
    memcpy(error, ERR_SECTOR_COUNT, strlen(ERR_SECTOR_COUNT));
    return -1;
  }

  line[31] = '\0';

  if (1 != sscanf(line, "%lu", &dd->sector_count)) {
    memcpy(error, ERR_SECTOR_COUNT, strlen(ERR_SECTOR_COUNT));
    return -1;
  }

  idx += cut + strlen(n);
  // AccountName
  cut = get_until(buffer + idx, n, dd->accountName, ACCOUNT_NAME_LEN);

  if (-1 == cut) {
    memcpy(error, ERR_ACCOUNT_NAME, strlen(ERR_ACCOUNT_NAME));
    return -1;
  }

  idx += cut + strlen(n);
  // Account Key
  cut = get_until(buffer + idx, n, dd->sas, SAS_LEN);

  if (-1 == cut) {
    memcpy(error, ERR_ACCOUNT_KEY, strlen(ERR_ACCOUNT_KEY));
    return -1;
  }

  idx += cut + strlen(n);
  // Path
  cut = get_until(buffer + idx, n, dd->path, BLOB_PATH_LEN);

  if (-1 == cut) {
    memcpy(error, ERR_PATH, strlen(ERR_PATH));
    return -1;
  }

  idx += cut + strlen(n);
  // Host
  cut = get_until(buffer + idx, n, dd->host, HOST_LEN);

  if (-1 == cut) {
    memcpy(error, ERR_HOST, strlen(ERR_HOST));
    return -1;
  }

  idx += cut + strlen(n);
  // IP
  cut = get_until(buffer + idx, n, dd->ip, IP_LEN);

  if (-1 == cut) {
    memcpy(error, ERR_IP, strlen(ERR_IP));
    return -1;
  }

  idx += cut + strlen(n);
  // Lease
  cut = get_until(buffer + idx, n, dd->lease_id, LEASE_ID_LEN);

  if (-1 == cut) {
    memcpy(error, ERR_LEASE_ID, strlen(ERR_LEASE_ID));
    return -1;
  }

  idx += cut + strlen(n);
  // is vhd
  memset(line, 0, LINE_LENGTH);
  cut = get_until(buffer + idx, n, line, LINE_LENGTH);

  if (-1 == cut) {
    memcpy(error, ERR_VHD, strlen(ERR_VHD));
    return -1;
  }

  line[1] = '\0';

  if (1 != sscanf(line, "%d", &dd->is_vhd)) {
    memcpy(error, ERR_VHD, strlen(ERR_VHD));
    return -1;
  }

  idx += cut + strlen(n);
  return 0;
}

// IOCTL Mount
long dysk_mount(struct file *f, char *user_buffer)
{
  char *buffer = NULL;
  char *out    = NULL;
  dysk_def *dd = NULL;
  dysk *d      = NULL;
  size_t len   = MAX_IN_OUT;
  long ret     = -ENOMEM;
  int mounted  = 0;
  // int buffer
  buffer = kmalloc(len, GFP_KERNEL);

  if (!buffer) goto done;

  memset(buffer, 0, len);
  // allocate buffer out up front
  out = kmalloc(MAX_IN_OUT, GFP_KERNEL);

  if (!out) goto done;

  memset(out, 0, MAX_IN_OUT);
  // allocate dysk_def
  dd = kmalloc(sizeof(dysk_def), GFP_KERNEL);

  if (!dd) goto done;

  memset(dd, 0, sizeof(dysk_def));
  // allocate dysk
  d = kmalloc(sizeof(dysk), GFP_KERNEL);

  if (!d) goto done;

  memset(d, 0, sizeof(dysk));

  // Copy data from user
  if (0 != copy_from_user(buffer, user_buffer, len)) {
    ret = -EACCES;
    goto done;
  }

  // assume error
  memcpy(out, dysk_err, strlen(dysk_err));

  // Convert to dd
  if (0 != (ret = dysk_def_from_buffer(buffer, len, dd, out + strlen(dysk_err)))) {
    if (0 != copy_to_user(user_buffer, out, strlen(out))) {
      printk(KERN_ERR "dysk failed mount, failed to respond to user with:%s", out);
      ret = -EACCES;
      goto done;
    } else {
      // Failed to convert to dd
      ret = strlen(out);
      goto done;
    }
  }

  d->def = dd;

  // Add it and hook it up
  if (0 != dysk_add(d, out + strlen(dysk_err))) {
    if (0 != copy_to_user(user_buffer, out, strlen(out))) {
      printk(KERN_ERR "dysk failed mount, failed to respond to user with:%s", out);
      ret = -EACCES;
      goto done;
    } else {
      // Failed to convert to dd
      ret = strlen(out);
      goto done;
    }
  }

  // Respond to user with new dysk
  memset(out, 0, MAX_IN_OUT);
  memcpy(out, dysk_ok, strlen(dysk_ok));
  // updated dysk
  dysk_def_to_buffer(d->def, out + strlen(dysk_ok));

  if (0 != copy_to_user(user_buffer, out, strlen(out))) {
    printk(KERN_ERR "dysk[%s] mount was ok but failed to respond to user with:%s", d->def->deviceName, out);
    ret = -EACCES;
  } else {
    // Failed to convert to dd
    ret = strlen(out);
  }

  // ok!
  mounted = 1;
done:

  if (buffer) kfree(buffer);

  if (out) kfree(out);

  if (0 == mounted) { //failed?
    if (dd) kfree(dd);

    if (d) kfree(d);
  }

  return ret;
}
//IOCTL unmount
long dysk_unmount(struct file *f, char *user_buffer)
{
  char *buffer = NULL;
  char *out    = NULL;
  dysk *d      = NULL;
  char line[DEVICE_NAME_LEN] = {0};
  size_t len   = MAX_IN_OUT;
  long ret     = -ENOMEM;
  // int buffer
  buffer = kmalloc(len, GFP_KERNEL);

  if (!buffer) goto done;

  memset(buffer, 0, len);
  // allocate buffer out up front
  out = kmalloc(MAX_IN_OUT, GFP_KERNEL);

  if (!out) goto done;

  memset(out, 0, MAX_IN_OUT);

  // Copy data from user
  if (0 != copy_from_user(buffer, user_buffer, len)) {
    ret = -EACCES;
    goto done;
  }

  if (-1 == get_until(buffer, n, line, DEVICE_NAME_LEN)) {
    ret = -EINVAL;
    goto done;
  }

  // assume error
  memcpy(out, dysk_err, strlen(dysk_err));

  if (0 != dysk_del(line, out + strlen(dysk_err))) {
    if (0 != copy_to_user(user_buffer, out, strlen(out))) {
      printk(KERN_ERR "dysk[%s] unmount failed and failed to respond to user with:%s", d->def->deviceName, out);
      ret = -EACCES;
    }
  } else {
    memset(out, 0, MAX_IN_OUT);
    memcpy(out, dysk_ok, strlen(dysk_ok));

    if (0 != copy_to_user(user_buffer, out, strlen(out))) {
      printk(KERN_ERR "dysk[%s] unmount was ok but failed to respond to user with:%s", d->def->deviceName, out);
      ret = -EACCES;
    }
  }

  ret = strlen(out);
done:

  if (buffer) kfree(buffer);

  if (out) kfree(out);

  return ret;
}
// IOCTL get
long dysk_get(struct file *f, char *user_buffer)
{
  // Errors
  const char *ERR_DYSK_GET_DOES_NOT_EXIST =  "Failed to get dysk, device with name:%s does not exists";
  char *buffer = NULL;
  char *out    = NULL;
  dysk *d      = NULL;
  size_t len   = MAX_IN_OUT;
  long ret     = -ENOMEM;
  char line[DEVICE_NAME_LEN] = {0};
  // int buffer
  buffer = kmalloc(len, GFP_KERNEL);

  if (!buffer) goto done;

  memset(buffer, 0, len);
  // allocate buffer out up front
  out = kmalloc(MAX_IN_OUT, GFP_KERNEL);

  if (!out) goto done;

  memset(out, 0, MAX_IN_OUT);

  // Copy data from user buffer is dysk->def->deviceName
  if (0 != copy_from_user(buffer, user_buffer, len)) {
    ret = -EACCES;
    goto done;
  }

  if (-1 == get_until(buffer, n, line, DEVICE_NAME_LEN)) {
    ret = -EINVAL;
    goto done;
  }

  // assume error
  memcpy(out, dysk_err, strlen(dysk_err));

  // Do we have it
  if (NULL == (d = dysk_exist(line))) {
    sprintf(out + strlen(dysk_err), ERR_DYSK_GET_DOES_NOT_EXIST, line);

    if (0 != copy_to_user(user_buffer, out, strlen(out))) {
      printk(KERN_ERR "dysk failed to get and failed to respond to user with:%s", out);
      ret = -EACCES;
      goto done;
    } else {
      // Failed to convert to dd
      ret = strlen(out);
      goto done;
    }
  }

  // Respond to user with dysk
  memset(out, 0, MAX_IN_OUT);
  memcpy(out, dysk_ok, strlen(dysk_ok));
  // updated dysk
  dysk_def_to_buffer(d->def, out + strlen(dysk_ok));
  //memset(user_buffer,0, len);

  if (0 != copy_to_user(user_buffer, out, strlen(out))) {
    printk(KERN_ERR "dysk[%s] get was ok but failed to respond to user with:%s", d->def->deviceName, out);
    ret = -EACCES;
  } else {
    // Failed to convert to dd
    ret = strlen(out);
    goto done;
  }

done:

  if (buffer) kfree(buffer);

  if (out) kfree(out);

  return ret;
}
//IOCTL list
long dysk_list(struct file *f, char *user_buffer)
{
  const char *format = "%s\n";
  char buffer_line[DEVICE_NAME_LEN + 1] = {0};
  size_t idx = 0;
  size_t len = 0;
  char *out  = NULL;
  dysk *d    = NULL;
  long ret   = -ENOMEM;
  // allocate buffer out up front
  out = kmalloc(MAX_IN_OUT, GFP_KERNEL);

  if (!out) goto done;

  memset(out, 0, MAX_IN_OUT);
  // Respond to user with dysk
  memset(out, 0, MAX_IN_OUT);
  memcpy(out, dysk_ok, strlen(dysk_ok));
  idx += strlen(dysk_ok);
  // Build response string
  spin_lock(&dysks.lock);
  list_for_each_entry(d, &dysks.head.list, list) {
    sprintf(buffer_line, format, d->def->deviceName);
    len = strlen(buffer_line);

    if ((len + idx) > (MAX_IN_OUT - 1)) break;

    // Copy
    memcpy(out + idx, buffer_line, len);
    idx += len;
    // Reset buffer
    memset(buffer_line, 0,  DEVICE_NAME_LEN + 1);
  }
  spin_unlock(&dysks.lock);

  // Copy to user buffer
  if (0 != copy_to_user(user_buffer, out, strlen(out))) {
    printk(KERN_ERR "dysk list was ok but failed to respond to user with:%s", out);
    ret = -EACCES;
    goto done;
  }

  ret = strlen(out);
done:

  if (out) kfree(out);

  return ret;
}
// -------------------------------------
// Endpoint char device routines
// -------------------------------------
/* Endpoint char device does not do read/write only IOCTL */
static int ep_release(struct inode *n, struct file *f)
{
  return 0;
}
static ssize_t ep_read(struct file *f, char __user *buffer, size_t size, loff_t *offset)
{
  return 0;
}
static ssize_t ep_write(struct file *f, const char __user *buffer, size_t size, loff_t *offset)
{
  return 0;
}

long ep_ioctl(struct file *f, unsigned int cmd, unsigned long args)
{
  switch (cmd) {
    case IOCTLMOUNTDYSK:
      return dysk_mount(f, (char *)args);

    case IOCTLUNMOUNTDYSK:
      return dysk_unmount(f, (char *)args);

    case IOCTGETDYSK:
      return dysk_get(f, (char *)args);

    case IOCTLISTDYYSKS:
      return dysk_list(f, (char *)args);

    default:
      return -EINVAL;
  }
}
// ------------------------------
// Endpoint Lifecycle
// ------------------------------
int endpoint_stop(void)
{
  /* TODO: CHECK DEVICE OPEN - need to?*/
  if (device) device_destroy(class, MKDEV(endpoint_major, 0));

  if (class) class_destroy(class);

  unregister_chrdev(endpoint_major, EP_DEVICE_NAME);
  return 0;
}

int endpoint_start(void)
{
  if (0 >= (endpoint_major = register_chrdev(endpoint_major, EP_DEVICE_NAME, &ep_ops))) {
    printk(KERN_ERR "dysk: failed to register char device");
    return -1;
  }

  class = class_create(THIS_MODULE, "dysk");

  if (IS_ERR(class)) {
    printk(KERN_ERR "dysk: failed to create class");
    endpoint_stop();
    return -1;
  }

  device = device_create(class, NULL, MKDEV(endpoint_major, 0), "%s", "dysk");

  if (IS_ERR(device)) {
    printk(KERN_ERR "dysk: failed to create device");
    endpoint_stop();
    return -1;
  }

  printk(KERN_NOTICE "dysk: endpoint device registered with %d major", endpoint_major);
  return 0;
}
// -------------------------------------------
// Dysk: Request Mgmt
// ------------------------------------------
// Moves a request from kernel queue to dysk
static void io_request(struct request_queue *q)
{
  struct request *req = NULL;
  dysk *d             = NULL;
  d = (dysk *) q->queuedata;

  while (NULL != (req = blk_peek_request(q))) {
    // If dysk in catastrophe or being deleted
    if (DYSK_OK != d->status) {
      blk_start_request(req);
      io_end_request(d, req, -ENODEV);
      continue;
    }

    /* With disabling zeros, discard and write-same
     *  all incoming requests will be xfer
     *
    // Only xfer reqs
    if(req->cmd_type != REQ_TYPE_FS)
    {
      printk(KERN_INFO "NON TRANSFER");
      blk_start_request(req);
      io_end_request(d,req, -EINVAL);
      continue;
    }
    */
    /* by setting the disk to RO at add_disk() time, we no longer need this*/
    /* TODO: REMOVE */
    if (WRITE == rq_data_dir(req) && 1 == d->def->readOnly) {
      printk(KERN_ERR "dysk %s is readonly, a write request was received", d->def->deviceName);
      blk_start_request(req);
      io_end_request(d, req, -EROFS);
      continue;
    }

    // if queue accepted the request..
    if (0 == az_do_request(d, req))
      blk_start_request(req);
  }
}
// Set dysk in catastrophe mode, and delete it
// any process that attempts to write to this disk
// will get EIO then disk will disappear
void dysk_catastrophe(dysk *d)
{
  char dummy[256] = {0};
  int success;
  d->status = DYSK_CATASTROPHE;
  printk(KERN_ERR "dysk:%s is entered catastrophe mode", d->def->deviceName);

  // Keep trying to delete until either deleted by us or somebody else
  while (1) {
    success = dysk_del(d->def->deviceName, (char *) &dummy);

    if (-1 == success || 0 == success) break;
  }

  printk(KERN_ERR "Catastrophe dysk deleted!");
}

// All our requests are atomic (all or none)
void io_end_request(dysk *d, struct request *req, int err)
{
  blk_end_request_all(req, err);
}

// -------------------------------------------
// BLKDEV OPS
// -------------------------------------------

int dysk_getgeo(struct block_device *dev, struct hd_geometry *geo)
{
  sector_t n_sectors = 0;
  size_t size        = 0;
  n_sectors = get_capacity(dev->bd_disk);
  size = n_sectors * 512;
  geo->cylinders = (size & ~0x3f) >> 6;
  geo->heads = 4;
  geo->sectors = 16;
  geo->start = 0;
  return 0;
}
static int dysk_open(struct block_device *bd, fmode_t mode)
{
  return 0;
}
static void dysk_release(struct gendisk *gd, fmode_t mode)
{
  // no-op
}
static int dysk_revalidate(struct gendisk *gd)
{
  return 0;
}

static int dysk_ioctl(struct block_device *bd, fmode_t mode, unsigned int cmd, unsigned long arg)
{
  //DEBUG
  //printk(KERN_INFO "DYSK IO-CTL is GEO:%d CMD:%d", cmd == HDIO_GETGEO, cmd );
  return -EINVAL;
}
// ----------------------------
// BLDEV integration
// ----------------------------
// Hooks a dysk to linux io
int io_hook(dysk *d)
{
  struct gendisk *gd       = NULL;
  struct request_queue *rq = NULL;
  int ret = -1;
  int slot = -1;
  slot = find_set_dysk_slots();

  if (-1 == slot) {
    printk(KERN_INFO "failed to mount dysk:%s currently at max(%d)",
           d->def->deviceName,
           MAX_DYSKS);
    goto clean_no_mem;
  }

  d->slot = slot;
  rq = blk_init_queue(io_request, &d->lock);

  if (!rq) goto clean_no_mem;

  blk_queue_max_hw_sectors(rq, 2 * 1024 * 4);   /* 4 megs */
  blk_queue_physical_block_size(rq, 512);
  blk_queue_io_min(rq, 512);
  // No support for discard and zeros for this version
  //  TODO: Enable discard, 0s and write-same
#if NEW_KERNEL
    blk_queue_max_write_zeroes_sectors(rq, 0);
#endif

  blk_queue_max_discard_sectors(rq, 0);
  blk_queue_max_write_same_sectors(rq, 0);
  rq->queuedata = d;
  gd = alloc_disk(DYSK_MINORS);

  if (!gd) goto clean_no_mem;

  gd->private_data = d;
  gd->queue        = rq;
  gd->major        = dysk_major;
  gd->minors       = DYSK_MINORS;
  gd->first_minor  = slot * DYSK_MINORS;
  gd->fops         = &dysk_ops;
  gd->flags        |= GENHD_FL_REMOVABLE;
  //prep device name.
  memcpy(gd->disk_name, d->def->deviceName, DEVICE_NAME_LEN);
  d->gd = gd;
  d->def->major = dysk_major;
  d->def->minor = gd->first_minor;
  // set capacity for this disk
  set_capacity(gd, d->def->sector_count);

  // if disk is read only -- all partitions
  if (1 == d->def->readOnly) set_disk_ro(gd, 1);

  // add it
  add_disk(gd);
  printk(KERN_NOTICE "dysk: disk with name %s was created", d->def->deviceName);
  return 0;
clean_no_mem:
  printk(KERN_WARNING "dysk: failed to allocate memory to init dysk:%s", d->def->deviceName);

  if (gd) {
    del_gendisk(gd);
    put_disk(gd);
  }

  if (rq) {
    blk_cleanup_queue(rq);
  }

  return ret;
}

int io_unhook(dysk *d)
{
  struct gendisk *gd = (struct gendisk *) d->gd;
  struct request_queue *rq = gd->queue;
  free_dysk_slot(d->slot);
  blk_cleanup_queue(rq);
  put_disk(gd);
  printk(KERN_INFO "dysk: %s unhooked from i/o", d->def->deviceName);
  d->gd = NULL;
  return 0;
}

// -----------------------------------
// Module Lifecycle
// -----------------------------------

static void unload(void)
{
  // Worker tear down
  dysk_worker_teardown(&default_worker);
  // stop endpoint
  endpoint_stop();

  // deregister our major
  if (-1 != dysk_major) unregister_blkdev(dysk_major, DYSK_BD_NAME);

  // tear down az
  az_teardown();
}

static int __init _init_module(void)
{
  int success = 0;
  INIT_LIST_HEAD(&dysks.head.list);
  spin_lock_init(&dysks.lock);

  // Azure transfer library
  if (-1 == az_init()) {
    printk(KERN_ERR "dysk: failed to init Azure transfer library, module is in failed state");
    unload();
    return -1;
  }

  // Register dysk major
  if (0 >= (dysk_major = register_blkdev(0, DYSK_BD_NAME))) {
    printk(KERN_ERR "dysk: failed to register block device, module is in failed state");
    unload();
    return -1;
  }

  // endpoint
  if (0 != endpoint_start()) {
    printk(KERN_ERR "dysk: failed to start device endpoint, module is in failed state");
    unload();
    return -1;
  }

  if (0 != (success = dysk_worker_init(&default_worker))) {
    printk(KERN_ERR "dysk: failed to init the worker, module is in failed state");
    unload();
    return success;
  }

  // Although the head does not do anywork, we need it
  // during delete dysk routing check dysk_del(..)
  dysks.head.worker = &default_worker;
  dysks.count = -1;
  printk(KERN_INFO "dysk init routine completed successfully");
  return 0;
}

static void __exit _module_teardown(void)
{
  //az_teardown_for_dysk(d);
  printk(KERN_INFO "dysk unloading");
  unload();
}

// ---------------------------------------
// LKM Things
// ---------------------------------------
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Khaled Henidak (Kal) - khnidk@outlook.com");
MODULE_DESCRIPTION("Mount cloud storage as block devices");
MODULE_VERSION("0.0.1");

module_init(_init_module);
module_exit(_module_teardown);
