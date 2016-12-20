/*
 * A simple port realy program just for fun.
 * Tue Dec 21 08:45:58 UTC 2016
 */
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>

#define BUFF_SIZE 1024
#define MAX_EVENTS 128
#define TIME_GAP 10


struct relay_struct {
	int type;     /* tcp:1, udp:2, tcp+udp:3 */
	int epoll_fd;
	int local_fd;
	struct sockaddr_in dst_addr;
	struct sockaddr_in local_addr;
};

struct sockpair_struct {
	int server_fd;
	struct sockaddr_in *server_addr;
	size_t up_len;
	int client_fd;
	struct sockaddr_in *client_addr;
	size_t down_len;
};

int sockpipefd[2];
struct sockpair_struct sockpair;

void usage(char *cmd)
{
	/*
	 * portRelay -t <tcp|udp> <localip> <localport> <dstip> <dstport>
	 */
	printf("%s -t <tcp|udp> <localip> <localport> <dstip> <dstport>\n", cmd);
}

void sig_handle(int signo)
{
	int orig_errno = errno;
	send(sockpipefd[1], (char*)&signo, sizeof(char), 0);
	errno = orig_errno;
}

int set_nonblockfd(int fd)
{
	int ret;
	ret = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, ret|O_NONBLOCK);
	return ret;
}

int add_fd(int epoll_fd, int fd)
{
	int ret;
	struct epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET | EPOLLERR;

	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
	if(ret < 0) {
		perror("epoll_ctl");
		return 1;
	}
	set_nonblockfd(fd);

	return 0;
}

void print_addr(struct sockaddr_in *ptr_addr)
{
	if(ptr_addr) {
		printf("%s:%d\n", inet_ntoa(ptr_addr->sin_addr), ntohs(ptr_addr->sin_port));
	}
}

int tcp_prepare(struct relay_struct *relay)
{
	/* set socket option*/
	int ret;
	int opt = 1;
	socklen_t socklen = sizeof(struct sockaddr_in);

	relay->local_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(relay->local_fd < 0) {
		perror("socket");
		return 1;
	}

	ret = setsockopt(relay->local_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	if (ret < 0) {
		perror("setsockopt");
		return 2;
	}


	ret = bind(relay->local_fd, (struct sockaddr*)&relay->local_addr, socklen);
	if (ret < 0) {
		perror("bind() error");
		return 3;
	}

	ret = listen(relay->local_fd, 10);
	if(ret < 0) {
		perror("listen");
		return 4;
	}
	sockpair.server_addr = NULL;
	sockpair.client_addr = NULL;
	sockpair.up_len = 0;
	sockpair.down_len = 0;

	return 0;
}

int udp_prepare(struct relay_struct *relay)
{
	int ret;
	int opt = 1;
	socklen_t socklen = sizeof(struct sockaddr_in);
	relay->local_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (relay->local_fd < 0){
		perror("socket error");
		return 1;
	}

	if((ret = setsockopt(relay->local_fd,SOL_SOCKET, SO_REUSEADDR,&opt,sizeof(opt))) < 0){
		perror("setsockopt error");
		return 2;
	}

	ret = bind(relay->local_fd, (struct sockaddr*)&relay->local_addr, socklen);
	if (ret < 0 ) {
		perror("bind error");
		return 3;
	}

	sockpair.server_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sockpair.server_fd < 0) {
		perror("socket");
		// release resource here
		return 4;
	}

	ret = add_fd(relay->epoll_fd, sockpair.server_fd);
	if( ret ) {
		printf("add_fd sockpair.server_fd\n");
		return 5;
	}
	sockpair.server_addr = malloc(socklen);
	if(sockpair.server_addr == NULL) {
		perror("malloc");
		return 6;
	}
	memcpy(sockpair.server_addr, &relay->dst_addr, socklen);

	sockpair.client_fd = relay->local_fd;
	sockpair.client_addr = malloc(socklen);
	if(sockpair.client_addr == NULL) {
		perror("malloc");
		return 7;
	}

	sockpair.up_len = 0;
	sockpair.down_len = 0;

	return 0;
}

