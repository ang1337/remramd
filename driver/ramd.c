#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/string.h>
#include <linux/bvec.h>

/*
 * WARNING:
 * This device driver works on >=5.0 kernel versions
 * Check your kernel version prior to compiling and insmoding 
 * */

// Maximum depth of request I/O queue
#define MQ_Q_DEPTH 128
#define GB 1024 * 1024 * 1024

MODULE_AUTHOR("ang1337");
MODULE_DESCRIPTION("RAM disk implementation");
MODULE_LICENSE("GPL");

// this structure represents the most important driver metadata
struct ramd {
    // this buffer will carry all data from/to the virtual drive
    u8 *data;
    // represents virtual drive within a kernel
    struct gendisk *gd;
    struct blk_mq_tag_set tag_set;
    // pointer to the top of the entire request queue
    struct request_queue *rq_queue;
    unsigned long size; // measured in bytes
    // amount of virtual drive users at any point of time
    // atomic type eliminates race condition for this variable
    atomic_t openers;
};

static struct ramd *ramd_dev = NULL;
// let the kernel choose a major number automagically
static unsigned ramd_major = 0;
static const u8 *ramd_name = "ramd";
// the device itself + partition that can be formatted to any kind of filesystem and be mounted further
static int dev_cnt = 2; 
static unsigned blk_size = 4096; // block != sector, blocks are 4 KB in my implementation
// 1 GB default RAM disk capacity
// In case of invalid user-defined disk size, the size will be aligned during driver initialization 
// User is expected to define custom disk capacity in MB
static unsigned long ramd_size = GB; 
module_param(ramd_size, ulong, 0);

// function declarations
static int __init ramd_init(void);
static void __exit ramd_exit(void);
static int setup_ramd_device(void);
static void deallocate_dev_resources(void);
static blk_status_t rmq_handler(struct blk_mq_hw_ctx *, const struct blk_mq_queue_data *);
static blk_status_t process_current_request(struct request *, unsigned long *);
static int ramd_open(struct block_device *, fmode_t);
static void ramd_release(struct gendisk *, fmode_t);
static int ramd_ioctl(struct block_device *, fmode_t, unsigned, unsigned long);
static unsigned long ulong_pow(unsigned long, unsigned);
static void align_disk_size(void);

// force the disk size to be the result of 2^x in case of invalid driver user-defined argument (default is 1GB, minimum is 500 MB)
static void align_disk_size(void) {
    // minimal RAM disk size = 500 MB
    unsigned default_pow = 29; // 2 ^ 29 B = 500 MB
    unsigned long alignment = ulong_pow(2, default_pow);
    while (ramd_size > alignment) {
        alignment = ulong_pow(2, ++default_pow);
    } 
    ramd_size = alignment;
}

// calculate the positive exponent, unsigned integer overflow is possible for huge numbers (>2^64)
// but the possible overflow itself doesn't seem harmful anyway in this case
static unsigned long ulong_pow(unsigned long base, unsigned exponent) {
    unsigned i;
    unsigned long result = 1;
    if (!exponent) {
        return result;
    }
    for (i = 0; i < exponent; i++) {
        result *= base;
    }
    return result;
}

static int ramd_open(struct block_device *blk_dev, fmode_t mode) {
    atomic_inc(&ramd_dev->openers);
    pr_info("%s : the device has been opened, current openers: %d", ramd_name, ramd_dev->openers.counter);
    return 0;
}

static void ramd_release(struct gendisk *gdsk, fmode_t mode) {
    atomic_dec(&ramd_dev->openers);
    pr_info("%s : the device has been released, current openers: %d", ramd_name, ramd_dev->openers.counter);
}

// there is no real need for IOCTL, nowadays utilities like fdisk don't require
// disk geometry info via ioctl anymore, so the useful code can be neglected here
static int ramd_ioctl(struct block_device *blk_dev, fmode_t mode, unsigned cmd, unsigned long arg) {
    pr_info("%s : ioctl shim", ramd_name);
    return -ENOTTY;
}

