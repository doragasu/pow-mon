#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "sock.h"

int SckSetKeepalive(int s, int idle, int interval, int maxpkt) {
	int tmp = 1;

    if (setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &tmp, sizeof(int)) < 0)
			return -1;
    if (setsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(int)) < 0)
			return -1;
    if (setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(int)) < 0)
			return -1;
    if (setsockopt(s, IPPROTO_TCP, TCP_KEEPCNT, &maxpkt, sizeof(int)) < 0)
			return -1;

	return s;
}

int SckCreate(void) {
	int s;

	// Create socket, set options
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		return -1;
	}
    
    return s;
}

int SckBind(int s, uint32_t addr, uint16_t port) {
    struct sockaddr_in saddr;

    // Fill in address information
	memset((char*)&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = htonl(addr);
	saddr.sin_port = htons(port);

	// Bind to address
	if (bind(s, (struct sockaddr*)&saddr, sizeof(saddr)) < -1) {
		close(s);
		return -1;
	}

	// Listen for incoming connections
	if (listen(s, 0) < 0) {
		close(s);
		return -1;
	}

    return s;
}

int SckConnect(int s, const char addr[], uint16_t port) {
    struct sockaddr_in daddr;

    memset(&daddr, 0, sizeof(daddr));
    // Alternatively, use getaddrinfo() or gethostbyname()
    daddr.sin_addr.s_addr = inet_addr(addr);
    daddr.sin_family = AF_INET;
    daddr.sin_port = htons(port);
    if(connect(s, (struct sockaddr*) &daddr, sizeof(daddr)) != 0) {
        return -1;
    }
    return s;
}

int SckWaitConnection(int serv_sock) {
    int sock;

    if ((sock = accept(serv_sock, NULL, 0)) < 0) {
        return -1;
    }

    return sock;
}

int SckRecvEventWait(fd_set *fd, int max, uint32_t tout_ms) {
    struct timeval tv;

    if (tout_ms) {
        tv.tv_sec = tout_ms / 1000;
        tv.tv_usec = (tout_ms % 1000) * 1000;
        return select(max + 1, fd, NULL, NULL, &tv);
    } else {
        return select(max + 1, fd, NULL, NULL, NULL);
    }
}

ssize_t SckRecv(int socket, uint8_t *buf, ssize_t length) {
    // Total number of bytes received
    ssize_t recvd_total;
    // Bytes received on the last recv() call
    ssize_t recvd_iter;

    for (recvd_total = 0; recvd_total < length; recvd_total += recvd_iter) {
        recvd_iter = recv(socket, buf + recvd_total, length - recvd_total, 0);
        // If error or if other end closed the connection, return error code
        if (recvd_iter <= 0) return recvd_iter;
    }
    return recvd_total;
}

