#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <string.h>

// Função auxiliar para imprimir mensagem de erro e encerrar o programa
void logexit(const char *msg){
    perror(msg);
    exit(EXIT_FAILURE);
}

// Função que converte endereço e porta (em string) para estrutura sockaddr_storage (IPv4 ou IPv6)
// Retorna 0 em sucesso, -1 em erro
int addrparse(const char *addrstr, const char *portstr, struct sockaddr_storage *storage){
    
    //parser da porta
    if (addrstr == NULL || portstr == NULL){
        return -1;
    }
    // Converte porta string para número inteiro
    uint16_t port = (uint16_t)atoi(portstr);
    if(port == 0){
        return -1;
    }
    // Converte para big-endian
    port = htons(port);


    //parser do endereço, primeiro tentamos IPV4 
    // 32 bits para IPv4
    struct in_addr inaddr4;
    if(inet_pton(AF_INET,addrstr,&inaddr4)){
        struct sockaddr_in *addr4 = (struct sockaddr_in*)storage;
        addr4->sin_family = AF_INET; 
        addr4->sin_port = port;
        addr4->sin_addr = inaddr4;
        return 0;
    }
    // Tenta interpretar como IPv6 se falhou como IPv4
    // 128 bits para IPv6
    struct in6_addr inaddr6;
    if(inet_pton(AF_INET6,addrstr,&inaddr6)){
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6*)storage;
        addr6->sin6_family = AF_INET6; 
        addr6->sin6_port = port;
        //addr6->sin6_addr = inaddr6;
        memcpy(&(addr6->sin6_addr), &inaddr6, sizeof(inaddr6));
        return 0;
    }
    // Se não for nem IPv4 nem IPv6 válido
    return -1;
}
// Função que converte uma estrutura sockaddr para string legível com IP, versão e porta
void addrtostr(const struct sockaddr *addr, char *str, size_t strsize){
    int version;
    char addrstr[INET6_ADDRSTRLEN +1] = "";
    uint16_t port;
    // Testa se é IPv4
    if(addr-> sa_family == AF_INET){
        version = 4;
        struct sockaddr_in *addr4 = (struct sockaddr_in *)addr;
        // Converte IP de binário para string
        if(!inet_ntop(AF_INET, &(addr4->sin_addr), addrstr, INET6_ADDRSTRLEN + 1)){
            logexit("ntop");
        }
        // Converte porta de rede para host
        port = ntohs(addr4->sin_port);
    }
    // Testa se é IPv6 
    else if(addr->sa_family == AF_INET6){
        version  = 6;
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)addr;
        if(!inet_ntop(AF_INET, &(addr6->sin6_addr),addrstr, INET6_ADDRSTRLEN+1)){
            logexit("ntop");
        }
        // Converte porta
        port = ntohs(addr6->sin6_port);
    } else {
        // Família de protocolo não suportada
        logexit("unknown protocol family");
    }
    // Monta string final no formato: IPvX endereço porta
    if(str){
        snprintf(str, strsize, "IPv%d %s %hu", version, addrstr, port);
    }
}
// Função usada apenas pelo SERVIDOR para configurar endereço (IPv4 ou IPv6) para bind()
// Retorna 0 em sucesso, -1 em erro
int server_sockaddr_init(const char *proto, const char* portstr, struct sockaddr_storage *storage){
    
    uint16_t port = (uint16_t)atoi(portstr);
    if(port == 0){
        // Porta inválida
        return -1;
    }
    // Converte para big-endian
    port = htons(port);

    memset(storage, 0, sizeof(*storage));
    //protocolo IPV4
    if(0 == strcmp(proto, "v4")){
        struct sockaddr_in *addr4 = (struct sockaddr_in *)storage;
        addr4->sin_family = AF_INET; 
        addr4->sin_addr.s_addr = INADDR_ANY;
        addr4->sin_port = port;
        return 0;
    }//protocolo IPV6
    else if(0 == strcmp(proto, "v6")){
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)storage;
        addr6->sin6_family = AF_INET6;
        addr6->sin6_addr = in6addr_any;
        addr6->sin6_port = port;
        return 0;
    } else{
        return -1;
    }
}
