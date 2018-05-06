#include <stdint.h>

#define RPG_BASE	0x00200000
#define RPG_REG_TABLE(REG_EXPANSION_MACRO)			\
	REG_EXPANSION_MACRO(GPFSEL0, 0x00)			\
	REG_EXPANSION_MACRO(GPFSEL1, 0x01)			\
	REG_EXPANSION_MACRO(GPFSEL2, 0x02)			\
	REG_EXPANSION_MACRO(GPFSEL3, 0x03)			\
	REG_EXPANSION_MACRO(GPFSEL4, 0x04)			\
	REG_EXPANSION_MACRO(GPFSEL5, 0x05)			\
	REG_EXPANSION_MACRO(GPSET0,  0x07)			\
	REG_EXPANSION_MACRO(GPSET1,  0x08)			\
	REG_EXPANSION_MACRO(GPCLR0,  0x0A)			\
	REG_EXPANSION_MACRO(GPCLR1,  0x0B)			\
	REG_EXPANSION_MACRO(GPLEV0,  0x0D)			\
	REG_EXPANSION_MACRO(GPLEV1,  0x0E)			\

#define RPG_EXPAND_AS_ENUM(reg_name, offset)			\
	reg_name = offset,

/// Maximum GPIO pin number
#define RPG_PIN_MAX			53

enum {
	RPG_REG_TABLE(RPG_EXPAND_AS_ENUM)
};

typedef enum {
	RPG_FUNC_INPUT		= 0,
	RPG_FUNC_OUTPUT		= 1,
	RPG_FUNC_ALTERNATE0	= 4,
	RPG_FUNC_ALTERNATE1	= 5,
	RPG_FUNC_ALTERNATE2	= 6,
	RPG_FUNC_ALTERNATE3	= 7,
	RPG_FUNC_ALTERNATE4	= 3,
	RPG_FUNC_ALTERNATE5	= 2,
} rpg_func;

typedef enum {
	RPG_OK			=  0,
	RPG_ERR_UNDEFINED	= -1,
	RPG_ERR_PERMISSIONS	= -2,
	RPG_ERR_MAP		= -3,
	RPG_ERR_PIN		= -4,
	RPG_ERR_OPEN		= -5,
	RPG_ERR_WRITE		= -6,
	RPG_ERR_READ		= -7,
} rpg_retval;

rpg_retval rpg_init(void);

rpg_retval rpg_configure(int pin, rpg_func func);

int rpg_read(int pin);

void rpg_set(int pin);

void rpg_clear(int pin);

void rpg_reg_print(void);

void rpg_perror(rpg_retval retval, const char msg[]);

/// \brief Configures an interrupt and obtains the file descriptor associated
/// with the corresponding pin. This descriptor can be read using poll/select
/// to effectively implement an interrupt.
rpg_retval rpg_int_enable(int pin, const char edge[], int *fd);

