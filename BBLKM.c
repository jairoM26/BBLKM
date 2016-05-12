
/**
 * @file   BBLKM.c
 * @author Jairo Mendez and Cristian Castillo
 * @date   12 May 2016
 * @brief  A kernel module for controlling a button (or any signal) that is connected to
 * a GPIO. It has full support for interrupts and for sysfs entries so that an interface
 * can be created to the button or the button can be configured from Linux userspace.
 * The sysfs entry appears at /sys/dev
 * @see http://www.derekmolloy.ie/ for reference
*/
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
#include <linux/interrupt.h>  // Required for the IRQ code
#include <linux/time.h>       // Using the clock to measure time between button presses


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

MODULE_LICENSE("GPL");              ///< The license type -- this affects runtime behavior
MODULE_AUTHOR("jairoM26 and Cristian");      ///< The author -- visible when you use modinfo
MODULE_DESCRIPTION("LKM implemented in ubuntu-arm 12.04 in beagleboard, controlling leds with a button");  ///< The description -- see modinfo
MODULE_VERSION("1.0");              ///< The version of the module

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Message macros */

#define  DEBOUNCE_TIME 200    ///< The default bounce time -- 200ms
#define RISING_DESCRIPTION " Rising edge = 1 (default), Falling edge = 0" // Rising param description

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* 
 * Global variables are declared as static, so are global within the file. 
 */

/**
 * @brief Rising edge is the default IRQ property
 * var #1
*/
static bool isRising = 1;
module_param(isRising, bool, S_IRUGO);      ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(isRising, RISING_DESCRIPTION); 

/**
 *@brief Set the value of the gpio, in the beagleboard the pin number is 3
*/
static unsigned int gpioLED1 = 139;           ///< Default GPIO for the LED is 139

/**
 *@brief Set the value of the gpio, in the beagleboard the pin number is 5
*/
static unsigned int gpioLED2 = 138;           ///< Default GPIO for the LED is 1378

/**
 *@brief Set the value of the gpio, in the beagleboard the pin number is 7
*/
static unsigned int gpioLED3 = 137;           ///< Default GPIO for the LED is 137

/**
 *@brief Set the value of the gpio, in the beagleboard the pin number is 9
*/
static unsigned int gpioButton = 136;   ///< hard coding the button gpio for this example to P9_9 (GPIO136)

static unsigned int irqNumber;          ///< Used to share the IRQ number within this file
static unsigned int numberPresses = 0;  ///< For information, store the number of button presses

static int    irqNumber;                    ///< Used to share the IRQ number within this file
static int    numberPresses = 0;            ///< For information, store the number of button presses
static bool   isDebounce = 1;               ///< Use to store the debounce state (on by default)
static struct timespec ts_last, ts_current, ts_diff;  ///< timespecs from linux/time.h (has nano precision)
static unsigned int burstRep = 1;     ///< The blink period in ms
static unsigned int blinkPeriod = 1000;     ///< The blink period in ms
static bool ledOn = 0;                      ///< Is the LED on or off? Used for flashing
enum modes { DEFAULT, ON, BURST };              ///< The available LED modes -- static not useful here
static enum modes LEDMode = DEFAULT;             ///< Default mode is flashing

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

/** @brief A callback function to output the numberPresses variable
 *  @param kobj represents a kernel object device that appears in the sysfs filesystem
 *  @param attr the pointer to the kobj_attribute struct
 *  @param buf the buffer to which to write the number of presses
 *  @return return the total number of characters written to the buffer (excluding null)
 */
static ssize_t numberPresses_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   return sprintf(buf, "%d\n", numberPresses);
}


/** @brief A callback function to read in the numberPresses variable
 *  @param kobj represents a kernel object device that appears in the sysfs filesystem
 *  @param attr the pointer to the kobj_attribute struct
 *  @param buf the buffer from which to read the number of presses (e.g., reset to 0).
 *  @param count the number characters in the buffer
 *  @return return should return the total number of characters used from the buffer
 */
static ssize_t numberPresses_store(struct kobject *kobj, struct kobj_attribute *attr,
                                   const char *buf, size_t count){
   sscanf(buf, "%du", &numberPresses);
   return count;
}

/** @brief Displays if the LED is on or off */
static ssize_t ledOn_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   return sprintf(buf, "%d\n", ledOn);
}

/** @brief Displays the last time the button was pressed -- manually output the date (no localization) */
static ssize_t lastTime_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   return sprintf(buf, "%.2lu:%.2lu:%.2lu:%.9lu \n", (ts_last.tv_sec/3600)%24,
          (ts_last.tv_sec/60) % 60, ts_last.tv_sec % 60, ts_last.tv_nsec );
}

