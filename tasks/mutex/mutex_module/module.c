#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <uapi/linux/fs.h>
#include <uapi/linux/stat.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/rculist.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include "mutex_ioctl.h"

#define LOG_TAG "[MUTEX_MODULE] "

// this is kernel mutex
typedef struct tgroup_mutex {
    struct hlist_node hnode;
    mutex_id_t id;
    spinlock_t wlock;
    wait_queue_head_t wqh; // tasks waiting on this
} tgroup_mutex_t;

// 1 per process
typedef struct tgroup_mutex_state {
    struct hlist_node hnode;
    pid_t tgid;
    // lock only when adding/deleting mutex
    spinlock_t wlock;
    mutex_id_t next_mid; // seq id. No need to care about overflow.
    struct hlist_head mlist; // list of mutexes for this process
} tgroup_mutex_state_t;

typedef struct system_mutex_state {
    // lock only when adding/deleting tgroup
    spinlock_t wlock;
    struct hlist_head tgstates;
} system_mutex_state_t;

typedef struct mutex_dev {
    struct miscdevice mdev;
    system_mutex_state_t sysmstate;
} mutex_dev_t;

static mutex_dev_t *mutex_dev;

// TODO implement all the missing

static tgroup_mutex_state_t * lookup_tgroup_mutex_state(pid_t tgid) {
    tgroup_mutex_state_t *mstate = NULL;

    hlist_for_each_entry_rcu(mstate, &mutex_dev->sysmstate.tgstates, hnode) {
        if (mstate->tgid == tgid) {
            return mstate;
        }
    }

    return NULL;
}

static tgroup_mutex_t * lookup_mutex(tgroup_mutex_state_t *mstate, mutex_id_t mid) {
    tgroup_mutex_t *mutex = NULL;

    hlist_for_each_entry_rcu(mutex, &mstate->mlist, hnode) {
        if (mutex->id == mid) {
            return mutex;
        }
    }

    return NULL;
}

static void init_system_mutex_state(system_mutex_state_t *sysmstate) {
    spin_lock_init(&sysmstate->wlock);
    INIT_HLIST_HEAD(&sysmstate->tgstates);
}

static void deinit_system_mutex_state(system_mutex_state_t *sysmstate) {
    tgroup_mutex_state_t *mstate = NULL;
    tgroup_mutex_t *mutex = NULL;

    while (!hlist_empty(&sysmstate->tgstates)) {
        struct hlist_node *n = sysmstate->tgstates.first;
        hlist_del(n);

        mstate = hlist_entry(n, typeof(*mstate), hnode);

        while (!hlist_empty(&mstate->mlist)) {
            n = mstate->mlist.first;
            hlist_del(n);
            mutex = hlist_entry(n, typeof(*mutex), hnode);
            kfree(mutex);
        }

        kfree(mstate);
    }
}

static int mutex_dev_open(struct inode *inode, struct file *filp)
{
    pr_notice(LOG_TAG "mutex dev opened\n");
    return 0;
}

static int mutex_dev_release(struct inode *inode, struct file *filp)
{
    pr_notice(LOG_TAG "mutex dev closed\n");
    return 0;
}

