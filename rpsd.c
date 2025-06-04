#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netdb.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <poll.h>


#include "network.h"

#define PROTOCOL_VERSION "1"
#define MAX_ACTIVE 100

typedef struct {
    pid_t pid;
    char name[256];
} Active;

static Active active[MAX_ACTIVE];
static int active_used = 0;

static int peer_closed(int fd)
{
    char dummy;
    ssize_t n = recv(fd, &dummy, 1, MSG_PEEK | MSG_DONTWAIT);
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
      return 1;
    }
    if (n == 0) {
      return 1;
    }
    return 0;
}

static int name_is_duplicate(char (*queue_names)[256], int qsize, const char *name)
{
  int name_in_use = 2;  
  for (int i = 0; i < active_used; i++) {
    if (strcmp(active[i].name, name) == 0) {
      name_in_use = 1;
      break;
    }
  }
  if (name_in_use != 1) {
    name_in_use = 0;
  }

  int name_in_waiting_queue;
  for (int i = 0; i < qsize; i++) {
    if (queue_names[i][0] && strcmp(queue_names[i], name) == 0) {
      name_in_waiting_queue = 1;
      break;
    }
  }
  if (name_in_waiting_queue != 1) {
    name_in_waiting_queue = 0;
  }
      
  return name_in_use || name_in_waiting_queue;
}

static void reap_children(int sig)
{
    (void)sig;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, 1)) > 0) {
      for (int i = 0; i < active_used; ) {
        if (active[i].pid == pid) {
            active_used--;
            memmove(&active[i], &active[i+1], (active_used - i) * sizeof(Active));
        }
        else {
            i++;
        }
      }
    }
}

typedef struct {
    int  sock;
    char name[256];
    char move[10];
    int  wants_rematch;
} Player;

static int read_message(int sock, char *buf, int cap);
static char determine_winner(const char *a, const char *b);
static void play_game(Player *p1, Player *p2);

static int get_one_move(Player *p1, Player *p2)
{
    struct pollfd fds[2];
    fds[0].fd      = p1->sock;
    fds[0].events  = POLLIN | POLLHUP | POLLERR;
    fds[1].fd      = p2->sock;
    fds[1].events  = POLLIN | POLLHUP | POLLERR;

    while (p1->move[0] == '\0' || p2->move[0] == '\0') {

        if (poll(fds, 2, -1) < 0) {
            return -1;                          /* interrupted → error */
        }

        for (int i = 0; i < 2; ++i) {

            Player *p;
            if (i == 0) {
                p = p1;
            }
            else {
                p = p2;
            }

            if (fds[i].revents & (POLLHUP | POLLERR)) {
                if (p == p1) {
                    return 1;
                }
                else {
                    return 2;
                }
            }

            if (fds[i].revents & POLLIN) {
                char buf[256];

                if (read_message(p->sock, buf, sizeof buf) <= 0 || buf[0] != 'M') {
                    if (p == p1) {
                        return 1;
                    }
                    else {
                        return 2;
                    }
                }

                char *s = strchr(buf, '|') + 1;
                char *e = strstr(buf, "||");

                memcpy(p->move, s, (size_t)(e - s));
                p->move[e - s] = '\0';

                fds[i].events = 0;
            }
        }
    }
    return 0;
}


