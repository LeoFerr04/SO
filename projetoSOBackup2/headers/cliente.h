#pragma once
#include "util.h"

// Mensagens (se quiseres manter para clareza)
#define MSG_TURNO_PREFIX        "TURNO|"
#define MSG_AGUARDA_TURNO       "AGUARDA_TURNO"
#define MSG_NAO_E_TUA_VEZ       "NAO_E_TUA_VEZ"
#define MSG_JOGO_TERMINOU       "JOGO_TERMINOU"
#define MSG_JOGO_CANCELADO      "JOGO_CANCELADO"

// Protótipos que EXISTEM no cliente.c atual
void carregarConfigCliente(char *nomeFicheiro, struct ClienteConfig *clienteConfig);
void imprimirTabuleiro(char *jogo);

void logEventoCliente(const char *message, struct ClienteConfig *clienteConfig);
void logQueEventoCliente(int numero, struct ClienteConfig clienteConfig);

void construtorCliente(int dominio, unsigned int porta, __u_long interface, struct ClienteConfig *clienteConfig);
void iniciarClienteSocket(struct ClienteConfig *clienteConfig);

void tentarSolucaoParcial(char tentativaAtual[], char valoresCorretos[]);
void tentarSolucaoCompleta(char tentativaAtual[], char valoresCorretos[]);

void mandarETratarMSG(struct ClienteConfig *clienteConfig);
bool desistirDeResolver(void);