int prepare(struct relay_struct *relay)
{
	int retval = 0;
	struct sigaction sact;
	struct epoll_event evnt;

	retval = socketpair(AF_UNIX, SOCK_STREAM, 0, sockpipefd);
	if( retval < 0) {
		perror("socketpair");
		return 1;
	}

	sact.sa_handler = sig_handle;
	sact.sa_flags = SA_NOCLDSTOP|~SA_RESETHAND;
	sigfillset(&sact.sa_mask);
	retval = sigaction(SIGINT, &sact, NULL);
	if(retval == -1) {
		perror("sigaction\n");
		return 2;
	}
	retval = sigaction(SIGTERM, &sact, NULL);
	if(retval == -1) {
		perror("sigaction\n");
		return 3;
	}
	retval = sigaction(SIGALRM, &sact, NULL);
	if(retval == -1) {
		perror("sigaction\n");
		return 4;
	}
	alarm(TIME_GAP);


	retval = add_fd(relay->epoll_fd, sockpipefd[0]);
	if(retval) {
		printf("add_fd sockpipefd[0]\n");
		return 6;
	}

	retval = add_fd(relay->epoll_fd, relay->local_fd);
	if(retval) {
		printf("add_fd relay->local_fd\n");
		return 7;
	}

	return 0;
}

int do_accept(struct relay_struct *relay)
{
	int ret = 0;
	struct epoll_event evnt;
	struct sockaddr_in client_addr;
	socklen_t socklen = sizeof(struct sockaddr_in);

	sockpair.client_fd = accept(relay->local_fd, (struct sockaddr*)&client_addr, &socklen);
	if(sockpair.client_fd < 0) {
		perror("accept");
		return 1;
	}

	ret = add_fd(relay->epoll_fd, sockpair.client_fd);
	if( ret < 0) {
		printf("add_fd sockpair.client_fd\n");
		return 2;
	}

	sockpair.server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockpair.server_fd < 0) {
		perror("socket");
		// release resource here
		return 3;
	}

	ret = connect(sockpair.server_fd, (struct sockaddr*)&relay->dst_addr, socklen);
	if(ret < 0) {
		perror("connect...");
		print_addr(&relay->dst_addr);
		// release resource here
		return 4;
	}

	ret = add_fd(relay->epoll_fd, sockpair.server_fd);
	if( ret < 0) {
		printf("add_fd sockpair.server_fd\n");
		return 5;
	}
	return 0;
}

int do_transmit(int fd, struct relay_struct *relay)
{
	int ret = 0;
	char buff[BUFF_SIZE];
	struct sockaddr *from_addr, *to_addr;
	int to_fd = 0;
	socklen_t socklen = sizeof(struct sockaddr_in);

	if(fd == sockpair.client_fd) {
		from_addr = (struct sockaddr*)sockpair.client_addr;
		to_addr = (struct sockaddr*)sockpair.server_addr;
		to_fd = sockpair.server_fd;
	}
	else if ( fd == sockpair.server_fd) {
		from_addr = (struct sockaddr*)sockpair.server_addr;
		to_addr = (struct sockaddr*)sockpair.client_addr;
		to_fd = sockpair.client_fd;
	}
	else {
		printf("unknow fd\n");
		return 1;
	}

	ret = recvfrom(fd, buff, BUFF_SIZE, 0, from_addr, &socklen);
	if(ret < 0) {
		perror("recvfrom server error");
		return 2;
	}
	else if (ret == 0) {
		printf("recvfrom data lenth is 0, socket is closed\n");
		return 3;
		/* release resource here */
	}
	ret = sendto(to_fd, buff, ret, 0, to_addr, socklen);
	if(ret <= 0) {
		printf("transfer data failed[%d]\n", ret);
		return 4;
	}
	else {
		printf("transfer data successed[%d]\n", ret);
	}

	/* count up/down data */
	if(fd == sockpair.client_fd) {
		sockpair.up_len = ret;
	}
	else {
		sockpair.down_len = ret;
	}

	return 0;
}

