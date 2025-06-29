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

// Variáveis globais de controle do cliente

// socket conectado ao servidor
int socket_global;
// indica se o jogador pode realizar cashout
int pode_sacar = 0;
// controla se o jogador já sacou nesta rodada 
int ja_saquei = 0;
// ID fornecido pelo servidor    
int player_id = -1;
// guarda o valor da última aposta feita      
float ultima_aposta = 0.0; 
// apelido do jogador
char nickname[64];         

// Exibe instrução de uso do programa caso os argumentos estejam errados
void usage(char *progname) {
    printf("Uso: %s <IP servidor> <porta> -nick <apelido>\n", progname);
    exit(EXIT_FAILURE);
}

// Thread que fica escutando a entrada do jogador
void *thread_entrada(void *arg) {
    char entrada[BUFSZ];

    while (fgets(entrada, BUFSZ, stdin)) {
        // remove \n do final da string
        entrada[strcspn(entrada, "\n")] = 0; 

        //Bloco para fazer o comando de saída do jogo: Q ou q
        if (strcmp(entrada, "Q") == 0 || strcmp(entrada, "q") == 0) {
            struct aviator_msg bye;
            memset(&bye, 0, sizeof(bye));
            strncpy(bye.type, "bye", STR_LEN);
            bye.player_id = player_id;
            send(socket_global, &bye, sizeof(bye), 0);
            printf("Aposte com responsabilidade. A plataforma é nova e tá com horário bugado. Volte logo, %s.\n", nickname);
            break;
        }

        // BLoco para fazer o comando de saque da aposta: C ou c 
        else if ((strcmp(entrada, "C") == 0 || strcmp(entrada, "c") == 0) && pode_sacar && !ja_saquei) {
            struct aviator_msg cashout;
            memset(&cashout, 0, sizeof(cashout));
            strncpy(cashout.type, "cashout", STR_LEN);
            cashout.player_id = player_id;
            send(socket_global, &cashout, sizeof(cashout), 0);
            ja_saquei = 1;
        }

        // Tentativa de aposta
        else {
            // Impede apostas se a rodada já estiver fechada
            if (pode_sacar) {
                printf("Apostas encerradas! Não é mais possível apostar nesta rodada.\n");
                continue;
            }

            // Conversão da entrada para float
            char *endptr;
            float valor = strtof(entrada, &endptr);

            // Verifica se a entrada não foi um número válido
            if (entrada[0] == '\0' || *endptr != '\0') {
                printf("Error: Invalid command\n");
                continue;
            }

            // Verifica se o valor da aposta é positivo
            if (valor <= 0) {
                printf("Error: Invalid bet value\n");
                continue;
            }

            // Envia a aposta para o servidor
            struct aviator_msg aposta;
            memset(&aposta, 0, sizeof(aposta));
            aposta.player_id = player_id;
            aposta.value = valor;
            strncpy(aposta.type, "bet", STR_LEN);
            send(socket_global, &aposta, sizeof(aposta), 0);

            ultima_aposta = valor;
            ja_saquei = 0;
            printf("Aposta enviada: R$ %.2f\n", valor);
        }
    }

    // Finaliza conexão após sair do loop
    close(socket_global);
    exit(0);
    return NULL;
}

int main(int argc, char **argv) {
    // Validação dos argumentos de linha de comando
    if (argc != 5){
        if(strcmp(argv[3], "-nick") != 0){
            printf("Error: Expected '-nick' argument\n");
            exit(EXIT_FAILURE);
        }
        printf("Error: Invalid number of arguments\n");
        exit(EXIT_FAILURE);
    }

    // Verifica se o nickname tem até 13 caracteres
    if (strlen(argv[4]) > 13) {
        printf("Error: Nickname too long (max 13)\n");
        exit(EXIT_FAILURE);
    }

    // Copia o apelido do jogador para variável global
    strncpy(nickname, argv[4], sizeof(nickname) - 1);
    nickname[sizeof(nickname) - 1] = '\0';

    // Prepara o endereço IP e porta para conectar
    struct sockaddr_storage storage;
    if (addrparse(argv[1], argv[2], &storage) != 0) usage(argv[0]);

    // Cria socket TCP
    int s = socket(storage.ss_family, SOCK_STREAM, 0);
    if (s == -1) logexit("socket");

    // Conecta ao servidor
    struct sockaddr *addr = (struct sockaddr *)(&storage);
    if (connect(s, addr, sizeof(storage)) != 0) logexit("connect");

    socket_global = s;

    // Cria uma thread para lidar com os comandos do jogador
    pthread_t tid_input;
    pthread_create(&tid_input, NULL, thread_entrada, NULL);

    // Loop principal para receber mensagens do servidor
    while (1) {
        struct aviator_msg msg;
        ssize_t count = recv(s, &msg, sizeof(msg), 0);
        if (count <= 0) {
            printf("O servidor caiu, mas sua esperança pode continuar de pé. Até breve!\n");
            break;
        }

        // Guarda o ID do jogador (fornecido pela primeira mensagem)
        if (player_id == -1 && msg.player_id > 0)
            player_id = msg.player_id;

        // Recebe mensagem de início da rodada
        if (strncmp(msg.type, "start", STR_LEN) == 0) {
            printf("\nRodada aberta! Digite o valor da aposta ou [Q] para sair (%.0f segundos restantes):\n", msg.value);
            pode_sacar = 0;
            ja_saquei = 0;
            ultima_aposta = 0.0;
        }

        // Mensagem indicando que apostas foram encerradas
        else if (strncmp(msg.type, "closed", STR_LEN) == 0) {
            printf("Apostas encerradas! Não é mais possível apostar nesta rodada.\n");
            if (ultima_aposta > 0){
                printf("Digite [C] para sacar.\n");
            }
            pode_sacar = 1;
        }

        // Atualização do multiplicador na tela do cliente
        else if (strncmp(msg.type, "multiplier", STR_LEN) == 0) {
            if (!ja_saquei && ultima_aposta > 0.0f){
                printf("Multiplicador atual: %.2fx\n", msg.value);
            }
        }

        // Mensagem de saque bem-sucedido
        else if (strncmp(msg.type, "payout", STR_LEN) == 0) {
            printf("Você sacou em %.2fx e ganhou R$ %.2f!\n", msg.value, msg.player_profit);
        }

        // Atualização de lucro do jogador
        else if (strncmp(msg.type, "profit", STR_LEN) == 0) {
            printf("Profit atual: R$ %.2f\n", msg.player_profit);
        }

        // Explosão do aviãozinho, fim da rodada
        else if (strncmp(msg.type, "explode", STR_LEN) == 0) {
            printf("Aviãozinho explodiu em: %.2fx\n", msg.value);
            if (ultima_aposta > 0 && !ja_saquei)
                printf("Você perdeu R$ %.2f. Tente novamente na próxima rodada! Aviãozinho tá pagando :)\n", ultima_aposta);
            printf("Profit da casa: R$ %.2f\n", msg.house_profit);
        }

        // Encerramento da conexão pelo servidor
        else if (strncmp(msg.type, "bye", STR_LEN) == 0) {
            printf("O servidor caiu, mas sua esperança pode continuar de pé. Até breve!\n");
            break;
        }
    }

    // Fecha o socket e finaliza o programa
    close(s);
    return 0;
}
