#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "creme.h"

typedef struct Contact {
    char pseudo[BEUIP_MAX_PSEUDO + 1];
    char ip[16];
    struct Contact *next;
} Contact;

static pthread_t udp_thread;
static pthread_t tcp_thread;
static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;

static Contact *contacts = NULL;
static int udp_sock = -1;
static int tcp_sock = -1;
static int server_running = 0;
static char local_pseudo[BEUIP_MAX_PSEUDO + 1] = "";
static char share_dir[256] = BEUIP_SHARE_DIR;

static void tracef(const char *fmt, ...)
{
#ifdef TRACE
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
#else
    (void)fmt;
#endif
}

static int is_localhost_ip(const char *ip)
{
    return strcmp(ip, "127.0.0.1") == 0;
}

static void trim_pseudo_copy(char *dst, const char *src)
{
    strncpy(dst, src, BEUIP_MAX_PSEUDO);
    dst[BEUIP_MAX_PSEUDO] = '\0';
}

static int filename_is_safe(const char *name)
{
    if (name == NULL || *name == '\0') {
        return 0;
    }
    if (strstr(name, "..") != NULL) {
        return 0;
    }
    if (strchr(name, '/') != NULL) {
        return 0;
    }
    return 1;
}

static void free_contact_list(void)
{
    Contact *cur;
    pthread_mutex_lock(&list_mutex);
    cur = contacts;
    contacts = NULL;
    while (cur != NULL) {
        Contact *next = cur->next;
        free(cur);
        cur = next;
    }
    pthread_mutex_unlock(&list_mutex);
}

static void add_contact_locked(const char *pseudo, const char *ip)
{
    Contact *cur = contacts;
    Contact *prev = NULL;
    Contact *node;

    while (cur != NULL) {
        if (strcmp(cur->ip, ip) == 0) {
            trim_pseudo_copy(cur->pseudo, pseudo);
            return;
        }
        if (strcmp(cur->pseudo, pseudo) == 0 && strcmp(cur->ip, ip) == 0) {
            return;
        }
        if (strcmp(cur->pseudo, pseudo) >= 0) {
            break;
        }
        prev = cur;
        cur = cur->next;
    }

    node = calloc(1, sizeof(*node));
    if (node == NULL) {
        perror("calloc");
        return;
    }
    trim_pseudo_copy(node->pseudo, pseudo);
    strncpy(node->ip, ip, sizeof(node->ip) - 1);

    if (prev == NULL) {
        node->next = contacts;
        contacts = node;
    } else {
        node->next = cur;
        prev->next = node;
    }
}

static void add_contact(const char *pseudo, const char *ip)
{
    pthread_mutex_lock(&list_mutex);
    add_contact_locked(pseudo, ip);
    pthread_mutex_unlock(&list_mutex);
}

static void remove_contact_by_ip(const char *ip)
{
    Contact *cur;
    Contact *prev = NULL;

    pthread_mutex_lock(&list_mutex);
    cur = contacts;
    while (cur != NULL) {
        if (strcmp(cur->ip, ip) == 0) {
            if (prev == NULL) {
                contacts = cur->next;
            } else {
                prev->next = cur->next;
            }
            free(cur);
            break;
        }
        prev = cur;
        cur = cur->next;
    }
    pthread_mutex_unlock(&list_mutex);
}