// NB ioctl can be preempted!
static long mutex_ioctl_lock_create(mutex_ioctl_lock_create_arg_t __user *uarg)
{
    tgroup_mutex_state_t *mstate = NULL;
    tgroup_mutex_t *mutex = NULL;
    mutex_ioctl_lock_create_arg_t arg;

    rcu_read_lock();
    mstate = lookup_tgroup_mutex_state(current->tgid);
    if (!mstate) {
        spin_lock(&mutex_dev->sysmstate.wlock);
        mstate = lookup_tgroup_mutex_state(current->tgid);
        if (mstate) {
            spin_unlock(&mutex_dev->sysmstate.wlock);
            rcu_read_unlock();
            goto tgid_present;
        }
        rcu_read_unlock();

        // no one owns it yet, no need in rcu_locks
        mstate = (tgroup_mutex_state_t*) 
            kzalloc(sizeof(tgroup_mutex_state_t), GFP_KERNEL);
        if (!mstate) {
            spin_unlock(&mutex_dev->sysmstate.wlock);
            return -ENOMEM;
        }

        mstate->tgid = current->tgid;
        mstate->next_mid = 0;
        spin_lock_init(&mstate->wlock);
        INIT_HLIST_HEAD(&mstate->mlist);
        INIT_HLIST_NODE(&mstate->hnode);

        hlist_add_head_rcu(&mstate->hnode, &mutex_dev->sysmstate.tgstates);

        spin_unlock(&mutex_dev->sysmstate.wlock);
    }

tgid_present:
    mutex = kzalloc(sizeof(tgroup_mutex_t), GFP_KERNEL);
    if (!mutex) {
        return -ENOMEM;
    }
    INIT_HLIST_NODE(&mutex->hnode);
    spin_lock_init(&mutex->wlock);

    spin_lock(&mstate->wlock);
    hlist_add_head_rcu(&mutex->hnode, &mstate->mlist);
    mutex->id = mstate->next_mid;
    mstate->next_mid++;
    init_waitqueue_head(&mutex->wqh);
    spin_unlock(&mstate->wlock);

    arg.id = mutex->id;

    if (copy_to_user(uarg, &arg, sizeof(mutex_ioctl_lock_create_arg_t))) {
        hlist_del_rcu(&mutex->hnode);
        synchronize_rcu();
        kfree(mutex);
        return -EFAULT;
    }

    return 0;
}

static long mutex_ioctl_lock_destroy(mutex_ioctl_lock_destroy_arg_t __user *uarg)
{
    tgroup_mutex_state_t *mstate = NULL;
    tgroup_mutex_t *mutex = NULL;
    mutex_ioctl_lock_destroy_arg_t arg;

    if (copy_from_user(&arg, uarg, sizeof(arg))) {
        return -EFAULT;
    }

    rcu_read_lock();
    mstate = lookup_tgroup_mutex_state(current->tgid);
    if (!mstate) { // this guy doesn't have registered mutexes at all!
        rcu_read_unlock();
        return -EINVAL;
    }

    mutex = lookup_mutex(mstate, arg.id);
    if (!mutex) { // no such mutex registered
        rcu_read_unlock();
        return -EINVAL;
    }

    hlist_del_rcu(&mutex->hnode);
    rcu_read_unlock();

    wake_up_all(&mutex->wqh);
    spin_lock(&mstate->wlock);
    synchronize_rcu();
    kfree(mutex);
    spin_unlock(&mstate->wlock);

    return 0;
}

static long mutex_queue_wait(shared_spinlock_t *spinlock, mutex_id_t mid)
{
    DEFINE_WAIT(wait);
    long ret = 0;
    tgroup_mutex_state_t *mstate = NULL;
    tgroup_mutex_t *mutex = NULL;

    rcu_read_lock();
    mstate = lookup_tgroup_mutex_state(current->tgid);
    if (!mstate) {
        rcu_read_unlock();
        return -EINVAL;
    }

    mutex = lookup_mutex(mstate, mid);
    if (!mutex) {
        rcu_read_unlock();
        return -EINVAL;
    }

    spin_lock(&mutex->wlock);
    // Check for probably lost wakeup
    if (!shared_spin_islocked(spinlock)) {
        spin_unlock(&mutex->wlock);
        rcu_read_unlock();
        return 0;
    }

    prepare_to_wait_exclusive(&mutex->wqh, &wait, TASK_INTERRUPTIBLE);
    spin_unlock(&mutex->wlock);
    mstate = NULL;
    mutex = NULL;
    rcu_read_unlock();

    schedule();
    if (signal_pending(current))
        ret = -ERESTARTSYS;

    rcu_read_lock();
    mstate = lookup_tgroup_mutex_state(current->tgid);
    if (!mstate) {
        rcu_read_unlock();
        return -EINVAL;
    }

    mutex = lookup_mutex(mstate, mid);
    if (!mutex) {
        rcu_read_unlock();
        return -EINVAL;
    }
    finish_wait(&mutex->wqh, &wait);
    rcu_read_unlock();
    return ret;
}

