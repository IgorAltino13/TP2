#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>

#define BUFSZ 1024
#define MAX_CLIENTS 10

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
volatile float current_multiplier = 1.00f;

typedef struct {
    int csock;
    struct sockaddr_storage storage;
    int player_id;
    float bet;
    float profit;
    int has_bet;
    int has_cashout;
    int active;
} client_data_t;

client_data_t *clients[MAX_CLIENTS] = {0};
float house_profit = 0.0f;
int next_id = 1;

void broadcast(struct aviator_msg *msg) {
    pthread_mutex_lock(&lock);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] && clients[i]->active) {
            send(clients[i]->csock, msg, sizeof(*msg), 0);
        }
    }
    pthread_mutex_unlock(&lock);
}

void reset_bets() {
    pthread_mutex_lock(&lock);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i]) {
            clients[i]->bet = 0;
            clients[i]->has_bet = 0;
            clients[i]->has_cashout = 0;
        }
    }
    pthread_mutex_unlock(&lock);
}

void *game_loop(void *arg) {
    while (1) {
        // Espera por pelo menos um jogador ativo
        int has_players = 0;
        do {
            pthread_mutex_lock(&lock);
            for (int i = 0; i < MAX_CLIENTS; ++i) {
                if (clients[i] && clients[i]->active) {
                    has_players = 1;
                    break;
                }
            }
            pthread_mutex_unlock(&lock);
            if (!has_players) sleep(1);
        } while (!has_players);

        struct aviator_msg start_msg = {.player_id = 0};
        strncpy(start_msg.type, "start", STR_LEN);
        broadcast(&start_msg);
        printf("event=start | id=* | N=*\n");
        sleep(10);  // Janela de apostas

        // Fecha apostas e calcula N e V
        int N = 0;
        float V = 0.0f;
        pthread_mutex_lock(&lock);
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (clients[i] && clients[i]->active && clients[i]->has_bet) {
                N++;
                V += clients[i]->bet;
            }
        }
        pthread_mutex_unlock(&lock);

        struct aviator_msg closed_msg = {.player_id = 0};
        strncpy(closed_msg.type, "closed", STR_LEN);
        broadcast(&closed_msg);
        printf("event=closed | id=* | N=%d | V=%.2f\n", N, V);

        if (N == 0) {
            reset_bets();
            continue;
        }

        float me = sqrtf(1.0f + N + 0.01f * V);
        float m = 1.00f;

        while (m < me) {
            current_multiplier = m;

            struct aviator_msg mult_msg = {.player_id = 0, .value = m};
            strncpy(mult_msg.type, "multiplier", STR_LEN);
            broadcast(&mult_msg);
            printf("event=multiplier | id=* | m=%.2f\n", m);
            usleep(100000);  // 100ms
            m += 0.01f;
        }

        // Explodir
        current_multiplier = me;
        struct aviator_msg explode_msg = {.player_id = 0, .value = me};
        strncpy(explode_msg.type, "explode", STR_LEN);
        broadcast(&explode_msg);
        printf("event=explode | id=* | m=%.2f\n", me);

        pthread_mutex_lock(&lock);
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (clients[i] && clients[i]->active && clients[i]->has_bet && !clients[i]->has_cashout) {
                clients[i]->profit -= clients[i]->bet;
                house_profit += clients[i]->bet;

                struct aviator_msg profit_msg = {
                    .player_id = clients[i]->player_id,
                    .player_profit = clients[i]->profit,
                    .house_profit = house_profit
                };
                strncpy(profit_msg.type, "profit", STR_LEN);
                send(clients[i]->csock, &profit_msg, sizeof(profit_msg), 0);

                printf("event=explode | id=%d | m=%.2f\n", clients[i]->player_id, me);
                printf("event=profit | id=%d | player_profit=%.2f\n", clients[i]->player_id, clients[i]->profit);
            }
        }
        printf("event=profit | id=* | house_profit=%.2f\n", house_profit);
        pthread_mutex_unlock(&lock);

        reset_bets();
    }
    return NULL;
}

void *client_thread(void *data) {
    client_data_t *c = (client_data_t *)data;
    char caddrstr[BUFSZ];
    addrtostr((struct sockaddr *)(&c->storage), caddrstr, BUFSZ);
    printf("[log] connection from %s\n", caddrstr);

    while (1) {
        struct aviator_msg msg = {0};
        int count = recv(c->csock, &msg, sizeof(msg), 0);
        if (count <= 0) break;

        pthread_mutex_lock(&lock);
        if (strncmp(msg.type, "bet", STR_LEN) == 0 && !c->has_bet && msg.value > 0) {
            c->bet = msg.value;
            c->has_bet = 1;
            printf("event=bet | id=%d | bet=%.2f\n", c->player_id, c->bet);
        } else if (strncmp(msg.type, "cashout", STR_LEN) == 0 && !c->has_cashout && c->has_bet) {
            float payout = c->bet * current_multiplier;
            c->profit += payout - c->bet;
            house_profit -= payout - c->bet;
            c->has_cashout = 1;

            struct aviator_msg payout_msg = {
                .player_id = c->player_id,
                .value = payout,
                .player_profit = c->profit
            };
            strncpy(payout_msg.type, "payout", STR_LEN);
            send(c->csock, &payout_msg, sizeof(payout_msg), 0);

            struct aviator_msg prof_msg = payout_msg;
            strncpy(prof_msg.type, "profit", STR_LEN);
            prof_msg.house_profit = house_profit;
            send(c->csock, &prof_msg, sizeof(prof_msg), 0);

            printf("event=cashout | id=%d | m=%.2f\n", c->player_id, current_multiplier);
            printf("event=payout | id=%d | payout=%.2f\n", c->player_id, payout);
            printf("event=profit | id=%d | player_profit=%.2f\n", c->player_id, c->profit);
        } else if (strncmp(msg.type, "bye", STR_LEN) == 0) {
            pthread_mutex_unlock(&lock);
            break;
        }
        pthread_mutex_unlock(&lock);
    }

    close(c->csock);
    pthread_mutex_lock(&lock);
    printf("event=bye | id=%d\n", c->player_id);
    c->active = 0;
    pthread_mutex_unlock(&lock);
    free(c);
    pthread_exit(NULL);
}

void usage() {
    printf("Usage: ./server <v4|v6> <port>\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
    if (argc != 3) usage();

    struct sockaddr_storage storage;
    if (server_sockaddr_init(argv[1], argv[2], &storage) != 0) usage();

    int s = socket(storage.ss_family, SOCK_STREAM, 0);
    if (s < 0) logexit("socket");

    int optval = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if (bind(s, (struct sockaddr *)(&storage), sizeof(storage)) != 0) logexit("bind");
    if (listen(s, MAX_CLIENTS) != 0) logexit("listen");

    pthread_t game_tid;
    pthread_create(&game_tid, NULL, game_loop, NULL);

    while (1) {
        struct sockaddr_storage cstorage;
        socklen_t caddrlen = sizeof(cstorage);
        int csock = accept(s, (struct sockaddr *)(&cstorage), &caddrlen);
        if (csock < 0) continue;

        client_data_t *c = malloc(sizeof(client_data_t));
        c->csock = csock;
        memcpy(&c->storage, &cstorage, sizeof(cstorage));

        pthread_mutex_lock(&lock);
        c->player_id = next_id++;
        c->bet = 0;
        c->profit = 0;
        c->has_bet = 0;
        c->has_cashout = 0;
        c->active = 1;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i]) {
                clients[i] = c;
                break;
            }
        }
        pthread_mutex_unlock(&lock);

        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, c);
        pthread_detach(tid);
    }

    return 0;
}
