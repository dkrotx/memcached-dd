#include "memcached.h"
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

#define ARR_SIZE(x) (sizeof(x) / sizeof(x[0]))
#define STATIC_STRLEN(x) ( sizeof(x) - 1 )
#define MAGIC_BYTES "\xe2\x92\x11\x60\xf5\x6c\xba\x8b\x2a\x28\xa7\x04\xe0\x42\xb6\x0e"
#define MAGIC_LEN STATIC_STRLEN(MAGIC_BYTES)

struct snapshot_hdr {
    uint32_t dump_time;
    uint32_t nelems;
    uint8_t  hashpower;
    char sync[MAGIC_LEN];
} __attribute__((__packed__));

struct item_image_hdr {
    uint32_t nbytes;
    uint32_t ttl;
    uint8_t  nkey;
} __attribute__((__packed__));

static int sync_file(FILE *f)
{
    return
#ifdef HAVE_FSYNC
    fsync(fileno(f));
#else
    sync();
#endif
}

static int dd_readheader(const char *path, snapshot_status *st)
{
    struct snapshot_hdr header;

    st->f = fopen(path, "r");
    if (!st->f) {
        fprintf(stderr, "Can't open dumpfile %s: %s\n", path, strerror(errno));
        goto hdr_failed;
    }

    if (-1 == fseek(st->f, -sizeof(header), SEEK_END)) {
        fprintf(stderr, "Can't find header in %s: %s\n", path, strerror(errno));
        goto hdr_failed;
    }

    if (fread(&header, 1, sizeof(header), st->f) != sizeof(header)) {
        fprintf(stderr, "Can't read header from %s: %s\n", path, strerror(errno));
        goto hdr_failed;
    }

    if (memcmp(header.sync, MAGIC_BYTES, MAGIC_LEN)) {
        fprintf(stderr, "Dumpfile %s damaged (while checking sync bytes)\n", path);
        goto hdr_failed;
    }

    st->hashpower = header.hashpower;
    st->nelems    = header.nelems;
    return 1;

hdr_failed:
    if (st->f) {
        fclose(st->f);
        st->f = NULL;
    }
    return 0;
}


snapshot_status *dd_open(const char *file)
{
    static snapshot_status cursnap;
    return (dd_readheader(settings.dump_file, &cursnap)) ? &cursnap : NULL;
}


bool dd_dump(FILE *f)
{
    uint64_t nbytes_total = 0;
    int nexpired = 0, nflushed = 0;
    unsigned ib;
    assoc_storage st;
    struct snapshot_hdr snap_hdr;
    item *items_cache[32];
    bool ok = true;
    rel_time_t flush_time;

    flush_time = (settings.oldest_live != 0 && settings.oldest_live <= current_time) ? settings.oldest_live : 0;

    assoc_get_storage(&st);

    snap_hdr.dump_time = current_time;
    snap_hdr.hashpower = st.hashpower;
    snap_hdr.nelems = 0;

    for(ib = 0; ok && ib < st.nbuckets; ib++) {
        /** Dump elements and do not block usual operations:
         *  For every bucket in assoc list do the following:
         *   - lock cache for a while
         *   - read items incrementing their refs (prevents free())
         *   - unlock cache
         *   - dump items decrementing refs
         */
        int i, n = 0; 
        item *it;
        mutex_lock(&cache_lock);
        for (it = st.buckets[ib]; it && n < ARR_SIZE(items_cache);
             it = it->h_next)
        {
            refcount_incr(&it->refcount);
            items_cache[n++] = it;
        }
        mutex_unlock(&cache_lock);
      
        for (i = 0; ok && i < n; i++) {
            struct item_image_hdr hdr;
            int ttl;

            it  = items_cache[i];
            ttl = it->exptime - snap_hdr.dump_time;
            if (it->time <= flush_time) {
                nflushed++; /* nuked by flush */
            }
            else if (it->exptime && ttl <= 0) {
                /* expired during dump (since lazy expiration) */
                nexpired++;
            }
            else {
                hdr.nbytes = it->nbytes;
                hdr.ttl  = ttl;
                hdr.nkey = it->nkey;

                if (fwrite(&hdr, 1, sizeof(hdr), f) == sizeof(hdr) &&
                    fwrite(ITEM_key(it), 1, hdr.nkey, f) == hdr.nkey &&
                    fwrite(ITEM_data(it), 1, hdr.nbytes, f) == hdr.nbytes) 
                {
                    snap_hdr.nelems++;
                    nbytes_total += hdr.nkey + hdr.nbytes + sizeof(hdr);
                }
                else {
                    ok = false;
                }
            }

            item_remove(it);
        }
    }

    if (ok) {
        memcpy(snap_hdr.sync, MAGIC_BYTES, MAGIC_LEN);
        if (fwrite(&snap_hdr, 1, sizeof(snap_hdr), f) == sizeof(snap_hdr) && 
            fflush(f) == 0 && sync_file(f) == 0) 
        {
            nbytes_total += sizeof(snap_hdr);
            fprintf(stderr,
                "%dMb dumped: %d items (%u expired during dump, %u nuked by flush)\n", 
                (int)(nbytes_total >> 20),
                (int)snap_hdr.nelems,
                nexpired,
                nflushed);

            return true;
        }
    }

    return false;
}


/* load snapshot */
int dd_restore(snapshot_status *st)
{
    unsigned n, nfail = 0;
    char kbuf[KEY_MAX_LENGTH + 2];
    struct item_image_hdr hdr;

    rewind(st->f);

    for (n = 0; n < st->nelems; n++)
    {
        item *it;
        assert(!feof(st->f));

        if (fread(&hdr, 1, sizeof(hdr), st->f) != sizeof(hdr) ||
            fread(&kbuf[0], 1, hdr.nkey, st->f) != hdr.nkey)
            break;

        it = item_alloc(kbuf, hdr.nkey, 0, realtime(hdr.ttl), hdr.nbytes);
        if (it) {
            if (fread(ITEM_data(it), 1, hdr.nbytes, st->f) != hdr.nbytes) {
                item_free(it);
                break;
            }
            
            item_link(it);
        }
        else 
            nfail++;
            
        item_remove(it); /* release reference */
    }
    
    fprintf(stderr, "%d / %d elements read from snapshot (%d failed)\n", n, st->nelems, nfail);
    fclose(st->f);
    st->f = NULL;
    return n;
}