static long mutex_queue_wake(mutex_id_t mid) {
    tgroup_mutex_state_t *mstate = NULL;
    tgroup_mutex_t *mutex = NULL;

    rcu_read_lock();
    mstate = lookup_tgroup_mutex_state(current->tgid);
    if (!mstate) {
        rcu_read_unlock();
        return -EINVAL;
    }
    mutex = lookup_mutex(mstate, mid);
    if (!mutex) {
        rcu_read_unlock();
        return -EINVAL;
    }

    
    wake_up_interruptible(&mutex->wqh);
    rcu_read_unlock();

    return 0;
}

static long mutex_ioctl_lock_wait(mutex_ioctl_lock_wait_arg_t *uarg)
{
    // Note: to perform cross kernel-userspace CAS
    // your code can work with userspace addresses directly.
    // This is needed for simplification.

    while (1) { 
        long res = mutex_queue_wait(uarg->spinlock, uarg->id);

        if (!res) 
            return 0;

        if (res == -EINVAL) {// either mutex is destroyed or the whole process 
            return res;
        }

        return res; // signal_pending?
    }
    return -EBUSY;
}

static long mutex_ioctl_lock_wake(mutex_ioctl_lock_wake_arg_t *uarg)
{
    // Note: to perform cross kernel-userspace CAS
    // your code can work with userspace addresses directly.
    // This is needed for simplification.
    if (!shared_spin_islocked(uarg->spinlock))  // we are not owning mutex, screw it!
        return -EINVAL; 

    if (mutex_queue_wake(uarg->id))
        return -EINVAL; 

    // don't unlock!! because the awaken guy will need to acquire it anyway
    //shared_spin_unlock(uarg->spinlock); // DO NOT UNCOMMENT!
    return 0;
}

static long mutex_dev_ioctl(struct file *filp, unsigned int cmd,
        unsigned long arg)
{
    switch(cmd) {
        case MUTEX_IOCTL_LOCK_CREATE:
            return mutex_ioctl_lock_create(
                    (mutex_ioctl_lock_create_arg_t*)arg);
        case MUTEX_IOCTL_LOCK_DESTROY:
            return mutex_ioctl_lock_destroy(
                    (mutex_ioctl_lock_destroy_arg_t*)arg);
        case MUTEX_IOCTL_LOCK_WAIT:
            return mutex_ioctl_lock_wait(
                    (mutex_ioctl_lock_wait_arg_t*)arg);
        case MUTEX_IOCTL_LOCK_WAKE:
            return mutex_ioctl_lock_wake(
                    (mutex_ioctl_lock_wake_arg_t*)arg);
        default:
            return -ENOTTY;
    }
}

static struct file_operations mutex_dev_fops = {
    .owner = THIS_MODULE,
    .open = mutex_dev_open,
    .release = mutex_dev_release,
    .unlocked_ioctl = mutex_dev_ioctl
};

static int __init mutex_module_init(void)
{
    int ret = 0;
    mutex_dev = (mutex_dev_t*)
        kzalloc(sizeof(*mutex_dev), GFP_KERNEL);
    if (!mutex_dev) {
        ret = -ENOMEM;
        pr_warn(LOG_TAG "Can't allocate memory\n");
        goto error_alloc;
    }
    mutex_dev->mdev.minor = MISC_DYNAMIC_MINOR;
    mutex_dev->mdev.name = "mutex";
    mutex_dev->mdev.fops = &mutex_dev_fops;
    mutex_dev->mdev.mode = S_IRUSR | S_IRGRP | S_IROTH
        | S_IWUSR| S_IWGRP | S_IWOTH;
    init_system_mutex_state(&mutex_dev->sysmstate);

    if ((ret = misc_register(&mutex_dev->mdev)))
        goto error_misc_reg;

    pr_notice(LOG_TAG "Mutex dev with MINOR %u"
        " has started successfully\n", mutex_dev->mdev.minor);
    return 0;

error_misc_reg:
    kfree(mutex_dev);
    mutex_dev = NULL;
error_alloc:
    return ret;
}

static void __exit mutex_module_exit(void)
{
    pr_notice(LOG_TAG "Removing mutex device %s\n", mutex_dev->mdev.name);
    misc_deregister(&mutex_dev->mdev);
    deinit_system_mutex_state(&mutex_dev->sysmstate);
    kfree(mutex_dev);
    mutex_dev = NULL;
}

module_init(mutex_module_init);
module_exit(mutex_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AU user space mutex kernel side support module");
MODULE_AUTHOR("Kernel hacker!");
