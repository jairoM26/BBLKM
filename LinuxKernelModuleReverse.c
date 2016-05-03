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

MODULE_LICENSE("GPL");              ///< The license type -- this affects runtime behavior
MODULE_AUTHOR("jairoM26");      ///< The author -- visible when you use modinfo
MODULE_DESCRIPTION("A simple Linux driver for the BBB.");  ///< The description -- see modinfo
MODULE_VERSION("1.0");              ///< The version of the module


static char *name = "world";        ///< An example LKM argument -- default value is "world"
module_param(name, charp, S_IRUGO); ///< Param desc. charp = char ptr, S_IRUGO can be read/not changed
MODULE_PARM_DESC(name, "The name to display in /var/log/kern.log");  ///< parameter description

/**
 * @brief The LKM initialization function
 * The static keywords restricts the visibility of the functions to within
 * The __init macro means that for a built-in driver the function is only for initialization time and can be free after that
 * @return 0 if it is sucessful	
*/
static int __init LKM_init(void){
	printk(KERN_INFO "EBB: WELCOME %s THIS IS THE LKM\n", name);
	return 0;
}

/**
 * @brief The LKM ended-cleanup function
 *  
*/
static void __exit LKM_exit(void){
	printk(KERN_INFO "EBB: CLOSING %s THE LKM PROCEDURE, BYE\n", name);
}

/**
 * @brief A module use the module_init() module_exit() macros from linux/init.h, which
 * identify the initialization function time ande cleanup function
*/
 module_init(LKM_init);
 module_exit(LKM_exit);