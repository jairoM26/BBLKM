
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
#include <linux/time.h>       // Using the clock to measure time between button presses
#include <linux/interrupt.h>  // Required for the IRQ code

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
/* 
 * Global variables are declared as static, so are global within the file. 
 */
static unsigned int gpioLED1 = 139;           ///< Default GPIO for the LED is 139
module_param(gpioLED1, uint, S_IRUGO);       ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(gpioLED1, " GPIO LED number (default=139)");     ///< parameter description
static unsigned int gpioLED2 = 138;           ///< Default GPIO for the LED is 138
module_param(gpioLED2, uint, S_IRUGO);       ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(gpioLED2, " GPIO LED number (default=138)");     ///< parameter description
static unsigned int gpioLED3 = 137;           ///< Default GPIO for the LED is 137
module_param(gpioLED3, uint, S_IRUGO);       ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(gpioLED3, " GPIO LED number (default=137)");     ///< parameter description

static unsigned int gpioButton = 136;       ///< Default GPIO is 136
module_param(gpioButton, uint, S_IRUGO);    ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(gpioButton, " GPIO Button number (default=136)");  ///< parameter description

static unsigned int burstRep = 1;     ///< The blink period in ms
module_param(burstRep, uint, S_IRUGO);   ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(burstRep, " Burst is repite n times");

static unsigned int blinkPeriod = 1000;     ///< The blink period in ms
module_param(blinkPeriod, uint, S_IRUGO);   ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(blinkPeriod, " LED blink period in ms, default = 1000");

static unsigned int ButtonStats = 0;     ///< Count the number of times the button was pressed
module_param(ButtonStats, uint, S_IRUGO);   ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(ButtonStats, "Count button pressed");

static int button = 0;     ///< Count the number of times the button was pressed
module_param(button, uint, S_IRUGO);   ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(button, "Count button pressed");

static unsigned int numberPress = 0;     ///< Count the number of times the button was pressed
module_param(numberPress, uint, S_IRUGO);   ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(numberPress, "Count button pressed");

static struct timespec timeLast, timeCurrent, timeDiff;  ///< timespecs from linux/time.h (has nano precision)

static char ledName[6] = "BBLKM";          ///< Null terminated default string -- just in case
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
   blinkPeriod = period;                 // Within range, assign to blinkPeriod variable
   
   return period;
}

/** @brief A callback function to show the LEDs n times */
static ssize_t burstRep_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   return sprintf(buf, "%d\n", burstRep);}

/** @brief A callback function to store the LEDs n times */
static ssize_t burstRep_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count){
   unsigned int n;                     // Using a variable to validate the data sent
   sscanf(buf, "%du", &n);             // Read in the repite as an unsigned int

   burstRep = n;                 // assign to burstRep variable
   return n;
}

/** @brief A callback function to show the LEDs n times */
static ssize_t button_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   return sprintf(buf, "%d\n", button);}

/** @brief A callback function to store the LEDs n times */
static ssize_t button_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count){
   int n;                     // Using a variable to validate the data sent
   sscanf(buf, "%du", &n);             // Read in the repite as an unsigned int
   button = n;                 // assign to burstRep variable
   numberPress++;
   return n;
}

/** @brief A callback function to show the LEDs n times */
static ssize_t number_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   return sprintf(buf, "%d\n", numberPress);}


/** @brief A callback function to show the led status */
static ssize_t ledStats_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   return sprintf(buf, "%d\n", ledOn);
}

/** @brief Displays the last time the button was pressed -- manually output the date (no localization) */
static ssize_t lastTime_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   return sprintf(buf, "%.2lu:%.2lu:%.2lu:%.9lu \n", (timeLast.tv_sec/3600)%24,
          (timeLast.tv_sec/60) % 60, timeLast.tv_sec % 60, timeLast.tv_nsec );
}
 
/** @brief Display the time difference in the form secs.nanosecs to 9 places */
static ssize_t diffTime_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   return sprintf(buf, "%lu.%.9lu\n", timeDiff.tv_sec, timeDiff.tv_nsec);
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
static struct kobj_attribute burstRep_attr = __ATTR(burstRep, 0666, burstRep_show, burstRep_store);
static struct kobj_attribute mode_attr = __ATTR(LEDMode, 0666, mode_show, mode_store);
static struct kobj_attribute button_attr = __ATTR(button, 0666, button_show, button_store);

static struct kobj_attribute ledon_attr = __ATTR_RO(ledStats);     ///< the ledon kobject attr
static struct kobj_attribute number_attr = __ATTR_RO(number);     ///< the ledon kobject attr
static struct kobj_attribute time_attr  = __ATTR_RO(lastTime);  ///< the last time pressed kobject attr
static struct kobj_attribute diff_attr  = __ATTR_RO(diffTime);  ///< the difference in time attr

/** The ebb_attrs[] is an array of attributes that is used to create the attribute group below.
 *  The attr property of the kobj_attribute is used to extract the attribute struct
 */
static struct attribute *BBLKM_attrs[] = {
   &period_attr.attr,                       // The period at which the LED flashes
   &mode_attr.attr,                         // Is the LED on or off?
   &burstRep_attr.attr,                         // Burst the LEDs?
   &ledon_attr.attr,                         //led status
   &time_attr.attr,
   &diff_attr.attr,
   &number_attr.attr,
   &button_attr.attr,
   NULL,
};