static void play_game(Player *p1, Player *p2)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "B|%s||", p2->name);
    size_t len = strlen(buf);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(p1->sock, buf + sent, len - sent);
        if (n <= 0) {
          p1->sock = -1; return; 
        }
        sent += (size_t)n;
    }

    snprintf(buf, sizeof(buf), "B|%s||", p1->name);
    len  = strlen(buf);
    sent = 0;
    while (sent < len) {
        ssize_t n = write(p2->sock, buf + sent, len - sent);
        if (n <= 0) {
          p2->sock = -1; return;
        }
        sent += (size_t)n;
    }

    p1->move[0] = '\0';
    p2->move[0] = '\0';

    int result = get_one_move(p1, p2);
    if (result == 1) {
        // P1 forfeited
        char msg[] = "R|F|||";
        write(p2->sock, msg, strlen(msg));
        close(p1->sock);
        p1->sock = -1;
        p1->wants_rematch = 0;
        p2->wants_rematch = 0;
        return;
    }
    if (result == 2) {
        // P2 forfeited
        char msg[] = "R|F|||";
        write(p1->sock, msg, strlen(msg));
        close(p2->sock);
        p2->sock = -1;
        p1->wants_rematch = 0;
        p2->wants_rematch = 0;
        return;
    }


    char r1 = determine_winner(p1->move, p2->move);
    char r2;
    if (r1 == 'W') {
        r2 = 'L';
    }
    else if (r1 == 'L') {
        r2 = 'W';
    }
    else {
        r2 = 'D';
    }

    snprintf(buf, sizeof(buf), "R|%c|%s||", r1, p2->move);
    len = strlen(buf);
    sent = 0;
    while (sent < len) {
        ssize_t k = write(p1->sock, buf + sent, len - sent);
        if (k <= 0) {
          p1->sock = -1;
          return;
        }
        sent += (size_t)k;
    }

    snprintf(buf, sizeof(buf), "R|%c|%s||", r2, p1->move);
    len  = strlen(buf);
    sent = 0;
    while (sent < len) {
        ssize_t k = write(p2->sock, buf + sent, len - sent);
        if (k <= 0) {
          p2->sock = -1;
          return;
        }
        sent += (size_t)k;
    }

    p1->wants_rematch = 0;
    p2->wants_rematch = 0;

    if (read_message(p1->sock, buf, sizeof(buf)) > 0 && buf[0] == 'C') {
      p1->wants_rematch = 1;
    }
    else { 
      close(p1->sock);
      p1->sock = -1;
    }

    if (read_message(p2->sock, buf, sizeof(buf)) > 0 && buf[0] == 'C') {
      p2->wants_rematch = 1;
    }
    else { 
      close(p2->sock);
      p2->sock = -1;
    }

    if (p1->sock != -1 && !p2->wants_rematch) {
      close(p1->sock);
      p1->sock = -1;
    }
    if (p2->sock != -1 && !p1->wants_rematch) {
      close(p2->sock);
      p2->sock = -1;
    }
}


static char determine_winner(const char *a, const char *b)
{
    if (strcmp(a, b) == 0) {
      return 'D';
    }

    if ((strcmp(a, "ROCK"    ) == 0 && strcmp(b, "SCISSORS") == 0) ||
        (strcmp(a, "SCISSORS") == 0 && strcmp(b, "PAPER"   ) == 0) ||
        (strcmp(a, "PAPER"   ) == 0 && strcmp(b, "ROCK"    ) == 0)) {
          return 'W';
    }

    return 'L';
}


static int read_message(int sock, char *buf, int cap)
{
    int pos = 0;
    int pipes = 0;
    char c;

    while (pos < cap - 1) {
        int n = read(sock, &c, 1);
        if (n <= 0) {
          return -1;
        }
        buf[pos++] = c;

        if (pos == 1 && (c == 'C' || c == 'Q')) {
          buf[pos] = '\0'; return pos;
        }

        if (c == '|') {
            pipes++;
            if (pipes == 2) {
              buf[pos] = '\0'; return pos;
            }
        }
        else {
            pipes = 0;
        }
    }
    return -2;
}


