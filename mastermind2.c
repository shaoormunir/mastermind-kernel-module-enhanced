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
#define USER_VIEW_LINE_SIZE 22

static int NUM_COLORS = 6;

/** number of games currently active */
static int games_active = 0;

/** number of games started */
static int games_started = 0;

/** number of times the code was changed **/
static int codes_changed = 0;

/** number of times there was an invalid attempt to change colors */
static int invalid_attempts = 0;

struct mm_game
{
	struct list_head list;
	kuid_t uid;
	bool game_active;
	int target_code[NUM_PEGS];
	unsigned num_guesses;
	char last_result[4];
	char *user_view;
	size_t user_view_size;
	loff_t user_view_pointer;
};

struct list_head game_list;
LIST_HEAD(game_list);

DEFINE_SPINLOCK(device_data_lock);

/**
 * initialize_game() - initializes all required variables for the game
 * */
static void initialize_game(struct mm_game * game)
{
	size_t i;
	game->target_code[0] = 4;
	game->target_code[1] = 2;
	game->target_code[2] = 1;
	game->target_code[3] = 1;
	game->num_guesses = 0;
	for (i = 0; i < 4096; i++)
	{
		game->user_view[i] = 0;
	}
	game->user_view_pointer = 0;
	game->user_view_size = 0;
	game->game_active = true;
	games_started++;
	games_active++;
	game->last_result[0] = 'B';
	game->last_result[1] = '-';
	game->last_result[2] = 'W';
	game->last_result[3] = '-';
}

static struct mm_game *mm_find_game(kuid_t uid){
	struct list_head *ptr;
	struct mm_game * game;
	for (ptr = game_list.next; ptr!=&game_list; ptr = ptr->next){
		game = list_entry (ptr, struct mm_game, list);
		if (uid_eq(game->uid, uid)){
			return game;
		} 
	}
	struct mm_game * new = (struct mm_game *) kzalloc(sizeof(struct mm_game), GFP_KERNEL);
	new->uid = uid;
	spin_lock(&device_data_lock);
	new->user_view = vmalloc(PAGE_SIZE);
	if (!new->user_view)
	{
		pr_err("Could not allocate memory\n");
		spin_unlock(&device_data_lock);
		return -ENOMEM;
	}
	list_add_tail(&new->list, game_list.next);
	spin_unlock(&device_data_lock);
	return new;
}
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
 * compare_strings() - takes in two char strings along with their sizes and checks if both strings
 * are equal or not 
 * @source_string: source string to compare
 * @source_size: size of source string
 * @dest_stirng: destination string
 * @dest_size: size of destination string
 * */
static bool compare_strings(const char *source_string, size_t source_size, const char *dest_string, size_t dest_size)
{
	bool areEqual = true;
	size_t i;
	size_t j;
	for (i = 0, j = 0; i < source_size && j < dest_size; i++, j++)
	{
		if (source_string[i] != dest_string[j])
		{
			areEqual = false;
			break;
		}
	}
	return areEqual;
}

/**
 * mm_read() - callback invoked when a process reads from
 * /dev/mm
 * @filp: process's file object that is reading from this device (ignored)
 * @ubuf: destination buffer to store output
 * @count: number of bytes in @ubuf
 * @ppos: file offset (in/out parameter)
 *
 * Write to @ubuf the last result of the game, offset by
 * @ppos. Copy the lesser of @count and (string length of @last_result
 * - *@ppos). Then increment the value pointed to by @ppos by the
 * number of bytes copied. If @ppos is greater than or equal to the
 * length of @last_result, then copy nothing.
 *
 * If no game is active, instead copy to @ubuf up to four '?'
 * characters.
 *
 * Return: number of bytes written to @ubuf, or negative on error
 */
static ssize_t mm_read(struct file *filp, char __user *ubuf, size_t count,
					   loff_t *ppos)
{
	struct mm_game * game = mm_find_game(current_cred()->uid);
	size_t bytes_to_copy = 4 - *ppos;
	if (bytes_to_copy > count && count > 0)
	{
		bytes_to_copy = count;
	}
	if (*ppos >= 4)
	{
		return -1;
	}
	else
	{
		spin_lock(&device_data_lock);
		int copy_result = 0;
		if (game->game_active)
		{
			copy_result = copy_to_user(ubuf + *ppos, game->last_result, bytes_to_copy);
		}
		else
		{
			copy_result = copy_to_user(ubuf + *ppos, "????", bytes_to_copy);
		}
		if (copy_result != 0)
		{
			spin_unlock(&device_data_lock);
			return -1;
		}
		*ppos += bytes_to_copy;
		spin_unlock(&device_data_lock);
		return bytes_to_copy;
	}
}
/**
 * convert_number_to_array() - takes in a number, converts it into a string and stores the result in 
 * the result array provided in the arguments
 * @number: number to be converted
 * @result: destination array to store the converted string in
 * 
 * Return: size of the string
 * */
