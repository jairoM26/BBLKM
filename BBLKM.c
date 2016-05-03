/**
 * @file LinuxKernelModuleReverse.c
 * @author jairoM26
 * @date 2-05-2016
 * @brief This is an example of a LKM that reverse the messages that it recieves
 * The LKM is based on this article .... 

*/



/**
Including libraries
*/
#include <linux/init.h>             // Macros used to mark up functions e.g., __init __exit
#include <linux/module.h>           // Core header for loading LKMs into the kernel
#include <linux/kernel.h>           // Contains types, macros, functions for the kernel
#include <linux/fs.h>
#include <asm/segment.h>
#include <asm/uaccess.h>			//for put user
#include <asm/errno.h>
#include <linux/buffer_head.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Macros - alyas definitions
*/

 #define SUCCESS 0
 #define LKM_INITIALIZATION "EBB:  %s LKM initialization\n" 
 #define LKM_ENDED "EBB: CLOSING %s the LKM\n"

//files name
 #define DEVICE_LED_MODE "LEDMode"
 #define DEVICE_LED_STATUS "LEDStatus"
 #define DEVICE_BUTTON_STATUS "ButtonStats"
 #define DEVICE_lED_BURST_REP "burstRep"
 #define DEVICE_LED_BLINK_PERIOD "blinkPeriod"
 #define DEVICE_BUTTON_LAST_TIME "lastTime"
 #define DEVICE_BUTTON_DIFF_TIME "diffTime"


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

MODULE_LICENSE("GPL");              ///< The license type -- this affects runtime behavior
MODULE_AUTHOR("jairoM26");      ///< The author -- visible when you use modinfo
MODULE_DESCRIPTION("A simple Linux driver for the BBB.");  ///< The description -- see modinfo
MODULE_VERSION("1.0");              ///< The version of the module

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static char *name = "world";        ///< An example LKM argument -- default value is "world"
module_param(name, charp, S_IRUGO); ///< Param desc. charp = char ptr, S_IRUGO can be read/not changed
MODULE_PARM_DESC(name, "The name to display in /var/log/kern.log");  ///< parameter description

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Functions declarations
*/
static int __init LKM_init(void);

static void __exit LKM_exit(void);

static int device_open(struct inode *, struct file *);

static int device_release(struct inode *, struct file *);

static ssize_t device_read(struct file *, char *, size_t, loff_t *);

static ssize_t device_write(struct file *, const char *, size_t, loff_t *);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief The LKM initialization function
 * The static keywords restricts the visibility of the functions to within
 * The __init macro means that for a built-in driver the function is only for initialization time and can be free after that
 * @return 0 if it is sucessful	
*/
static int __init LKM_init(void){
	printk(KERN_INFO LKM_INITIALIZATION, name);
	return SUCCESS;
}

/**
 * @brief The LKM ended-cleanup function
 *  
*/
static void __exit LKM_exit(void){
	printk(KERN_INFO LKM_ENDED, name);
}

/**
 * @brief A module use the module_init() module_exit() macros from linux/init.h, which
 * identify the initialization function time ande cleanup function
*/
 module_init(LKM_init);
 module_exit(LKM_exit);

 /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////