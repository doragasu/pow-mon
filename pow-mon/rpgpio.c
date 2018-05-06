#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <bcm_host.h>

#include "rpgpio.h"

static volatile uint32_t *gpio = NULL;

/// Write a GPIO register
#define RPG_WRITE(reg_name, value) do {			\
	*(gpio + reg_name) = value;} while(0)

/// Read a GPIO register
#define RPG_READ(reg_name) *(gpio + reg_name)

/// Expands register table as printfs printing register addresses and values
#define RPG_EXPAND_AS_PRINT_REGS(reg_name, offset)			\
		printf(#reg_name ": (0x%02X) = 0x%08X\n", (offset)*4, RPG_READ(offset));

void rpg_reg_print(void) {
	RPG_REG_TABLE(RPG_EXPAND_AS_PRINT_REGS);
}

void rpg_perror(rpg_retval retval, const char msg[]) {
	char *str;

	switch (retval) {
		case RPG_OK:
			str = "Success!";
			break;

		case RPG_ERR_UNDEFINED:
			str = "Undefined error.";
			break;

		case RPG_ERR_PERMISSIONS:
			str = "Not enough permissions.";
			break;

		case RPG_ERR_MAP:
			str = "Memory map failed.";
			break;

		case RPG_ERR_PIN:
			str = "Invalid pin.";
			break;

		case RPG_ERR_OPEN:
			str = "Could not open file.";
			break;

		case RPG_ERR_WRITE:
			str = "Write to file failed.";
			break;

		case RPG_ERR_READ:
			str = "File read failed.";
			break;

		default:
			str ="Unspecified error code.";
	}
	if (msg) {
		// Not safe!
		fprintf(stderr, "%s: ", msg);
	}
	fprintf(stderr, "%s\n", str);
}

int rpg_init(void) {
	int fd;
	uint32_t base;

	// Usually requires root permissions
	if ((fd = open ("/dev/mem", O_RDWR | O_SYNC) ) < 0) {
		return -1;
	}

	base = bcm_host_get_peripheral_address() + RPG_BASE;
	gpio = (volatile uint32_t *)mmap(0, getpagesize(), PROT_READ |
			PROT_WRITE, MAP_SHARED, fd, base);
	
	if (MAP_FAILED == gpio) {
	    return -1;
	}

	return RPG_OK;
}

rpg_retval rpg_configure(int pin, rpg_func func) {
	int idx, pos;
	uint32_t scratch;

	// Check pin is valid
	if (pin < 0 || pin > RPG_PIN_MAX) return RPG_ERR_PIN;

	// Get GPFSELx index, and pin position in the register
	idx = pin / 10;
	pos = 3 * (pin - 10 * idx);

	// Configure requested pin
	scratch = RPG_READ(GPFSEL0 + idx);
	scratch &= ~(7<<pos);
	scratch |= (func<<pos);
	RPG_WRITE(GPFSEL0 + idx, scratch);

	return RPG_OK;
}

int rpg_read(int pin) {
	int bank;

	bank = pin / 32;
	pin -= bank * 32;
	return (RPG_READ(GPLEV0 + bank) & (1<<pin))>>pin;
}

void rpg_set(int pin) {
	int bank;

	bank = pin / 32;
	pin -= bank * 32;
	RPG_WRITE(GPSET0 + bank, 1<<pin);
}

void rpg_clear(int pin) {
	int bank;

	bank = pin / 32;
	pin -= bank * 32;
	RPG_WRITE(GPCLR0 + bank, 1<<pin);
}

/* sysfs style functions are used only for interrupt handling */

#define RPG_MAX_FILE_LEN	64

#define RPG_GPIO_PATH		"/sys/class/gpio/"

/// Opens the file, writes the string and closes it immediately
rpg_retval rpg_file_write(const char filename[], const char string[]) {
	int fd;
	size_t len;

	fd = open(filename, O_WRONLY);
	if (fd < 0) return RPG_ERR_OPEN;

	len = strlen(string);
	if (write(fd, string, len) != len) {
		close(fd);
		return RPG_ERR_WRITE;
	}
	close(fd);
	return RPG_OK;
}

/// Opens the file, reads the string and closes it immediately
rpg_retval rpg_file_read(const char filename[], char string[], int maxLen) {
	int fd;
	ssize_t readed;
	ssize_t iteration;

	fd = open(filename, O_RDONLY);
	if (fd < 0) return RPG_ERR_OPEN;

	// Safe read
	readed = 0;
	do {
		iteration = read(fd, string + readed, maxLen - readed);
		readed += iteration;
	} while (readed == maxLen || iteration <= 0);
	close(fd);

	if (iteration < 0) {
		close(fd);
		return RPG_ERR_READ;
	}
	return RPG_OK;
}

/// \brief Configures an interrupt and obtains the file descriptor associated
/// with the corresponding pin. This descriptor can be read using poll/select
/// to effectively implement an interrupt.
rpg_retval rpg_int_enable(int pin, const char edge[], int *fd) {
	char fname[RPG_MAX_FILE_LEN];
	char param[10];

	rpg_retval retval = RPG_OK;
	*fd = 0;

	// Export if not already exported
	snprintf(fname, RPG_MAX_FILE_LEN - 1, RPG_GPIO_PATH "gpio%d", pin);
	fname[RPG_MAX_FILE_LEN - 1] = '\0';
	if (access(fname, F_OK) == -1) {
		snprintf(param, 6, "%d\n", pin);
		retval = rpg_file_write(RPG_GPIO_PATH "export", param);
	}

	// Configure pin as input
	if (RPG_OK == retval) {
		snprintf(fname, RPG_MAX_FILE_LEN - 1, RPG_GPIO_PATH "gpio%d/direction", pin);
		fname[RPG_MAX_FILE_LEN - 1] = '\0';
		retval = rpg_file_write(fname, "in\n");
	}

	// Set edge
	if (RPG_OK == retval) {
		snprintf(fname, RPG_MAX_FILE_LEN - 1, RPG_GPIO_PATH "gpio%d/edge", pin);
		fname[RPG_MAX_FILE_LEN - 1] = '\0';
		retval = rpg_file_write(fname, edge);
	}

	// Open file descriptor
	if (RPG_OK == retval) {
		snprintf(fname, RPG_MAX_FILE_LEN - 1, RPG_GPIO_PATH "gpio%d/value", pin);
		fname[RPG_MAX_FILE_LEN - 1] = '\0';
		*fd = open(fname, O_RDONLY);
		if (*fd < 0) retval = RPG_ERR_OPEN;
	}
	return retval;
}