// block device operations struct contains entry points for syscalls
static const struct block_device_operations blkops = {
    .owner   = THIS_MODULE,
    .open    = ramd_open,
    .release = ramd_release,
    .ioctl   = ramd_ioctl
}; 

// block multiqueue operations' entry point is called by kernel on every I/O request
static const struct blk_mq_ops mqops = {
    .queue_rq = rmq_handler
};

// write to/reads from the disk via memcpy over the driver data buffer
// The device driver memory is logically separated to blocks (that are mapped to sectors)
// Arguments:
// 1) Pointer to the current request
// 2) Pointer to the amount of read bytes
// Return value: block device I/O status. Failure = no data read from/written to the RAM disk
static blk_status_t process_current_request(struct request *req, unsigned long *blk_transferred) {
    blk_status_t retval = BLK_STS_OK;
    struct bio_vec bvec;
    struct req_iterator riter;
    struct ramd *dev = req->q->queuedata;
    loff_t offset = blk_rq_pos(req) * SECTOR_SIZE;
    void *data;
    unsigned long data_len;
    // iterate over all bio vectors passed via I/O request
    rq_for_each_segment(bvec, req, riter) {
        data = page_address(bvec.bv_page) + bvec.bv_offset;
        data_len = bvec.bv_len;
        if (((unsigned long)offset + data_len) < ramd_size) {
            // write data to the drive
            if (rq_data_dir(req) == WRITE) {
                memcpy(dev->data + offset, data, data_len);
            } else { // read data from the drive
                memcpy(data, dev->data + offset, data_len);
            }
            pr_info("%s: sector: %lu | offset %lu", ramd_name,
                                     (unsigned long)offset >> SECTOR_SHIFT,
                                                    (unsigned long)offset);
        } else {
            pr_info("%s: overflow, I/O is declined -> offset %lu | data len: %lu | dev size: %lu", ramd_name,
                                                                                       (unsigned long)offset,
                                                                                       data_len,
                                                                                       ramd_size);
            // I/O has been failed
            retval = BLK_STS_IOERR;
            goto io_fail;
        }
        offset += data_len;
        *blk_transferred += data_len;
    }
io_fail:
    return retval;
}

// this is blk_mq_ops entry point that is called by the kernel on each I/O request
// The first argument is irrelevant for this driver, because it is not a real hardware
// The second argument is a pointer to I/O multiqueue 
static blk_status_t rmq_handler(struct blk_mq_hw_ctx *hw_ctx, 
                                const struct blk_mq_queue_data *mq_data) {
    blk_status_t retval = BLK_STS_OK;
    unsigned long nbytes = 0;
    struct request *curr_rq = mq_data->rq; // get the next request to be processed
    blk_mq_start_request(curr_rq);
    // read/write actual data in I/O request
    retval = process_current_request(curr_rq, &nbytes);
    // detect I/O error
    if (blk_update_request(curr_rq, retval, nbytes)) {
        BUG();
    } else {
        pr_info("%s: %lu bytes has been processed during the last request handling", ramd_name, 
                                                                                     nbytes);
    }
    // notifies the kernel that the request processing has ended
    __blk_mq_end_request(curr_rq, retval);
    return retval;
}

