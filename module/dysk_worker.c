#include <linux/types.h>
#include <linux/list.h>
#include <linux/kthread.h>
#include <linux/jiffies.h>

#include "dysk_bdd.h"
/*
Worker is a giant infinite loop over a linked list
of tasks. Each task can register using queue_w_task function. Tasks
register using a state which will be passed on to task upon execution.
A task can also register a cleanup routine which will be called
when the function is completed. If no clean routine registered
a default kfree(state).

When a task executes it can return one of the following results
done: task is completed, the cleanup routine will be called
retry_now: task will be retried immediately
retry_later: task will be executed again in next worker round.
throttle: puts the dysk linked to the task in throttle mode.
catastrophe: puts the dysk in catastrophe mode.

in addition to throttling the worker also manages the timeout
(expiration)for tasks.

Finally when a dysk is deleted or in catastrophe mode the worker
does not execute linked tasks instead calls the clean up routines.

all tasks are expected to be non-blocking mode.
*/

#define W_TASK_TIMEOUT jiffies + (300 * HZ)
#define DYSK_THROTTLE_DEFAULT jiffies + (HZ / 10)
#define WORKER_SLAB_NAME "dysk_worker_tasks"
// Default clean up function for state, we use kfree
void default_w_task_state_clean(w_task *this_task, task_clean_reason clean_reason)
{
  if (this_task->state) {
    kfree(this_task->state);
    this_task->state = NULL;
  }
}

int queue_w_task(w_task *parent_task, dysk *d, w_task_exec_fn exec_fn, w_task_state_clean_fn state_clean_fn, task_mode mode, void *state)
{
  w_task *w       = NULL;
  dysk_worker *dw = NULL;
  dw = d->worker;
  w = kmem_cache_alloc(dw->tasks_slab, GFP_NOIO);

  if (!w) return -ENOMEM;

  memset(w, 0, sizeof(w_task));
  w->mode       = mode;
  w->state      = state;
  w->clean_fn   = (NULL != state_clean_fn) ?  state_clean_fn : &default_w_task_state_clean;
  w->exec_fn    = exec_fn;
  w->d          = d;
  w->expires_on = (NULL != parent_task) ? parent_task->expires_on : W_TASK_TIMEOUT;
  // add it to the queue
  spin_lock(&dw->lock);
  list_add_tail(&w->list, &dw->head->list);
  spin_unlock(&dw->lock);
  // Increase # of tasks
  atomic_inc(&d->worker->count_tasks);
  return 0;
}
// -----------------------------
// Worker Big Loop
// -----------------------------

// Executes a single task
static void execute(dysk_worker *dw, w_task *w)
{
  // Tasks returning retry_now will be executed to max then retried later.
#define max_retry_now_count 3 // Max # of retrying a task that said retry now
  task_result taskresult;
  task_clean_reason clean_reason = clean_done;
  int execCount     = 0;
  dysk *d;
  d = w->d;

  // if dysk is deleting or catastrophe dequeue
  if (DYSK_OK != w->d->status) {
    if (DYSK_DELETING == d->status) clean_reason = clean_dysk_del;

    if (DYSK_CATASTROPHE == d->status) clean_reason = clean_dysk_catastrohpe;

    goto dequeue_task;
  }

  // if dysk was throttled, check if we still need to be
  if (0 != w->d->throttle_until && time_after(jiffies, w->d->throttle_until)) {
    printk(KERN_INFO "dysk: %s throttling is completed", d->def->deviceName);
    d->throttle_until = 0;
  }

  // dysk is throttled only tasks marked with no_throttle will execute
  if (0 != d->throttle_until && no_throttle != w->mode)
    return;

  while (execCount < max_retry_now_count) {
    execCount++;
    taskresult =  w->exec_fn(w);

    switch (taskresult) {
      case done:
        goto dequeue_task;

      case retry_now:
        continue;

      case retry_later:
        break;

      case throttle_dysk: {
        if (0 == d->throttle_until) {
          d->throttle_until = DYSK_THROTTLE_DEFAULT;
          printk(KERN_INFO "dysk: %s is entering throttling mode", d->def->deviceName);
        }

        goto dequeue_task;
      }

      case catastrophe: {
        dysk_catastrophe(d);
        clean_reason = clean_dysk_catastrohpe;
        goto dequeue_task;
      }
    }
  }

  // has expired?
  if (time_after(jiffies, w->expires_on)) {
    printk(KERN_INFO "This task is timing out");
    clean_reason = clean_timeout;
    goto dequeue_task;
  }

  return;
dequeue_task:
  // dequeue
  spin_lock(&dw->lock);
  list_del(&w->list);
  spin_unlock(&dw->lock);
  // decrease counter
  atomic_dec(&dw->count_tasks);
  // clean
  w->clean_fn(w, clean_reason);
  // free
  kmem_cache_free(dw->tasks_slab, w);
}
// big loop
static int work_thread_fn(void *args)
{
  dysk_worker *dw;
  dw = (dysk_worker *) args;
  printk(KERN_INFO "Dysk worker starting");

  while (!kthread_should_stop()) {
    w_task *t, *next;
    // loop and execute
    list_for_each_entry_safe(t, next, &dw->head->list, list)
    execute(dw, t);

    if (0 == atomic_read(&dw->count_tasks)) {
      // Yield cpu if we have no work.
      set_current_state(TASK_INTERRUPTIBLE);
      schedule_timeout(HZ / 1000);
    }
  }

  dw->working = 0;
  return 0;
}
// -----------------------------
// init + tear down routines
// -----------------------------
int dysk_worker_init(dysk_worker *dw)
{
  w_task *w = NULL;
  // allocate slab
  dw->tasks_slab = kmem_cache_create(WORKER_SLAB_NAME,
                                     sizeof(w_task),
                                     0, /*no special behavior */
                                     0, /* no alignment a cache miss is ok, for now */
                                     NULL /*let kernel create pages */);

  if (NULL == dw->tasks_slab) {
    printk(KERN_ERR "Failed to create slab cache for worker");
    goto fail;
  }

  w = kmem_cache_alloc(dw->tasks_slab, GFP_NOIO);

  if (!w) goto fail;

  memset(w, 0, sizeof(w_task));
  dw->head = w;
  // stop signal
  dw->working = 1;
  // count of tasks
  atomic_set(&dw->count_tasks, 0);
  // init queue
  INIT_LIST_HEAD(&dw->head->list);
  // init the lock
  spin_lock_init(&dw->lock);
  // Create worker thread
  // We are using one worker per entire dysk, hence the static name
  dw->worker_thread = kthread_run(work_thread_fn, dw, "dysk-worker-%d", 0);

  if (IS_ERR(dw->worker_thread)) goto fail;

  return 0;
fail:
  dysk_worker_teardown(dw);
  return -ENOMEM;
}

void dysk_worker_teardown(dysk_worker *dw)
{
  if (!dw) return;

  // assuming that stop func deallocates the memory allocated for worker_thread
  if (dw->worker_thread) {
    kthread_stop(dw->worker_thread);

    while (1 == dw->working) {
      printk(KERN_INFO "Waiting for worker to stop..");
      set_current_state(TASK_INTERRUPTIBLE);
      schedule_timeout(1 * HZ);
    }
  }

  // if head object is there free it.
  if (dw->head) kmem_cache_free(dw->tasks_slab, dw->head);

  // destroy the cache
  if (dw->tasks_slab) kmem_cache_destroy(dw->tasks_slab);
}