int sigalarm_process()
{
	size_t up = sockpair.up_len;
	sockpair.up_len = 0;
	size_t down = sockpair.down_len;
	sockpair.down_len = 0;

	if( 0 != up || 0 != down ) {
		printf("up   : %8u bytes/%ds\n", up, TIME_GAP);
		printf("down : %8u bytes/%ds\n", down, TIME_GAP);
		printf("total: %8u bytes/%ds\n", up + down, TIME_GAP);
		printf("-----------------------------------\n");
	}

	return 0;
}

int do_while(struct relay_struct *relay)
{
	int ret = 0;
	int i, fd, ep_ret;
	int run_flag = 1;
	struct epoll_event evnts[MAX_EVENTS];


	while(run_flag) {
		ep_ret = epoll_wait(relay->epoll_fd, evnts, MAX_EVENTS, -1);
		for(i=0; i<ep_ret; i++) {
			fd = evnts[i].data.fd;
			if(relay->local_fd == fd && relay->type == 1) {
				printf("Get a tcp Connection event\n");
				do_accept(relay);
			}
			else if(sockpipefd[0] == fd && evnts[i].events & EPOLLIN) {
				int j = 0;
				char signals[256];
				ret = recv(fd, signals, sizeof(signals), 0);
				if(ret <= 0) {
					perror("socketpair recv");
					continue;
				}
				for(j=0; j<ret; j++) {
					switch(signals[j]) {
						case SIGTERM:
						case SIGINT:
							run_flag = 0;
							printf("Get a quit event\n");
							break;
						case SIGALRM:
							sigalarm_process();
							alarm(TIME_GAP);
							break;
						default:
							;
					}
				}
			}
			else if(evnts[i].events & EPOLLIN) {
				ret = do_transmit(fd, relay);
			}
			else if(evnts[i].events & EPOLLERR) {
				printf("reveive EPOLLERR\n");
			}
			else {
				printf("unknow\n");
			}
		}
	}

	return 0;
}

int main(int argc,char *argv[])
{
	int flag = 0;
	int port = 0;
	int retval = 0;
	struct relay_struct relay;

	if(argc != 7) {
		usage(argv[0]);
		return 1;
	} else {
		if(strncmp(argv[2], "tcp", 3) == 0) {
			flag = 1;
		} else if(strncmp(argv[2], "udp", 3) == 0) {
			flag = 2;
		} else if(strncmp(argv[2], "all", 3) == 0) {
			flag = 3;
		} else {
			usage(argv[0]);
			return 2;
		}
	}
	memset(&relay, 0, sizeof(relay));
	relay.local_addr.sin_family = AF_INET;
	port = atoi(argv[4]);
	relay.local_addr.sin_port = htons(port);
	inet_pton(AF_INET,argv[3],&relay.local_addr.sin_addr.s_addr);

	relay.dst_addr.sin_family = AF_INET;
	port = atoi(argv[6]);
	relay.dst_addr.sin_port = htons(port);
	inet_pton(AF_INET, argv[5], &relay.dst_addr.sin_addr);

	relay.epoll_fd = epoll_create(8);
	if(relay.epoll_fd < 0) {
		perror("epoll_crete");
		return 3;
	}

	if(flag == 1) {
		relay.type = 1;
		retval = tcp_prepare(&relay);
	} else if(flag == 2) {
		relay.type = 2;
		retval = udp_prepare(&relay);
	} else if(flag == 3) {
		relay.type = 3;
		// prepare both tcp and udp
		// not support now
	} else {
		printf("unknow error\n");
		return 1;
	}

	retval = prepare(&relay);
	if(retval != 0) {
		printf("prepare failed[%d]\n", retval);
		return 2;
	}

	do_while(&relay);

	close(relay.epoll_fd);
	close(relay.local_fd);
	/* release more resource here */

	return 0;
}

