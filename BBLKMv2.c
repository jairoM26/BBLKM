/**
 * @file BBLKMv2.c
 * @author jairoM26
 * @date 2-05-2016
 * @brief This is an example of a button with gpio and LKM that reverse the messages that it recieves

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
#include <linux/gpio.h>       // Required for the GPIO functions
#include <linux/interrupt.h>  // REquired for the IRQ
#include <linux/kobject.h>
#include <linux/time.h>    

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* 
 * Global variables are declared as static, so are global within the file. 
 */

static int    majorNumber;                  ///< Stores the device number -- determined automatically
static char   message[256] = {0};           ///< Memory for the string that is passed from userspace
static short  size_of_message;              ///< Used to remember the size of the string stored
static int    numberOpens = 0;              ///< Counts the number of times the device is opened
static struct class*  LKMClass  = NULL; ///< The device-driver class struct pointer
static struct device* BBLKMDevice = NULL; ///< The device-driver device struct pointer
static unsigned gpioLED=49;
static unsigned gpioButton=115;
static unsigned irqNumber;  //Used to shared the IRQ number withinthis file
static unsigned numberPresses=0; //store the number of button presses
static bool ledOn=0; //State of LED (off default)


/**
 * Functions declarations
*/
static int __init LKM_init(void);

static void __exit LKM_exit(void);



static int     dev_open(struct inode *, struct file *);
static int     dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);


static irq_handler_t  ebbgpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Macros - alyas definitions
*/

#define SUCCESS 0

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
#define LKM_STATE_BUTTON "GPIO_TEST: The button state is currently: %d\n"
#define LKM_STATE_BUTTON_INTERRUPT "GPIO_TEST: Interrupt! (button state is %d)\n"
#define LMK_NUMBER_BUTTON_PRESSES "GPIO_TEST: The button was pressed %d times\n"
#define LKM_LED_VALIDATION "GPIO_TEST: invalid LED GPIO\n"
#define LKM_BUTTON_MAPPED "GPIO_TEST: The button is mapped to IRQ: %d\n"
#define LKM_BUTON_RESULT "GPIO_TEST: The interrupt request result is: %d\n"
/** */
#define  DEVICE_NAME "BBLKM"    ///< The device will appear at /dev/ebbchar using this value
#define  CLASS_NAME  "LKM"        ///< The device class -- this is a character device driver

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

MODULE_LICENSE("GPL");              ///< The license type -- this affects runtime behavior
MODULE_AUTHOR("jairoM26");      ///< The author -- visible when you use modinfo
MODULE_DESCRIPTION("A simple Linux driver for the BBB.");  ///< The description -- see modinfo
MODULE_VERSION("1.0");              ///< The version of the module

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Structs definitions
 * This part of the code it's copy for http://derekmolloy.ie/writing-a-linux-kernel-module-part-2-a-character-device/
 * @brief Devices are represented as file structure in the kernel. The file_operations structure from
 *  /linux/fs.h lists the callback functions that you wish to associated with your file operations
 *  using a C99 syntax structure. char devices usually implement open, read, write and release calls
*/
 // Note: __user refers to a user-space address.
static struct file_operations fops =
{
   .open = dev_open,
   .read = dev_read,
   .write = dev_write,
   .release = dev_release,
};


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Functions definitions
*/