static void print_contacts(void)
{
    Contact *cur;

    pthread_mutex_lock(&list_mutex);
    cur = contacts;
    while (cur != NULL) {
        if (!is_localhost_ip(cur->ip)) {
            printf("%s : %s\n", cur->ip, cur->pseudo);
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&list_mutex);
}

static int find_ip_for_pseudo(const char *pseudo, char *out_ip, size_t out_sz)
{
    Contact *cur;
    int found = 0;

    pthread_mutex_lock(&list_mutex);
    cur = contacts;
    while (cur != NULL) {
        if (strcmp(cur->pseudo, pseudo) == 0) {
            strncpy(out_ip, cur->ip, out_sz - 1);
            out_ip[out_sz - 1] = '\0';
            found = 1;
            break;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&list_mutex);
    return found;
}

static int find_pseudo_for_ip(const char *ip, char *out_pseudo, size_t out_sz)
{
    Contact *cur;
    int found = 0;

    pthread_mutex_lock(&list_mutex);
    cur = contacts;
    while (cur != NULL) {
        if (strcmp(cur->ip, ip) == 0) {
            strncpy(out_pseudo, cur->pseudo, out_sz - 1);
            out_pseudo[out_sz - 1] = '\0';
            found = 1;
            break;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&list_mutex);
    return found;
}

static int send_udp_packet(const char *ip, char code, const char *payload)
{
    struct sockaddr_in dst;
    char packet[BEUIP_MAX_MSG];
    int len;

    if (udp_sock < 0) {
        return -1;
    }

    if (payload == NULL) {
        payload = "";
    }

    len = snprintf(packet, sizeof(packet), "%c%s%s", code, BEUIP_MAGIC, payload);
    if (len < 0 || len >= (int)sizeof(packet)) {
        fprintf(stderr, "beuip: paquet trop grand\n");
        return -1;
    }

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(BEUIP_PORT);
    if (inet_aton(ip, &dst.sin_addr) == 0) {
        fprintf(stderr, "beuip: IP invalide '%s'\n", ip);
        return -1;
    }

    if (sendto(udp_sock, packet, (size_t)len, 0, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        perror("sendto");
        return -1;
    }
    return 0;
}

static void broadcast_identification(void)
{
    struct ifaddrs *ifaddr = NULL;
    struct ifaddrs *ifa;
    char host[NI_MAXHOST];

    /* Adresse fixe du reseau du TP - toujours envoyee en premier */
    send_udp_packet(BEUIP_BROADCAST, '1', local_pseudo);

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_broadaddr == NULL)
            continue;
        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;
        if (getnameinfo(ifa->ifa_broadaddr, sizeof(struct sockaddr_in),
                        host, sizeof(host), NULL, 0, NI_NUMERICHOST) != 0)
            continue;
        if (strcmp(host, "127.0.0.1") == 0 || strcmp(host, "0.0.0.0") == 0)
            continue;
        if (strcmp(host, BEUIP_BROADCAST) == 0)
            continue;
        tracef("[TRACE] broadcast sur %s\n", host);
        send_udp_packet(host, '1', local_pseudo);
    }

    freeifaddrs(ifaddr);
}


static void send_depart_to_all(void)
{
    Contact *cur = contacts;
    while (cur != NULL) {
        if (!is_localhost_ip(cur->ip))
            send_udp_packet(cur->ip, '0', local_pseudo);
        cur = cur->next;
    }
}

static void send_message_to_user(const char *pseudo, const char *message)
{
    Contact *cur = contacts;
    while (cur != NULL) {
        if (strcmp(cur->pseudo, pseudo) == 0) {
            send_udp_packet(cur->ip, '4', message);
            return;
        }
        cur = cur->next;
    }
    fprintf(stderr, "beuip: pseudo '%s' introuvable\n", pseudo);
}

static void send_message_to_all(const char *message)
{
    Contact *cur = contacts;
    while (cur != NULL) {
        if (!is_localhost_ip(cur->ip))
            send_udp_packet(cur->ip, '5', message);
        cur = cur->next;
    }
}

static void local_command(char code, const char *message, const char *pseudo)
{
    if (udp_sock < 0)
        return;

    pthread_mutex_lock(&list_mutex);
    if (code == '0')
        send_depart_to_all();
    else if (code == '4')
        send_message_to_user(pseudo, message);
    else if (code == '5')
        send_message_to_all(message != NULL ? message : "");
    pthread_mutex_unlock(&list_mutex);
}

static void wake_udp_thread(void)
{
    int s;
    struct sockaddr_in dst;
    char dummy = '!';

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        return;
    }
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(BEUIP_PORT);
    inet_aton("127.0.0.1", &dst.sin_addr);
    sendto(s, &dummy, 1, 0, (struct sockaddr *)&dst, sizeof(dst));
    close(s);
}

static void wake_tcp_thread(void)
{
    int s;
    struct sockaddr_in dst;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return;
    }
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(BEUIP_PORT);
    inet_aton("127.0.0.1", &dst.sin_addr);
    connect(s, (struct sockaddr *)&dst, sizeof(dst));
    close(s);
}

static int ensure_share_dir(void)
{
    struct stat st;

    if (stat(share_dir, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            fprintf(stderr, "beuip: '%s' existe mais n'est pas un repertoire\n", share_dir);
            return -1;
        }
        return 0;
    }

    if (mkdir(share_dir, 0755) < 0) {
        perror("mkdir reppub");
        return -1;
    }
    return 0;
}

static void send_file_list(int fd)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }
    if (pid == 0) {
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
        execlp("ls", "ls", "-l", share_dir, (char *)NULL);
        perror("execlp ls");
        _exit(1);
    }
    waitpid(pid, NULL, 0);
}

static void send_named_file(int fd, const char *name)
{
    char path[512];
    pid_t pid;

    if (!filename_is_safe(name)) {
        dprintf(fd, "nom de fichier invalide\n");
        return;
    }

    snprintf(path, sizeof(path), "%s/%s", share_dir, name);
    if (access(path, R_OK) != 0) {
        dprintf(fd, "fichier introuvable: %s\n", name);
        return;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }
    if (pid == 0) {
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
        execlp("cat", "cat", path, (char *)NULL);
        perror("execlp cat");
        _exit(1);
    }
    waitpid(pid, NULL, 0);
}

