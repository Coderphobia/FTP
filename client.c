
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define FTPD_IP      "127.0.0.1" /* test only */
#define FTPD_PORT     10010
#define BUF_SIZE      1024

/* a simple structure of file metadata */
struct file_info_struct {
    char name[32];
    long length;
};

int connect_retry(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int ftpc_recv_info(int sockfd, char *buf, int len);
int main(int argc, char argv[])
{
	int client_sock, retval;
	char msgbuf[BUF_SIZE], path[BUF_SIZE] = "./Download/";
	struct sockaddr_in addr;
	struct file_info_struct info;
	
	/* socket() */
	client_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (client_sock < 0) {
		fprintf(stderr, "client socket failed.\n");
		exit(1);
	}

	/* connect() */
	memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(FTPD_PORT);
    addr.sin_addr.s_addr = inet_addr(FTPD_IP);
	retval = connect_retry(client_sock, (struct sockaddr *)&addr, sizeof(addr));
	if (retval < 0) {
		fprintf(stderr, "connect to ftp server failed.\n");
		shutdown(client_sock, SHUT_RDWR);
		exit(2);
	} else {
		printf("connected to ftp server: %s\n", FTPD_IP);
	}
	
	/* wait for file_info_struct */
	retval = ftpc_recv_info(client_sock, msgbuf, sizeof(info));
	if (retval < 0) {
		fprintf(stderr, "Recv File Info Error Or Disconnect.\n");
		shutdown(client_sock, SHUT_RDWR);
		exit(3);
	}
	msgbuf[retval] = '\0';
	if (strstr(msgbuf, "Error")) {
		fprintf(stderr, "Download File Failed.\n");
		shutdown(client_sock, SHUT_RDWR);
		exit(4);
	}
	
	/* set dest file path */
	memcpy(&info, msgbuf, sizeof(info));
	printf("file name: %s, size: %ld\n", info.name, info.length);
	if (access("./Download", F_OK) != 0)
		mkdir("./Download", 0755);
	strcat(path, info.name);
	
	int fd, bytes_recvd = 0, bytes_read;
	char *t, values[BUF_SIZE];

	/* open or create dest file */
	fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, 0644);
	if (fd < 0) {
		fprintf(stderr, "Open File Failed.\n");
		shutdown(client_sock, SHUT_RDWR);
		exit(5);
	}

	/* start downloading */
	while (1) {
		bytes_read = read(client_sock, msgbuf, BUF_SIZE);
		if (bytes_read > 0) {
			write(fd, msgbuf, bytes_read);
			bytes_recvd += bytes_read;
			/* brief info when downloading */
			snprintf(values, BUF_SIZE, "%lf", \
					  100*(bytes_recvd/(double)(info.length))); 
			t=strchr(values,'.'); 
			t[3]='\0'; 
			printf("Downd File: %s  %s%%\n", info.name, values); 
		} else if (bytes_read == 0) {
			fprintf(stderr, "Disconnected From FTP Server.\n");
			break;
		} else {
			fprintf(stderr, "Recv File Text Error.\n");
			break;
		}
	}
	
	close(fd);
	shutdown(client_sock, SHUT_RDWR);
	return 0;
}

/**
 * connect_retry - a timeout version of system call connect()
 */
int connect_retry(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	int i, retval;

	for (i = 0; i < 20; i++) {
		retval = connect(sockfd, addr, addrlen);
		if (retval == 0)    break;
		usleep(200000); /* 0.2s retry */
	}

	return retval;
}

/**
 * ftpc_recv_info - receive file infomation sent by ftp server
 * @sockfd: the socket connect ftp server and client
 * @buf: buffer used to store  reveived infomation
 * @len: length of buffer pointed by @buf
 */
int ftpc_recv_info(int sockfd, char *buf, int len)
{
	int bytes_recvd = 0, bytes_read;

	while (bytes_recvd != len) {
		bytes_read = read(sockfd, buf + bytes_recvd, len - bytes_recvd);
		if (bytes_read == 0 || bytes_read == -1)    return -1;
		bytes_recvd += bytes_read;
	}

	return bytes_recvd;
}

