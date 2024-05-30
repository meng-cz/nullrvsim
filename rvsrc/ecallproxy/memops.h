
void _rv64spinlock_lock(void *lock, int wait);

void _rv64spinlock_unlock(void *lock);

void _rv64memcpy(void *dst, void *src, unsigned long sz);

void _rv64memset(void *dst, int ch, unsigned long sz);

void _rv64memmove(void *dst, void *src, unsigned long sz);