/** @brief Display the time difference in the form secs.nanosecs to 9 places */
static ssize_t diffTime_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   return sprintf(buf, "%lu.%.9lu\n", ts_diff.tv_sec, ts_diff.tv_nsec);
}

/** @brief Displays if button debouncing is on or off */
static ssize_t isDebounce_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   return sprintf(buf, "%d\n", isDebounce);
}

/** @brief Stores and sets the debounce state */
static ssize_t isDebounce_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count){
   unsigned int temp;
   sscanf(buf, "%du", &temp);                // use a temp varable for correct int->bool
   gpio_set_debounce(gpioButton,0);
   isDebounce = temp;
   if(isDebounce) { gpio_set_debounce(gpioButton, DEBOUNCE_TIME);
      printk(KERN_INFO "BBLKM Button: Debounce on\n");
   }
   else { gpio_set_debounce(gpioButton, 0);  // set the debounce time to 0
      printk(KERN_INFO "BBLKM Button: Debounce off\n");
   }
   return count;
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

/**  Use these helper macros to define the name and access levels of the kobj_attributes
 *  The kobj_attribute has an attribute attr (name and mode), show and store function pointers
 *  The count variable is associated with the numberPresses variable and it is to be exposed
 *  with mode 0666 using the numberPresses_show and numberPresses_store functions above
 */
static struct kobj_attribute period_attr = __ATTR(blinkPeriod, 0666, period_show, period_store);
static struct kobj_attribute burst_attr = __ATTR(burstRep, 0666, burstRep_show, burstRep_store);
static struct kobj_attribute mode_attr = __ATTR(LEDMode, 0666, mode_show, mode_store);
static struct kobj_attribute count_attr = __ATTR(numberPresses, 0666, numberPresses_show, numberPresses_store);
static struct kobj_attribute debounce_attr = __ATTR(isDebounce, 0666, isDebounce_show, isDebounce_store);

/**  The __ATTR_RO macro defines a read-only attribute. There is no need to identify that the
 *  function is called _show, but it must be present. __ATTR_WO can be  used for a write-only
 *  attribute but only in Linux 3.11.x on.
 */
static struct kobj_attribute ledon_attr = __ATTR_RO(ledOn);     ///< the ledon kobject attr
static struct kobj_attribute time_attr  = __ATTR_RO(lastTime);  ///< the last time pressed kobject attr
static struct kobj_attribute diff_attr  = __ATTR_RO(diffTime);  ///< the difference in time attr



/** The ebb_attrs[] is an array of attributes that is used to create the attribute group below.
 *  The attr property of the kobj_attribute is used to extract the attribute struct
 */
static struct attribute *BBLKM_attrs[] = {

   &count_attr.attr,                  ///< The number of button presses
   &ledon_attr.attr,                  ///< Is the LED on or off?
   &time_attr.attr,                   ///< Time of the last button press in HH:MM:SS:NNNNNNNNN
   &diff_attr.attr,                   ///< The difference in time between the last two presses
   &debounce_attr.attr,               ///< Is the debounce state true or false
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
   .name  = "ledName",                        // The name is generated in ebbLED_init()
   .attrs = BBLKM_attrs,                      // The attributes array defined just above
};

static struct kobject *BBLKM_kobj;            /// The pointer to the kobject
static struct task_struct *task;            /// The pointer to the thread task


/**
 * Functions definitions
*/

/// Function prototype for the custom IRQ handler function -- see below for the implementation
static irq_handler_t  ebbgpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs);

/** @brief The LED Flasher main kthread loop
 *
 *  @param arg A void pointer used in order to pass data to the thread
 *  @return returns 0 if successful
 */
