// === SERVIDOR ===
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
#define max_jogadores 4

void usage(int argc, char **argv){
    printf("usage: %s <v4|v6> <server port>\n", argv[0]);
    exit(EXIT_FAILURE);
}

struct client_data {
    int csock;
    struct sockaddr_storage storage;
    int slot;
};

struct jogadores {
    int id_jogador;
    float valor_aposta;
    float lucro_jogador;
    int apostou;
    int cash;
    int csock;
};

struct jogadores jogador[max_jogadores];
pthread_mutex_t controle_jogador = PTHREAD_MUTEX_INITIALIZER;
int proximo_jogador_id = 1;
int num_jogadores_conectados = 0;

enum estado_rodada {
    AGUARDANDO_JOGADORES,
    APOSTAS_ABERTAS,
    APOSTAS_FECHADAS,
    EM_VOO
};

enum estado_rodada estado_atual = AGUARDANDO_JOGADORES;
float segundos_restantes = 0.0f;
float multiplicador_atual = 1.0f;
float lucro_total_casa = 0.0f;

void enviar_msg_para_todos(struct aviator_msg *msg){
    pthread_mutex_lock(&controle_jogador);
    for (int i = 0; i < max_jogadores; i++){
        if (jogador[i].csock != -1){
            
            send(jogador[i].csock, msg, sizeof(*msg), 0);
        }
    }
    pthread_mutex_unlock(&controle_jogador);
}

void *thread_voo(void *arg) {
    pthread_mutex_lock(&controle_jogador);
    estado_atual = EM_VOO;
    pthread_mutex_unlock(&controle_jogador);

    float m = 1.0f;
    int N = 0;
    float V = 0.0f;

    pthread_mutex_lock(&controle_jogador);
    for (int i = 0; i < max_jogadores; i++) {
        if (jogador[i].csock != -1 && jogador[i].apostou) {
            N++;
            V += jogador[i].valor_aposta;
        }
    }
    pthread_mutex_unlock(&controle_jogador);

    float m_e = sqrtf(1.0f + N + 0.01f * V);

    struct aviator_msg msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.type, "multiplier", STR_LEN);

    printf("[log] Iniciando voo. Explosão ocorrerá em m = %.2f\n", m_e);

    while (m < m_e) {
        usleep(100000);
        m *= 1.01f;
        multiplicador_atual = m;

        msg.value = m;
        strncpy(msg.type, "multiplier", STR_LEN);
        enviar_msg_para_todos(&msg);
    }

    // Atualiza lucro da casa com apostas perdidas
    pthread_mutex_lock(&controle_jogador);
    for (int i = 0; i < max_jogadores; i++) {
        if (jogador[i].csock != -1 && jogador[i].apostou && !jogador[i].cash) {
            lucro_total_casa += jogador[i].valor_aposta;
        }
    }
    pthread_mutex_unlock(&controle_jogador);

    memset(&msg, 0, sizeof(msg));
    strncpy(msg.type, "explode", STR_LEN);
    msg.value = m_e;
    msg.house_profit = lucro_total_casa;
    enviar_msg_para_todos(&msg);

    printf("[log] Avião explodiu em m = %.2f. Mensagem 'explode' enviada.\n", m_e);

    pthread_mutex_lock(&controle_jogador);
    estado_atual = AGUARDANDO_JOGADORES;
    pthread_mutex_unlock(&controle_jogador);

    pthread_exit(NULL);
}

void *thread_countdown(void *arg) {
    for (int i = 10; i > 0; i--) {
        pthread_mutex_lock(&controle_jogador);
        segundos_restantes = i;
        pthread_mutex_unlock(&controle_jogador);
        sleep(1);
    }

    pthread_mutex_lock(&controle_jogador);
    estado_atual = APOSTAS_FECHADAS;
    segundos_restantes = 0.0f;
    pthread_mutex_unlock(&controle_jogador);

    struct aviator_msg msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.type, "closed", STR_LEN);
    enviar_msg_para_todos(&msg);
    printf("[log] Apostas encerradas. Mensagem 'closed' enviada.\n");

    pthread_t tid_voo;
    pthread_create(&tid_voo, NULL, thread_voo, NULL);
    pthread_detach(tid_voo);

    pthread_exit(NULL);
}