static int setup_ramd_device(void) {
    struct gendisk *this_disk = NULL;
    // this struct is not very big, so for cache friendliness we can demand 
    // that this memory will be physically contiguous
    ramd_dev = kzalloc(sizeof(struct ramd), GFP_KERNEL); 
    if (!ramd_dev) {
        pr_info("%s : can't allocate %lu bytes of contiguous memory", ramd_name, 
                                                                      sizeof(struct ramd));
        return -ENOMEM;
    }
    ramd_dev->size = ramd_size;
    // vmalloc-like functions doesn't demand physically contiguous heap memory from the kernel
    // so it is the best option for big in-kernel heap allocations 
    ramd_dev->data = vzalloc(ramd_size);
    if (!ramd_dev->data) {
        pr_info("%s : cannot allocate %lu bytes for RAM disk device data", 
                                                    ramd_name, ramd_size);
        return -ENOMEM;
    }
    // allocate request I/O multiqueue
    ramd_dev->rq_queue = blk_mq_init_sq_queue(&ramd_dev->tag_set, 
                                              &mqops, 
                                              MQ_Q_DEPTH, 
                                              BLK_MQ_F_SHOULD_MERGE);
    if (IS_ERR(ramd_dev->rq_queue)) {
        pr_info("%s : cannot allocate request I/O multiqueue for the device", 
                                                                  ramd_name);
        return PTR_ERR(ramd_dev->rq_queue);
    }
    ramd_dev->rq_queue->queuedata = ramd_dev;
    blk_queue_logical_block_size(ramd_dev->rq_queue, blk_size);
    if (!(ramd_dev->gd = alloc_disk(dev_cnt))) {
        pr_info("%s : cannot allocate the gendisk structure for the device", 
                                                                 ramd_name);
        return -ENOMEM;
    }
    this_disk = ramd_dev->gd;
    this_disk->major = ramd_major;
    this_disk->first_minor = 0;
    this_disk->fops = &blkops;
    this_disk->private_data = ramd_dev;
    this_disk->queue = ramd_dev->rq_queue;
    strlcpy(this_disk->disk_name, ramd_name, DISK_NAME_LEN - 1); 
    // capacity is measured in sectors, not blocks, which are 512 bytes always
    set_capacity(this_disk, ramd_size / SECTOR_SIZE);
    add_disk(this_disk);
    atomic_set(&ramd_dev->openers, 0);
    pr_info("%s : the device was successfully registered and is ready for use",
                                                                    ramd_name);
    return 0;
}

static int __init ramd_init(void) {
    int retval = 0;
    // let the kernel to choose the major number for the device
    ramd_major = register_blkdev(ramd_major, ramd_name);
    if (ramd_major <= 0) {
        pr_info("%s : unable to register the device", ramd_name);
        return -EBUSY;
    }
    if (ramd_size != GB) {
        ramd_size *= (1024 * 1024); // from MB to bytes
    }
    // align the drive size according to 2^x numbers
    align_disk_size();
    retval = setup_ramd_device();
    if (retval) {
        pr_info("%s : unable to register the device", ramd_name);
        deallocate_dev_resources();
        unregister_blkdev(ramd_major, ramd_name);
    }
    return retval;
}

static void deallocate_dev_resources(void) {
    if (ramd_dev) {
        if (ramd_dev->data) {
            vfree(ramd_dev->data);
            ramd_dev->data = NULL;
        }
        if (ramd_dev->gd) {
            del_gendisk(ramd_dev->gd);
        }
        if (ramd_dev->rq_queue) {
            blk_cleanup_queue(ramd_dev->rq_queue);
            ramd_dev->rq_queue = NULL;
        }
        if (ramd_dev->tag_set.tags) {
            blk_mq_free_tag_set(&ramd_dev->tag_set);
        }
        if (ramd_dev->gd) {
            put_disk(ramd_dev->gd);
            ramd_dev->gd = NULL;
        }
        if (ramd_dev->data) {
            vfree(ramd_dev->data);
            ramd_dev->data = NULL;
        }
        kfree(ramd_dev);
        ramd_dev = NULL;
        pr_info("%s : device heap memory is fully deallocated", ramd_name);
    }
}

static void __exit ramd_exit(void) {
    deallocate_dev_resources();
    if (ramd_major > 0) {
        unregister_blkdev(ramd_major, ramd_name);
    }
}

module_init(ramd_init);
module_exit(ramd_exit);
