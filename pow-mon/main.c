#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdbool.h>

#include "rpgpio.h"
#include "sock.h"

/// Default server port
#define DEF_SERV_PORT			8888

/// One minute idle time for socket KeepAlive
#define SOCK_KA_IDLE_TIME		120

/// Five seconds retry interval for socket Keepalive
#define SOCK_KA_RETR_INTERVAL	5

/// Nine retries before giving up socket KeepAlive
#define SOCK_KA_RETRIES			9

/// Used to restrict which pins are writeable (only GPIO3 and GPIO4)
#define WRITE_MASK				0x00000018

/// Minimum time in us to pass for pushbutton presses to be accepted
#define MIN_DELTA_US			2000000

/// Pin used to control power
#define HW_GPIO_PON				3

/// Script to run when power is turned on
#define SCRIPT_POWERUP			"/etc/pow-mon/power-up"
/// Script to run before removing power
#define SCRIPT_POWERDOWN		"/etc/pow-mon/power-down"

/// Helper macro for item to string conversion. Do not use directly
#define STR_HELPER(item)        #item
/// Convert item (e.g. a number) to C string.
#define STR(item)               STR_HELPER(item)

#define TV_EQUAL		 		 0
#define TV_IN1_GREATER			 1
#define TV_IN2_GREATER			-1

enum {
	CMD_READ  = 3,
	CMD_WRITE = 4,
};

typedef struct {
	int32_t cmd;
	int32_t p1;
	int32_t p2;
	int32_t p3;
} command;

static void parse_command(command *cmd)
{
	FILE *f;

	// Only commands implemented so far are READ and WRITE
	switch (cmd->cmd) {
		case CMD_READ:
			cmd->p3 = rpg_read(cmd->p1);
#ifdef __DEBUG
			printf("READ GPIO%d = %d\n", cmd->p1, cmd->p3);
#endif
			break;

		case CMD_WRITE:
			// Check pin is writeable
			if ((1<<cmd->p1) & WRITE_MASK) {
				if (cmd->p2) {
					rpg_set(cmd->p1);
					if (HW_GPIO_PON == cmd->p1) {
						// Run powerup script
						if ((f = popen(SCRIPT_POWERUP, "r")))
								pclose(f);
					}
				} else {
					if (HW_GPIO_PON == cmd->p1) {
						// Run powerdown script
						if ((f = popen(SCRIPT_POWERDOWN, "r")))
								pclose(f);
					}
					rpg_clear(cmd->p1);
				}
#ifdef __DEBUG
				printf("WRITE GPIO%d <- %d\n", cmd->p1, cmd->p2);
#endif
				// If written pin was HW_GPIO_PON, run script
				if (HW_GPIO_PON == cmd->p2) {
					if (cmd->p3) {
					} else {
					}
				}
				cmd->p3 = 0;
			} else {
				fprintf(stderr, "pin %d is not writable\n", cmd->p1);
				cmd->p3 = -2000;
			}
			break;
			
		default:
			fprintf(stderr, "invalid command %d\n", cmd->cmd);
			cmd->p3 = -2000;
			break;
	}
}

static void timeval_add_us(const struct timeval *in, suseconds_t usec,
		struct timeval *out)
{
	usec += in->tv_usec;
	out->tv_usec = usec % 1000000;
	out->tv_sec = in->tv_sec + usec / 1000000;
}

static int timeval_compare(const struct timeval *in1, const struct timeval *in2)
{
	if (in1->tv_sec > in2->tv_sec) return TV_IN1_GREATER;
	else if (in1->tv_sec < in2->tv_sec) return TV_IN2_GREATER;
	// Seconds are equal, compare usecs
	if (in1->tv_usec > in2->tv_usec) return TV_IN1_GREATER;
	else if (in1->tv_usec < in2->tv_usec) return TV_IN2_GREATER;
	return TV_EQUAL;
}

static void Usage(void)
{
	printf("Usage:\n\n");
	printf("pow-mon [server-port]\n\n");
	printf("Starts power monitor on specified server port. If port is "
		   "omitted, default port " STR(DEF_SERV_PORT) " is used.\n");
}

static bool parse_client_command(int s)
{
	command cmd;

	if (sizeof(cmd) != recv(s, &cmd, sizeof(cmd), 0)) {
#ifdef __DEBUG
		puts("connection closed py peer");
#endif
		return true;
	}
	parse_command(&cmd);
#ifdef __DEBUG
	puts("SEND PAYLOAD");
#endif
	if (sizeof(cmd) != send(s, &cmd, sizeof(cmd), 0)) {
		fprintf(stderr, "send failed\n");
		return true;
	}

	return false;
}

static void *client_thread(void *arg)
{
	int s = (intptr_t)arg;
	bool err = false;

	while (!err) {
		err = parse_client_command(s);
	}

	close(s);
	pthread_exit(NULL);
}

