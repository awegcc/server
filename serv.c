#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BUFF_SIZE 1024
#define MAX_EVENTS 1024
#define SERV_PORT 12345

int sockpipefd[2];

void sig_handle(int signo)
{
	/* save orignal errno */
	int orig_errno = errno;
	send(sockpipefd[1], &signo, sizeof(signo), 0);
	errno = orig_errno;
}

int set_nonblockfd(int fd)
{
	int ret;
	ret = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, ret|O_NONBLOCK);
	return ret;
}

int main(int argc, char* argv[])
{
	int signo;
	int ret, epret, i, not_stop;
	int pipefd[1];
	int fd, epfd, connfd, sockfd;
	char buffer[BUFF_SIZE];
	char *data = "sending data...";
	struct sockaddr_in serv_addr, clnt_addr;
	struct epoll_event evnt, evnts[MAX_EVENTS];
	struct sigaction sact;
	
	not_stop = 1;
	/* daemonize *
	ret = daemon(0, 0);
	assert(ret == 0);
	*/
	sact.sa_handler = sig_handle;
	sigfillset(&sact.sa_mask);
	ret = sigaction(SIGINT, &sact, NULL);
	assert(ret != -1);

	ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sockpipefd);
	assert(ret != -1);
	ret = pipe(pipefd);
	assert(ret != -1);
	
	epfd = epoll_create(8);
	assert(epfd > 0);
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	assert(sockfd > 0);
	
	bzero(&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	/* 
	 * inet_pton(AF_INET, "0.0.0.0", &serv_addr.sin_addr);
	 */
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(SERV_PORT);

	ret = bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
	if(ret == -1) {
		perror("bind");
		return 1;
	}

	ret = listen(sockfd, 5);
	assert(ret != -1);
	printf("pid: %d listen on port: %d\n", getpid(), SERV_PORT);
	set_nonblockfd(sockfd);
	
	evnt.data.fd = sockfd;
	evnt.events = EPOLLIN|EPOLLET;
	ret = epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &evnt);
	assert(ret != -1);

	evnt.data.fd = sockpipefd[0];
	evnt.events = EPOLLIN|EPOLLET;
	ret = epoll_ctl(epfd, EPOLL_CTL_ADD, sockpipefd[0], &evnt);
	assert(ret != -1);

	bzero(&clnt_addr, sizeof(clnt_addr));
	socklen_t clnt_addrlen = sizeof(clnt_addr);
	
	while(not_stop) {
		epret = epoll_wait(epfd, evnts, MAX_EVENTS, -1);
		for(i=0; i<epret; i++) {
			fd = evnts[i].data.fd;
			if(sockfd == fd) {
				connfd = accept(sockfd, (struct sockaddr*)&clnt_addr, &clnt_addrlen);
				if(connfd < 0) {
					perror("accept error\n");
					continue;
				}
				set_nonblockfd(connfd);
				evnt.data.fd = connfd;
				evnt.events = EPOLLET|EPOLLIN|EPOLLOUT|EPOLLRDHUP|EPOLLERR|EPOLLHUP|EPOLLPRI;
				ret = epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &evnt);
				if(ret < 0) {
					perror("epoll_ctl error");
				}
			} else if(sockpipefd[0] == fd) {
				ret = recv(fd, &signo, sizeof(signo), 0);
				if(ret <= 0) {
					perror("socketpair recv");
				}
				switch(signo) {
				case SIGINT:
					printf("SIGINT: %d\n", signo);
					not_stop = 0;
					break;
				case SIGTERM:
					printf("SIGTERM: %d\n", signo);
					break;
				default:
					printf("Unknow\n");
				}
			} else if(evnts[i].events&EPOLLIN) {
				ret = splice(fd, NULL, pipefd[1], NULL, 32767, SPLICE_F_MOVE|SPLICE_F_MORE);
				if(ret == 0) {
					printf("connection closed\n");
					ret = epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
				} else {
					ret = splice(pipefd[0], NULL, fd, NULL, 32767, SPLICE_F_MOVE|SPLICE_F_MORE);
				}
			} else if(evnts[i].events&EPOLLOUT) {
				printf("trigger EPOLLOUT: %s\n", data);
				ret = send(fd, data, strlen(data), 0);
				if(ret < 0) {
					perror("send");
				}
				/* use EPOLL_CTL_MOD after EPOLLOUT to recv EPOLLOUT, but do not do it this way
				evnt.data.fd = fd;
				evnt.events = EPOLLET|EPOLLIN|EPOLLOUT|EPOLLRDHUP|EPOLLERR|EPOLLHUP|EPOLLPRI;
				ret = epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &evnt);
				if(ret < 0) {
					perror("epoll_ctl error");
				} */
			} else if(evnts[i].events&EPOLLRDHUP) {
				printf("EPOLLRDHUP:\n");
			} else if(evnts[i].events&EPOLLPRI) {
				printf("EPOLLPRI:\n");
			} else if(evnts[i].events&EPOLLERR) {
				printf("EPOLLERR\n");
			} else if(evnts[i].events&EPOLLHUP) {
				printf("EPOLLHUP\n");
			} else {
				printf("unknow\n");
			}
		}
	}

	printf("signo: %d, byebye...\n", signo);
	close(sockfd);
	close(epfd);
	return 0;
}
