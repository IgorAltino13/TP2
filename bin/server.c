/* ---------- server.c (versão final) ---------- */
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
#include <errno.h>

#define BUFSZ           1024
#define MAX_JOGADORES   10

/* ---------- Protótipos ---------- */
void *thread_countdown(void *);
void *thread_voo(void *);
void *thread_teclado(void *);
void  iniciar_nova_rodada(void);
void  encerrar_threads_rodada(void);
void  enviar_msg_para_todos(struct aviator_msg *);
void  shutdown_servidor(void);
void  usage(int, char **);

/* ---------- Estruturas ---------- */
struct client_data {
    int csock;
    struct sockaddr_storage storage;
    int slot;
};

struct jogador_t {
    int    id;
    double valor_aposta;
    double lucro;
    int    apostou;
    int    sacou;
    int    csock;
};
static struct jogador_t jogador[MAX_JOGADORES];

/* ---------- Variáveis globais ---------- */
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static int proximo_id      = 1;
static int num_jogadores   = 0;

enum estado_rodada { AGUARDANDO, APOSTAS_ABERTAS, APOSTAS_FECHADAS, EM_VOO };
static enum estado_rodada estado = AGUARDANDO;

static double seg_restantes    = 0.0;
static double mult_atual       = 1.0;
static double lucro_casa       = 0.0;

/* Threads internas */
static pthread_t tid_countdown, tid_voo, tid_teclado;
static int countdown_ativo = 0, voo_ativo = 0;

/* Socket de escuta global para fechar no shutdown */
static int sock_listen;

/* ---------- Utilidades ---------- */
void usage(int argc, char **argv) {
    fprintf(stderr, "usage: %s <v4|v6> <server port>\n", argv[0]);
    exit(EXIT_FAILURE);
}

void enviar_msg_para_todos(struct aviator_msg *msg) {
    for (int i = 0; i < MAX_JOGADORES; i++)
        if (jogador[i].csock != -1)
            send(jogador[i].csock, msg, sizeof(*msg), 0);
}

void resetar_apostas(void) {
    for (int i = 0; i < MAX_JOGADORES; i++) {
        jogador[i].apostou = jogador[i].sacou = 0;
        jogador[i].valor_aposta = 0.0;
    }
}

void encerrar_threads_rodada(void) {
    if (countdown_ativo) {
        pthread_cancel(tid_countdown);
        pthread_join(tid_countdown, NULL);
        countdown_ativo = 0;
    }
    if (voo_ativo) {
        pthread_cancel(tid_voo);
        pthread_join(tid_voo, NULL);
        voo_ativo = 0;
    }
    estado = AGUARDANDO;
}

/* ---------- Shutdown global ---------- */
void shutdown_servidor(void) {
    pthread_mutex_lock(&mtx);

    /* avisa clientes */
    struct aviator_msg bye = {0};
    strncpy(bye.type, "bye", STR_LEN);
    enviar_msg_para_todos(&bye);
    printf("event=bye | id=*\n");
    printf("Encerrando servidor.\n");

    /* fecha sockets de clientes */
    for (int i = 0; i < MAX_JOGADORES; i++)
        if (jogador[i].csock != -1) {
            close(jogador[i].csock);
            jogador[i].csock = -1;
        }

    /* cancela threads de rodada */
    encerrar_threads_rodada();

    /* fecha socket de escuta para desbloquear accept() */
    close(sock_listen);

    pthread_mutex_unlock(&mtx);

    /* termina processo */
    exit(EXIT_SUCCESS);
}

/* ---------- Thread de leitura do teclado ---------- */
void *thread_teclado(void *arg) {
    (void)arg;
    int ch;
    while ((ch = getchar()) != EOF) {
        if (ch == 'Q' || ch == 'q')
            shutdown_servidor();
    }
    return NULL;
}

/* ---------- Thread countdown ---------- */
void *thread_countdown(void *arg) {
    (void)arg;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    for (int i = 10; i > 0; i--) {
        pthread_mutex_lock(&mtx);
        seg_restantes = i;
        pthread_mutex_unlock(&mtx);
        sleep(1);
    }

    pthread_mutex_lock(&mtx);
    estado = APOSTAS_FECHADAS;
    struct aviator_msg msg = {0};
    strncpy(msg.type, "closed", STR_LEN);
    enviar_msg_para_todos(&msg);
    printf("event=closed | id=*\n");

    /* inicia voo */
    voo_ativo = 1;
    pthread_create(&tid_voo, NULL, thread_voo, NULL);
    pthread_mutex_unlock(&mtx);

    countdown_ativo = 0;
    return NULL;
}

