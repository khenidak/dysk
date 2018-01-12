#ifndef _AZ_H
#define _AZ_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/genhd.h>
#include <linux/errno.h>

#include "dysk_bdd.h"

void test_request(void);


// Module init/teardown
int az_init(void);
void az_teardown(void);
// Init and tear routines (for every dysk)
int az_init_for_dysk(dysk *d);
void az_teardown_for_dysk(dysk *d);

int az_do_request(dysk *d, struct request *req);

#endif