/** The attribute group uses the attribute array and a name, which is exposed on sysfs -- in this
 *  case it is gpio136, which is automatically defined in the LED_init() function below
 *  using the custom kernel parameter that can be passed when the module is loaded.
 */
static struct attribute_group attr_group = {
   .name  = ledName,                        // The name is generated in ebbLED_init()
   .attrs = BBLKM_attrs,                      // The attributes array defined just above
};

static struct kobject *BBLKM_kobj;            /// The pointer to the kobject
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
   while(!kthread_should_stop()){           // Returns true when kthread_stop() is called
      set_current_state(TASK_RUNNING);
      if (LEDMode==BURST){
         if (button != 0){
            int tmp = burstRep;
            while(tmp>0){
               gpio_set_value(gpioLED1, true);       // Use the LED state to light/turn on the LED
               set_current_state(TASK_RUNNING);
               msleep(blinkPeriod);
               gpio_set_value(gpioLED1, false);       // Use the LED state to light/turn of the LED
               set_current_state(TASK_RUNNING);
               msleep(blinkPeriod/2);
               gpio_set_value(gpioLED2, true);       // Use the LED state to light/turn on the LED
               set_current_state(TASK_RUNNING);
               msleep(blinkPeriod);
               gpio_set_value(gpioLED2, false);       // Use the LED state to light/turn of the LED
               set_current_state(TASK_RUNNING);
               msleep(blinkPeriod/2);
               gpio_set_value(gpioLED3, true);       // Use the LED state to light/turn on the LED
               set_current_state(TASK_RUNNING);
               msleep(blinkPeriod);
               gpio_set_value(gpioLED3, false);       // Use the LED state to light/turn of the LED
               set_current_state(TASK_RUNNING);
               tmp--;
            }
            button = 0;
         }
      }
      else if (LEDMode==ON){
         if (button != 0){
            ledOn = true;
            gpio_set_value(gpioLED1, ledOn);       // Use the LED state to light/turn on the LED
            gpio_set_value(gpioLED2, ledOn);       // Use the LED state to light/turn on the LED
            gpio_set_value(gpioLED3, ledOn);       // Use the LED state to light/turn on the LED
            set_current_state(TASK_RUNNING);
         }
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
   return 0;
}

/** @brief The LKM initialization function
 *  The static keyword restricts the visibility of the function to within this C file. The __init
 *  macro means that for a built-in driver (not a LKM) the function is only used at initialization
 *  time and that it can be discarded and its memory freed up after that point. In this example this
 *  function sets up the GPIOs and the IRQ
 *  @return returns 0 if successful
 */
static int __init BBLKM_init(void){
   int result = 0;

   printk(KERN_INFO "EBB LED: Initializing the EBB LED LKM\n");
   sprintf(ledName, "led%d", gpioLED1);      // Create the gpio115 name for /sys/ebb/led49
   sprintf(ledName, "led%d", gpioLED2);      // Create the gpio115 name for /sys/ebb/led49
   sprintf(ledName, "led%d", gpioLED3);      // Create the gpio115 name for /sys/ebb/led49

   BBLKM_kobj = kobject_create_and_add("BBLKM", kernel_kobj->parent); // kernel_kobj points to /sys/kernel
   if(!BBLKM_kobj){
      printk(KERN_ALERT "BBLKM: failed to create kobject\n");
      return -ENOMEM;
   }
   // add the attributes to /sys/ebb/ -- for example, /sys/ebb/led49/ledOn
   result = sysfs_create_group(BBLKM_kobj, &attr_group);
   if(result) {
      printk(KERN_ALERT "BBLKM: failed to create sysfs group\n");
      kobject_put(BBLKM_kobj);                // clean up -- remove the kobject sysfs entry
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
      printk(KERN_ALERT "BBLKM: failed to create the task\n");
      return PTR_ERR(task);
   }
   return result;
}

/** @brief The LKM cleanup function
 *  Similar to the initialization function, it is static. The __exit macro notifies that if this
 *  code is used for a built-in driver (not a LKM) that this function is not required.
 */
static void __exit BBLKM_exit(void){
   kthread_stop(task);                      // Stop the LED flashing thread
   kobject_put(BBLKM_kobj);                   // clean up -- remove the kobject sysfs entry
   gpio_set_value(gpioLED1, 0);              // Turn the LED1 off, indicates device was unloaded
   gpio_unexport(gpioLED1);                  // Unexport the Button GPIO
   gpio_free(gpioLED1);                      // Free the LED1 GPIO

   gpio_set_value(gpioLED2, 0);              // Turn the LED2 off, indicates device was unloaded
   gpio_unexport(gpioLED2);                  // Unexport the Button GPIO
   gpio_free(gpioLED2);                      // Free the LED2 GPIO

   gpio_set_value(gpioLED3, 0);              // Turn the LED3 off, indicates device was unloaded
   gpio_unexport(gpioLED3);                  // Unexport the Button GPIO
   gpio_free(gpioLED3);                      // Free the LED3 GPIO


   printk(KERN_INFO "Goodbye from BBLKM\n");
}

/// This next calls are  mandatory -- they identify the initialization function
/// and the cleanup function (as above).
module_init(BBLKM_init);
module_exit(BBLKM_exit);
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
