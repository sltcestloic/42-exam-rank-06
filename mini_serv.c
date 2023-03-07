#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <stdlib.h>
#include <stdio.h>

int sockfd;
size_t id_max;
fd_set pool_set;
fd_set read_set;
fd_set write_set;

typedef struct s_client {
    int id, fd, newline;
    struct s_client *next;
} t_client;

t_client *list;

void fatal() {
    char msg[] = "Fatal error\n";
    write(2, msg, strlen(msg));
    exit(1);
}

void fatal_args() {
    char msg[] = "Wrong number of arguments\n";
    write(2, msg, strlen(msg));
    exit(1);
}

t_client *get_client(int fd) {
    for (t_client *tmp = list; tmp; tmp = tmp->next)
        if (tmp->fd == fd)
            return tmp;
    return NULL;
}

int get_max_fd() {
    int fd = 0;
    for (t_client *tmp = list; tmp; tmp = tmp->next) {
        if (tmp->fd > fd)
            fd = tmp->fd;
    }
    if (fd == 0)
        return sockfd;
    return fd;
}

void send_to_all(char *msg, int len, int sender_fd) {
    for (t_client *tmp = list; tmp; tmp = tmp->next) {
        if (tmp->fd == sender_fd || !FD_ISSET(tmp->fd, &write_set))
            continue;
        if (send(tmp->fd, msg, len, 0) < 0)
            fatal();
    }
}

void add_to_list(t_client *client) {
    if (list == NULL)
        list = client;
    else {
        for (t_client *tmp = list; tmp; tmp = tmp->next) {
            if (tmp->next == NULL) {
                tmp->next = client;
                break;
            }
        }
    }
}

void add_client() {
    char msg[100];
    t_client *new_client = calloc(1, sizeof(t_client));
    if (new_client == NULL)
        fatal();

    bzero(msg, 100);
    new_client->fd = accept(sockfd, NULL, 0);
    if (new_client->fd < 0)
        fatal();
    new_client->id = id_max;
    new_client->next = NULL;
    new_client->newline = 1;
    id_max++;
    FD_SET(new_client->fd, &pool_set);
    add_to_list(new_client);
    sprintf(msg, "server: client %d just arrived\n", new_client->id);
    send_to_all(msg, strlen(msg), new_client->id);
}

void remove_client(int fd) {
    char msg[100];
    bzero(msg, 100);
    t_client *client = NULL;

    if (fd == list->fd) {
        client = list;
        list = list->next;
    } else {
        for (t_client *tmp = list; tmp->next; tmp = tmp->next)
            if (tmp->next->fd == fd) {
                client = tmp->next;
                tmp->next = client->next;
                break;
            }
    }
    sprintf(msg, "server: client %d just left\n", client->id);
    FD_CLR(fd, &pool_set);
    close(fd);
    send_to_all(msg, strlen(msg), fd);
    free(client);
}

int count_nl(char *str) {
    int ret = 0;
    for (int i = 0; str[i]; i++)
        if (str[i] == '\n')
            ret++;
    return ret;
}

int receive_big_buffer(int fd) {
    char buff[4096];
    char msg[100];
    bzero(buff, 4096);
    bzero(msg, 100);
    t_client *client  = get_client(fd);
    sprintf(msg, "client %d: ", client->id);
    if (recv(fd, buff, 4095, 0) <= 0) // bien penser à recv 1 de moins que la taille du buffer pour le \0
        return EXIT_FAILURE;

    int nls = count_nl(buff);
    if (nls == 0 || (strlen(buff) < 4096 && nls < 2)) { // si le buff a pas de \n OU si il fait - de 4096 caractères et qu'il en a que 1 (celui à la fin en général)
                                                        // on l'envoie en un seul send
        if (client->newline) {
            send_to_all(msg, strlen(msg), fd);
            client->newline = 0;
        }
        if (buff[strlen(buff) - 1] == '\n')
            client->newline == 1;
        send_to_all(buff, strlen(buff), fd);
    } else { // sinon on send 1 par 1 pour être sur de bien gérer les \n
        for (int i = 0; buff[i]; i++) {
            if (client->newline) {
                send_to_all(msg, strlen(msg), fd);
                client->newline = 0;
            }
            if (buff[i] == '\n')
                client->newline = 1;
            send_to_all(&buff[i], 1, fd);
        }
    }
    return EXIT_SUCCESS;
}

int main(int ac, char **av) {
	struct sockaddr_in servaddr;
    sockfd = 0;
    id_max = 0;
    list = NULL;
    uint16_t port = 0;

	// socket create and verification 
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1)
        fatal();
    if (ac != 2)
        fatal_args();
    port = atoi(av[1]);

	bzero(&servaddr, sizeof(servaddr)); 

	// assign IP, PORT 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(port); 
  
	// Binding newly created socket to given IP and verification 
	if ((bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0)
        fatal();
	if (listen(sockfd, 128) != 0)
        fatal();
	
    FD_ZERO(&pool_set);
    FD_ZERO(&read_set);
    FD_ZERO(&write_set);

    FD_SET(sockfd, &pool_set);

    while (1) {
        int nfds = get_max_fd();

        FD_COPY(&pool_set, &read_set);
        FD_COPY(&pool_set, &write_set);

        if (select(nfds + 1, &read_set, &write_set, NULL, 0) < 0)
            continue;
        for (int i = 0; i < nfds + 1; i++) {
            if (FD_ISSET(i, &read_set)) {
                if (i == sockfd) {
                    add_client();
                    break;
                }
                else if (receive_big_buffer(i) == EXIT_FAILURE)
                    remove_client(i);
            }
        }
    }
}