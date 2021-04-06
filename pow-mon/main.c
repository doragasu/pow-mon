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
#include <gpiod.h>

#include "sock.h"

/// Computes a bit mask
#define BIT(n) (1<<(n))

/// Default server port
#define DEF_SERV_PORT 8888

/// One minute idle time for socket KeepAlive
#define SOCK_KA_IDLE_TIME 120

/// Five seconds retry interval for socket Keepalive
#define SOCK_KA_RETR_INTERVAL 5

/// Nine retries before giving up socket KeepAlive
#define SOCK_KA_RETRIES 9

/// Pin used to control power supply ON
#define HW_GPIO_PON 3

/// Pin used to control LED
#define HW_GPIO_LED 4

/// Pin used to read pushbutton
#define HW_GPIO_BUTTON 2

/// Minimum time in us to pass for pushbutton presses to be accepted
#define MIN_DELTA_US			2000000

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

static struct {
	struct gpiod_chip *chip;
	struct gpiod_line *power_on;
	struct gpiod_line *power_button;
	bool power_stat;
} io = {};

static void set_power(bool value)
{
	FILE *f;

	gpiod_line_set_value(io.power_on, value);
	if (value) {
		// Run powerup script
		if ((f = popen(SCRIPT_POWERUP, "r"))) {
			pclose(f);
		}
	} else {
		// Run powerdown script
		if ((f = popen(SCRIPT_POWERDOWN, "r"))) {
			pclose(f);
		}
	}
#ifdef __DEBUG
	const char * const valstr[] = {"OFF", "ON"};
	printf("POWER %s\n", valstr[value]);
#endif
	io.power_stat = value;
}

static void parse_command(command *cmd)
{
	// Only commands implemented so far are READ and WRITE
	switch (cmd->cmd) {
		case CMD_READ:
			if (HW_GPIO_BUTTON == cmd->p1) {
				cmd->p3 = gpiod_line_get_value(io.power_button);
#ifdef __DEBUG
				printf("READ GPIO%d = %d\n", cmd->p1, cmd->p3);
#endif
			} else if (HW_GPIO_PON == cmd->p1) {
				cmd->p3 = io.power_stat;
			} else {
				fprintf(stderr, "pin %d is not readable\n", cmd->p1);
				cmd->p3 = -2000;
			}
			break;

		case CMD_WRITE:
			// Check pin is writeable
			if (HW_GPIO_PON == cmd->p1) {
				// Power ON/OFF depending on cmd->p2
				set_power(cmd->p2);
				cmd->p3 = 0;
			} else if (HW_GPIO_LED == cmd->p2) {
				// TODO
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
		fprintf(stderr, "attr_init: %s\n", strerror(errno));
		return true;
	}
	// Set thread as detached
	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0) {
		fprintf(stderr, "attr_setdetachstate error\n");
		return true;
	}
	// Create thread
	if ((ret = pthread_create(&tid, &attr, &client_thread,
					(void*)((intptr_t)client_socket))) != 0) {
		fprintf(stderr, "pthread_create error\n");
		return true;
	}
	// Free thread attributes
	if ((ret = pthread_attr_destroy(&attr)) != 0) {
		fprintf(stderr, "attr_destroy error\n");
		return true;
	}
	return false;
}

static void button_proc(const struct gpiod_line_event *ev)
{
	(void)ev;
	static struct timeval last = {0, 0};
	struct timeval current, limit;

	// Events accepted only if predefined time has passed
	gettimeofday(&current, NULL);
	timeval_add_us(&last, MIN_DELTA_US, &limit);
	if (timeval_compare(&current, &limit) >= 0) {
		// Required time elapsed, toggle power status
		set_power(!io.power_stat);
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
	(void)n_desc;
	int ret;
	struct gpiod_line_event event;

	ret = poll(pfd, 2, -1);
	if (ret <= 0) {
		perror("poll failed");
	} else if (pfd[0].revents & POLLIN) {
		ret = gpiod_line_event_read_fd(pfd[0].fd, &event);
		if (ret < 0) {
			perror("event read failed");
		} else {
			// Button press
			button_proc(&event);
		}
	} else if (pfd[1].revents & POLLIN) {
		// Incoming connection
		socket_proc(pfd[1].fd);
		ret = 0;
	} else {
		perror("poll failed");
		ret = -1;
	}

	return ret;
}

static struct gpiod_line* output_config(int pin, int initial_value)
{
	struct gpiod_line *line;
	line = gpiod_chip_get_line(io.chip, pin);
	if (!line) {
		perror("Getting GPIO pin");
		goto err;
	}
	if (gpiod_line_request_output(line, "pow-mon_out", 0) < 0) {
		perror("Configuring output pin");
		goto release;
	}
	if (gpiod_line_set_value(line, initial_value) < 0) {
		perror("Setting output initial value");
		goto release;
	}

	return line;

release:
	gpiod_line_release(line);
err:
	return NULL;
}

static struct gpiod_line* event_config(int pin, int *fd)
{
	char dummy;
	struct gpiod_line *line = gpiod_chip_get_line(io.chip, pin);
	if (!line) {
		perror("Getting GPIO pin");
		goto err;
	}
	if (gpiod_line_request_falling_edge_events(line, "pow-mon_event") < 0) {
		perror("Configuring notification");
		goto release;
	}

	*fd = gpiod_line_event_get_fd(line);
	if (*fd < 0) {
		perror("Getting fd from input line");
		goto release;
	}
	// Read to clear spurious interrupt
	read(*fd, &dummy, 1);

	return line;

release:
	gpiod_line_release(line);
err:
	return NULL;
}

int main(int argc, char **argv)
{
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

	io.chip = gpiod_chip_open_by_name("gpiochip0");
	if (!io.chip) {
		perror("Open chip failed");
		return 1;
	}

	// TODO properly free resources
	io.power_on = output_config(HW_GPIO_PON, 0);
	if (!io.power_on) {
		goto release;
	}
	io.power_button = event_config(HW_GPIO_BUTTON, &pfd[0].fd);
	if (!io.power_button) {
		goto release;
	}

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
	pfd[0].events = POLLIN | POLLERR;
	pfd[1].fd = ss;
	pfd[1].events = POLLIN;
	printf("RPGPIO ready to accept commands on port %ld!\n", port);
	while (1) {
		event_proc(pfd, 2);
	}

	close(pfd[0].fd);
release:
	if (io.power_on) gpiod_line_release(io.power_on);
	if (io.power_button) gpiod_line_release(io.power_button);
	gpiod_chip_close(io.chip);
	return 1;
}
