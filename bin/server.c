/* COMPLETO (CORRIGIDO): server.c */
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>

#define BUFSZ 1024
#define MAX_PLAYERS 10
#define START_MULTIPLIER 1.00f
#define MULTIPLIER_STEP 0.01f
#define BROADCAST_INTERVAL_MS 100

struct client_data {
    int csock;
    struct sockaddr_storage storage;
    int player_id;
    float bet;
    int has_bet;
    int has_cashout;
    float profit;
};

pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
struct client_data *clients[MAX_PLAYERS] = {NULL};
int client_count = 0;
float house_profit = 0.0f;

void usage(int argc, char **argv) {
    printf("usage: %s <v4|v6> <server port>\n", argv[0]);
    printf("example: %s v4 51511\n", argv[0]);
    exit(EXIT_FAILURE);
}

void broadcast(struct aviator_msg *msg) {
    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (clients[i]) {
            send(clients[i]->csock, msg, sizeof(*msg), 0);
        }
    }
    pthread_mutex_unlock(&clients_lock);
}

void *game_loop(void *arg) {
    while (1) {
        // Espera até que pelo menos um jogador tenha feito aposta
        while (1) {
            pthread_mutex_lock(&clients_lock);
            int has_bet = 0;
            for (int i = 0; i < MAX_PLAYERS; ++i) {
                if (clients[i] && clients[i]->has_bet) {
                    has_bet = 1;
                    break;
                }
            }
            pthread_mutex_unlock(&clients_lock);
            if (has_bet) break;
            usleep(100000); // 100 ms
        }

        pthread_mutex_lock(&clients_lock);
        int n = 0;
        float V = 0.0f;
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            if (clients[i] && clients[i]->has_bet) {
                V += clients[i]->bet;
                n++;
            }
        }
        pthread_mutex_unlock(&clients_lock);

        struct aviator_msg msg = {.player_id = 0, .value = 10.0f};
        strncpy(msg.type, "start", STR_LEN);
        broadcast(&msg);
        sleep(10);

        msg.value = 0.0f;
        strncpy(msg.type, "closed", STR_LEN);
        broadcast(&msg);

        float m_e = sqrtf(1 + n + 0.01f * V);
        float m = START_MULTIPLIER;

        while (m < m_e) {
            usleep(BROADCAST_INTERVAL_MS * 1000);
            msg.value = m;
            strncpy(msg.type, "multiplier", STR_LEN);
            broadcast(&msg);
            m += MULTIPLIER_STEP;
        }

        msg.value = m_e;
        strncpy(msg.type, "explode", STR_LEN);
        broadcast(&msg);

        pthread_mutex_lock(&clients_lock);
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            if (clients[i] && clients[i]->has_bet && !clients[i]->has_cashout) {
                clients[i]->profit -= clients[i]->bet;
                house_profit += clients[i]->bet;

                msg.player_id = clients[i]->player_id;
                msg.player_profit = clients[i]->profit;
                msg.house_profit = house_profit;
                strncpy(msg.type, "profit", STR_LEN);
                send(clients[i]->csock, &msg, sizeof(msg), 0);
            }
        }

        for (int i = 0; i < MAX_PLAYERS; ++i) {
            if (clients[i]) {
                clients[i]->has_bet = 0;
                clients[i]->has_cashout = 0;
                clients[i]->bet = 0;
            }
        }
        pthread_mutex_unlock(&clients_lock);
    }
    return NULL;
}

void *client_thread(void *data) {
    struct client_data *cdata = (struct client_data *)data;

    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (!clients[i]) {
            cdata->player_id = i + 1;
            clients[i] = cdata;
            break;
        }
    }
    pthread_mutex_unlock(&clients_lock);

    struct aviator_msg msg;
    while (1) {
        ssize_t count = recv(cdata->csock, &msg, sizeof(msg), 0);
        if (count <= 0) break;

        if (strncmp(msg.type, "bet", STR_LEN) == 0) {
            cdata->bet = msg.value;
            cdata->has_bet = 1;
        } else if (strncmp(msg.type, "cashout", STR_LEN) == 0) {
            float multiplier = msg.value;
            float payout = multiplier * cdata->bet;
            cdata->profit += payout - cdata->bet;
            house_profit -= payout - cdata->bet;
            cdata->has_cashout = 1;

            msg.player_id = cdata->player_id;
            msg.value = payout;
            strncpy(msg.type, "payout", STR_LEN);
            send(cdata->csock, &msg, sizeof(msg), 0);

            msg.player_profit = cdata->profit;
            msg.house_profit = house_profit;
            strncpy(msg.type, "profit", STR_LEN);
            send(cdata->csock, &msg, sizeof(msg), 0);
        } else if (strncmp(msg.type, "bye", STR_LEN) == 0) {
            break;
        }
    }

    close(cdata->csock);
    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (clients[i] == cdata) {
            clients[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&clients_lock);
    free(cdata);
    pthread_exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
    if (argc < 3) usage(argc, argv);

    struct sockaddr_storage storage;
    if (0 != server_sockaddr_init(argv[1], argv[2], &storage)) usage(argc, argv);

    int s = socket(storage.ss_family, SOCK_STREAM, 0);
    if (s == -1) logexit("socket");

    int enable = 1;
    if (0 != setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int))) logexit("setsockopt");

    struct sockaddr *addr = (struct sockaddr *)(&storage);
    if (0 != bind(s, addr, sizeof(storage))) logexit("bind");

    if (0 != listen(s, MAX_PLAYERS)) logexit("listen");

    char addrstr[BUFSZ];
    addrtostr(addr, addrstr, BUFSZ);
    printf("bound to %s, waiting for connections\n", addrstr);

    pthread_t game_tid;
    pthread_create(&game_tid, NULL, game_loop, NULL);

    while (1) {
        struct sockaddr_storage cstorage;
        struct sockaddr *caddr = (struct sockaddr *)(&cstorage);
        socklen_t caddrlen = sizeof(cstorage);
        int csock = accept(s, caddr, &caddrlen);
        if (csock == -1) logexit("accept");

        struct client_data *cdata = malloc(sizeof(*cdata));
        cdata->csock = csock;
        memcpy(&(cdata->storage), &cstorage, sizeof(cstorage));
        cdata->bet = 0.0f;
        cdata->has_bet = 0;
        cdata->has_cashout = 0;
        cdata->profit = 0.0f;

        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, cdata);
        pthread_detach(tid);
    }

    exit(EXIT_SUCCESS);
}
