/**
	@author jairoM26
	@brief This is an example of a LKM that reverse the messages that it recieves
*/

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jairo Méndez Martínez  <jairomendezmartinez@gmail.com>");
MODULE_DESCRIPTION("example of a LKM that reverse the messages that it recieves");

