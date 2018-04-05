#ifndef _DYSK_BDD_H
#define _DYSK_BDD_H

#include <linux/types.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/version.h>

//Completion variables
#include <linux/completion.h>

#define KERNEL_SECTOR_SIZE 512

// Lengths
#define ACCOUNT_NAME_LEN   256
#define SAS_LEN            128
#define DEVICE_NAME_LEN    32
#define BLOB_PATH_LEN      1024
#define HOST_LEN           512
#define IP_LEN             32
#define LEASE_ID_LEN       64

#define DYSK_OK          0 // Healthy and working
#define DYSK_DELETING    1 // Deleting based on user request
#define DYSK_CATASTROPHE 2 // Something is wrong with connection, lease etc.

// We make a distinction between 4.x and 3.x
// kernels. The difference is in BLKDEV and
// sock* apis
#define NEW_KERNEL (LINUX_VERSION_CODE >= KERNEL_VERSION(4,0,0))

// Instance of a mounted disk
typedef struct dysk dysk;

// Definition of one
typedef struct dysk_def dysk_def;

// worker is a big loop that serves the requests for dysks
typedef struct dysk_worker dysk_worker;

// a task is a unit of work for dysk_worker
typedef struct w_task w_task;

// Ends an io request
void io_end_request(dysk *d, struct request *req, int err);

// sets dysk in catastrophe mode
void dysk_catastrophe(dysk *d);

struct dysk_def {
  //device name
  char deviceName[DEVICE_NAME_LEN];

  // count 512 sectors
  size_t sector_count;

  // is readonly device
  int readOnly;

  // storage account name
  char accountName[ACCOUNT_NAME_LEN];

  // sas token
  char sas[SAS_LEN];

  // page blob path including leading /
  char path[BLOB_PATH_LEN];

  // host
  char host[HOST_LEN];

  //ip
  char ip[IP_LEN];

  // Lease Id
  char lease_id[LEASE_ID_LEN];
  //runtime major/minor
  int major;
  int minor;

  int is_vhd; // maintained only to allow get/list cli functions without having to go to the cloud
};


struct dysk {
  // active/deleting/catastrophe
  unsigned int status;

  // slot used by this dysk in track
  unsigned int slot;

  // dysk throttling
  unsigned long throttle_until;

  // i/o queue
  spinlock_t lock;

  // original def used for this dysk
  dysk_def *def;

  // gen disk as a result of hooking to io scheduler
  struct gendisk *gd;

  // working serving this dysk
  dysk_worker *worker;

  // state used by the transfer logic
  void *xfer_state;

  // Linked list pluming
  struct list_head list;
};

// -------------------------------
// Worker details
// -------------------------------
// returned from an excuted task
typedef enum task_result task_result;
// task clean reason
typedef enum task_clean_reason task_clean_reason;
// as tasks register themselves with worker they define a mode
typedef enum task_mode task_mode;
// Task executer prototype function
typedef task_result(*w_task_exec_fn)(w_task *this_task);
// Task clean up
typedef void(*w_task_state_clean_fn)(w_task *this_task, task_clean_reason clean_reason); // General cleaning function is used when queued with clean=null

//enqueues a new task in worker queue
int queue_w_task(w_task *parent_task, dysk *d, w_task_exec_fn exec_fn, w_task_state_clean_fn state_clean_fn, task_mode mode, void *state);
// TODO: Do we need this?
void inline dysk_worker_work_available(dysk_worker *dw);
// Start worker
int dysk_worker_init(dysk_worker *dw);
// Stop worker
void dysk_worker_teardown(dysk_worker *dw);



enum task_clean_reason {
  clean_done             = 1 << 0, // successful compeletion
  clean_timeout          = 1 << 1, // Task is cleaned because it is a timeout
  clean_dysk_del         = 1 << 2, //  Task's dysk is being deleted
  clean_dysk_catastrohpe = 1 << 3, // Task's dysk is undergoing a catastrophe
};

enum task_result {
  done          = 1 << 0, // Task executed will be removed from queue
  retry_now     = 1 << 1, // Task will be retried immediatly
  retry_later   = 1 << 2, // Task will be retried next worker round
  throttle_dysk = 1 << 3, // dysk attached to this task will be throttled (affects all tasks related this dysk)
  catastrophe   = 1 << 4 // dysk failed. dysk failure routine will kick off
};
enum task_mode {
  normal      = 1 << 0, // Task will be throttled when dysk is throttled
  no_throttle = 1 << 1 // task will not be throttled  when dysk is throttled
};

// Dysk work
struct dysk_worker {
  // w_task (linked list head)
  w_task *head;
  // Keep working, signal used to stop
  int working;
  // Number of tasks in queue
  atomic_t count_tasks;
  // Number of throtlled (waiting) tasks in queue
  int count_throttled_tasks;
  // Lock used for add/delete tasks to the queue
  spinlock_t lock;
  // Worker thread
  struct task_struct *worker_thread;
  /*
  if super dysks (dysks with dedicated worker,
  or smaller # of dysks share worker) ever became
  a thing then slab should be elvated to dysk_bdd
  and shared across all workers
  */
  struct kmem_cache *tasks_slab;
};

// worker task -- linked list
struct w_task {
  // Expires on (jiffies)
  unsigned long expires_on;
  // Task mode as defined below
  task_mode mode;
  // Function to execution
  w_task_exec_fn exec_fn;
  // Function to execute, if used worker will call this to clean up
  // otherwise it will use default free routine
  w_task_state_clean_fn clean_fn;
  // Function parameter
  void *state;
  // Dysk
  dysk *d;
  // Linked list pluming
  struct list_head list;
};
#endif