bool client_thread_start(int client_socket)
{
	int ret;
	pthread_t tid;
	pthread_attr_t attr;

	// Set Keepalive to avoid socket being opened while inactive for a long time
	SckSetKeepalive(client_socket, SOCK_KA_IDLE_TIME,
			SOCK_KA_RETR_INTERVAL, SOCK_KA_RETRIES);
	// Thread attributes initialization
	if ((ret = pthread_attr_init(&attr)) != 0) {
		fprintf(stderr, "attr_init: %s", strerror(errno));
		return true;
	}
	// Set thread as detached
	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0) {
		fprintf(stderr, "attr_setdetachstate error");
		return true;
	}
	// Create thread
	if ((ret = pthread_create(&tid, &attr, &client_thread,
					(void*)((intptr_t)client_socket))) != 0) {
		fprintf(stderr, "pthread_create error");
		return true;
	}
	// Free thread attributes
	if ((ret = pthread_attr_destroy(&attr)) != 0) {
		fprintf(stderr, "attr_destroy error");
		return true;
	}
	return false;
}

static void button_proc(int fd)
{
	static struct timeval last = {0, 0};
	struct timeval current, limit;
	char val;
	FILE *f;

	lseek(fd, 0, SEEK_SET);
	read(fd, &val, 1);
	// Events accepted only if predefined time has passed
//			printf("val = %c\n", val);
	gettimeofday(&current, NULL);
	timeval_add_us(&last, MIN_DELTA_US, &limit);
	if (timeval_compare(&current, &limit) >= 0) {
		// Required time elapsed, toggle power status
		if (rpg_read(HW_GPIO_PON)) {
			rpg_clear(HW_GPIO_PON);
			if ((f = popen(SCRIPT_POWERDOWN, "r")))
					pclose(f);
			else fprintf(stderr, "popen failed\n");
#ifdef __DEBUG
			puts("POWER OFF");
#endif
		} else {
			rpg_set(HW_GPIO_PON);
#ifdef __DEBUG
			puts("POWER ON");
#endif
			if ((f = popen(SCRIPT_POWERUP, "r")))
					pclose(f);
			else fprintf(stderr, "popen failed\n");
		}

	}
	last = current;
}

static void socket_proc(int ss)
{
	// Client socket
	int s;

	if ((s = SckAccept(ss)) > 0) {
#ifdef __DEBUG
		puts("Connected");
#endif
		client_thread_start(s);
	} else {
		perror("accept failed");
	}
}

static int event_proc(struct pollfd *pfd, int n_desc)
{
	int ret;

	ret = poll(pfd, 2, -1);
	if (ret <= 0) {
		perror("poll failed");
		close(pfd[0].fd);
		return 1;
	} else if (pfd[0].revents & POLLPRI) {
		// Button press
		button_proc(pfd[0].fd);
	} else if (pfd[1].revents & POLLIN) {
		// Incoming connection
		socket_proc(pfd[1].fd);
	} else {
		perror("poll failed");
	}

	return 0;
}

int main(int argc, char **argv)
{
	char dummy;
	rpg_retval retval;
	// File descriptor for GPIO pin, and socket
	struct pollfd pfd[2] = {0};
	long port = DEF_SERV_PORT;
	int val_true = 1;
	int ss = -1;

	// argc can be 1 (no parameters) or 2 (server port)
	if (argc > 2) {
		fprintf(stderr, "Invalid command invocation.\n");
		Usage();
		return 1;
	}

	if (2 == argc) {
		port = atol(argv[1]);
		if (port <= 0 || port > 65535) {
			fprintf(stderr, "Invalid port %s specified, must be in the [1 ~ "
					"65535] range.\n", argv[1]);
			return 1;
		}
	}

	if ((retval = rpg_init()) < 0) {
		rpg_perror(retval, "rpgpio initialization");
		return 1;
	}
#ifdef __DEBUG
	rpg_reg_print();
#endif
	retval = rpg_int_enable(2, "falling\n", &pfd[0].fd);
	if (RPG_OK != retval) {
		rpg_perror(retval, NULL);
		return 1;
	}
	// Configure GPIO outputs
	rpg_configure(3, RPG_FUNC_OUTPUT);
	rpg_configure(4, RPG_FUNC_OUTPUT);

	// Create socket and start server
	if ((ss = SckCreate()) < 0) {
		perror("server socket creation failed");
		return 1;
	}
	setsockopt(ss, SOL_SOCKET, SO_REUSEADDR, &val_true, sizeof(val_true));
	if (SckBind(ss, INADDR_ANY, port) != ss) {
		perror("socket bind failed");
		return 1;
	}
	pfd[0].events = POLLPRI | POLLERR;
	pfd[1].fd = ss;
	pfd[1].events = POLLIN;
	// Read to clear pending interrupts
	read(pfd[0].fd, &dummy, 1);
	printf("RPGPIO ready to accept commands on port %ld!\n", port);
	while (1) {
		event_proc(pfd, 2);
	}

	close(pfd[0].fd);
	return 0;
}
