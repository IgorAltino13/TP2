// === CLIENTE ===
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

int socket_global;
int pode_sacar = 0;
int ja_saquei = 0;

void usage(int argc, char **argv){
    printf("usage: %s <server IP> <server port>\n", argv[0]);
    exit(EXIT_FAILURE);
}

void *input_thread(void *arg) {
    char entrada[BUFSZ];
    while (fgets(entrada, BUFSZ, stdin)) {
        entrada[strcspn(entrada, "\n")] = 0;

        if (strcmp(entrada, "Q") == 0 || strcmp(entrada, "q") == 0) {
            printf("Saindo do jogo.\n");
            break;
        } else if ((strcmp(entrada, "C") == 0 || strcmp(entrada, "c") == 0) && pode_sacar && !ja_saquei) {
            struct aviator_msg cashout;
            memset(&cashout, 0, sizeof(cashout));
            strncpy(cashout.type, "cashout", STR_LEN);
            send(socket_global, &cashout, sizeof(cashout), 0);
            ja_saquei = 1;
        } else {
            float valor = atof(entrada);
            if (valor <= 0) {
                printf("Aposta inválida.\n");
                continue;
            }
            struct aviator_msg aposta;
            memset(&aposta, 0, sizeof(aposta));
            aposta.value = valor;
            strncpy(aposta.type, "bet", STR_LEN);
            send(socket_global, &aposta, sizeof(aposta), 0);
            printf("Aposta de R$ %.2f enviada.\n", valor);
            ja_saquei = 0;
        }
    }
    close(socket_global);
    exit(0);
    return NULL;
}

int main(int argc, char **argv){
    if (argc != 5 || strcmp(argv[3], "-nick") != 0) usage(argc, argv);

    if (strlen(argv[4]) > 13) {
        fprintf(stderr, "Erro: nickname muito longo (máx 13 caracteres).\n");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_storage storage;
    if (0 != addrparse(argv[1], argv[2], &storage)) usage(argc, argv);

    int s = socket(storage.ss_family, SOCK_STREAM, 0);
    if (s == -1) logexit("socket");

    struct sockaddr *addr = (struct sockaddr *)(&storage);
    if (0 != connect(s, addr, sizeof(storage))) logexit("connect");

    socket_global = s;

    pthread_t tid_input;
    pthread_create(&tid_input, NULL, input_thread, NULL);

    while (1) {
        struct aviator_msg msg;
        ssize_t count = recv(s, &msg, sizeof(msg), 0);
        if (count <= 0) {
            printf("Desconectado do servidor.\n");
            break;
        }

        if (strncmp(msg.type, "start", STR_LEN) == 0) {
            printf("Rodada aberta! Digite o valor da aposta ou [Q] para sair (%.0f segundos restantes):\n", msg.value);
            ja_saquei = 0;
        } else if (strncmp(msg.type, "closed", STR_LEN) == 0) {
            printf("Apostas encerradas! Aguarde o voo do aviãozinho...\n");
            pode_sacar = 1;
        } else if (strncmp(msg.type, "multiplier", STR_LEN) == 0) {
             if (!ja_saquei) {
            printf("Multiplicador atual: x%.2f\n", msg.value);
         }
        }else if (strncmp(msg.type, "payout", STR_LEN) == 0) {
            printf("Você sacou e ganhou R$ %.2f!\n", msg.value);
        } else if (strncmp(msg.type, "profit", STR_LEN) == 0) {
            printf("Profit atual: R$ %.2f\n", msg.player_profit);
            pode_sacar = 0;
        } else if (strncmp(msg.type, "explode", STR_LEN) == 0) {
            printf("Aviaozinho explodiu em %.2fx\n", msg.value);
            printf("Profit da casa: R$%.2f\n", msg.house_profit);
            pode_sacar = 0;
        }
    }

    close(s);
    return 0;
}