/* ---------- Thread voo ---------- */
void *thread_voo(void *arg) {
    (void)arg;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    pthread_mutex_lock(&mtx);
    estado = EM_VOO;
    double m = 1.0, V = 0.0;
    int N = 0;
    for (int i = 0; i < MAX_JOGADORES; i++)
        if (jogador[i].csock != -1 && jogador[i].apostou) {
            N++;
            V += jogador[i].valor_aposta;
        }
    const double m_e = sqrt(1.0 + N + 0.01 * V);
    pthread_mutex_unlock(&mtx);

    struct aviator_msg msg;
    strncpy(msg.type, "multiplier", STR_LEN);

    while (m < m_e) {
        usleep(100000);
        m *= 1.01;
        pthread_mutex_lock(&mtx);
        mult_atual = m;
        msg.value  = (float)m;
        enviar_msg_para_todos(&msg);
        pthread_mutex_unlock(&mtx);
        printf("event=multiplier | id=* | m=%.2f\n", m);
    }

    /* explosão */
    pthread_mutex_lock(&mtx);
    for (int i = 0; i < MAX_JOGADORES; i++)
        if (jogador[i].csock != -1 && jogador[i].apostou && !jogador[i].sacou) {
            jogador[i].lucro -= jogador[i].valor_aposta;
            lucro_casa       += jogador[i].valor_aposta;

            struct aviator_msg lucro = {0};
            strncpy(lucro.type, "profit", STR_LEN);
            lucro.player_profit = (float)jogador[i].lucro;
            send(jogador[i].csock, &lucro, sizeof(lucro), 0);

            printf("event=explode | id=%d | m=%.2f\n", jogador[i].id, m);
            printf("event=profit | id=%d | player_profit=%.2f\n",
                   jogador[i].id, jogador[i].lucro);
        }

    memset(&msg, 0, sizeof(msg));
    strncpy(msg.type, "explode", STR_LEN);
    msg.value        = (float)m;
    msg.house_profit = (float)lucro_casa;
    enviar_msg_para_todos(&msg);
    printf("event=explode | id=* | m=%.2f\n", m);
    printf("event=profit  | id=* | house_profit=%.2f\n", lucro_casa);

    resetar_apostas();
    voo_ativo = 0;
    if (num_jogadores > 0) iniciar_nova_rodada();
    else estado = AGUARDANDO;
    pthread_mutex_unlock(&mtx);

    return NULL;
}

/* ---------- Inicia nova rodada ---------- */
void iniciar_nova_rodada(void) {
    estado        = APOSTAS_ABERTAS;
    seg_restantes = 10.0;
    struct aviator_msg msg = {0};
    strncpy(msg.type, "start", STR_LEN);
    msg.value = (float)seg_restantes;
    enviar_msg_para_todos(&msg);
    printf("event=start | id=* | N=%d\n", num_jogadores);

    countdown_ativo = 1;
    pthread_create(&tid_countdown, NULL, thread_countdown, NULL);
}

