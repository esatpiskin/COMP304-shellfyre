#include<linux/kernel.h>
#include<linux/init.h>
#include<linux/module.h>
#include<linux/kdev_t.h>
#include<linux/fs.h>
#include<linux/cdev.h>
#include<linux/device.h>
#include<linux/slab.h>
#include<linux/uaccess.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <stdlib.h>

/* TODO 1: define the size of the dynamically
*  allocated memory region as a macro here and
*  declare the global pointer variable that will
*  point to a dynamically allocated memory block
*/
#define mem_size 1024

// Kernel buffer for writing to kernel space.
uint8_t *kernel_buffer;
// Function for getting task struct for a process pid.
extern struct task_struct *find_task_by_vpid(pid_t nr);

dev_t dev = 0;

static struct class *dev_class;
static struct cdev my_cdev;

static int __init my_driver_init(void);
static void __exit my_driver_exit(void);
static int my_open(struct inode *inode, struct file *file);
static int my_release(struct inode *inode, struct file *file);
static ssize_t my_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t my_write(struct file *filp, const char *buf, size_t len, loff_t *off);

static struct file_operations fops =
{
	.owner	= THIS_MODULE,
	.read 	= my_read,
	.write	= my_write,
	.open	= my_open,
	.release = my_release,
};

// node struct for DFS and BFS.
struct node {
    int data;
    char name[256];
    struct node* left;
    struct node* right;
};

// Helper function for DFS and BFS.
struct node* newNode(int data,char* name)
{
    struct node* node = (struct node*)
        malloc(sizeof(struct node));
    node->data = data;
    strcpy(node->name,name);
    node->left = NULL;
    node->right = NULL;
 
    return (node);
}

// DFS function.
void printInorder(struct node* node)
{
    if (node == NULL)
        return;
 
    /* first recur on left child */
    printInorder(node->left);
 
    /* then print the data of node */
    printf("%s%s %s%d\n","Process Name: ",node->name,"Process PID: ", node->data);
 
    /* now recur on right child */
    printInorder(node->right);
}


static int my_open(struct inode *inode, struct file * file)
{
/* TODO 2: dynamically allocate a single block
*  of memory region here
*/
	if((kernel_buffer = kmalloc(mem_size, GFP_KERNEL)) == 0) {
		printk(KERN_INFO "Cannot allocate the memory to the kernel...\n");
		return -1;
	}
	printk(KERN_INFO "Device file opened...\n");
	return 0;
}

static int my_release(struct inode *inode, struct file *file)
{
/* TODO 3: free the dynamically allocated memory region here
*/
	kfree(kernel_buffer);
	printk(KERN_INFO "Device FILE closed...\n");
	return 0;
}

static ssize_t my_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
/* TODO 4: make the user application read the content of
*  the dynamically allocated memory region here
*/
	copy_to_user(buf, kernel_buffer, mem_size);
	printk(KERN_INFO "Data read : BONE...\n");
	return mem_size;
}

// Writing to kernel space and doing DFS or BFS.
static ssize_t my_write(struct file *filp,const char __user *buf, size_t len, loff_t* off)
{
/* TODO 5: make the user application write to
*  the dynamically allocated memory region here
*/
	copy_from_user(kernel_buffer, buf, len);
	//printk(KERN_INFO "Data is written successfully...\n");

    char *token=strtok(kernel_buffer,">>");
    int root_pid=atoi(token);
    token=strtok(NULL,">>");
    char sub_selection[2];
    strcpy(sub_selection,token);

    // CHecking for -d or -b.
    if(!strcmp("-d",sub_selection)){
        struct task_struct *root_task=find_task_by_vpid(root_pid);
        

    }else if(!strcmp("-b",sub_selection)){

    }else{
        printk(KERN_INFO "Error while pstraversing.\n");
    }

	return len;
}

static int __init my_driver_init(void)
{
	/* Allocating Major number dynamically*/
	/* &dev is address of the device */
	/* <0 to check if major number is created */
	if((alloc_chrdev_region(&dev, 0, 1, "my_Dev")) < 0) {
		printk(KERN_INFO"Cannot allocate the major number...\n");
	}

	printk(KERN_INFO"Major = %d Minor =  %d..\n", MAJOR(dev),MINOR(dev));

	/* creating cdev structure*/
	cdev_init(&my_cdev, &fops);

	/* adding character device to the system */
	if((cdev_add(&my_cdev, dev, 1)) < 0) {
		printk(KERN_INFO "Cannot add the device to the system...\n");
		goto r_class;
	}

	/* creating struct class */
	if((dev_class =  class_create(THIS_MODULE, "my_class")) == NULL) {
		printk(KERN_INFO " cannot create the struct class...\n");
		goto r_class;
	}

	/* creating device */

	if((device_create(dev_class, NULL, dev, NULL, "my_device")) == NULL) {
		printk(KERN_INFO " cannot create the device ..\n");
		goto r_device;
	}

	printk(KERN_INFO"Device driver insert...done properly...");
	return 0;

r_device:
	class_destroy(dev_class);

r_class:
	unregister_chrdev_region(dev, 1);
	return -1;
}

void __exit my_driver_exit(void) {
	device_destroy(dev_class, dev);
	class_destroy(dev_class);
	cdev_del(&my_cdev);
	unregister_chrdev_region(dev, 1);
	printk(KERN_INFO "Device driver is removed successfully...\n");
}

module_init(my_driver_init);
module_exit(my_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aditya");
MODULE_DESCRIPTION("The character device driver");
