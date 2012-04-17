#ifndef MEMCACHED_DD_H
#define MEMCACHED_DD_H

#include <stdio.h>

typedef struct snapshot_status_ {
    FILE *f;
    int hashpower;
    unsigned nelems;
} snapshot_status;

snapshot_status *dd_open(const char *file);
bool dd_dump(FILE *f);
int  dd_restore(snapshot_status *st);

#endif /* MEMCACHED_DD_H */