/* ---------- Thread de cliente ---------- */
void *client_thread(void *data) {
    struct client_data *cdata = (struct client_data *)data;
    const int slot = cdata->slot;

    char caddrstr[BUFSZ];
    addrtostr((struct sockaddr *)&cdata->storage, caddrstr, BUFSZ);
    printf("event=connect | id=%d | addr=%s\n", jogador[slot].id, caddrstr);

    while (1) {
        struct aviator_msg msg;
        ssize_t cnt = recv(cdata->csock, &msg, sizeof(msg), 0);
        if (cnt <= 0) break;

        if (strncmp(msg.type, "bet", STR_LEN) == 0) {
            pthread_mutex_lock(&mtx);
            if (estado == APOSTAS_ABERTAS && !jogador[slot].apostou) {
                jogador[slot].valor_aposta = msg.value;
                jogador[slot].apostou      = 1;
                int N = 0; double V = 0.0;
                for (int i = 0; i < MAX_JOGADORES; i++)
                    if (jogador[i].csock != -1 && jogador[i].apostou) {
                        N++; V += jogador[i].valor_aposta;
                    }
                printf("event=bet | id=%d | bet=%.2f | N=%d | V=%.2f\n",
                       jogador[slot].id, msg.value, N, V);
            }
            pthread_mutex_unlock(&mtx);
        }

        else if (strncmp(msg.type, "cashout", STR_LEN) == 0) {
            pthread_mutex_lock(&mtx);
            if (estado == EM_VOO && jogador[slot].apostou && !jogador[slot].sacou) {
                jogador[slot].sacou = 1;
                double mult = floor(mult_atual * 100.0 + 0.5) / 100.0;
                double pay  = jogador[slot].valor_aposta * mult;
                jogador[slot].lucro += pay - jogador[slot].valor_aposta;
                lucro_casa          -= (pay - jogador[slot].valor_aposta);

                struct aviator_msg payout = {0};
                strncpy(payout.type, "payout", STR_LEN);
                payout.value        = (float)pay;
                payout.player_profit= (float)mult;
                send(jogador[slot].csock, &payout, sizeof(payout), 0);
                printf("event=payout | id=%d | payout=%.2f\n", jogador[slot].id, pay);

                struct aviator_msg lucro = {0};
                strncpy(lucro.type, "profit", STR_LEN);
                lucro.player_profit = (float)jogador[slot].lucro;
                send(jogador[slot].csock, &lucro, sizeof(lucro), 0);
                printf("event=profit | id=%d | player_profit=%.2f\n",
                       jogador[slot].id, jogador[slot].lucro);
            }
            pthread_mutex_unlock(&mtx);
        }

        else if (strncmp(msg.type, "bye", STR_LEN) == 0) break;
    }

    /* desconexão */
    pthread_mutex_lock(&mtx);
    close(jogador[slot].csock);
    jogador[slot].csock = -1;
    num_jogadores--;
    printf("event=bye | id=%d\n", jogador[slot].id);

    if (num_jogadores == 0)
        encerrar_threads_rodada();
    pthread_mutex_unlock(&mtx);

    free(cdata);
    return NULL;
}

/* ---------- main ---------- */
int main(int argc, char **argv) {
    if (argc < 3) usage(argc, argv);

    for (int i = 0; i < MAX_JOGADORES; i++) jogador[i].csock = -1;

    struct sockaddr_storage storage;
    if (server_sockaddr_init(argv[1], argv[2], &storage)) usage(argc, argv);

    sock_listen = socket(storage.ss_family, SOCK_STREAM, 0);
    if (sock_listen == -1) logexit("socket");

    int enable = 1;
    setsockopt(sock_listen, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    if (bind(sock_listen, (struct sockaddr *)&storage, sizeof(storage))) logexit("bind");
    if (listen(sock_listen, 10)) logexit("listen");

    /* thread de teclado: encerra com Q */
    pthread_create(&tid_teclado, NULL, thread_teclado, NULL);

    char addrstr[BUFSZ];
    addrtostr((struct sockaddr *)&storage, addrstr, BUFSZ);
    printf("bound to %s, waiting connections...\n", addrstr);

    /* loop de accept */
    while (1) {
        struct sockaddr_storage cstorage;
        socklen_t len = sizeof(cstorage);
        int csock = accept(sock_listen, (struct sockaddr *)&cstorage, &len);
        if (csock == -1) {
            /* se o socket foi fechado pelo shutdown, saímos do loop */
            if (errno == EBADF) break;
            logexit("accept");
        }

        pthread_mutex_lock(&mtx);
        int slot = -1;
        for (int i = 0; i < MAX_JOGADORES; i++)
            if (jogador[i].csock == -1) { slot = i; break; }
        if (slot == -1) {
            pthread_mutex_unlock(&mtx);
            fprintf(stderr, "Máximo de jogadores atingido\n");
            close(csock);
            continue;
        }

        jogador[slot].csock = csock;
        jogador[slot].id    = proximo_id++;
        jogador[slot].apostou = jogador[slot].sacou = 0;
        jogador[slot].valor_aposta = jogador[slot].lucro = 0.0;
        num_jogadores++;

        /* envia estado atual */
        if (estado == APOSTAS_ABERTAS || estado == APOSTAS_FECHADAS || estado == EM_VOO) {
            struct aviator_msg init = {0};
            if (estado == APOSTAS_ABERTAS) {
                strncpy(init.type, "start", STR_LEN);
                init.value = (float)seg_restantes;
            } else {
                strncpy(init.type, "closed", STR_LEN);
            }
            send(csock, &init, sizeof(init), 0);
        }

        if (estado == AGUARDANDO)
            iniciar_nova_rodada();
        pthread_mutex_unlock(&mtx);

        struct client_data *cd = malloc(sizeof(*cd));
        cd->csock = csock; cd->slot = slot; memcpy(&cd->storage, &cstorage, sizeof(cstorage));
        pthread_t tid; pthread_create(&tid, NULL, client_thread, cd); pthread_detach(tid);
    }
    return 0;
}