static size_t convert_number_to_array(int number, char **result)
{
	int remainder;
	int digit;
	size_t i;
	size_t array_size = 0;
	remainder = number;
	while (remainder > 0)
	{
		remainder = remainder / 10;
		array_size++;
	}
	if(array_size == 0) array_size = 1;
	*result = vmalloc(array_size);
	remainder = number;
	for (i = 0; i < array_size; i++)
	{
		digit = remainder % 10;
		*result[i] = '0' + digit;
		remainder = remainder / 10;
	}
	return array_size;
}
/**
 * write_last_result_to_user_view() - takes a characte arrays consisting of user's last guess and
 * writes the result to user view array
 * @user_guess: user's guess
 * */
static void write_last_result_to_user_view(char *user_guess, struct mm_game * game)
{

	char result_to_write[USER_VIEW_LINE_SIZE];
	char *guess_number_char_array;
	size_t guess_array_size;
	size_t i;
	loff_t current_result_buffer_pointer;
	current_result_buffer_pointer = 0;
	for (i = 0; i < USER_VIEW_LINE_SIZE; i++)
	{
		result_to_write[i] = 0;
	}
	current_result_buffer_pointer += scnprintf(result_to_write, 7, "Guess ");
	guess_array_size = convert_number_to_array(game->num_guesses, &guess_number_char_array);

	current_result_buffer_pointer += scnprintf(result_to_write + current_result_buffer_pointer, guess_array_size + 1, guess_number_char_array);
	current_result_buffer_pointer += scnprintf(result_to_write + current_result_buffer_pointer, 3, ": ");
	current_result_buffer_pointer += scnprintf(result_to_write + current_result_buffer_pointer, 5, game->last_result);
	current_result_buffer_pointer += scnprintf(result_to_write + current_result_buffer_pointer, 4, " | ");
	current_result_buffer_pointer += scnprintf(result_to_write + current_result_buffer_pointer, NUM_PEGS + 1, user_guess);
	current_result_buffer_pointer += scnprintf(result_to_write + current_result_buffer_pointer, 2, "\n");
	strcpy(game->user_view + game->user_view_pointer, result_to_write);
	game->user_view_pointer += USER_VIEW_LINE_SIZE;
	game->user_view_size += USER_VIEW_LINE_SIZE;
}

static void write_success_message_to_user_view(struct mm_game * game){
	char result_to_write[USER_VIEW_LINE_SIZE];
	loff_t current_result_buffer_pointer;
	current_result_buffer_pointer = 0;
	current_result_buffer_pointer += scnprintf(result_to_write, 20, "You won, game over!");
	current_result_buffer_pointer += scnprintf(result_to_write + current_result_buffer_pointer, 2, "\n");
	strcpy(game->user_view + game->user_view_pointer, result_to_write);
	game->user_view_pointer += USER_VIEW_LINE_SIZE;
	game->user_view_size += USER_VIEW_LINE_SIZE;
}

/**
 * mm_write() - callback invoked when a process writes to /dev/mm
 * @filp: process's file object that is reading from this device (ignored)
 * @ubuf: source buffer from user
 * @count: number of bytes in @ubuf
 * @ppos: file offset (ignored)
 *
 * If the user is not currently playing a game, then return -EINVAL.
 *
 * If @count is less than NUM_PEGS, then return -EINVAL. Otherwise,
 * interpret the first NUM_PEGS characters in @ubuf as the user's
 * guess. Calculate how many are in the correct value and position,
 * and how many are simply the correct value. Then update
 * @num_guesses, @last_result, and @user_view.
 *
 * <em>Caution: @ubuf is NOT a string; it is not necessarily
 * null-terminated.</em> You CANNOT use strcpy() or strlen() on it!
 *
 * Return: @count, or negative on error
 */
static ssize_t
mm_write(struct file *filp, const char __user *ubuf,
		 size_t count, loff_t *ppos)
{
	struct mm_game * game = mm_find_game(current_cred()->uid);
	int correct_place_guesses = 0;
	int correct_value_guesses = 0;
	char temp_array[NUM_PEGS] = {};
	int user_guess[NUM_PEGS] = {};
	size_t i;
	if (game->game_active)
	{
		if (count < NUM_PEGS)
		{
			return -EINVAL;
		}
		else
		{
			copy_from_user(temp_array, ubuf, NUM_PEGS);
			spin_lock(&device_data_lock);
			for (i = 0; i < NUM_PEGS; i++)
			{
				user_guess[i] = temp_array[i] - '0';
			}
			mm_num_pegs(game->target_code, user_guess, &correct_place_guesses, &correct_value_guesses);
			game->last_result[1] = '0' + correct_place_guesses;
			game->last_result[3] = '0' + correct_value_guesses;
			game->num_guesses++;
			write_last_result_to_user_view(temp_array, game);
			if(correct_place_guesses == 4){
				write_success_message_to_user_view(game);
				game->game_active = false;
			}
			spin_unlock(&device_data_lock);
			return count;
		}
	}
	else
	{
		return -EINVAL;
	}
}