int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int listen_sock = open_listener(argv[1], 10);
    if (listen_sock < 0) {
      return EXIT_FAILURE;
    }

    fprintf(stderr, "RPS server listening on port %s\n", argv[1]);

    struct sigaction sa = {0};
    sa.sa_handler = reap_children;
    sigaction(SIGCHLD, &sa, NULL);

    int  player_fd [2] = { -1, -1 };
    char player_name[2][256] = {{0}};
    int  waiting = 0;
    
    for (;;) {
        /* ---- prune waiting queue before anything else ---- */
        if (waiting > 0) {
            for (int i = 0; i < waiting; ) {
                if (peer_closed(player_fd[i])) {
                    close(player_fd[i]);
                    /* compact the queue */
                    --waiting;
                    memmove(&player_fd[i], &player_fd[i+1], (2-i-1)*sizeof(player_fd[0]));
                    memmove(&player_name[i], &player_name[i+1], (2-i-1)*sizeof(player_name[0]));
                } else {
                    ++i;
                }
            }
        }
    
        int cfd = accept(listen_sock, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR) {
                continue;
            }
            else {
                perror("accept");
                continue;
            }
        }
    
        char pmsg[256];
        if (read_message(cfd, pmsg, sizeof(pmsg)) <= 0 || pmsg[0] != 'P') {
            close(cfd);
            continue;
        }
    
        char *s = strchr(pmsg, '|') + 1;
        char *e = strstr(pmsg, "||");
        size_t nlen = (size_t)(e - s);
        if (nlen >= sizeof(player_name[0])) {
            nlen = sizeof(player_name[0]) - 1;
        }
        memmove(player_name[waiting], s, nlen);
        player_name[waiting][nlen] = '\0';
    
        int must_reject = 0;                      /* new flag */

        while (name_is_duplicate(player_name, waiting, player_name[waiting])) {
            int killed = 0;
            for (int i = 0; i < waiting; ++i) {
                if (strcmp(player_name[i], player_name[waiting]) == 0 &&
                    peer_closed(player_fd[i])) {
                    close(player_fd[i]);
                    --waiting;
                    memmove(&player_fd[i],   &player_fd[i+1],   (2-i-1)*sizeof(player_fd[0]));
                    memmove(&player_name[i], &player_name[i+1], (2-i-1)*sizeof(player_name[0]));
                    killed = 1;
                    break;
                }
            }
            if (!killed) {               /* live duplicate found → reject caller */
                must_reject = 1;
                break;                   /* break out of while loop              */
            }
        }

        if (must_reject) {               /* send “already logged in” and restart */
            char dup[] = "R|L|Logged in||";
            write(cfd, dup, strlen(dup));
            close(cfd);
            continue;                    /* continue the outer for(;;) loop      */
        }
    
        char w[] = "W|" PROTOCOL_VERSION "||";
        write(cfd, w, strlen(w));
    
        player_fd[waiting] = cfd;
        waiting++;
    
        if (waiting != 2) {
            continue;
        }
    

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(player_fd[0]);
            close(player_fd[1]);
            waiting = 0;
            continue;
        }

        if (pid == 0) {
            close(listen_sock);

            Player p1 = { .sock = player_fd[0], .wants_rematch = 1 };
            Player p2 = { .sock = player_fd[1], .wants_rematch = 1 };

            strcpy(p1.name, player_name[0]);
            strcpy(p2.name, player_name[1]);

            while (p1.wants_rematch && p2.wants_rematch) {
                play_game(&p1, &p2);
                if (p1.sock == -1 || p2.sock == -1) {
                  break;
                }
              }
            if (p1.sock != -1) {
              close(p1.sock);
            }
            if (p2.sock != -1) {
              close(p2.sock);
            }
            exit(EXIT_SUCCESS);
        }

        /* ---- parent: mark names active (with child pid) --------------- */
        if (active_used < MAX_ACTIVE) {
          active[active_used].pid = pid;
          strncpy(active[active_used].name, player_name[0], 255);
          active[active_used].name[255] = '\0';
          active_used++;
        }
        if (active_used < MAX_ACTIVE) {
          active[active_used].pid = pid;
          strncpy(active[active_used].name, player_name[1], 255);
          active[active_used].name[255] = '\0';
          active_used++;
        }

        /* ---- parent: reset queue and close local copies --------------- */
        close(player_fd[0]);
        close(player_fd[1]);
        player_fd[0] = player_fd[1] = -1;
        player_name[0][0] = player_name[1][0] = '\0';
        waiting = 0;
    }
}