static int BUTTON_init(void){
    int result = 0;
    
    // Is the GPIO a valid GPIO number (e.g., the BBB has 4x32 but not all available)
    if (!gpio_is_valid(gpioLED)){
       printk(KERN_INFO LKM_LED_VALIDATION);
       return -ENODEV;
    }
    // Going to set up the LED. It is a GPIO in output mode and will be on by default
    ledOn = true;
    gpio_request(gpioLED, "sysfs");          // gpioLED is hardcoded to 49, request it
    gpio_direction_output(gpioLED, ledOn);   // Set the gpio to be in output mode and on

    gpio_export(gpioLED, false);             // Causes gpio49 to appear in /sys/class/gpio
                     // the bool argument prevents the direction from being changed
    gpio_request(gpioButton, "sysfs");       // Set up the gpioButton
    gpio_direction_input(gpioButton);        // Set the button GPIO to be an input
    gpio_set_debounce(gpioButton, 200);      // Debounce the button with a delay of 200ms
    gpio_export(gpioButton, false);          // Causes gpio115 to appear in /sys/class/gpio
                     // the bool argument prevents the direction from being changed
    // Perform a quick test to see that the button is working as expected on LKM load
    printk(KERN_INFO  LKM_STATE_BUTTON, gpio_get_value(gpioButton));
 
    // GPIO numbers and IRQ numbers are not the same! This function performs the mapping for us
    irqNumber = gpio_to_irq(gpioButton);
    printk(KERN_INFO LKM_BUTTON_MAPPED, irqNumber);
 
    // This next call requests an interrupt line
    result = request_irq(irqNumber,             // The interrupt number requested
                         (irq_handler_t) ebbgpio_irq_handler, // The pointer to the handler function below
                         IRQF_TRIGGER_RISING,   // Interrupt on rising edge (button press, not release)
                         "ebb_gpio_handler",    // Used in /proc/interrupts to identify the owner
                         NULL);                 // The *dev_id for shared interrupt lines, NULL is okay
 
    printk(KERN_INFO LKM_BUTON_RESULT, result);
    return result;


}	
/**
 * @brief The LKM initialization function
 * The static keywords restricts the visibility of the functions to within
 * The __init macro means that for a built-in driver the function is only for initialization time and can be free after that
 * @return 0 if it is sucessful	
*/
static int __init LKM_init(void){

    
	printk(KERN_INFO LKM_INITIALIZATION);
	BUTTON_init();

	// Try to dynamically allocate a major number for the device -- more difficult but worth it
    majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
    if (majorNumber<0){
       printk(KERN_ALERT LKM_FILE_REGISTER_MAJOR_NUMBER);
       return majorNumber;
    }
    printk(KERN_INFO LKM_REGISTER_MAJOR_NUMBER_CORRECTLY, majorNumber);
    // Register the device class
    LKMClass = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(LKMClass)){                // Check for error and clean up if there is
       unregister_chrdev(majorNumber, DEVICE_NAME);
       printk(KERN_ALERT LKM_FILE_REGISTER_DEVICE_CLASS);
       return PTR_ERR(LKMClass);          // Correct way to return an error on a pointer
    }
    printk(KERN_INFO LKM_DEVICE_CLASS_REGISTERED_CORRECTLY);

    // Register the device driver
    BBLKMDevice = device_create(LKMClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
    if (IS_ERR(BBLKMDevice)){               // Clean up if there is an error
       class_destroy(LKMClass);           // Repeated code but the alternative is goto statements
       unregister_chrdev(majorNumber, DEVICE_NAME);
       printk(KERN_ALERT LKM_FAILED_IN_CREATED_THE_DEVICE);
       return PTR_ERR(BBLKMDevice);
    }
    printk(KERN_INFO LKM_CREATE_DEVICE_CORRECTLY); // Made it! device was initialized
	return SUCCESS;
}

static void BUTTON_exit(void){
    printk(KERN_INFO LKM_STATE_BUTTON, gpio_get_value(gpioButton));
    printk(KERN_INFO LMK_NUMBER_BUTTON_PRESSES, numberPresses);
    gpio_set_value(gpioLED, 0);              // Turn the LED off, makes it clear the device was unloaded
    gpio_unexport(gpioLED);                  // Unexport the LED GPIO
    free_irq(irqNumber, NULL);               // Free the IRQ number, no *dev_id required in this case
    gpio_unexport(gpioButton);               // Unexport the Button GPIO
    gpio_free(gpioLED);                      // Free the LED GPIO
    gpio_free(gpioButton);                   // Free the Button GPIO


}