static void serve_tcp_client(int fd)
{
    char first;

    if (read(fd, &first, 1) != 1) {
        return;
    }

    if (first == 'L') {
        send_file_list(fd);
        return;
    }

    if (first == 'F') {
        char name[256];
        int i = 0;
        while (i < (int)sizeof(name) - 1) {
            char c;
            ssize_t n = read(fd, &c, 1);
            if (n <= 0 || c == '\n') {
                break;
            }
            name[i++] = c;
        }
        name[i] = '\0';
        send_named_file(fd, name);
        return;
    }

    dprintf(fd, "commande TCP inconnue\n");
}

static void *udp_server_main(void *arg)
{
    const char *pseudo = (const char *)arg;
    struct sockaddr_in addr;
    int yes = 1;

    (void)pseudo;

    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        perror("socket UDP");
        return NULL;
    }

    setsockopt(udp_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(BEUIP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(udp_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind UDP");
        close(udp_sock);
        udp_sock = -1;
        return NULL;
    }

    add_contact(local_pseudo, "127.0.0.1");
    broadcast_identification();

    while (server_running) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        char buf[BEUIP_MAX_MSG];
        ssize_t n = recvfrom(udp_sock, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&from, &fromlen);
        char sender_ip[16];
        char sender_name[BEUIP_MAX_PSEUDO + 1];

        if (n <= 0) {
            if (!server_running) {
                break;
            }
            continue;
        }

        buf[n] = '\0';
        inet_ntop(AF_INET, &from.sin_addr, sender_ip, sizeof(sender_ip));

        if (n < 6 || memcmp(buf + 1, BEUIP_MAGIC, 5) != 0) {
            tracef("[TRACE] paquet invalide depuis %s\n", sender_ip);
            continue;
        }

        switch (buf[0]) {
        case '0':
            remove_contact_by_ip(sender_ip);
            break;
        case '1':
            add_contact(buf + 6, sender_ip);
            send_udp_packet(sender_ip, '2', local_pseudo);
            break;
        case '2':
            add_contact(buf + 6, sender_ip);
            break;
        case '4':
        case '5':
            if (find_pseudo_for_ip(sender_ip, sender_name, sizeof(sender_name))) {
                printf("\nMessage de %s : %s\n", sender_name, buf + 6);
            } else {
                printf("\nMessage de %s : %s\n", sender_ip, buf + 6);
            }
            fflush(stdout);
            break;
        default:
            tracef("[TRACE] code interdit '%c' depuis %s\n", buf[0], sender_ip);
            break;
        }
    }

    if (udp_sock >= 0) {
        close(udp_sock);
        udp_sock = -1;
    }
    return NULL;
}

