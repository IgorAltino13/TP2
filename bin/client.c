/* COMPLETO: client.c */
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFSZ 1024

int running = 1;
int bet_sent = 0;
float current_multiplier = 1.0f;
float my_bet = 0.0f;

void *recv_thread(void *arg) {
    int sock = *(int *)arg;
    struct aviator_msg msg;
    while (running) {
        ssize_t count = recv(sock, &msg, sizeof(msg), 0);
        if (count <= 0) break;

        if (strncmp(msg.type, "start", STR_LEN) == 0) {
            printf("\nRodada aberta! Digite o valor da aposta ou [Q] para sair (%.0f segundos restantes):\n", msg.value);
        } else if (strncmp(msg.type, "closed", STR_LEN) == 0) {
            printf("\nApostas encerradas! Não é mais possível apostar nesta rodada.\n");
            if (bet_sent) {
                printf("Digite [C] para sacar.\n");
            }
        } else if (strncmp(msg.type, "multiplier", STR_LEN) == 0) {
            current_multiplier = msg.value;
            printf("Multiplicador atual: %.2fx\n", current_multiplier);
        } else if (strncmp(msg.type, "explode", STR_LEN) == 0) {
            printf("Aviãozinho explodiu em: %.2fx\n", msg.value);
        } else if (strncmp(msg.type, "payout", STR_LEN) == 0) {
            printf("Você sacou em %.2fx e ganhou R$ %.2f!\n", current_multiplier, msg.value);
        } else if (strncmp(msg.type, "profit", STR_LEN) == 0) {
            printf("Profit atual: R$ %.2f\n", msg.player_profit);
            printf("Profit da casa: R$ %.2f\n", msg.house_profit);
        } else if (strncmp(msg.type, "bye", STR_LEN) == 0) {
            printf("O servidor caiu, mas sua esperança pode continuar de pé. Até breve!\n");
            running = 0;
            break;
        }
    }
    running = 0;
    pthread_exit(NULL);
}

void usage(int argc, char **argv) {
    printf("usage: %s <server IP> <server port> -nick <apelido>\n", argv[0]);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
    if (argc != 5) {
        if (argc >= 4 && strcmp(argv[3], "-nick") != 0) {
            fprintf(stderr, "Error: Expected '-nick' argument\n");
        } else {
            fprintf(stderr, "Error: Invalid number of arguments\n");
        }
        exit(EXIT_FAILURE);
    }

    if (strlen(argv[4]) > 13) {
        fprintf(stderr, "Error: Nickname too long (max 13)\n");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_storage storage;
    if (0 != addrparse(argv[1], argv[2], &storage)) {
        usage(argc, argv);
    }

    int s = socket(storage.ss_family, SOCK_STREAM, 0);
    if (s == -1) {
        logexit("socket");
    }

    struct sockaddr *addr = (struct sockaddr *)(&storage);
    if (0 != connect(s, addr, sizeof(storage))) {
        logexit("connect");
    }

    char addrstr[BUFSZ];
    addrtostr(addr, addrstr, BUFSZ);
    printf("Conectado ao servidor.\n");

    pthread_t tid;
    pthread_create(&tid, NULL, recv_thread, &s);

    char input[BUFSZ];
    while (running) {
        if (!bet_sent) {
            printf("> ");
            fgets(input, BUFSZ, stdin);
            input[strcspn(input, "\n")] = '\0';

            if (strcasecmp(input, "Q") == 0) {
                struct aviator_msg msg = {0};
                strncpy(msg.type, "bye", STR_LEN);
                send(s, &msg, sizeof(msg), 0);
                running = 0;
                break;
            } else {
                float val = atof(input);
                if (val <= 0) {
                    printf("Error: Invalid bet value\n");
                    continue;
                }

                struct aviator_msg msg = {0};
                strncpy(msg.type, "bet", STR_LEN);
                msg.value = val;
                my_bet = val;
                bet_sent = 1;
                send(s, &msg, sizeof(msg), 0);
                printf("Aposta recebida: R$ %.2f\n", val);
            }
        } else {
            fgets(input, BUFSZ, stdin);
            input[strcspn(input, "\n")] = '\0';
            if (strcasecmp(input, "C") == 0) {
                struct aviator_msg msg = {0};
                strncpy(msg.type, "cashout", STR_LEN);
                msg.value = current_multiplier;
                send(s, &msg, sizeof(msg), 0);
            } else if (strcasecmp(input, "Q") == 0) {
                struct aviator_msg msg = {0};
                strncpy(msg.type, "bye", STR_LEN);
                send(s, &msg, sizeof(msg), 0);
                running = 0;
                break;
            } else {
                printf("Error: Invalid command\n");
            }
        }
    }

    pthread_join(tid, NULL);
    close(s);
    printf("Aposte com responsabilidade. A plataforma é nova e tá com horário bugado. Volte logo, %s.\n", argv[4]);
    return 0;
}