void *client_thread(void *data){
    struct client_data *cdata = (struct client_data *)data;
    int slot = cdata->slot;
    struct sockaddr *caddr = (struct sockaddr *)(&cdata->storage);
    char caddrstr[BUFSZ];
    addrtostr(caddr, caddrstr, BUFSZ);
    printf("[log] connection from %s\n", caddrstr);

    while (1) {
        struct aviator_msg msg;
        ssize_t count = recv(cdata->csock, &msg, sizeof(msg), 0);
        if (count <= 0) break;

        pthread_mutex_lock(&controle_jogador);

        if (strncmp(msg.type, "bet", STR_LEN) == 0) {
            if (estado_atual != APOSTAS_ABERTAS) {
                printf("[log] Aposta rejeitada: fora do tempo permitido.\n");
            } else if (jogador[slot].apostou) {
                printf("[log] Jogador %d já apostou nesta rodada.\n", jogador[slot].id_jogador);
            } else {
                jogador[slot].valor_aposta = msg.value;
                jogador[slot].apostou = 1;
                jogador[slot].cash = 0;
                printf("[log] Aposta recebida de jogador %d: R$ %.2f\n",
                       jogador[slot].id_jogador, msg.value);
            }
        }

        else if (strncmp(msg.type, "cashout", STR_LEN) == 0) {
            if (estado_atual == EM_VOO && jogador[slot].apostou && !jogador[slot].cash) {
                jogador[slot].cash = 1;
                float payout = jogador[slot].valor_aposta * multiplicador_atual;
                jogador[slot].lucro_jogador = payout - jogador[slot].valor_aposta;
                lucro_total_casa  = lucro_total_casa - (payout - jogador[slot].valor_aposta);

                memset(&msg, 0, sizeof(msg));
                strncpy(msg.type, "payout", STR_LEN);
                msg.value = payout;
                send(jogador[slot].csock, &msg, sizeof(msg), 0);

                memset(&msg, 0, sizeof(msg));
                strncpy(msg.type, "profit", STR_LEN);
                msg.player_profit = jogador[slot].lucro_jogador;
                send(jogador[slot].csock, &msg, sizeof(msg), 0);

                printf("[log] Jogador %d sacou com m = %.2f, payout = %.2f\n",
                    jogador[slot].id_jogador, multiplicador_atual, payout);
            }
        }

        pthread_mutex_unlock(&controle_jogador);
    }

    pthread_mutex_lock(&controle_jogador);
    jogador[slot].csock = -1;
    num_jogadores_conectados--;
    pthread_mutex_unlock(&controle_jogador);

    close(cdata->csock);
    free(cdata);
    pthread_exit(EXIT_SUCCESS);
}

int main(int argc, char **argv){
    if (argc < 3) usage(argc, argv);

    srand(time(NULL));
    memset(jogador, 0, sizeof(jogador));
    for (int i = 0; i < max_jogadores; i++) jogador[i].csock = -1;

    struct sockaddr_storage storage;
    if (0 != server_sockaddr_init(argv[1], argv[2], &storage)) usage(argc, argv);

    int s = socket(storage.ss_family, SOCK_STREAM, 0);
    if (s == -1) logexit("socket");

    int enable = 1;
    if (0 != setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int))) logexit("setsockopt");

    struct sockaddr *addr = (struct sockaddr *)(&storage);
    if (0 != bind(s, addr, sizeof(storage))) logexit("bind");
    if (0 != listen(s, 10)) logexit("listen");

    char addrstr[BUFSZ];
    addrtostr(addr, addrstr, BUFSZ);
    printf("bound to %s, waiting connections...\n", addrstr);

    while (1){
        struct sockaddr_storage cstorage;
        struct sockaddr *caddr = (struct sockaddr *)(&cstorage);
        socklen_t caddrlen = sizeof(cstorage);
        int csock = accept(s, caddr, &caddrlen);
        if (csock == -1) logexit("accept");

        pthread_mutex_lock(&controle_jogador);
        int aux = -1;
        for (int i = 0; i < max_jogadores; i++){
            if (jogador[i].csock == -1){ aux = i; break; }
        }
        if (aux == -1){
            pthread_mutex_unlock(&controle_jogador);
            printf("[log] Máximo de jogadores atingido\n");
            close(csock);
            continue;
        }

        jogador[aux].csock = csock;
        jogador[aux].id_jogador = proximo_jogador_id++;
        jogador[aux].apostou = 0;
        jogador[aux].cash = 0;
        jogador[aux].lucro_jogador = 0.0f;
        jogador[aux].valor_aposta = 0.0f;
        num_jogadores_conectados++;

        struct aviator_msg msg;
        memset(&msg, 0, sizeof(msg));
        if (estado_atual == APOSTAS_ABERTAS) {
            strncpy(msg.type, "start", STR_LEN);
            msg.value = segundos_restantes > 0 ? segundos_restantes : 10.0f;
            send(csock, &msg, sizeof(msg), 0);
        } else if (estado_atual == APOSTAS_FECHADAS || estado_atual == EM_VOO) {
            strncpy(msg.type, "closed", STR_LEN);
            msg.value = 0.0f;
            send(csock, &msg, sizeof(msg), 0);
        }
        pthread_mutex_unlock(&controle_jogador);

        if (estado_atual == AGUARDANDO_JOGADORES && num_jogadores_conectados >= 1){
            estado_atual = APOSTAS_ABERTAS;
            segundos_restantes = 30.0f;
            memset(&msg, 0, sizeof(msg));
            strncpy(msg.type, "start", STR_LEN);
            msg.value = segundos_restantes;
            enviar_msg_para_todos(&msg);
            printf("[log] Rodada iniciada. Enviando start a todos.\n");

            pthread_t tid_countdown;
            pthread_create(&tid_countdown, NULL, thread_countdown, NULL);
            pthread_detach(tid_countdown);
        }

        struct client_data *cdata = malloc(sizeof(*cdata));
        if (!cdata) logexit("malloc");
        cdata->csock = csock;
        memcpy(&(cdata->storage), &cstorage, sizeof(cstorage));
        cdata->slot = aux;

        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, cdata);
        pthread_detach(tid);
    }

    exit(EXIT_SUCCESS);
}