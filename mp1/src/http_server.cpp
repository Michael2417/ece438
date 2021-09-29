#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#include <vector>
#include <iostream>
#include <string>
#include <fstream>

// #define PORT "3490"  // the port users will be connecting to

#define BACKLOG 10	 // how many pending connections queue will hold

#define BUF_SIZE 500 

using namespace std;
string decode_dir(string& request){
	string dir = "";
	int len = request.length();
	for(int i = 0; i < len;i++){
		if(request[i] == '/'){
			for(int j = i; j < len;j++){
				if(request[j] == ' '){
					dir = request.substr(i+1,j-i);
					break;
				}
			}
			break;
		}
	}
	return dir;
}

void send_file(string dir, int sockfd){
	char buf[BUF_SIZE];
	FILE* fp = fopen(dir.c_str(),"rb");
	if(fp == NULL ){
		string header = "HTTP/1.1 404 Not Found\r\n\r\n";
		if(send(sockfd,header.c_str(),header.length(),0) == -1){
			perror("send 404 error");
		}
		return;
	}
	else{
		string header = "HTTP/1.1 200 OK\r\n\r\n";
		if(send(sockfd,header.c_str(),header.length(),0) == -1){
			perror("send 200 error");
		}
		while(!feof(fp)){
			size_t ret = fread(buf,sizeof(char),BUF_SIZE,fp);
			if(ret <= 0){
				printf("finish reading.\n");
				break;
			}
			else if(ret <= BUF_SIZE)
			{
				printf("reading.\n");
				if(send(sockfd,buf,(int) ret,0) == - 1){
					perror("send");
				}
			}	
		}
	}
	fclose(fp);
	return;
}
void sigchld_handler(int s)
{
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[])
{
	int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;
	
	if(argc != 2){
		printf("Usage : %s <port>\n",argv[0]);
		exit(1);
	}

	string PORT = string(argv[1]);

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, PORT.c_str(), &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		return 2;
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connections...\n");

	while(1) {  // main accept() loop
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family,
			get_in_addr((struct sockaddr *)&their_addr),
			s, sizeof s);
		printf("server: got connection from %s\n", s);

		if (!fork()) { // this is the child process
			close(sockfd); // child doesn't need the listener
			int numbytes;
			char recvbuf[BUF_SIZE];
			string request = "";
			string dir = "";
			string method = "";
			while(1){
				numbytes = recv(new_fd,recvbuf,BUF_SIZE,0);
				if(numbytes <= 0) break;
				request = string(recvbuf);
				method = request.substr(0,3);
				if(method != "GET"){
					string header = "HTTP/1.1 400 Bad Request\r\n\r\n";
					if(send(sockfd,header.c_str(),header.length(),0) == -1){
						perror("send 400 error");
					}
					break;
				}
				dir = decode_dir(request);
				send_file(dir,new_fd);
				break;
			}
			close(new_fd);
			exit(0);
		}
		close(new_fd);  // parent doesn't need this
	}

	return 0;
}