static int flash(void *arg){
   while(!kthread_should_stop()){           // Returns true when kthread_stop() is called
      set_current_state(TASK_RUNNING);
      if (LEDMode==BURST){
         int tmp = 0;
         if (ledOn){
            ledOn = false;
            gpio_set_value(gpioLED1, ledOn);       // Use the LED state to light/turn on the LED
            gpio_set_value(gpioLED2, ledOn);       // Use the LED state to light/turn on the LED
            gpio_set_value(gpioLED3, ledOn);       // Use the LED state to light/turn on the LED
            set_current_state(TASK_RUNNING);
         }
         while(tmp != burstRep){
            gpio_set_value(gpioLED1, true);       // Use the LED state to light/turn on the LED
            set_current_state(TASK_RUNNING);
            msleep(blinkPeriod);
            gpio_set_value(gpioLED1, false);       // Use the LED state to light/turn of the LED
            gpio_set_value(gpioLED2, true);       // Use the LED state to light/turn on the LED
            set_current_state(TASK_RUNNING);
            msleep(blinkPeriod);
            gpio_set_value(gpioLED2, false);       // Use the LED state to light/turn of the LED
            gpio_set_value(gpioLED3, true);       // Use the LED state to light/turn on the LED
            set_current_state(TASK_RUNNING);
            msleep(blinkPeriod);
            gpio_set_value(gpioLED3, false);       // Use the LED state to light/turn of the LED
            set_current_state(TASK_RUNNING);
            tmp++;
         }
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
   getnstimeofday(&ts_current);         // Get the current time as ts_current
   ts_diff = timespec_sub(ts_current, ts_last);   // Determine the time difference between last 2 presses
   ts_last = ts_current;                // Store the current time as the last time ts_last
   printk(KERN_INFO "EBB Button: The button state is currently: %d\n", gpio_get_value(gpioButton));
   numberPresses++;                     // Global counter, will be outputted when the module is unloaded
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
   unsigned long IRQflags = IRQF_TRIGGER_RISING;      // The default is a rising-edge interrupt


   printk(KERN_INFO "Initializing the BBLKM\n");
   sprintf("file", "led%d", gpioLED1);      // Create the gpio115 name for /sys/ebb/led49
   sprintf("file", "led%d", gpioLED2);      // Create the gpio115 name for /sys/ebb/led49
   sprintf("file", "led%d", gpioLED3);      // Create the gpio115 name for /sys/ebb/led49

   BBLKM_kobj = kobject_create_and_add("BBLKM", kernel_kobj->parent); // kernel_kobj points to /sys/kernel
   if(!BBLKM_kobj){
      printk(KERN_ALERT "BBLKM: failed to create kobject\n");
      return -ENOMEM;
   }
   // add the attributes to /sys/ebb/ -- for example, /sys/BBLKM/led138/ledOn
   result = sysfs_create_group(BBLKM_kobj, &attr_group);
   if(result) {
      printk(KERN_ALERT "BBLKM: failed to create sysfs group\n");
      kobject_put(BBLKM_kobj);                // clean up -- remove the kobject sysfs entry
      return result;
   }

   getnstimeofday(&ts_last);                          // set the last time to be the current time
   ts_diff = timespec_sub(ts_last, ts_last);          // set the initial time difference to be 0


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

   gpio_request(gpioButton, "sysfs");       // Set up the gpioButton
   gpio_direction_input(gpioButton);        // Set the button GPIO to be an input
   gpio_set_debounce(gpioButton, DEBOUNCE_TIME); // Debounce the button with a delay of 200ms
   gpio_export(gpioButton, false);          // Causes gpio115 to appear in /sys/class/gpio

    /// GPIO numbers and IRQ numbers are not the same! This function performs the mapping for us
   irqNumber = gpio_to_irq(gpioButton);
   printk(KERN_INFO "BBLKM button is mapped to IRQ: %d\n", irqNumber);

   if(!isRising){                           // If the kernel parameter isRising=0 is supplied
      IRQflags = IRQF_TRIGGER_FALLING;      // Set the interrupt to be on the falling edge
   }

   // This next call requests an interrupt line
   result = request_irq(irqNumber,             // The interrupt number requested
                        (irq_handler_t) BBLKMgpio_irq_handler, // The pointer to the handler function below
                        IRQF_TRIGGER_RISING,   // Interrupt on rising edge (button press, not release)
                        "BBLKM_gpio_handler",    // Used in /proc/interrupts to identify the owner
                        NULL);                 // The *dev_id for shared interrupt lines, NULL is okay
   
  
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


   printk(KERN_INFO "Goodbye from the BBLKM!\n");
}


/** @brief The GPIO IRQ Handler function
 *  This function is a custom interrupt handler that is attached to the GPIO above. The same interrupt
 *  handler cannot be invoked concurrently as the interrupt line is masked out until the function is complete.
 *  This function is static as it should not be invoked directly from outside of this file.
 *  @param irq    the IRQ number that is associated with the GPIO -- useful for logging.
 *  @param dev_id the *dev_id that is provided -- can be used to identify which device caused the interrupt
 *  Not used in this example as NULL is passed.
 *  @param regs   h/w specific register values -- only really ever used for debugging.
 *  return returns IRQ_HANDLED if successful -- should return IRQ_NONE otherwise.
 */
static irq_handler_t BBLKMgpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs){

   task = kthread_run(buttonInterruption, NULL, "LED_flash_thread");  // Start the LED flashing thread

   numberPresses++;                         // Global counter, will be outputted when the module is unloaded
   return (irq_handler_t) IRQ_HANDLED;      // Announce that the IRQ has been handled correctly
}

static void buttonInterruption(void){
   task = kthread_run(flash, NULL, "LED_flash_thread");  // Start the LED flashing thread
}

/// This next calls are  mandatory -- they identify the initialization function
/// and the cleanup function (as above).
module_init(BBLKM_init);
module_exit(BBLKM_exit);
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




