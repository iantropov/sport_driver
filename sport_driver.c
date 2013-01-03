/* 
	A Driver of serial port without terminal interface
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <asm/io.h>
#include <asm/signal.h>
#include <asm/uaccess.h>

#define SPORT_IRQ 4
#define SPORT_BASE 0x3f8
#define INT_READ 0x04
#define INT_WRITE 0x02
#define NO_INT 0x01

MODULE_LICENSE("Dual BSD/GPL");


dev_t sport_number;
static DECLARE_WAIT_QUEUE_HEAD(wq);


/*******************************************************************/
irqreturn_t sport_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	char a;
	while(!(a=inb(SPORT_BASE+2) & NO_INT));
	if((a & INT_READ)||(a & INT_WRITE))wake_up(&wq);
	return IRQ_HANDLED;
}
/*******************************************************************/
int sport_open(struct inode *inode, struct file *filp)
{
	if(!request_region(SPORT_BASE, 8,"sport_region")){
		printk(KERN_ERR "sport : can`t request region from %d\n",SPORT_BASE);
		return -EIO;
	}
	outb(0x80,SPORT_BASE+3);

	outb(0x00,SPORT_BASE);
	outb(0x0c,SPORT_BASE+1);

	outb(0x83,SPORT_BASE+3);

	outb(0x03,SPORT_BASE+1);

	if(request_irq(SPORT_IRQ, sport_interrupt,SA_INTERRUPT, "sport", NULL)){
		release_region(SPORT_BASE,8);
		printk(KERN_ERR "sport : can`t register IRQ %d\n",SPORT_IRQ);
		return -EIO;
	}
	
	return 0;
}
/*******************************************************************/
int sport_release(struct inode *inode, struct file *filp)
{
	outb(0x00,SPORT_BASE+1);
	free_irq(SPORT_IRQ, NULL); 
	release_region(SPORT_BASE,8);
	return 0;
}
/******************************************************************/
ssize_t sport_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	DECLARE_WAITQUEUE(wait,current);
	int i=count;
	char a, *buffer=kmalloc(count, GFP_KERNEL);
	if (!buffer)return -ENOMEM;
	memset(buffer,0,count);

	add_wait_queue(&wq, &wait);

	while(count){
		for (;;){
			set_current_state(TASK_INTERRUPTIBLE);
			if((inb(SPORT_BASE+5) & 0x01))break;
			schedule();

		}
		current->state = TASK_RUNNING;
		a=inb(SPORT_BASE);
		buffer[i-count]=a;
		count--;
	}
	remove_wait_queue(&wq,&wait);
	if(copy_to_user (buf, buffer, count)){
		kfree(buffer);
		return -EFAULT;
	}
	kfree(buffer);
	return 0;
}
/*****************************************************************/
ssize_t sport_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	DECLARE_WAITQUEUE(wait,current);
	int i=count;
	char a, *buffer=kmalloc(count, GFP_KERNEL);
	if (!buffer)return -ENOMEM;
	if(copy_from_user (buffer, buf, count)){
		kfree(buffer);
		return -EFAULT;
	}	
	add_wait_queue(&wq, &wait);

	while(count){
		for (;;){
			set_current_state(TASK_INTERRUPTIBLE);
			if((inb(SPORT_BASE+5) & 0x20))break;
			schedule();

		}
		current->state = TASK_RUNNING;
		a=buffer[i-count];
		outb(a,SPORT_BASE);
		count--;
	}
	remove_wait_queue(&wq,&wait);
	kfree(buffer);
	return count;
}
/*****************************************************************/

struct file_operations sport_fops = {
	.owner =    THIS_MODULE,
	.read =     sport_read,
	.write =    sport_write,
	.open =     sport_open,
	.release =  sport_release,
};

static int sport_init(void)
{
	int result=alloc_chrdev_region(&sport_number,0,1,"com_port");
	if(!result){
		printk(KERN_ALERT "I can`t allocate numbers!\n");
		return 0;
	}
	struct cdev *sport_cdev;
	sport_cdev = cdev_alloc();
	sport_cdev->ops = &sport_fops;
	result = cdev_add (sport_cdev, sport_number, 1);
	if (result) printk(KERN_ALERT "Error on adding SPORT");
	return 0;
}

static void sport_exit(void)
{
	unregister_chrdev_region(sport_number, 1);
}



module_init(sport_init);
module_exit(sport_exit);