static void *tcp_server_main(void *arg)
{
    const char *rep = (const char *)arg;
    struct sockaddr_in addr;
    int yes = 1;

    (void)rep;

    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sock < 0) {
        perror("socket TCP");
        return NULL;
    }

    setsockopt(tcp_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(BEUIP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(tcp_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind TCP");
        close(tcp_sock);
        tcp_sock = -1;
        return NULL;
    }

    if (listen(tcp_sock, 8) < 0) {
        perror("listen");
        close(tcp_sock);
        tcp_sock = -1;
        return NULL;
    }

    while (server_running) {
        int fd = accept(tcp_sock, NULL, NULL);
        if (fd < 0) {
            if (!server_running) {
                break;
            }
            continue;
        }
        serve_tcp_client(fd);
        close(fd);
    }

    if (tcp_sock >= 0) {
        close(tcp_sock);
        tcp_sock = -1;
    }
    return NULL;
}

int beuip_is_running(void)
{
    int running;
    pthread_mutex_lock(&state_mutex);
    running = server_running;
    pthread_mutex_unlock(&state_mutex);
    return running;
}

int beuip_start(const char *pseudo)
{
    if (pseudo == NULL || *pseudo == '\0') {
        fprintf(stderr, "beuip: pseudo manquant\n");
        return 1;
    }
    if (strlen(pseudo) > BEUIP_MAX_PSEUDO) {
        fprintf(stderr, "beuip: pseudo trop long (max %d)\n", BEUIP_MAX_PSEUDO);
        return 1;
    }
    if (ensure_share_dir() != 0) {
        return 1;
    }

    pthread_mutex_lock(&state_mutex);
    if (server_running) {
        pthread_mutex_unlock(&state_mutex);
        fprintf(stderr, "beuip: deja demarre\n");
        return 1;
    }

    trim_pseudo_copy(local_pseudo, pseudo);
    server_running = 1;
    pthread_mutex_unlock(&state_mutex);

    if (pthread_create(&udp_thread, NULL, udp_server_main, local_pseudo) != 0) {
        perror("pthread_create udp");
        pthread_mutex_lock(&state_mutex);
        server_running = 0;
        pthread_mutex_unlock(&state_mutex);
        return 1;
    }

    if (pthread_create(&tcp_thread, NULL, tcp_server_main, share_dir) != 0) {
        perror("pthread_create tcp");
        pthread_mutex_lock(&state_mutex);
        server_running = 0;
        pthread_mutex_unlock(&state_mutex);
        wake_udp_thread();
        pthread_join(udp_thread, NULL);
        return 1;
    }

    printf("beuip: demarre avec le pseudo '%s'\n", local_pseudo);
    return 0;
}

int beuip_stop(void)
{
    pthread_mutex_lock(&state_mutex);
    if (!server_running) {
        pthread_mutex_unlock(&state_mutex);
        fprintf(stderr, "beuip: non demarre\n");
        return 1;
    }

    local_command('0', NULL, NULL);
    server_running = 0;
    pthread_mutex_unlock(&state_mutex);

    wake_udp_thread();
    wake_tcp_thread();

    pthread_join(udp_thread, NULL);
    pthread_join(tcp_thread, NULL);

    free_contact_list();
    local_pseudo[0] = '\0';
    printf("beuip: arrete\n");
    return 0;
}

void beuip_liste(void)
{
    if (!beuip_is_running()) {
        fprintf(stderr, "beuip: non demarre\n");
        return;
    }
    print_contacts();
}

int beuip_message_user(const char *pseudo, const char *message)
{
    if (!beuip_is_running()) {
        fprintf(stderr, "beuip: non demarre\n");
        return 1;
    }
    if (pseudo == NULL || message == NULL) {
        return 1;
    }
    local_command('4', message, pseudo);
    return 0;
}

int beuip_message_all(const char *message)
{
    if (!beuip_is_running()) {
        fprintf(stderr, "beuip: non demarre\n");
        return 1;
    }
    if (message == NULL) {
        return 1;
    }
    local_command('5', message, NULL);
    return 0;
}

int beuip_ls(const char *pseudo)
{
    int s;
    struct sockaddr_in dst;
    char ip[16];
    char buf[512];
    ssize_t n;

    if (!beuip_is_running()) {
        fprintf(stderr, "beuip: non demarre\n");
        return 1;
    }
    if (!find_ip_for_pseudo(pseudo, ip, sizeof(ip))) {
        fprintf(stderr, "beuip ls: pseudo '%s' inconnu\n", pseudo);
        return 1;
    }

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("socket");
        return 1;
    }

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(BEUIP_PORT);
    inet_aton(ip, &dst.sin_addr);

    if (connect(s, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        perror("connect");
        close(s);
        return 1;
    }

    if (write(s, "L", 1) != 1) {
        perror("write");
        close(s);
        return 1;
    }

    while ((n = read(s, buf, sizeof(buf))) > 0) {
        write(STDOUT_FILENO, buf, (size_t)n);
    }

    close(s);
    return 0;
}

int beuip_get(const char *pseudo, const char *nomfic)
{
    int s;
    int fd;
    struct sockaddr_in dst;
    char ip[16];
    char req[300];
    char path[512];
    char buf[1024];
    ssize_t n;

    if (!beuip_is_running()) {
        fprintf(stderr, "beuip: non demarre\n");
        return 1;
    }
    if (!filename_is_safe(nomfic)) {
        fprintf(stderr, "beuip get: nom de fichier invalide\n");
        return 1;
    }
    if (!find_ip_for_pseudo(pseudo, ip, sizeof(ip))) {
        fprintf(stderr, "beuip get: pseudo '%s' inconnu\n", pseudo);
        return 1;
    }

    snprintf(path, sizeof(path), "%s/%s", share_dir, nomfic);
    if (access(path, F_OK) == 0) {
        fprintf(stderr, "beuip get: le fichier local '%s' existe deja\n", path);
        return 1;
    }

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("socket");
        return 1;
    }

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(BEUIP_PORT);
    inet_aton(ip, &dst.sin_addr);

    if (connect(s, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        perror("connect");
        close(s);
        return 1;
    }

    snprintf(req, sizeof(req), "F%s\n", nomfic);
    if (write(s, req, strlen(req)) < 0) {
        perror("write");
        close(s);
        return 1;
    }

    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        perror(path);
        close(s);
        return 1;
    }

    while ((n = read(s, buf, sizeof(buf))) > 0) {
        if (write(fd, buf, (size_t)n) != n) {
            perror("write fichier");
            close(fd);
            close(s);
            unlink(path);
            return 1;
        }
    }

    close(fd);
    close(s);
    printf("beuip get: fichier recu dans %s\n", path);
    return 0;
}