/**
 * mm_mmap() - callback invoked when a process mmap()s to /dev/mm
 * @filp: process's file object that is mapping to this device (ignored)
 * @vma: virtual memory allocation object containing mmap() request
 *
 * Create a read-only mapping from kernel memory (specifically,
 * @user_view) into user space.
 *
 * Code based upon
 * <a href="http://bloggar.combitech.se/ldc/2015/01/21/mmap-memory-between-kernel-and-userspace/">http://bloggar.combitech.se/ldc/2015/01/21/mmap-memory-between-kernel-and-userspace/</a>
 *
 * You do not need to modify this function.
 *
 * Return: 0 on success, negative on error.
 */
static int mm_mmap(struct file *filp, struct vm_area_struct *vma)
{

	struct mm_game * game = mm_find_game(current_cred()->uid);
	unsigned long size = (unsigned long)(vma->vm_end - vma->vm_start);
	unsigned long page = vmalloc_to_pfn(game->user_view);
	if (size > PAGE_SIZE)
		return -EIO;
	vma->vm_pgoff = 0;
	vma->vm_page_prot = PAGE_READONLY;
	if (remap_pfn_range(vma, vma->vm_start, page, size, vma->vm_page_prot))
		return -EAGAIN;
	return 0;
}

/**
 * mm_ctl_write() - callback invoked when a process writes to
 * /dev/mm_ctl
 * @filp: process's file object that is writing to this device (ignored)
 * @ubuf: source buffer from user
 * @count: number of bytes in @ubuf
 * @ppos: file offset (ignored)
 *
 * Copy the contents of @ubuf, up to the lesser of @count and 8 bytes,
 * to a temporary location. Then parse that character array as
 * following:
 *
 *  start - Start a new game. If a game was already in progress, restart it.
 *  quit  - Quit the current game. If no game was in progress, do nothing.
 *
 * If the input is neither of the above, then return -EINVAL.
 *
 * <em>Caution: @ubuf is NOT a string;</em> it is not necessarily
 * null-terminated, nor does it necessarily have a trailing
 * newline. You CANNOT use strcpy() or strlen() on it!
 *
 * Return: @count, or negative on error
 */
static ssize_t mm_ctl_write(struct file *filp, const char __user *ubuf,
							size_t count, loff_t *ppos)
{
	struct mm_game * game = mm_find_game(current_cred()->uid);
	char temp_array[8] = {};
	size_t temp_length = 8;
	if (count < 8)
	{
		temp_length = count;
	}

	copy_from_user(temp_array, ubuf, temp_length);
	spin_lock(&device_data_lock);

	if (compare_strings(temp_array, temp_length, "start", 5))
	{
		initialize_game(game);
	}
	else if (compare_strings(temp_array, temp_length, "quit", 4))
	{
		game->game_active = false;
	}
	else if (compare_strings(temp_array, 6, "colors", 6)){
		if (!capable(CAP_SYS_ADMIN)){
			spin_unlock(&device_data_lock);
			return -EACCES;
		}
		else{
			int colors = temp_array[7] - 48;
			if (colors >= 2 && colors <= 9){
				NUM_COLORS = colors;	
			}
			else
			{
				spin_unlock(&device_data_lock);
				return -EINVAL;
			}
			
		}
	}
	spin_unlock(&device_data_lock);
	return count;
}

/** strcut to handle call backs to dev/mm */
static const struct file_operations mm_operations = {
	.read = mm_read,
	.write = mm_write,
	.mmap = mm_mmap,
};

static struct miscdevice mastermind_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mm",
	.fops = &mm_operations,
	.mode = 0666,
};

/** strcut to handle call backs to dev/mm _ctl*/
static const struct file_operations mm_ctl_operations = {
	.write = mm_ctl_write,
};

