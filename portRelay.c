#define _GNU_SOURCE /* needed for splice */
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

#define BUFF_SIZE 32
#define MAX_EVENTS 128
#define TIME_GAP 5


struct relay_struct {
	int epoll_fd;
	int local_fd;
	struct sockaddr_in dst_addr;
	struct sockaddr_in local_addr;
};

struct fd_pair_struct {
	int server_fd;
	size_t up_len;
	int client_fd;
	size_t down_len;
};

int sockpipefd[2];
struct fd_pair_struct fd_pair;

void usage(char *cmd)
{
	/*
	 * portRelay -t <tcp|udp> <localip> <localport> <dstip> <dstport>
	 */
	printf("%s -t<tcp|udp> <localip> <localport> <dstip> <dstport>\n", cmd);
	exit(1);
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

	if (bind(relay->local_fd, (struct sockaddr*)&relay->local_addr, socklen) < 0 ) {
		perror("bind error");
		return 3;
	}
	
	return 0;
}

int do_accept(struct relay_struct *relay)
{
	int ret = 0;
	struct epoll_event evnt;
	struct sockaddr_in client_addr;
	socklen_t socklen = sizeof(struct sockaddr_in);
	fd_pair.client_fd = accept(relay->local_fd, (struct sockaddr*)&client_addr, &socklen);
	if(fd_pair.client_fd < 0) {
		perror("accept");
		return 1;
	}
	set_nonblockfd(fd_pair.client_fd);
	evnt.data.fd = fd_pair.client_fd;
	evnt.events = EPOLLET | EPOLLIN | EPOLLERR;
	
	ret = epoll_ctl(relay->epoll_fd, EPOLL_CTL_ADD, fd_pair.client_fd, &evnt);
	if( ret < 0) {
		perror("epoll_ctl");
		return 2;
	}

	fd_pair.server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(fd_pair.server_fd < 0) {
		perror("cocket");
		// release resource here
		return 3;
	}
	
	ret = connect(fd_pair.server_fd, (struct sockaddr*)&relay->dst_addr, socklen);
	if(ret < 0) {
		perror("connect...");
		print_addr(&relay->dst_addr);
		// release resource here
		return 4;
	}
	set_nonblockfd(fd_pair.server_fd);
	evnt.data.fd = fd_pair.server_fd;
	evnt.events = EPOLLET | EPOLLIN | EPOLLERR;
	
	ret = epoll_ctl(relay->epoll_fd, EPOLL_CTL_ADD, fd_pair.server_fd, &evnt);
	if( ret < 0) {
		perror("epoll_ctl");
		// release resource here
		return 5;
	}
	return 0;
}

int do_transmit(int fd, struct relay_struct *relay)
{
	int ret = 0;
	char buff[1024];
	size_t data_len = 0;

	data_len = recv(fd, buff, 1024, 0);
	if(data_len < 0) {
		perror("recv");
	} else if (data_len == 0) {
		printf("recv data lenth is 0\n");
	} else {
		buff[data_len] = 0;
		if(fd == fd_pair.client_fd) {
			printf("transfer data to real server\n");
			ret = send(fd_pair.server_fd, buff, data_len, 0);
			fd_pair.up_len += data_len;
		} else if ( fd == fd_pair.server_fd) {
			printf("transfer data to real client\n");
			ret = send(fd_pair.client_fd, buff, data_len, 0);
			fd_pair.down_len += data_len;
		} else {
			printf("unknow fd\n");
		}
	}
	return 0;
}

int sigalarm_process()
{
	size_t up = fd_pair.up_len;
	fd_pair.up_len = 0;
	size_t down = fd_pair.down_len;
	fd_pair.down_len = 0;

	printf("up   : %8u bytes/%ds\n", up, TIME_GAP);
	printf("down : %8u bytes/%ds\n", down, TIME_GAP);
	printf("total: %8u bytes/%ds\n", up + down, TIME_GAP);
	printf("--------------------------------------\n");

	return 0;
}

int do_while(struct relay_struct *relay)
{
	int ret = 0;
	int i, fd, ep_ret;
	int run_flag = 1;
	struct epoll_event evnt, evnts[MAX_EVENTS];

	struct sigaction sact;

	relay->epoll_fd = epoll_create(8);
	if(relay->epoll_fd < 0) {
		perror("epoll_crete");
		return 1;
	}

	evnt.data.fd = sockpipefd[0];
	evnt.events = EPOLLIN|EPOLLET;
	ret = epoll_ctl(relay->epoll_fd, EPOLL_CTL_ADD, sockpipefd[0], &evnt);
	if(ret == -1) {
		perror("epoll_ctl");
		return 2;
	}

	evnt.data.fd = relay->local_fd;
	evnt.events = EPOLLIN|EPOLLET;
	ret = epoll_ctl(relay->epoll_fd, EPOLL_CTL_ADD, relay->local_fd, &evnt);
	if(ret == -1) {
		perror("epoll_ctl");
		return 3;
	}

	while(run_flag) {
		ep_ret = epoll_wait(relay->epoll_fd, evnts, MAX_EVENTS, -1);
		for(i=0; i<ep_ret; i++) {
			fd = evnts[i].data.fd;
			if(relay->local_fd == fd) {
				printf("Get a Connection event\n");
				do_accept(relay);
			} else if(sockpipefd[0] == fd && evnts[i].events & EPOLLIN) {
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
			} else if(evnts[i].events & EPOLLIN) {
				ret = do_transmit(fd, relay);
			} else {
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
	struct sigaction sact;
	struct relay_struct relay;

	if(argc != 7) {
		usage(argv[0]);
	} else {
		if(strncmp(argv[2], "tcp", 3) == 0) {
			flag = 1;
		} else if(strncmp(argv[2], "udp", 3) == 0) {
			flag = 2;
		} else {
			usage(argv[0]);
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


	if(flag == 2) {
		retval = udp_prepare(&relay);
	} else if(flag == 1) {
		retval = tcp_prepare(&relay);
	} else {
		printf("unknow error\n");
		return 1;
	}
	
	retval = socketpair(AF_UNIX, SOCK_STREAM, 0, sockpipefd);
	if( retval < 0) {
		perror("sockpair");
		return 2;
	}

	sact.sa_handler = sig_handle;
	sact.sa_flags = SA_NOCLDSTOP|~SA_RESETHAND;
	sigfillset(&sact.sa_mask);
	retval = sigaction(SIGINT, &sact, NULL);
	if(retval == -1) {
		perror("sigaction\n");
		return 3;
	}
	retval = sigaction(SIGTERM, &sact, NULL);
	if(retval == -1) {
		perror("sigaction\n");
		return 4;
	}
	retval = sigaction(SIGALRM, &sact, NULL);
	if(retval == -1) {
		perror("sigaction\n");
		return 4;
	}
	alarm(TIME_GAP);

	retval = do_while(&relay);
	return 0;
}

