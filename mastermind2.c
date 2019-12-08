/* YOUR FILE-HEADER COMMENT HERE */

/*
 * This file uses kernel-doc style comments, which is similar to
 * Javadoc and Doxygen-style comments.  See
 * ~/linux/Documentation/kernel-doc-nano-HOWTO.txt for details.
 */

/*
 * Getting compilation warnings?  The Linux kernel is written against
 * C89, which means:
 *  - No // comments, and
 *  - All variables must be declared at the top of functions.
 * Read ~/linux/Documentation/CodingStyle to ensure your project
 * compiles without warnings.
 */

#define pr_fmt(fmt) "mastermind2: " fmt

#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/uidgid.h>
#include <linux/vmalloc.h>

#include "nf_cs421net.h"

#define NUM_PEGS 4
#define NUM_COLORS 6

/** true if user is in the middle of a game */
static bool game_active;

/** code that player is trying to guess */
static int target_code[NUM_PEGS];

/** tracks number of guesses user has made */
static unsigned num_guesses;

/** result of most recent user guess */
static char last_result[4];

/** buffer that records all of user's guesses and their results */
static char *user_view;

/**
 * mm_num_pegs() - calculate number of black pegs and number of white pegs
 * @target: target code, up to NUM_PEGS elements
 * @guess: user's guess, up to NUM_PEGS elements
 * @num_black: *OUT* parameter, to store calculated number of black pegs
 * @num_white: *OUT* parameter, to store calculated number of white pegs
 *
 * You do not need to modify this function.
 *
 */
static void mm_num_pegs(int target[], int guess[], unsigned *num_black,
			unsigned *num_white)
{
	size_t i;
	size_t j;
	bool peg_black[NUM_PEGS];
	bool peg_used[NUM_PEGS];

	*num_black = 0;
	for (i = 0; i < NUM_PEGS; i++) {
		if (guess[i] == target[i]) {
			(*num_black)++;
			peg_black[i] = true;
			peg_used[i] = true;
		} else {
			peg_black[i] = false;
			peg_used[i] = false;
		}
	}

	*num_white = 0;
	for (i = 0; i < NUM_PEGS; i++) {
		if (peg_black[i])
			continue;
		for (j = 0; j < NUM_PEGS; j++) {
			if (guess[i] == target[j] && !peg_used[j]) {
				peg_used[j] = true;
				(*num_white)++;
				break;
			}
		}
	}
}

/* Copy mm_read(), mm_write(), mm_mmap(), and mm_ctl_write(), along
 * with all of your global variables and helper functions here.
 */
/* Part 1: YOUR CODE HERE */

/**
 * cs421net_top() - top-half of CS421Net ISR
 * @irq: IRQ that was invoked (ignored)
 * @cookie: Pointer to data that was passed into
 * request_threaded_irq() (ignored)
 *
 * If @irq is CS421NET_IRQ, then wake up the bottom-half. Otherwise,
 * return IRQ_NONE.
 */
static irqreturn_t cs421net_top(int irq, void *cookie)
{
	/* Part 4: YOUR CODE HERE */
	return 0;
}

/**
 * cs421net_bottom() - bottom-half to CS421Net ISR
 * @irq: IRQ that was invoked (ignore)
 * @cookie: Pointer that was passed into request_threaded_irq()
 * (ignored)
 *
 * Fetch the incoming packet, via cs421net_get_data(). If:
 *   1. The packet length is exactly equal to four bytes, and
 *   2. If all characters in the packet are valid ASCII representation
 *      of valid digits in the code, then
 * Set the target code to the new code, and increment the number of
 * tymes the code was changed remotely. Otherwise, ignore the packet
 * and increment the number of invalid change attempts.
 *
 * Because the payload is dynamically allocated, free it after parsing
 * it.
 *
 * During Part 5, update this function to change all codes for all
 * active games.
 *
 * Remember to add appropriate spin lock calls in this function.
 *
 * <em>Caution: The incoming payload is NOT a string; it is not
 * necessarily null-terminated.</em> You CANNOT use strcpy() or
 * strlen() on it!
 *
 * Return: always IRQ_HANDLED
 */
static irqreturn_t cs421net_bottom(int irq, void *cookie)
{
	/* Part 4: YOUR CODE HERE */
	return IRQ_HANDLED;
}

/**
 * mm_stats_show() - callback invoked when a process reads from
 * /sys/devices/platform/mastermind/stats
 *
 * @dev: device driver data for sysfs entry (ignored)
 * @attr: sysfs entry context (ignored)
 * @buf: destination to store game statistics
 *
 * Write to @buf, up to PAGE_SIZE characters, a human-readable message
 * containing these game statistics:
 *   - Number of colors (range of digits in target code)
 *   - Number of started games
 *   - Number of active games
 *   - Number of valid network messages (see Part 4)
 *   - Number of invalid network messages (see Part 4)
 * Note that @buf is a normal character buffer, not a __user
 * buffer. Use scnprintf() in this function.
 *
 * @return Number of bytes written to @buf, or negative on error.
 */
static ssize_t mm_stats_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	/* Part 3: YOUR CODE HERE */
	return -EPERM;
}

static DEVICE_ATTR(stats, S_IRUGO, mm_stats_show, NULL);

/**
 * mastermind_probe() - callback invoked when this driver is probed
 * @pdev platform device driver data
 *
 * Return: 0 on successful probing, negative on error
 */
static int mastermind_probe(struct platform_device *pdev)
{
	/* Merge the contents of your original mastermind_init() here. */
	/* Part 1: YOUR CODE HERE */

	/*
	 * You will need to integrate the following resource allocator
	 * into your code. That also means properly releasing the
	 * resource if the function fails.
	 */
	int retval;
	retval = device_create_file(&pdev->dev, &dev_attr_stats);
	if (retval) {
		pr_err("Could not create sysfs entry\n");
	}
	return retval;
}

/**
 * mastermind_remove() - callback when this driver is removed
 * @pdev platform device driver data
 *
 * Return: Always 0
 */
static int mastermind_remove(struct platform_device *pdev)
{
	/* Merge the contents of your original mastermind_exit() here. */
	/* Part 1: YOUR CODE HERE */

	device_remove_file(&pdev->dev, &dev_attr_stats);
	return 0;
}

static struct platform_driver cs421_driver = {
	.driver = {
		   .name = "mastermind",
		   },
	.probe = mastermind_probe,
	.remove = mastermind_remove,
};

static struct platform_device *pdev;

/**
 * cs421_init() -  create the platform driver
 * This is needed so that the device gains a sysfs group.
 *
 * <strong>You do not need to modify this function.</strong>
 */
static int __init cs421_init(void)
{
	pdev = platform_device_register_simple("mastermind", -1, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);
	return platform_driver_register(&cs421_driver);
}

/**
 * cs421_exit() - remove the platform driver
 * Unregister the driver from the platform bus.
 *
 * <strong>You do not need to modify this function.</strong>
 */
static void __exit cs421_exit(void)
{
	platform_driver_unregister(&cs421_driver);
	platform_device_unregister(pdev);
}

module_init(cs421_init);
module_exit(cs421_exit);

MODULE_DESCRIPTION("CS421 Mastermind Game++");
MODULE_LICENSE("GPL");
