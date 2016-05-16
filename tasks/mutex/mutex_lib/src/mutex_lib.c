#include <mutex.h>
#include <fcntl.h>
#include <stdio.h>

// TODO implement userspace part of mutex library

static int mutex_fd = -1;

#define LOCKFREE_VALUE_UPDATE(PTR, OLD_VAL, NEW_VAL) {            \
    size_t OLD_VAL = *PTR;                                        \
    while (1) {                                                   \
        if (__sync_bool_compare_and_swap(PTR, OLD_VAL, NEW_VAL)) {\
            break;                                                \
        }                                                         \
        OLD_VAL = m->kwaiters_cnt;                                \
        cpu_relax();                                              \
    }                                                             \
}

mutex_err_t mutex_init(mutex_t *m)
{
    mutex_ioctl_lock_create_arg_t arg;
    int ret = ioctl(mutex_fd, MUTEX_IOCTL_LOCK_CREATE, &arg);
    if (ret < 0)
        return MUTEX_INTERNAL_ERR;

    m->kid = arg.id;

    m->kwaiters_cnt = 0;
    shared_spinlock_init(&m->spinlock);
    return MUTEX_OK;
}

mutex_err_t mutex_deinit(mutex_t *m)
{
    mutex_ioctl_lock_destroy_arg_t arg;
    arg.id = m->kid;

    shared_spin_trylock(&m->spinlock);
    int ret = ioctl(mutex_fd, MUTEX_IOCTL_LOCK_DESTROY, &arg);

    return ret;
}

mutex_err_t mutex_lock(mutex_t *m)
{
    if (shared_spin_trylock(&m->spinlock)) {
        return MUTEX_OK;
    }

    LOCKFREE_VALUE_UPDATE(&m->kwaiters_cnt, x, x+1);
    mutex_ioctl_lock_wait_arg_t arg;
    arg.spinlock = &m->spinlock;
    arg.id = m->kid;
    int ret = ioctl(mutex_fd, MUTEX_IOCTL_LOCK_WAIT, &arg);
    return ret < 0 ? MUTEX_INTERNAL_ERR : MUTEX_OK;

    return MUTEX_OK;
}

mutex_err_t mutex_unlock(mutex_t *m)
{
    if (!shared_spin_islocked(&m->spinlock))
        return MUTEX_OK;

try_exit:
    shared_spin_unlock(&m->spinlock);
    if (m->kwaiters_cnt) {
        if (!shared_spin_one_try_lock(&m->spinlock)) {
            // Somebody has already acqired the lock. No need to wake 
            // and no need to change waiters count
            return MUTEX_OK; 
        }

        while (m->kwaiters_cnt > 0) {
            // only one thread can decrement counter for we do it under locked spinlock
            // but nobody forbids other threads to increase it. So we need CAS
            LOCKFREE_VALUE_UPDATE(&m->kwaiters_cnt, x, x-1);
            mutex_ioctl_lock_wake_arg_t arg;
            arg.spinlock = &m->spinlock;
            arg.id = m->kid;

            if (!ioctl(mutex_fd, MUTEX_IOCTL_LOCK_WAKE, &arg)) {
                return MUTEX_OK;
            }
        }
        goto try_exit;
    }

    return MUTEX_OK;
}

mutex_err_t mutex_lib_init()
{
    if (mutex_fd < 0) {
        mutex_fd = open("/dev/mutex", O_RDWR);
        return mutex_fd < 0 ? MUTEX_INTERNAL_ERR : MUTEX_OK;
    }
    return MUTEX_INTERNAL_ERR; // tried init more than once
}

mutex_err_t mutex_lib_deinit()
{
    if (mutex_fd < 0) // not inited!
        return MUTEX_INTERNAL_ERR;
    close(mutex_fd);
    mutex_fd = -1;
    return MUTEX_OK;
}
