#ifndef UTIL_H
#define UTIL_H

// IMPORTANTE: isto tem de vir ANTES de qualquer include,
// senão o strptime fica "implicit declaration"
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sched.h>
#include <poll.h>

#define MAX_CLIENTS 5000

#define CHECK_NULL(x) ((x) == NULL)

#define TEMPO_TAMANHO 64
#define BUF_SIZE 8192

// 81 + '\0'
#define NUMEROS_NO_JOGO 81
#define NUM_LINHAS 9
#define NUM_COLUNAS 9

#define INFO_SIZE 256
#define SHM_SIZE 1024
#define IP_SIZE 16
#define PATH_SIZE 32

#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_PURPLE "\033[0;35m"
#define COLOR_RESET "\033[0m"

// Forward declarations (evita erros de tipo incompleto)
struct MultiplayerState;
struct SalaSinglePlayer;
struct SalaMultiplayer;

struct JogoAtual
{
    int idJogo;
    char jogo[NUMEROS_NO_JOGO + 1];
    char valoresCorretos[NUMEROS_NO_JOGO + 1];
    char tempoInicio[TEMPO_TAMANHO];
    char tempoFinal[TEMPO_TAMANHO];
    bool resolvido;
    int numeroTentativas;
};

struct ClienteConfig
{
    pthread_t threadId;
    int idCliente;
    int numJogadoresASimular;
    int tempoEntreTentativas;
    int dominio;
    int idSala;
    unsigned int porta;
    __u_long interface;

    sem_t *sinalizarVerificaSala;
    sem_t acabouResolver;

    int socket;

    struct MultiplayerState *mpState;
    struct JogoAtual jogoAtual;

    char TemJogo[INFO_SIZE];
    char tipoJogo[INFO_SIZE];
    char tipoResolucao[INFO_SIZE];
    char ipServidor[IP_SIZE];

    bool jaSaiu;
};

struct ServidorConfig
{
    int dominio;
    int servico;
    int protocolo;
    __u_long interface;
    unsigned int porta;
    int backlog;

    char ficheiroJogosESolucoesCaminho[PATH_SIZE];

    struct SalaSinglePlayer *sala;
    struct SalaMultiplayer *salaMultiplayer;

    int numeroJogos;
    int NUM_MAX_CLIENTES_FILA_SINGLEPLAYER;
};

// Globais / utils
const char *getTempo();
const char *getTempoHoraMinutoSegundoMs();

void enviarMensagens();
void receberMensagens(int nUtilizadores);

int fecharFicheiro(FILE *file);
FILE *abrirFicheiroRead(char *filename);

int validarNomeFile(char *arquivoNome, char *padrao);

ssize_t readSocket(int socket, void *buffer, size_t length);
ssize_t writeSocket(int socket, const void *buffer, size_t length);

// Converter tempo em string para time_t
time_t converterTempoStringParaTimeT(const char *tempo);

#endif // UTIL_H
