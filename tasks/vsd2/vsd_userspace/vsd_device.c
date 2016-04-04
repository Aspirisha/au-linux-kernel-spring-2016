#include <fcntl.h>
#include <sys/ioctl.h> 
#include <vsd_ioctl.h>

#include "vsd_device.h"

static const char* DEVICE_FILE_NAME = "/dev/vsd";
static const size_t INITIAL_PAGES = 8;

#define CHECKED_OPEN(DESCRIPTOR_NAME, FLAGS, FAILURE_RETURN) \
    int DESCRIPTOR_NAME = open(DEVICE_FILE_NAME, FLAGS); \
    if (DESCRIPTOR_NAME < 0) \
        return FAILURE_RETURN;

int vsd_init()
{
    // It's not evident what should go here... 
    return 0;
}

int vsd_deinit()
{
    return 0;
}

int vsd_get_size(size_t *out_size)
{
    CHECKED_OPEN(fd, 0, -1);
    vsd_ioctl_get_size_arg_t size_arg;
    int err = ioctl(fd, VSD_IOCTL_GET_SIZE, &size_arg);

    close(fd);
    if (err)
        return err;

    if (!out_size)
        return -1;
    *out_size = size_arg.size;
    return 0;
}

int vsd_set_size(size_t size)
{
    CHECKED_OPEN(fd, 0, -1);
    vsd_ioctl_set_size_arg_t size_arg = {
        .size = size
    };
    int err = ioctl(fd, VSD_IOCTL_SET_SIZE, &size_arg);

    close(fd);
    if (err)
        return err;
    return 0;
}

ssize_t vsd_read(char* dst, off_t offset, size_t size)
{
    CHECKED_OPEN(fd, O_RDONLY, -1);
    ssize_t num_read = -1;
    off_t lseek_res = lseek(fd, offset, SEEK_SET);

    if (lseek_res == offset)
        num_read = read(fd, dst, size);

    close(fd);
    return num_read;
}

ssize_t vsd_write(const char* src, off_t offset, size_t size)
{
    CHECKED_OPEN(fd, O_WRONLY, -1);
    ssize_t num_written = -1;
    off_t lseek_res = lseek(fd, offset, SEEK_SET);

    if (lseek_res == offset)
        num_written = write(fd, src, size);

    close(fd);
    return num_written;
}

void* vsd_mmap(size_t offset)
{
    CHECKED_OPEN(fd, O_RDWR, MAP_FAILED);
    size_t vsd_size;
    void *result = MAP_FAILED;

    if (!vsd_get_size(&vsd_size)) { 
        result = mmap(0, vsd_size - offset, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
    }

    close(fd);
    return result;
}

int vsd_munmap(void* addr, size_t offset)
{
    size_t vsd_size;
    int result = -1;
    if (!vsd_get_size(&vsd_size))
        result =  munmap(addr, vsd_size - offset);

    return result;
}