/**
 * @brief The LKM ended-cleanup function
 *  /** @brief The LKM cleanup function
 *  Similar to the initialization function, it is static. The __exit macro notifies that if this
 *  code is used for a built-in driver (not a LKM) that this function is not required.
*/
static void __exit LKM_exit(void){
	BUTTON_exit();
	device_destroy(LKMClass, MKDEV(majorNumber, 0));     // remove the device
    class_unregister(LKMClass);                          // unregister the device class
    class_destroy(LKMClass);                             // remove the device class
    unregister_chrdev(majorNumber, DEVICE_NAME);             // unregister the major number
	printk(KERN_INFO LKM_ENDED);
}

/** @brief The device open function that is called each time the device is opened
 *  This will only increment the numberOpens counter in this case.
 *  @param inodep A pointer to an inode object (defined in linux/fs.h)
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int dev_open(struct inode *inodep, struct file *filep){
   numberOpens++;
   printk(KERN_INFO LKM_OPENED_DEVICE, numberOpens);
   return 0;
}

/** @brief This function is called whenever device is being read from user space i.e. data is
 *  being sent from the device to the user. In this case is uses the copy_to_user() function to
 *  send the buffer string to the user and captures any errors.
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 *  @param buffer The pointer to the buffer to which this function writes the data
 *  @param len The length of the b
 *  @param offset The offset if required
 */
static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset){
   int error_count = 0;
   // copy_to_user has the format ( * to, *from, size) and returns 0 on success
   error_count = copy_to_user(buffer, message, size_of_message);
 
   if (error_count==0){            // if true then have success
      printk(KERN_INFO LKM_SENT_DATA_TO_USER, size_of_message);
      return (size_of_message=0);  // clear the position to the start and return 0
   }
   else {
      printk(KERN_INFO LKM_FAILED_SENDING_DATA_TO_USER, error_count);
      return -EFAULT;              // Failed -- return a bad address message (i.e. -14)
   }
}

/** @brief This function is called whenever the device is being written to from user space i.e.
 *  data is sent to the device from the user. The data is copied to the message[] array in this
 *  LKM using the sprintf() function along with the length of the string.
 *  @param filep A pointer to a file object
 *  @param buffer The buffer to that contains the string to write to the device
 *  @param len The length of the array of data that is being passed in the const char buffer
 *  @param offset The offset if required
 */
static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset){
   sprintf(message, "%s(%d letters)", buffer, len);   // appending received string with its length
   size_of_message = strlen(message);                 // store the length of the stored message
   printk(KERN_INFO LKM_RECIEVE_DATA_FROM_USER, len);
   return len;
}
 
/** @brief The device release function that is called whenever the device is closed/released by
 *  the userspace program
 *  @param inodep A pointer to an inode object (defined in linux/fs.h)
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int dev_release(struct inode *inodep, struct file *filep){
   printk(KERN_INFO LKM_CLOSED_DEVICE_SUCCESSFULLY);
   return 0;
}

/** @brief The GPIO IRQ Handler function
 *  This function is a custom interrupt handler that is attached to the GPIO above. 
 *  
 *  @param irq    the IRQ number that is associated with the GPIO -- useful for logging.
 *  @param dev_id the *dev_id that is provided -- can be used to identify which device caused the interrupt
 *  
 *  @param regs   h/w specific register values -- only really ever used for debugging.
 *  return returns IRQ_HANDLED if successful -- should return IRQ_NONE otherwise.
 */
static irq_handler_t ebbgpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs){
   ledOn = !ledOn;                          // Invert the LED state on each button press
   gpio_set_value(gpioLED, ledOn);          // Set the physical LED accordingly
   printk(KERN_INFO LKM_STATE_BUTTON_INTERRUPT, gpio_get_value(gpioButton));
   numberPresses++;                         // Global counter, will be outputted when the module is unloaded
   return (irq_handler_t) IRQ_HANDLED;      // Announce that the IRQ has been handled correctly
}



/**
 * @brief A module use the module_init() module_exit() macros from linux/init.h, which
 * identify the initialization function time ande cleanup function
*/
 module_init(LKM_init);
 module_exit(LKM_exit);

 /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////