static struct miscdevice mastermind_ctl_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mm_ctl",
	.fops = &mm_ctl_operations,
	.mode = 0666,
};

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
	if(irq == CS421NET_IRQ){
		cs421net_bottom(irq,cookie);
		return IRQ_WAKE_THREAD;
	}else
	{
		return IRQ_NONE;
	}
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
	bool validData = true;
	size_t i;
	struct list_head *pos, *n;
	struct mm_game *temp;
	/* Part 4: YOUR CODE HERE */
	char * data = cs421net_get_data();
	for ( i = 0; i < 4; i++)
	{
		if(data[i] <48 && data[i] > 57){
			validData = false;
		}
	}
	if(validData){
		for (pos = game_list.next; pos != game_list.next; pos = pos->next)
		{
			 temp = list_entry(pos, struct mm_game, list);
			 for ( i = 0; i < 4; i++)
			 {
				 temp->target_code[i] = data[i];
			 }
		}
		codes_changed++;
	}
	else
	{
		invalid_attempts++;
	}
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
	char message_to_write[PAGE_SIZE];
	char *temp_number_array;
	size_t temp_array_size;
	size_t i;
	ssize_t buffer_size;
	loff_t current_message_buffer_pointer;
	current_message_buffer_pointer = 0;
	buffer_size = 0;
	for (i = 0; i < PAGE_SIZE; i++)
	{
		message_to_write[i] = 0;
	}

	current_message_buffer_pointer += scnprintf(message_to_write, 19, "Number of colors: ");
	buffer_size+=19;
	temp_array_size = convert_number_to_array(NUM_COLORS, &temp_number_array);
	current_message_buffer_pointer += scnprintf(message_to_write + current_message_buffer_pointer, temp_array_size + 1, temp_number_array);
	buffer_size+=temp_array_size+1;

	current_message_buffer_pointer += scnprintf(message_to_write + current_message_buffer_pointer, 28, "\nNumber of started games: ");
	buffer_size+=28;
	temp_array_size = convert_number_to_array(games_started, &temp_number_array);
	current_message_buffer_pointer += scnprintf(message_to_write + current_message_buffer_pointer, temp_array_size + 1, temp_number_array);
	buffer_size+=temp_array_size+1;

	current_message_buffer_pointer += scnprintf(message_to_write + current_message_buffer_pointer, 27, "\nNumber of active games: ");
	buffer_size+=27;
	temp_array_size = convert_number_to_array(games_active, &temp_number_array);
	current_message_buffer_pointer += scnprintf(message_to_write + current_message_buffer_pointer, temp_array_size + 1, temp_number_array);
	buffer_size+=temp_array_size+1;

	current_message_buffer_pointer += scnprintf(message_to_write + current_message_buffer_pointer, 27, "\nNumber of times code was changed: ");
	buffer_size+=27;
	temp_array_size = convert_number_to_array(codes_changed, &temp_number_array);
	current_message_buffer_pointer += scnprintf(message_to_write + current_message_buffer_pointer, temp_array_size + 1, temp_number_array);
	buffer_size+=temp_array_size+1;

	current_message_buffer_pointer += scnprintf(message_to_write + current_message_buffer_pointer, 27, "\nNumber of invalid code change attempts: ");
	buffer_size+=27;
	temp_array_size = convert_number_to_array(invalid_attempts, &temp_number_array);
	current_message_buffer_pointer += scnprintf(message_to_write + current_message_buffer_pointer, temp_array_size + 1, temp_number_array);
	buffer_size+=temp_array_size+1;

	current_message_buffer_pointer += scnprintf(message_to_write + current_message_buffer_pointer, 2, "\n");
	buffer_size+=temp_array_size+2;

	printk("The message to print is :");
	for (i = 0; i < PAGE_SIZE; i++)
	{
		printk("%c", message_to_write[i]);
	}
	printk("The total size of the buffer is: %ld", buffer_size);
	
	scnprintf(buf, buffer_size+1, message_to_write);
	return buffer_size+1;
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
	int retval;
	pr_info("Initializing the game.\n");
	retval = misc_register(&mastermind_device);
	if (retval)
	{
		printk("There was some error while registering main device.");
		pr_err("can't misc_register :(\n");
		return retval;
	}
	retval = misc_register(&mastermind_ctl_device);
	if (retval)
	{
		printk("There was some error while registering control device.");
		pr_err("can't misc_register :(\n");
		return retval;
	}

	/*
	 * You will need to integrate the following resource allocator
	 * into your code. That also means properly releasing the
	 * resource if the function fails.
	 */

	retval = request_threaded_irq(CS421NET_IRQ, cs421net_top, cs421net_bottom, IRQF_SHARED, "CS421IRQ", NULL);
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
	struct list_head *pos, *n;
	struct mm_game *temp;

	pr_info("Freeing resources.\n");
	 for (pos = game_list.next, n = pos->next; pos != game_list.next; pos = n, n = pos->next){
		 temp = list_entry(pos, struct mm_game, list);
		 list_del(pos);
		 vfree(temp->user_view);
		 vfree(temp);
	 }

	misc_deregister(&mastermind_device);
	misc_deregister(&mastermind_ctl_device);

	free_irq(CS421NET_IRQ, NULL);

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
