
/**
 * @file LinuxKernelModuleReverse.c
 * @author jairoM26
 * @author CritianMQ
 * @date 2-05-2016
 * @brief This is LKM implemented to control leds in a beagleboard with a button
 * The LKM is based on this article http://derekmolloy.ie/kernel-gpio-programming-buttons-and-leds/ 

*/

/**
Including libraries
*/
#include <linux/init.h>           // Macros used to mark up functions e.g. __init __exit
#include <linux/module.h>         // Core header for loading LKMs into the kernel
#include <linux/device.h>         // Header to support the kernel Driver Model
#include <linux/kernel.h>         // Contains types, macros, functions for the kernel
#include <linux/fs.h>             // Header for the Linux file system support
#include <asm/uaccess.h>          // Required for the copy to user function
#include <linux/gpio.h>           // Required for the GPIO functions
#include <linux/kobject.h>        // Using kobjects for the sysfs bindings
#include <linux/kthread.h>        // Using kthreads for the flashing functionality
#include <linux/delay.h>          // Using this header for the msleep() function

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Macros - alyas definitions
*/

MODULE_LICENSE("GPL");              ///< The license type -- this affects runtime behavior
MODULE_AUTHOR("jairoM26 and Cristian");      ///< The author -- visible when you use modinfo
MODULE_DESCRIPTION("LKM implemented in ubuntu-arm 12.04 in beagleboard, controlling leds with a button");  ///< The description -- see modinfo
MODULE_VERSION("1.0");              ///< The version of the module

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Message macros */
#define LKM_INITIALIZATION "BBLKM:  %s LKM initialization\n" 
#define LKM_ENDED "BBLKM: CLOSING %s the LKM\n"
#define LKM_FILE_REGISTER_MAJOR_NUMBER "BBLKM failed to register a major number\n"
#define LKM_REGISTER_MAJOR_NUMBER_CORRECTLY "BBLKM registered correctly with major number %d\n"
#define LKM_FILE_REGISTER_DEVICE_CLASS "Failed to register device class\n"
#define LKM_DEVICE_CLASS_REGISTERED_CORRECTLY "BBLKM: device class registered correctly\n"
#define LKM_FAILED_IN_CREATED_THE_DEVICE "Failed to create the device\n"
#define LKM_CREATE_DEVICE_CORRECTLY "BLKM device class created correctly\n"
#define LKM_OPENED_DEVICE "BBLKM: Device has been opened %d time(s)\n"
#define LKM_SENT_DATA_TO_USER "BBLKM: Sent %d characters to the user\n"
#define LKM_FAILED_SENDING_DATA_TO_USER "BBLKM: Failed to send %d characters to the user\n"
#define LKM_RECIEVE_DATA_FROM_USER "BBLKM: Received %d characters from the user\n"
#define LKM_CLOSED_DEVICE_SUCCESSFULLY "BBLKM: Device successfully closed\n"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* 
 * Global variables are declared as static, so are global within the file. 
 */
static unsigned int gpioLED1 = 139;           ///< Default GPIO for the LED is 139
module_param(gpioLED1, uint, S_IRUGO);       ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(gpioLED1, " GPIO LED number (default=139)");     ///< parameter description
static unsigned int gpioLED2 = 137;           ///< Default GPIO for the LED is 137
module_param(gpioLED2, uint, S_IRUGO);       ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(gpioLED2, " GPIO LED number (default=137)");     ///< parameter description
static unsigned int gpioLED3 = 138;           ///< Default GPIO for the LED is 138
module_param(gpioLED3, uint, S_IRUGO);       ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(gpioLED3, " GPIO LED number (default=138)");     ///< parameter description

static unsigned int burstRep = 1;     ///< The blink period in ms
module_param(burstRep, uint, S_IRUGO);   ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(burstRep, " Burst is repite n times");

static unsigned int blinkPeriod = 1000;     ///< The blink period in ms
module_param(blinkPeriod, uint, S_IRUGO);   ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(blinkPeriod, " LED blink period in ms (min=1, default=1000, max=10000)");

static char ledName[7] = "ledXXX";          ///< Null terminated default string -- just in case
static bool ledOn = 0;                      ///< Is the LED on or off? Used for flashing
enum modes { DEFAULT, ON, BURST };              ///< The available LED modes -- static not useful here
static enum modes LEDMode = DEFAULT;             ///< Default mode is flashing

/** @brief A callback function to display the LED mode
 *  @param kobj represents a kernel object device that appears in the sysfs filesystem
 *  @param attr the pointer to the kobj_attribute struct
 *  @param buf the buffer to which to write the number of presses
 *  @return return the number of characters of the mode string successfully displayed
 */
static ssize_t mode_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   switch(LEDMode){
      case DEFAULT:   return sprintf(buf, "default\n");       // Display the state -- simplistic approach
      case ON:    return sprintf(buf, "on\n");
      case BURST: return sprintf(buf, "burst\n");
      default:    return sprintf(buf, "LKM Error\n"); // Cannot get here
   }
}

