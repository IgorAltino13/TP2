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
int player_id = -1;
float ultima_aposta = 0.0;

void usage(int argc, char **argv){
    printf("Error: Invalid number of arguments\n");
    printf("usage: %s <server IP> <server port> -nick <nickname>\n", argv[0]);
    exit(EXIT_FAILURE);
}

void *input_thread(void *arg) {
    char entrada[BUFSZ];
    while (fgets(entrada, BUFSZ, stdin)) {
        entrada[strcspn(entrada, "\n")] = 0;

        if (strcmp(entrada, "Q") == 0 || strcmp(entrada, "q") == 0) {
            struct aviator_msg bye;
            memset(&bye, 0, sizeof(bye));
            strncpy(bye.type, "bye", STR_LEN);
            bye.player_id = player_id;
            send(socket_global, &bye, sizeof(bye), 0);
            printf("Aposte com responsabilidade. A plataforma é nova e tá com horário bugado. Volte logo.\n");
            break;
        } else if ((strcmp(entrada, "C") == 0 || strcmp(entrada, "c") == 0) && pode_sacar && !ja_saquei) {
            struct aviator_msg cashout;
            memset(&cashout, 0, sizeof(cashout));
            strncpy(cashout.type, "cashout", STR_LEN);
            cashout.player_id = player_id;
            send(socket_global, &cashout, sizeof(cashout), 0);
            ja_saquei = 1;
        } else {
            float valor = atof(entrada);
            if (valor <= 0) {
                printf("Error: Invalid bet value\n");
                continue;
            }
            struct aviator_msg aposta;
            memset(&aposta, 0, sizeof(aposta));
            aposta.player_id = player_id;
            aposta.value = valor;
            strncpy(aposta.type, "bet", STR_LEN);
            send(socket_global, &aposta, sizeof(aposta), 0);
            ultima_aposta = valor;
            ja_saquei = 0;
            printf("Aposta recebida: R$ %.2f\n", valor);
        }
    }
    close(socket_global);
    exit(0);
    return NULL;
}

int main(int argc, char **argv){
    if (argc != 5) usage(argc, argv);
    if (strcmp(argv[3], "-nick") != 0) {
        printf("Error: Expected '-nick' argument\n");
        exit(EXIT_FAILURE);
    }
    if (strlen(argv[4]) > 13) {
        printf("Error: Nickname too long (max 13)\n");
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
            printf("O servidor caiu, mas sua esperança pode continuar de pé. Até breve!\n");
            break;
        }

        if (player_id == -1 && msg.player_id > 0)
            player_id = msg.player_id;

        if (strncmp(msg.type, "start", STR_LEN) == 0) {
            printf("\nRodada aberta! Digite o valor da aposta ou [Q] para sair (%.0f segundos restantes):\n", msg.value);
            pode_sacar = 0;
            ja_saquei = 0;
            ultima_aposta = 0.0;
        } else if (strncmp(msg.type, "closed", STR_LEN) == 0) {
            printf("Apostas encerradas! Não é mais possível apostar nesta rodada.\n");
            if (ultima_aposta > 0)
                printf("Digite [C] para sacar.\n");
            pode_sacar = 1;
        } else if (strncmp(msg.type, "multiplier", STR_LEN) == 0) {
            if (!ja_saquei)
                printf("Multiplicador atual: %.2fx\n", msg.value);
        } else if (strncmp(msg.type, "payout", STR_LEN) == 0) {
            printf("Você sacou em %.2fx e ganhou R$ %.2f!\n", msg.player_profit, msg.value);
        } else if (strncmp(msg.type, "profit", STR_LEN) == 0) {
            printf("Profit atual: R$ %.2f\n", msg.player_profit);
        } else if (strncmp(msg.type, "explode", STR_LEN) == 0) {
            printf("Aviãozinho explodiu em: %.2fx\n", msg.value);
            if (ultima_aposta > 0 && !ja_saquei)
                printf("Você perdeu R$ %.2f. Tente novamente na próxima rodada! Aviãozinho tá pagando :)\n", ultima_aposta);
            printf("Profit da casa: R$ %.2f\n", msg.house_profit);
        }else if (strncmp(msg.type, "bye", STR_LEN) == 0) {
            printf("O servidor caiu, mas sua esperança pode continuar de pé. Até breve!\n");
            break;
        }
    }

    close(s);
    return 0;
}
