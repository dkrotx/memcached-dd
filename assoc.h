/* associative array */
void assoc_init(const int hashpower_init);
item *assoc_find(const char *key, const size_t nkey, const uint32_t hv);
int assoc_insert(item *item, const uint32_t hv);
void assoc_delete(const char *key, const size_t nkey, const uint32_t hv);
void do_assoc_move_next_bucket(void);
int start_assoc_maintenance_thread(void);
void stop_assoc_maintenance_thread(void);

typedef struct _assoc_storage
{
    item **buckets;
    unsigned int nbuckets;
    unsigned int hashpower;
} assoc_storage;

void assoc_get_storage(assoc_storage *storage);
bool assoc_lock_expansion(bool lock);

/* get power enought to store up to n items */
unsigned int assoc_getpower(unsigned n);

extern pthread_mutex_t assoc_expansion_lock;
