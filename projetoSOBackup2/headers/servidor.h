#pragma once

#define CAPACIDADE_CONFSERVER 1
#define CONFIGFILE "./configs/servidor.conf"

#include "util.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <string.h>

// ------------------------------
// Estruturas base
// ------------------------------
struct Jogo
{
    int idJogo;
    char jogo[NUMEROS_NO_JOGO + 1];
    char solucao[NUMEROS_NO_JOGO + 1];
};

struct ThreadCliente
{
    int socketCliente;
    struct ServidorConfig *server;
    int clienteId;
};

// ------------------------------
// Sala SinglePlayer
// ------------------------------
struct SalaSinglePlayer
{
    int idSala;
    int clientesMax;
    int clienteMin;
    int nClientes;
    bool jogadorAResolver;

    pthread_mutex_t mutexSala;
    sem_t jogadorFinalizou;
    sem_t salaPronta;

    int socketCliente;
    struct Jogo jogo;
    struct ClienteConfig clienteAtual;
};

// ------------------------------
// Multiplayer (COMPATIBILIDADE)
// Tens 2 modos no teu servidor.c atual:
//  - MUL_FASTER (winner, sushi bar, barreira, etc.)
//  - MUL (tu tinhas metade desligado)
// E agora queres:
//  - MUL_TURNOS (fila interna, acesso exclusivo por tentativa)
//
// Para não rebentar o build, esta struct contém
// campos para os 3 estilos.
// ------------------------------

#define MUL_MAX_CLIENTES 5

enum GameState {
    WAITING_PLAYERS,
    GAME_STARTING,
    GAME_RUNNING,
    GAME_ENDED
};

struct SalaMultiplayer {
    int idSala;
    int clientesMax;
    int clienteMin;
    int nClientes;

    // jogo partilhado
    struct Jogo jogo;
    char estadoAtual[NUMEROS_NO_JOGO + 1];
    char valoresCorretos[NUMEROS_NO_JOGO + 1];

    // gestão de vencedor
    bool hasWinner;
    int winnerID;
    pthread_mutex_t winnerMutex;

    // fila interna de turnos
    int fila[MUL_MAX_CLIENTES];
    int head;
    int tail;
    int count;
    int emTurno;

    // sincronização
    pthread_mutex_t mtx;
    pthread_cond_t cv;

    // clientes
    struct ClienteConfig *clientes;

    // flags do modo faster
    bool temDeEsperar;
    int esperandoEntrar;
    sem_t sinalizarVencedor;
};


// ------------------------------
// Fila (singleplayer)
// ------------------------------
struct filaClientesSinglePlayer
{
    struct ClienteConfig *cliente;
    int front;
    int rear;
    int tamanho;
    int capacidade;
    pthread_mutex_t mutex;
    sem_t customers;
};

// ------------------------------
// Mensagens
// ------------------------------
struct FormatoMensagens
{
    char *idCliente;
    char *tipoJogo;
    char *tipoResolucao;
    char *temJogo;
    char *idJogo;
    char *idSala;
    char *jogo;
    char *valoresCorretos;
    char *tempoInicio;
    char *tempoFinal;
    char *resolvido;
    char *numeroTentativas;
};

// ------------------------------
// Inicialização e configs
// ------------------------------
void carregarConfigServidor(char *nomeFicheiro, struct ServidorConfig *serverConfig);
void carregarFicheiroJogosSolucoes(char *nomeFicheiro, struct Jogo jogosEsolucoes[]);
void construtorServer(struct ServidorConfig *servidor,
                      int dominio,
                      int servico,
                      int protocolo,
                      __u_long interface,
                      int porta,
                      int backlog,
                      char *ficheiroJogosESolucoesCaminho);

void iniciarServidorSocket(struct ServidorConfig *server, struct Jogo jogosEsolucoes[]);

// ------------------------------
// Logs
// ------------------------------
void logEventoServidor(const char *message);
void logQueEventoServidor(int numero, int clienteID, int salaID);

// ------------------------------
// Lógica de jogo
// ------------------------------
char *atualizaValoresCorretos(char tentativaAtual[],
                             char valoresCorretos[],
                             char solucao[],
                             int *nTentativas);

bool verificaResolvido(char valoresCorretos[], char solucao[]);

// ------------------------------
// Tratar clientes
// ------------------------------
void *criaClienteThread(void *arg);

void receberMensagemETratarServer(char *buffer,
                                  int socketCliente,
                                  struct ClienteConfig clienteConfig,
                                  struct ServidorConfig *serverConfig);

// ------------------------------
// Singleplayer
// ------------------------------
struct SalaSinglePlayer* handleSinglePlayerFila(struct ClienteConfig *cliente, struct ServidorConfig* serverConfig);
void *SalaSinglePlayer(void *arg);
void *iniciarSalaSinglePlayer(void *arg);
void iniciarSalasJogoSinglePlayer(struct ServidorConfig *serverConfig, struct Jogo jogosEsolucoes[]);

// ------------------------------
// Multiplayer (faster + normal + turnos)
// ------------------------------
void *iniciarSalaMultiplayerFaster(void *arg);
void *iniciarSalaMultiplayer(void *arg);
void iniciarSalasJogoMultiplayer(struct ServidorConfig *serverConfig, struct Jogo jogosEsolucoes[]);

bool verSeJogoAcabouEAtualizarMultiplayerFaster(struct ClienteConfig *cliente, struct SalaMultiplayer *sala);

// ------------------------------
// Fila (singleplayer)
// ------------------------------
struct filaClientesSinglePlayer *criarFila(struct ServidorConfig *serverConfig);
bool enqueue(struct filaClientesSinglePlayer *fila, struct ClienteConfig cliente);
struct ClienteConfig dequeue(struct filaClientesSinglePlayer *fila);

// ------------------------------
// Parses/util
// ------------------------------
struct FormatoMensagens parseMensagem(char *buffer);
bool validarMensagemVazia(struct FormatoMensagens *msg);

void updateClientConfig(struct ClienteConfig *clienteConfig,
                        const struct FormatoMensagens *msg,
                        const char *jogoADar,
                        int nJogo,
                        int salaID);

void bufferParaStructCliente(char *buffer, const struct ClienteConfig *clienteConfig);

char *handleResolucaoJogoSIG(struct ClienteConfig *clienteConfig, struct SalaSinglePlayer *sala);

bool verSeJogoAcabouEAtualizar(struct ClienteConfig *cliente, struct SalaSinglePlayer *sala);
