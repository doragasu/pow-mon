#ifndef _SCK_H_
#define _SCK_H_

#include <stdint.h>
#include <sys/socket.h>

int SckCreate(void);

int SckBind(int s, uint32_t addr, uint16_t port);

#define SckAdd(s, fd_set, max)  do {                      \
    FD_SET(s, &(fd));                                       \
    max = MAX(s, max)                                       \
} while(0)

int SckConnect(int s, const char addr[], uint16_t port);

int SckWaitConnection(int serv_sock);

#define SckAccept(serv_sock)    SckWaitConnection(serv_sock)

int SckRecvEventWait(fd_set *fd, int max, uint32_t tout_ms);

int SckSetKeepalive(int s, int idle, int interval, int maxpkt);

ssize_t SckRecv(int socket, uint8_t *buf, ssize_t length);

#endif /*_SCK_H_*/

/** \} */