/** @brief A callback function to store the LED mode using the enum above */
static ssize_t mode_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count){
   // the count-1 is important as otherwise the \n is used in the comparison
   if (strncmp(buf,"on",count-1)==0 || strncmp(buf,"1",count-1)==0 ) { LEDMode = ON; }   // strncmp() compare with fixed number chars
   else if (strncmp(buf,"default",count-1)==0 || strncmp(buf,"0",count-1)==0) { LEDMode = DEFAULT; }
   else if (strncmp(buf,"burst",count-1)==0) { LEDMode = BURST; }
   return count;
}

/** @brief A callback function to display the LED period */
static ssize_t period_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   return sprintf(buf, "%d\n", blinkPeriod);
}

/** @brief A callback function to store the LED period value */
static ssize_t period_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count){
   unsigned int period;                     // Using a variable to validate the data sent
   sscanf(buf, "%du", &period);             // Read in the period as an unsigned int
   if ((period>1)&&(period<=10000)){        // Must be 2ms or greater, 10secs or less
      blinkPeriod = period;                 // Within range, assign to blinkPeriod variable
   }
   return period;
}

/** @brief A callback function to burst the LEDs n times */
static ssize_t burstRep_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   return sprintf(buf, "%d\n", burstRep);
}

/** @brief A callback function to store the LEDs n times */
static ssize_t burstRep_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count){
   unsigned int n;                     // Using a variable to validate the data sent
   sscanf(buf, "%du", &n);             // Read in the repite as an unsigned int

   burstRep = n;                 // assign to burstRep variable
   
   return n;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
* Structs definition
*/

/** Use these helper macros to define the name and access levels of the kobj_attributes
 *  The kobj_attribute has an attribute attr (name and mode), show and store function pointers
 *  The period variable is associated with the blinkPeriod variable and it is to be exposed
 *  with mode 0666 using the period_show and period_store functions above
 */
static struct kobj_attribute period_attr = __ATTR(blinkPeriod, 0666, period_show, period_store);
static struct kobj_attribute burst_attr = __ATTR(burstRep, 0666, burstRep_show, burstRep_store);
static struct kobj_attribute mode_attr = __ATTR(LEDMode, 0666, mode_show, mode_store);

/** The ebb_attrs[] is an array of attributes that is used to create the attribute group below.
 *  The attr property of the kobj_attribute is used to extract the attribute struct
 */
static struct attribute *ebb_attrs[] = {
   &period_attr.attr,                       // The period at which the LED flashes
   &mode_attr.attr,                         // Is the LED on or off?
   &burst_attr.attr,                         // Burst the LEDs?
   NULL,
};

/** The attribute group uses the attribute array and a name, which is exposed on sysfs -- in this
 *  case it is gpio49, which is automatically defined in the ebbLED_init() function below
 *  using the custom kernel parameter that can be passed when the module is loaded.
 */
static struct attribute_group attr_group = {
   .name  = ledName,                        // The name is generated in ebbLED_init()
   .attrs = ebb_attrs,                      // The attributes array defined just above
};

static struct kobject *ebb_kobj;            /// The pointer to the kobject
static struct task_struct *task;            /// The pointer to the thread task


/**
 * Functions definitions
*/

/** @brief The LED Flasher main kthread loop
 *
 *  @param arg A void pointer used in order to pass data to the thread
 *  @return returns 0 if successful
 */
static int flash(void *arg){
   printk(KERN_INFO "EBB LED: Thread has started running \n");
   while(!kthread_should_stop()){           // Returns true when kthread_stop() is called
      set_current_state(TASK_RUNNING);
      if (LEDMode==BURST){
         ledOn = true;
         gpio_set_value(gpioLED1, ledOn);       // Use the LED state to light/turn on the LED
         gpio_set_value(gpioLED2, ledOn);       // Use the LED state to light/turn on the LED
         gpio_set_value(gpioLED3, ledOn);       // Use the LED state to light/turn on the LED
         set_current_state(TASK_RUNNING);
         msleep(blinkPeriod/2); 
         ledOn = false;
         gpio_set_value(gpioLED1, ledOn);       // Use the LED state to light/turn on the LED
         gpio_set_value(gpioLED2, ledOn);       // Use the LED state to light/turn on the LED
         gpio_set_value(gpioLED3, ledOn);       // Use the LED state to light/turn on the LED
         set_current_state(TASK_RUNNING);
      }
      else if (LEDMode==ON){
         ledOn = true;
         gpio_set_value(gpioLED1, ledOn);       // Use the LED state to light/turn on the LED
         gpio_set_value(gpioLED2, ledOn);       // Use the LED state to light/turn on the LED
         gpio_set_value(gpioLED3, ledOn);       // Use the LED state to light/turn on the LED
         set_current_state(TASK_RUNNING);
      }	
      else {
         ledOn = false;
         gpio_set_value(gpioLED1, ledOn);       // Use the LED state to light/turn on the LED
         gpio_set_value(gpioLED2, ledOn);       // Use the LED state to light/turn on the LED
         gpio_set_value(gpioLED3, ledOn);       // Use the LED state to light/turn on the LED
         set_current_state(TASK_RUNNING);
      }
      set_current_state(TASK_RUNNING);
      msleep(blinkPeriod/2);                // millisecond sleep for half of the period
   }
   printk(KERN_INFO "EBB LED: Thread has run to completion \n");
   return 0;
}

/** @brief The LKM initialization function
 *  The static keyword restricts the visibility of the function to within this C file. The __init
 *  macro means that for a built-in driver (not a LKM) the function is only used at initialization
 *  time and that it can be discarded and its memory freed up after that point. In this example this
 *  function sets up the GPIOs and the IRQ
 *  @return returns 0 if successful
 */
static int __init ebbLED_init(void){
   int result = 0;

   printk(KERN_INFO "EBB LED: Initializing the EBB LED LKM\n");
   //sprintf(ledName, "led%d", gpioLED1);      // Create the gpio115 name for /sys/ebb/led49
   sprintf("dev", "BBLKM%d", 0);      // Create the gpio115 name for /sys/ebb/led49

   ebb_kobj = kobject_create_and_add("ebb", kernel_kobj->parent); // kernel_kobj points to /sys/kernel
   if(!ebb_kobj){
      printk(KERN_ALERT "EBB LED: failed to create kobject\n");
      return -ENOMEM;
   }
   // add the attributes to /sys/ebb/ -- for example, /sys/ebb/led49/ledOn
   result = sysfs_create_group(ebb_kobj, &attr_group);
   if(result) {
      printk(KERN_ALERT "EBB LED: failed to create sysfs group\n");
      kobject_put(ebb_kobj);                // clean up -- remove the kobject sysfs entry
      return result;
   }
   ledOn = true;
   gpio_request(gpioLED1, "sysfs");          // gpioLED1 is 139 by default, request it
   gpio_direction_output(gpioLED1, ledOn);   // Set the gpio to be in output mode and turn on
   gpio_export(gpioLED1, false);  // causes gpio49 to appear in /sys/class/gpio
                                 // the second argument prevents the direction from being changed

   gpio_request(gpioLED2, "sysfs");          // gpioLED2 is 138 by default, request it
   gpio_direction_output(gpioLED2, ledOn);   // Set the gpio to be in output mode and turn on
   gpio_export(gpioLED2, false);  // causes gpio49 to appear in /sys/class/gpio
                                 // the second argument prevents the direction from being changed


   gpio_request(gpioLED3, "sysfs");          // gpioLED3 is 137 by default, request it
   gpio_direction_output(gpioLED3, ledOn);   // Set the gpio to be in output mode and turn on
   gpio_export(gpioLED3, false);  // causes gpio49 to appear in /sys/class/gpio
                                 // the second argument prevents the direction from being changed


   task = kthread_run(flash, NULL, "LED_flash_thread");  // Start the LED flashing thread
   if(IS_ERR(task)){                                     // Kthread name is LED_flash_thread
      printk(KERN_ALERT "EBB LED: failed to create the task\n");
      return PTR_ERR(task);
   }
   return result;
}

/** @brief The LKM cleanup function
 *  Similar to the initialization function, it is static. The __exit macro notifies that if this
 *  code is used for a built-in driver (not a LKM) that this function is not required.
 */
static void __exit ebbLED_exit(void){
   kthread_stop(task);                      // Stop the LED flashing thread
   kobject_put(ebb_kobj);                   // clean up -- remove the kobject sysfs entry
   gpio_set_value(gpioLED1, 0);              // Turn the LED1 off, indicates device was unloaded
   gpio_unexport(gpioLED1);                  // Unexport the Button GPIO
   gpio_free(gpioLED1);                      // Free the LED1 GPIO

   gpio_set_value(gpioLED2, 0);              // Turn the LED2 off, indicates device was unloaded
   gpio_unexport(gpioLED2);                  // Unexport the Button GPIO
   gpio_free(gpioLED2);                      // Free the LED2 GPIO

   gpio_set_value(gpioLED3, 0);              // Turn the LED3 off, indicates device was unloaded
   gpio_unexport(gpioLED3);                  // Unexport the Button GPIO
   gpio_free(gpioLED3);                      // Free the LED3 GPIO


   printk(KERN_INFO "EBB LED: Goodbye from the EBB LED LKM!\n");
}

/// This next calls are  mandatory -- they identify the initialization function
/// and the cleanup function (as above).
module_init(ebbLED_init);
module_exit(ebbLED_exit);
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




