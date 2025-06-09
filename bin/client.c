#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define BUFSZ 1024

void usage(char *prog) {
    printf("Usage: %s <server_ip> <port> -nick <nickname>\n", prog);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
    if (argc != 5 || strcmp(argv[3], "-nick") != 0) {
        fprintf(stderr, "Error: Expected '-nick' argument\n");
        usage(argv[0]);
    }
    if (strlen(argv[4]) > 13) {
        fprintf(stderr, "Error: Nickname too long (max 13)\n");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_storage storage;
    if (addrparse(argv[1], argv[2], &storage) != 0) {
        logexit("addrparse");
    }

    int s = socket(storage.ss_family, SOCK_STREAM, 0);
    if (s == -1) logexit("socket");

    if (connect(s, (struct sockaddr *)(&storage), sizeof(storage)) != 0) {
        logexit("connect");
    }

    printf("Conectado ao servidor.\n");

    fd_set readfds;

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(s, &readfds);
        int maxfd = s;

        if (select(maxfd + 1, &readfds, NULL, NULL, NULL) == -1) {
            logexit("select");
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char input[BUFSZ];
            fgets(input, BUFSZ, stdin);
            input[strcspn(input, "\n")] = 0;

            struct aviator_msg msg = {.player_id = 0};

            if (strcmp(input, "Q") == 0) {
                strncpy(msg.type, "bye", STR_LEN);
                send(s, &msg, sizeof(msg), 0);
                break;
            } else if (strcmp(input, "C") == 0) {
                strncpy(msg.type, "cashout", STR_LEN);
                msg.value = 0;  // servidor calcula o valor do cashout
                send(s, &msg, sizeof(msg), 0);
            } else {
                float val = atof(input);
                if (val <= 0) {
                    fprintf(stderr, "Error: Invalid bet value\n");
                    continue;
                }
                msg.value = val;
                strncpy(msg.type, "bet", STR_LEN);
                send(s, &msg, sizeof(msg), 0);
                printf("Aposta recebida: R$ %.2f\n", val);
            }
        }

        if (FD_ISSET(s, &readfds)) {
            struct aviator_msg recv_msg;
            int count = recv(s, &recv_msg, sizeof(recv_msg), 0);
            if (count <= 0) break;

            if (strcmp(recv_msg.type, "start") == 0) {
                printf("Rodada aberta! Digite o valor da aposta ou [Q] para sair (10 segundos restantes):\n");
            } else if (strcmp(recv_msg.type, "closed") == 0) {
                printf("Apostas encerradas! Não é mais possível apostar nesta rodada.\nDigite [C] para sacar.\n");
            } else if (strcmp(recv_msg.type, "multiplier") == 0) {
                printf("Multiplicador atual: %.2fx\n", recv_msg.value);
            } else if (strcmp(recv_msg.type, "explode") == 0) {
                printf("Aviãozinho explodiu em: %.2fx\n", recv_msg.value);
            } else if (strcmp(recv_msg.type, "payout") == 0) {
                printf("Você sacou e ganhou R$ %.2f!\n", recv_msg.value);
            } else if (strcmp(recv_msg.type, "profit") == 0) {
                printf("Profit atual: R$ %.2f | Profit da casa: R$ %.2f\n", recv_msg.player_profit, recv_msg.house_profit);
            }
        }
    }

    close(s);
    printf("Aposte com responsabilidade. Volte logo, %s.\n", argv[4]);
    return 0;
}
