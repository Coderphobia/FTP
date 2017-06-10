
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define FTPD_IP    "127.0.0.1" /* test only */
#define FTPD_PORT   10010
#define WAIT_QUEUE  10 
#define BUF_SIZE    1024

/* a simple structure of file metadata */
struct file_info_struct {
    char name[32];
    long length;
};

int ftpd_startup(void);
void *ftpd_load_file(const char *path, struct file_info_struct *info);
int ftpd_send_file(int sockfd, struct sockaddr_in client, 
                    void *file, struct file_info_struct *info);
void wait_child(int signo);
int ftpd_send_info(int sockfd, void *info, int len);

int main(int argc, char *argv[])
{
    pid_t pid;
    void *file;
    struct file_info_struct info;
    int server_sock, client_sock;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char *err_msg = "Error: Service Not Available!";

    /* start up ftp */
    server_sock = ftpd_startup();
    if (server_sock < 0) {
        fprintf(stderr, "ftpd starts failed.\n");
        exit(1);
    } else {
        fprintf(stdout, "ftpd starts ok.\n");
    }

    /* load files to memory */
    file = ftpd_load_file("./testfile",&info);
    if (!file) {
        fprintf(stderr, "ftp load files failed.\n");
        shutdown(server_sock, SHUT_RDWR);
        exit(2);
    }

    while (1) {
        /* accept coming clients */
        client_sock = accept(server_sock, 
                            (struct sockaddr *)&client_addr,
                            &client_addr_len);
        if (client_sock < 0) {
            fprintf(stderr, "Accept Client Failed.\n");
            shutdown(server_sock, SHUT_RDWR);
            exit(3);
        }

        /* child process to serve accepted clients */
        pid = fork();
        if (pid < 0) {
            ftpd_send_info(client_sock, err_msg, 
                          sizeof(struct file_info_struct));
            shutdown(server_sock, SHUT_RDWR);
            exit(4);
        } else if(pid == 0) {
            ftpd_send_file(client_sock, client_addr, file, &info);
            exit(0); /* send file success */
        }
    }

    shutdown(server_sock, SHUT_RDWR);
    munmap(file, info.length);
    return 0;
}

/**
 * ftpd_startup - a typical server startup routine
 * socket() -> bind() -> listen()
 */
int ftpd_startup(void)
{
    int ftpd, ret;
    struct sockaddr_in addr;

    ftpd = socket(AF_INET, SOCK_STREAM, 0);
    if (ftpd < 0)    return -1;;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(FTPD_PORT);
    addr.sin_addr.s_addr = inet_addr(FTPD_IP);
    if (bind(ftpd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ret = -2;
        goto fail;
    }
    
    if (listen(ftpd, WAIT_QUEUE) < 0) {
        ret = -3;
        goto fail;
    }

    return ftpd;    
fail:
    shutdown(ftpd, SHUT_RDWR);
    return ret;
}

/**
 * ftpd_load_files - load files and map them into memmory 
 * @path: the path of the file to load
 * @info: store the file infomation
 */

void *ftpd_load_file(const char *path, struct file_info_struct *info)
{
    int fd;
    struct stat st; 
    void *vfp;
    char *tmp, *name; 

    fd = open(path, O_RDWR);
    if (fd < 0)
        goto err0;

    /* get file metadata */
    if (fstat(fd, &st) < 0)
        goto err1;
    
    /* i/o efficiency for multi-client downloading */
    vfp = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (vfp == MAP_FAILED)
        goto err1;

    /* parse path and get file name */
    tmp = strrchr(path, '/');
    if (tmp)
        name = tmp + 1;
    else
        name = (char *)path;
    strcpy(info->name, name);
    info->length = st.st_size;
    
    return vfp;
err1:
    close(fd);
err0:
    return NULL;
}

/**
 * ftpd_send_info - send infomation to client 
 * @sockfd: socket of accepted client
 * @info: including error message, file_info_struct and file content
 * @len: the length of @info
 */
int ftpd_send_info(int sockfd, void *info, int len)
{
    int bytes_sent = 0, bytes_written;

    while (bytes_sent != len) {
        bytes_written = write(sockfd, info, len - bytes_sent);
        if (bytes_written < 0)    return -1;
        bytes_sent += bytes_written;
    }

    return len; /* send success */
}

/**
 * ftpd_send_file - send file_info_struct and file content
 * @sockfd: socket of accepted client
 * @client: who is downloading file
 * @file: the file to be sent to @client
 * @info: file_info_struct of @file
 */
int ftpd_send_file(int sockfd, struct sockaddr_in client, 
                    void *file, struct file_info_struct *info)
{
    int bytes_sent = 0, bytes_not_sent, bytes_to_be_sent, retval;
        
    /* send file_info_struct */
    retval = ftpd_send_info(sockfd, info, sizeof(*info));
    if (retval < 0) {
        shutdown(sockfd, SHUT_RDWR);
        return -1;
    }

    /* send file content */
    while (bytes_sent != info->length) {
        bytes_not_sent = info->length - bytes_sent;
        if (bytes_not_sent > BUF_SIZE)
            bytes_to_be_sent = BUF_SIZE;
        else
            bytes_to_be_sent = bytes_not_sent;
        retval = ftpd_send_info(sockfd, file + bytes_sent, bytes_to_be_sent);
        if (retval < 0) {
            shutdown(sockfd, SHUT_RDWR);
            return -2;
        }
        bytes_sent += retval;
        usleep(100000);  /* simple flow control: 10kb/s */
    }

    shutdown(sockfd, SHUT_RDWR);
    return bytes_sent;
}

/**
 * wait_child - reclaim resources of child processes
 */
void wait_child(int signo)
{
    pid_t pid;

    if (signo != SIGCHLD)    return;
    while (1) {
        pid = waitpid(-1, NULL, WNOHANG | WUNTRACED | WCONTINUED);
        if (pid == -1 || pid == 0)    return; 
    }
}
