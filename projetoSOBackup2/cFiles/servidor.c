#include "../headers/servidor.h"

// ======================================================
// Correções aplicadas (copy/paste pronto):
// 1) Fila singleplayer: enqueue/dequeue circular corretos (sem shifts)
// 2) criaClienteThread: NÃO segurar mutexClienteID enquanto faz readSocket()
// 3) receberMensagemETratarServer: ignorar mensagens vazias (não matar cliente)
// 4) accept(): remover mutex aceitarCliente (inútil e pode atrapalhar)
// 5) clientesAtuais-- com mutex correto
// 6) validarMensagemVazia: incluir idSala (faltava)
// ======================================================

struct filaClientesSinglePlayer *filaClientesSinglePlayer;

#define MAX_MUL MUL_MAX_CLIENTES

// globais
static int idCliente = 0;

int numeroJogosResolvidos = 0;
int clientesAtuais = 0;
int clientesQueSairam = 0;
int clientesRejeitados = 0;
int clientesDesistiram = 0;

pthread_mutex_t numeroJogosResolvidosMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t clientesAtuaisMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t clientesQueSairamMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t clientesRejeitadosMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t clientesDesistiramMutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t mutexServidorLog = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutexClienteID = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t aceitarCliente = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutexAcabouJogo = PTHREAD_MUTEX_INITIALIZER;

// semáforos (singleplayer / faster)
sem_t acessoLugares;

// faster
sem_t mutexSalaMultiplayerFaster;
sem_t bloquearAcessoSala;
sem_t entradaPlayerMulPrimeiro;
sem_t capacidadeSalaMultiplayeFaster;
sem_t ultimoClienteSairSalaMultiplayerFaster;
sem_t jogoAcabouMultiplayerFaster;
pthread_barrier_t barreiraComecaTodos;

// ======================================================
// helpers
// ======================================================

static int so_espacos_ou_vazio(const char *s)
{
    if (!s) return 1;
    while (*s)
    {
        if (*s != ' ' && *s != '\n' && *s != '\r' && *s != '\t') return 0;
        s++;
    }
    return 1;
}

// ======================================================
// MULTIPLAYER NORMAL (fila interna + exclusividade)
// Requer SalaMultiplayer com:
// - clientesMax=5
// - estadoAtual[] e valoresCorretos[] partilhados
// - fila[] (IDs), head/tail/count, emTurno
// - pthread_mutex_t mtx; pthread_cond_t cv;
// ======================================================

static void mp_enqueue(struct SalaMultiplayer *s, int id)
{
    s->fila[s->tail] = id;
    s->tail = (s->tail + 1) % s->clientesMax;
    s->count++;
    if (s->count == 1) s->emTurno = id;
}

static void mp_rotate_after_play(struct SalaMultiplayer *s)
{
    if (s->count <= 1) return;

    int id = s->fila[s->head];
    s->head = (s->head + 1) % s->clientesMax;

    s->fila[s->tail] = id;
    s->tail = (s->tail + 1) % s->clientesMax;

    s->emTurno = s->fila[s->head];
    pthread_cond_broadcast(&s->cv);
}

static void mp_wait_turn(struct SalaMultiplayer *s, int id)
{
    while (s->emTurno != id && !s->hasWinner)
    {
        pthread_cond_wait(&s->cv, &s->mtx);
    }
}

static void mp_remove_player(struct SalaMultiplayer *s, int id)
{
    int tmp[MAX_MUL];
    int n = 0;

    for (int k = 0; k < s->count; k++)
    {
        int idx = (s->head + k) % s->clientesMax;
        if (s->fila[idx] != id) tmp[n++] = s->fila[idx];
    }

    s->head = 0;
    s->tail = n % s->clientesMax;
    s->count = n;
    for (int i = 0; i < n; i++) s->fila[i] = tmp[i];

    if (s->count > 0) s->emTurno = s->fila[s->head];
    else s->emTurno = -1;

    // remove de clientes[]
    int pos = -1;
    for (int i = 0; i < s->nClientes; i++)
    {
        if ((int)s->clientes[i].idCliente == id) { pos = i; break; }
    }
    if (pos >= 0)
    {
        for (int i = pos; i < s->nClientes - 1; i++) s->clientes[i] = s->clientes[i + 1];
        s->nClientes--;
    }

    // reset se sala vazia
    if (s->nClientes == 0)
    {
        s->hasWinner = false;
        s->winnerID = -1;
        s->head = s->tail = s->count = 0;
        s->emTurno = -1;

        strncpy(s->estadoAtual, s->jogo.jogo, NUMEROS_NO_JOGO);
        s->estadoAtual[NUMEROS_NO_JOGO] = '\0';

        strncpy(s->valoresCorretos, s->jogo.jogo, NUMEROS_NO_JOGO);
        s->valoresCorretos[NUMEROS_NO_JOGO] = '\0';
    }

    pthread_cond_broadcast(&s->cv);
}

// ======================================================
// Configs / logs
// ======================================================

void carregarConfigServidor(char *nomeFicheiro, struct ServidorConfig *serverConfig)
{
    FILE *config = abrirFicheiroRead(nomeFicheiro);

    char buffer[BUF_SIZE];
    int contadorConfigs = 0;

    while (fgets(buffer, PATH_SIZE, config) != NULL)
    {
        switch (contadorConfigs)
        {
        case 0:
            strcpy(serverConfig->ficheiroJogosESolucoesCaminho, strtok(buffer, "\n"));
            break;
        case 1:
            serverConfig->porta = (unsigned int)strtoul(strtok(buffer, "\n"), NULL, 10);
            break;
        case 2:
            serverConfig->NUM_MAX_CLIENTES_FILA_SINGLEPLAYER = atoi(strtok(buffer, "\n"));
            break;
        default:
            break;
        }
        contadorConfigs++;
    }
    fecharFicheiro(config);

    if (contadorConfigs == 0)
    {
        printf("Sem configs\n");
        exit(1);
    }
}

void logEventoServidor(const char *message)
{
    char *ficheiroLogs = "logs/LogServidor.txt";
    pthread_mutex_lock(&mutexServidorLog);
    FILE *file = fopen(ficheiroLogs, "a");
    if (file == NULL)
    {
        perror("Erro ao abrir o ficheiro de log");
        pthread_mutex_unlock(&mutexServidorLog);
        return;
    }
    fprintf(file, " %s\n", message);
    fclose(file);
    pthread_mutex_unlock(&mutexServidorLog);
}

void logQueEventoServidor(int numero, int clienteID, int salaID)
{
    char mensagem[512] = {0};

    switch (numero)
    {
    case 1:
        logEventoServidor("Servidor comecou");
        break;
    case 2:
        logEventoServidor("Servidor parou");
        break;
    case 3:
        sprintf(mensagem, "[%s] Cliente-%d conectado", getTempoHoraMinutoSegundoMs(), clienteID);
        logEventoServidor(mensagem);
        break;
    case 6:
        sprintf(mensagem, "[%s] Servidor enviou uma solucao para o cliente-%d", getTempoHoraMinutoSegundoMs(), clienteID);
        logEventoServidor(mensagem);
        break;
    case 7:
        sprintf(mensagem, "[%s] Cliente-%d desconectado", getTempoHoraMinutoSegundoMs(), clienteID);
        logEventoServidor(mensagem);
        break;
    case 8:
        sprintf(mensagem, "[%s] Cliente-%d resolveu o jogo da sala-%d", getTempoHoraMinutoSegundoMs(), clienteID, salaID);
        logEventoServidor(mensagem);
        break;
    case 9:
        sprintf(mensagem, "[%s] Cliente-%d entrou na fila", getTempoHoraMinutoSegundoMs(), clienteID);
        logEventoServidor(mensagem);
        break;
    case 10:
        sprintf(mensagem, "[%s] Cliente-%d removido da fila", getTempoHoraMinutoSegundoMs(), clienteID);
        logEventoServidor(mensagem);
        break;
    case 11:
        pthread_mutex_lock(&clientesRejeitadosMutex);
        clientesRejeitados++;
        pthread_mutex_unlock(&clientesRejeitadosMutex);
        sprintf(mensagem, "[%s] Cliente-%d rejeitado a entrar na fila", getTempoHoraMinutoSegundoMs(), clienteID);
        logEventoServidor(mensagem);
        break;
    case 12:
        sprintf(mensagem, "[%s] Cliente-%d entrou na sala-%d", getTempoHoraMinutoSegundoMs(), clienteID, salaID);
        logEventoServidor(mensagem);
        break;
    case 13:
        sprintf(mensagem, "[%s] Cliente-%d não resolveu o jogo da sala-%d", getTempoHoraMinutoSegundoMs(), clienteID, salaID);
        logEventoServidor(mensagem);
        break;
    case 14:
        sprintf(mensagem, "[%s] Atendendo Cliente-%d na sala-%d", getTempoHoraMinutoSegundoMs(), clienteID, salaID);
        logEventoServidor(mensagem);
        break;
    case 15:
        sprintf(mensagem, "[%s] Cliente-%d venceu jogo multiplayer", getTempoHoraMinutoSegundoMs(), clienteID);
        logEventoServidor(mensagem);
        break;
    default:
        logEventoServidor("Evento desconhecido");
        break;
    }
}

// ======================================================
// jogos / soluções
// ======================================================

char *atualizaValoresCorretos(char tentativaAtual[], char valoresCorretos[], char solucao[], int *nTentativas)
{
    (void)nTentativas;

    char *logClienteFinal = calloc(2 * BUF_SIZE, sizeof(char));
    if (logClienteFinal == NULL)
    {
        perror("Erro ao alocar memoria para logClienteFinal");
        exit(1);
    }

    char logCliente[BUF_SIZE] = "\n";

    for (int i = 0; i < (int)strlen(tentativaAtual); i++)
    {
        if (valoresCorretos[i] == '0' && tentativaAtual[i] != '0')
        {
            if (tentativaAtual[i] == solucao[i])
            {
                valoresCorretos[i] = tentativaAtual[i];
                char message[BUF_SIZE] = "";
                sprintf(message, "Valor correto(%c), na posição %d da String \n", tentativaAtual[i], i + 1);
                strcat(logCliente, message);
            }
            else
            {
                char message[BUF_SIZE] = "";
                sprintf(message, "Valor incorreto(%c), na posição %d da String \n", tentativaAtual[i], i + 1);
                strcat(logCliente, message);
            }
        }
    }

    snprintf(logClienteFinal, BUF_SIZE, "%s", logCliente);
    return logClienteFinal;
}

bool verificaResolvido(char valoresCorretos[], char solucao[])
{
    for (int i = 0; i < (int)strlen(valoresCorretos); i++)
    {
        if (valoresCorretos[i] != solucao[i]) return false;
    }
    return true;
}

int lerNumeroJogos(char *nomeFicheiro)
{
    FILE *config = abrirFicheiroRead(nomeFicheiro);
    char buffer[BUF_SIZE];
    int contadorConfigs = 0;
    int numeroJogos = 0;

    while (fgets(buffer, BUF_SIZE, config) != NULL)
    {
        if (contadorConfigs % 3 == 2) numeroJogos++;
        contadorConfigs++;
    }
    fecharFicheiro(config);

    if (contadorConfigs == 0)
    {
        printf("Sem configs\n");
        exit(1);
    }
    return numeroJogos;
}

void carregarFicheiroJogosSolucoes(char *nomeFicheiro, struct Jogo jogosEsolucoes[])
{
    FILE *config = abrirFicheiroRead(nomeFicheiro);
    char buffer[BUF_SIZE];
    int contadorConfigs = 0;
    int linhaAtual = 0;

    while (fgets(buffer, BUF_SIZE, config) != NULL)
    {
        switch (linhaAtual)
        {
        case 0:
            jogosEsolucoes[contadorConfigs].idJogo = atoi(strtok(buffer, "\n"));
            linhaAtual++;
            break;
        case 1:
            strcpy(jogosEsolucoes[contadorConfigs].jogo, strtok(buffer, "\n"));
            linhaAtual++;
            break;
        case 2:
            strcpy(jogosEsolucoes[contadorConfigs].solucao, strtok(buffer, "\n"));
            linhaAtual = 0;
            contadorConfigs++;
            break;
        default:
            perror("Erro ao ler ficheiro de jogos e soluções");
            fecharFicheiro(config);
            exit(1);
        }
    }

    fecharFicheiro(config);

    if (contadorConfigs == 0)
    {
        printf("Sem jogos\n");
        exit(1);
    }
}

void construtorServer(struct ServidorConfig *servidor,
                      int dominio,
                      int servico,
                      int protocolo,
                      __u_long interface,
                      int porta,
                      int backlog,
                      char *ficheiroJogosESolucoesCaminho)
{
    if (!servidor || !ficheiroJogosESolucoesCaminho)
    {
        perror("Erro ao criar servidor");
        exit(1);
    }

    servidor->dominio = dominio;
    servidor->servico = servico;
    servidor->protocolo = protocolo;
    servidor->interface = interface;
    servidor->porta = porta;
    servidor->backlog = backlog;

    strncpy(servidor->ficheiroJogosESolucoesCaminho, ficheiroJogosESolucoesCaminho, PATH_SIZE - 1);
    servidor->ficheiroJogosESolucoesCaminho[PATH_SIZE - 1] = '\0';

    servidor->sala = calloc(servidor->numeroJogos, sizeof(struct SalaSinglePlayer));
    if (!servidor->sala)
    {
        perror("Erro ao alocar memória para salas");
        exit(1);
    }

    for (int i = 0; i < servidor->numeroJogos; i++)
    {
        servidor->sala[i].idSala = -1;
        servidor->sala[i].nClientes = 0;
        servidor->sala[i].jogadorAResolver = false;
        servidor->sala[i].clienteAtual.idCliente = -1;
    }
}

// ======================================================
// parsing
// ======================================================

struct FormatoMensagens parseMensagem(char *buffer)
{
    struct FormatoMensagens msg;
    msg.idCliente = strtok(buffer, "|");
    msg.tipoJogo = strtok(NULL, "|");
    msg.tipoResolucao = strtok(NULL, "|");
    msg.temJogo = strtok(NULL, "|");
    msg.idJogo = strtok(NULL, "|");
    msg.idSala = strtok(NULL, "|");
    msg.jogo = strtok(NULL, "|");
    msg.valoresCorretos = strtok(NULL, "|");
    msg.tempoInicio = strtok(NULL, "|");
    msg.tempoFinal = strtok(NULL, "|");
    msg.resolvido = strtok(NULL, "|");
    msg.numeroTentativas = strtok(NULL, "|");
    return msg;
}

bool validarMensagemVazia(struct FormatoMensagens *msg)
{
    return !(CHECK_NULL(msg->idCliente) ||
             CHECK_NULL(msg->tipoJogo) ||
             CHECK_NULL(msg->tipoResolucao) ||
             CHECK_NULL(msg->temJogo) ||
             CHECK_NULL(msg->idJogo) ||
             CHECK_NULL(msg->idSala) ||          // <- faltava
             CHECK_NULL(msg->jogo) ||
             CHECK_NULL(msg->valoresCorretos) ||
             CHECK_NULL(msg->tempoInicio) ||
             CHECK_NULL(msg->tempoFinal) ||
             CHECK_NULL(msg->resolvido) ||
             CHECK_NULL(msg->numeroTentativas));
}

// ======================================================
// fila singleplayer (CORRIGIDA: circular real)
// ======================================================

struct filaClientesSinglePlayer *criarFila(struct ServidorConfig *serverConfig)
{
    struct filaClientesSinglePlayer *fila = (struct filaClientesSinglePlayer *)malloc(sizeof(struct filaClientesSinglePlayer));
    if (!fila)
    {
        perror("Erro ao alocar memoria para fila");
        return NULL;
    }
    memset(fila, 0, sizeof(struct filaClientesSinglePlayer));

    fila->cliente = (struct ClienteConfig *)calloc(serverConfig->NUM_MAX_CLIENTES_FILA_SINGLEPLAYER, sizeof(struct ClienteConfig));
    if (!fila->cliente)
    {
        free(fila);
        perror("Erro ao alocar memoria para clientes");
        return NULL;
    }

    fila->front = 0;
    fila->rear = -1;
    fila->tamanho = 0;
    fila->capacidade = serverConfig->NUM_MAX_CLIENTES_FILA_SINGLEPLAYER;

    if (pthread_mutex_init(&fila->mutex, NULL) != 0)
    {
        free(fila->cliente);
        free(fila);
        perror("Erro ao inicializar mutex");
        return NULL;
    }

    if (sem_init(&fila->customers, 0, 0) != 0)
    {
        pthread_mutex_destroy(&fila->mutex);
        free(fila->cliente);
        free(fila);
        perror("Erro ao inicializar semaforo");
        return NULL;
    }

    return fila;
}

bool enqueue(struct filaClientesSinglePlayer *fila, struct ClienteConfig cliente)
{
    if (fila->tamanho >= fila->capacidade)
    {
        printf(COLOR_PURPLE "[Fila] Rejeitado cliente %d - fila cheia (tamanho: %d)\n" COLOR_RESET,
               cliente.idCliente, fila->tamanho);
        logQueEventoServidor(11, cliente.idCliente, 0);
        return false;
    }

    fila->rear = (fila->rear + 1) % fila->capacidade;
    fila->cliente[fila->rear] = cliente;
    fila->tamanho++;

    printf(COLOR_GREEN "[Fila] Cliente %d entrou na fila (pos: %d, tamanho: %d)\n" COLOR_RESET,
           cliente.idCliente, fila->rear, fila->tamanho);
    logQueEventoServidor(9, cliente.idCliente, 0);

    return true;
}

struct ClienteConfig dequeue(struct filaClientesSinglePlayer *fila)
{
    struct ClienteConfig clienteInvalido = {0};
    clienteInvalido.idCliente = -1;

    if (fila->tamanho == 0) return clienteInvalido;

    struct ClienteConfig cliente = fila->cliente[fila->front];
    fila->front = (fila->front + 1) % fila->capacidade;
    fila->tamanho--;

    if (fila->tamanho == 0)
    {
        fila->front = 0;
        fila->rear = -1;
    }

    printf(COLOR_RED "[Fila] Cliente %d removido da fila (restantes: %d)\n" COLOR_RESET,
           cliente.idCliente, fila->tamanho);
    logQueEventoServidor(10, cliente.idCliente, 0);

    return cliente;
}

struct SalaSinglePlayer* handleSinglePlayerFila(struct ClienteConfig *cliente, struct ServidorConfig* serverConfig)
{
    struct SalaSinglePlayer* salaEncontrada = NULL;

    sem_wait(&acessoLugares);
    printf("Cliente %d entrou na fila\n", cliente->idCliente);

    if (!enqueue(filaClientesSinglePlayer, *cliente)) {
        sem_post(&acessoLugares);
        return NULL;
    }
    sem_post(&acessoLugares);

    sem_post(&filaClientesSinglePlayer->customers);
    sem_wait(cliente->sinalizarVerificaSala);

    for (int i = 0; i < serverConfig->numeroJogos; i++) {
        if (serverConfig->sala[i].clienteAtual.idCliente == cliente->idCliente) {
            salaEncontrada = &serverConfig->sala[i];
            break;
        }
    }
    return salaEncontrada;
}

// ======================================================
// helpers clienteConfig <-> buffer
// ======================================================

void updateClientConfig(struct ClienteConfig *clienteConfig,
                        const struct FormatoMensagens *msg,
                        const char *jogoADar,
                        int nJogo,
                        int salaID)
{
    strcpy(clienteConfig->tipoJogo, msg->tipoJogo);
    strcpy(clienteConfig->tipoResolucao, msg->tipoResolucao);
    strcpy(clienteConfig->TemJogo, "COM_JOGO");
    strcpy(clienteConfig->jogoAtual.jogo, jogoADar);
    strcpy(clienteConfig->jogoAtual.valoresCorretos, jogoADar);
    strcpy(clienteConfig->jogoAtual.tempoInicio, getTempoHoraMinutoSegundoMs());
    strcpy(clienteConfig->jogoAtual.tempoFinal, msg->tempoFinal);
    clienteConfig->jogoAtual.resolvido = atoi(msg->resolvido);
    clienteConfig->jogoAtual.numeroTentativas = atoi(msg->numeroTentativas);
    clienteConfig->jogoAtual.idJogo = nJogo;
    clienteConfig->idSala = salaID;
    clienteConfig->idCliente = atoi(msg->idCliente);
}

void bufferParaStructCliente(char *buffer, const struct ClienteConfig *clienteConfig)
{
    sprintf(buffer, "%u|%s|%s|%s|%d|%d|%s|%s|%s|%s|%d|%d",
            clienteConfig->idCliente,
            clienteConfig->tipoJogo,
            clienteConfig->tipoResolucao,
            clienteConfig->TemJogo,
            clienteConfig->jogoAtual.idJogo,
            clienteConfig->idSala,
            clienteConfig->jogoAtual.jogo,
            clienteConfig->jogoAtual.valoresCorretos,
            clienteConfig->jogoAtual.tempoInicio,
            clienteConfig->jogoAtual.tempoFinal,
            clienteConfig->jogoAtual.resolvido,
            clienteConfig->jogoAtual.numeroTentativas);
}

void atualizarClientConfig(struct ClienteConfig *clienteConfig, const struct FormatoMensagens *msgData)
{
    clienteConfig->idCliente = atoi(msgData->idCliente);
    strcpy(clienteConfig->TemJogo, msgData->temJogo);
    strcpy(clienteConfig->tipoJogo, msgData->tipoJogo);
    strcpy(clienteConfig->tipoResolucao, msgData->tipoResolucao);
    clienteConfig->jogoAtual.idJogo = atoi(msgData->idJogo);
    clienteConfig->idSala = atoi(msgData->idSala);
    strcpy(clienteConfig->jogoAtual.jogo, msgData->jogo);
    strcpy(clienteConfig->jogoAtual.valoresCorretos, msgData->valoresCorretos);
    strcpy(clienteConfig->jogoAtual.tempoInicio, msgData->tempoInicio);
    strcpy(clienteConfig->jogoAtual.tempoFinal, msgData->tempoFinal);
    clienteConfig->jogoAtual.resolvido = atoi(msgData->resolvido);
    clienteConfig->jogoAtual.numeroTentativas = atoi(msgData->numeroTentativas);
}

void prepararRespostaJogo(char *buffer, const struct ClienteConfig *clienteConfig, const char *logClienteEnviar)
{
    snprintf(buffer, BUF_SIZE, "%u|%s|%s|%s|%d|%d|%s|%s|%s|%s|%d|%d|%s",
             clienteConfig->idCliente,
             clienteConfig->tipoJogo,
             clienteConfig->tipoResolucao,
             clienteConfig->TemJogo,
             clienteConfig->jogoAtual.idJogo,
             clienteConfig->idSala,
             clienteConfig->jogoAtual.jogo,
             clienteConfig->jogoAtual.valoresCorretos,
             clienteConfig->jogoAtual.tempoInicio,
             clienteConfig->jogoAtual.tempoFinal,
             clienteConfig->jogoAtual.resolvido,
             clienteConfig->jogoAtual.numeroTentativas,
             logClienteEnviar ? logClienteEnviar : "");
}

// ======================================================
// singleplayer sala thread
// ======================================================

void* SalaSingleplayer(void* arg)
{
    struct SalaSinglePlayer* sala = (struct SalaSinglePlayer*)arg;
    printf("[Sala-%d] Iniciada-Singleplayer\n", sala->idSala);

    sem_init(&sala->salaPronta, 0, 0);
    sem_init(&sala->jogadorFinalizou, 0, 0);

    while (1)
    {
        sem_wait(&filaClientesSinglePlayer->customers);
        sem_wait(&acessoLugares);

        struct ClienteConfig cliente = dequeue(filaClientesSinglePlayer);
        if (cliente.idCliente == -1) {
            sem_post(&acessoLugares);
            continue;
        }

        sala->nClientes = 1;
        sala->jogadorAResolver = true;
        sala->clienteAtual = cliente;

        printf(COLOR_YELLOW "[Sala-%d] Atendendo cliente %d com jogo %d\n" COLOR_RESET,
               sala->idSala, sala->clienteAtual.idCliente, sala->jogo.idJogo);
        logQueEventoServidor(14, cliente.idCliente, sala->idSala);

        sleep(1);
        sem_post(&acessoLugares);

        sem_post(cliente.sinalizarVerificaSala);

        sem_wait(&sala->jogadorFinalizou);

        sala->nClientes = 0;
        memset(&sala->clienteAtual, 0, sizeof(struct ClienteConfig));
        sala->clienteAtual.idCliente = -1;
        sem_post(&sala->salaPronta);
    }
    return NULL;
}

bool verSeJogoAcabouEAtualizar(struct ClienteConfig *cliente, struct SalaSinglePlayer *sala)
{
    pthread_mutex_lock(&mutexAcabouJogo);

    if (verificaResolvido(cliente->jogoAtual.valoresCorretos, sala->jogo.solucao))
    {
        cliente->jogoAtual.resolvido = 1;
        strcpy(cliente->jogoAtual.tempoFinal, getTempoHoraMinutoSegundoMs());

        sala->jogadorAResolver = false;
        sala->nClientes = 0;
        sala->clienteAtual.idCliente = -1;

        printf("[Sala-%d] Cliente %d resolveu o jogo %d!\n",
               sala->idSala, cliente->idCliente, sala->jogo.idJogo);

        pthread_mutex_lock(&numeroJogosResolvidosMutex);
        numeroJogosResolvidos++;
        pthread_mutex_unlock(&numeroJogosResolvidosMutex);

        logQueEventoServidor(8, cliente->idCliente, sala->idSala);

        pthread_mutex_unlock(&mutexAcabouJogo);
        return true;
    }

    pthread_mutex_unlock(&mutexAcabouJogo);
    return false;
}

// ======================================================
// Multiplayer faster (mantido)
// ======================================================

bool verSeJogoAcabouEAtualizarMultiplayerFaster(struct ClienteConfig *cliente, struct SalaMultiplayer *sala)
{
    pthread_mutex_lock(&sala->winnerMutex);

    if (sala->hasWinner) {
        pthread_mutex_unlock(&sala->winnerMutex);
        return true;
    }

    if (verificaResolvido(cliente->jogoAtual.valoresCorretos, sala->jogo.solucao)) {
        cliente->jogoAtual.resolvido = 1;
        strcpy(cliente->jogoAtual.tempoFinal, getTempoHoraMinutoSegundoMs());

        sala->hasWinner = true;
        sala->winnerID = cliente->idCliente;

        printf("[Sala-%d] Cliente %d resolveu o jogo %d!\n",
               sala->idSala, cliente->idCliente, sala->jogo.idJogo);

        pthread_mutex_lock(&numeroJogosResolvidosMutex);
        numeroJogosResolvidos++;
        pthread_mutex_unlock(&numeroJogosResolvidosMutex);

        logQueEventoServidor(8, cliente->idCliente, sala->idSala);

        pthread_mutex_unlock(&sala->winnerMutex);
        return true;
    }

    pthread_mutex_unlock(&sala->winnerMutex);
    return false;
}

void limparAdicionarClienteSalaMultiplayerFaster(struct SalaMultiplayer *sala)
{
    for (int i = 0; i < sala->clientesMax; i++)
        memset(&sala->clientes[i], 0, sizeof(struct ClienteConfig));
}

void adicionarClienteSalaMultiplayerFaster(struct ServidorConfig *serverConfig, struct ClienteConfig cliente)
{
    struct SalaMultiplayer *sala = &(serverConfig->salaMultiplayer[0]);

    if (!sala->temDeEsperar)
    {
        for (int i = 0; i < sala->clientesMax; i++)
        {
            if (sala->clientes[i].idCliente == 0)
            {
                sala->clientes[i] = cliente;
                logQueEventoServidor(12, cliente.idCliente, sala->idSala);
                break;
            }
        }
    }
}

// 5 jogadores - faster
void* SalaMultiplayerFaster(void* arg)
{
    struct SalaMultiplayer* sala = (struct SalaMultiplayer*)arg;
    limparAdicionarClienteSalaMultiplayerFaster(sala);
    enum GameState state = WAITING_PLAYERS;
    char entrouSala[50];

    sala->temDeEsperar = false;
    sala->esperandoEntrar = 0;
    pthread_barrier_init(&barreiraComecaTodos, NULL, 5);

    if (sem_init(&mutexSalaMultiplayerFaster, 0, 1) ||
        sem_init(&bloquearAcessoSala, 0, 0) ||
        sem_init(&entradaPlayerMulPrimeiro, 0, 0) ||
        sem_init(&sala->sinalizarVencedor, 0, 0) ||
        sem_init(&capacidadeSalaMultiplayeFaster, 0, 5) ||
        sem_init(&ultimoClienteSairSalaMultiplayerFaster, 0, 0) ||
        sem_init(&jogoAcabouMultiplayerFaster, 0, 1))
    {
        perror("Erro ao inicializar semaforos");
        exit(1);
    }

    sala->hasWinner = false;
    sala->winnerID = -1;

    if (pthread_mutex_init(&sala->winnerMutex, NULL) != 0)
    {
        perror("Erro sala mutex");
        exit(1);
    }

    printf("[Sala-%d] Iniciada-Multiplayer-Faster\n", sala->idSala);

    while (1)
    {
        sem_wait(&entradaPlayerMulPrimeiro);

        sem_wait(&mutexSalaMultiplayerFaster);

        if (sala->temDeEsperar) {
            sala->esperandoEntrar++;
            sem_post(&mutexSalaMultiplayerFaster);
            sem_wait(&bloquearAcessoSala);
            sala->esperandoEntrar--;
        }

        sala->nClientes++;
        sala->temDeEsperar = (sala->nClientes == sala->clientesMax);

        if (sala->esperandoEntrar > 0 && !sala->temDeEsperar) sem_post(&bloquearAcessoSala);
        else sem_post(&mutexSalaMultiplayerFaster);

        switch (state)
        {
        case WAITING_PLAYERS:
        {
            printf("[Sala-%d] Jogadores na sala (%d/%d)\n", sala->idSala, sala->nClientes, sala->clientesMax);
            int faltam = sala->clientesMax - sala->nClientes;

            for (int i = 0; i < sala->nClientes; i++) {
                sprintf(entrouSala, "ENTROU_FASTER|%d", faltam);
                writeSocket(sala->clientes[i].socket, entrouSala, strlen(entrouSala));
            }

            if (sala->nClientes == 5) state = GAME_STARTING;
            else break;
        }
        case GAME_STARTING:
            printf("[Sala-%d] A iniciar jogo\n", sala->idSala);
            sem_wait(&jogoAcabouMultiplayerFaster);
            state = GAME_RUNNING;
        case GAME_RUNNING:
        {
            sem_wait(&sala->sinalizarVencedor);
            char winnerMsg[32];
            sprintf(winnerMsg, "WINNER|%d", sala->winnerID);

            printf("[Sala-%d] Vencedor ID: %d\n", sala->idSala, sala->winnerID);
            logQueEventoServidor(15, sala->winnerID, sala->idSala);

            for (int i = 0; i < sala->clientesMax; i++) {
                writeSocket(sala->clientes[i].socket, winnerMsg, strlen(winnerMsg));
            }

            sala->hasWinner = true;
            state = GAME_ENDED;
        }
        case GAME_ENDED:
            sem_post(&jogoAcabouMultiplayerFaster);
            sem_wait(&ultimoClienteSairSalaMultiplayerFaster);

            sem_wait(&mutexSalaMultiplayerFaster);

            printf("[Sala-%d] Jogo terminado e ultimo cliente saiu\n", sala->idSala);

            sala->hasWinner = false;
            sala->winnerID = -1;
            state = WAITING_PLAYERS;
            sala->temDeEsperar = false;

            limparAdicionarClienteSalaMultiplayerFaster(sala);

            if (sala->esperandoEntrar > 0) sem_post(&bloquearAcessoSala);
            else sem_post(&mutexSalaMultiplayerFaster);

            for (int i = 0; i < sala->clientesMax; i++) sem_post(&capacidadeSalaMultiplayeFaster);
            break;
        }
    }

    return NULL;
}

// ======================================================
// iniciar salas
// ======================================================

void *iniciarSalaSinglePlayer(void *arg) { return SalaSingleplayer(arg); }
void *iniciarSalaMultiplayerFaster(void *arg) { return SalaMultiplayerFaster(arg); }

void iniciarSalasJogoSinglePlayer(struct ServidorConfig *serverConfig, struct Jogo jogosEsolucoes[])
{
    if (!serverConfig || !serverConfig->sala)
    {
        perror("Server config ou salas nao inicializadas");
        exit(1);
    }

    for (int i = 0; i < serverConfig->numeroJogos; i++)
    {
        serverConfig->sala[i].idSala = i;
        serverConfig->sala[i].clientesMax = 1;
        serverConfig->sala[i].clienteMin = 1;
        serverConfig->sala[i].nClientes = 0;
        serverConfig->sala[i].jogadorAResolver = false;
        serverConfig->sala[i].clienteAtual.idCliente = -1;
        serverConfig->sala[i].jogo = jogosEsolucoes[i];

        if (pthread_mutex_init(&serverConfig->sala[i].mutexSala, NULL) != 0)
        {
            perror("Erro sala mutex");
            exit(1);
        }

        pthread_t threadSala;
        if (pthread_create(&threadSala, NULL, iniciarSalaSinglePlayer, &serverConfig->sala[i]) != 0)
        {
            perror("Erro criar tarefa sala single");
            exit(1);
        }
        pthread_detach(threadSala);
    }
}

void iniciarSalasJogoMultiplayer(struct ServidorConfig *serverConfig, struct Jogo jogosEsolucoes[])
{
    int numeroTotalSalas = serverConfig->numeroJogos;

    serverConfig->salaMultiplayer = calloc(2, sizeof(struct SalaMultiplayer));
    if (!serverConfig->salaMultiplayer)
    {
        perror("Erro ao alocar memoria para sala multiplayer");
        exit(1);
    }

    // idx 0: faster
    {
        int i = 0;
        serverConfig->salaMultiplayer[i].idSala = numeroTotalSalas + i;
        serverConfig->salaMultiplayer[i].clientesMax = 5;
        serverConfig->salaMultiplayer[i].clienteMin = 5;
        serverConfig->salaMultiplayer[i].nClientes = 0;
        serverConfig->salaMultiplayer[i].hasWinner = false;
        serverConfig->salaMultiplayer[i].winnerID = -1;

        serverConfig->salaMultiplayer[i].jogo = jogosEsolucoes[0];

        serverConfig->salaMultiplayer[i].clientes = malloc(serverConfig->salaMultiplayer[i].clientesMax * sizeof(struct ClienteConfig));
        memset(serverConfig->salaMultiplayer[i].clientes, 0, serverConfig->salaMultiplayer[i].clientesMax * sizeof(struct ClienteConfig));

        pthread_t threadSala;
        if (pthread_create(&threadSala, NULL, iniciarSalaMultiplayerFaster, &serverConfig->salaMultiplayer[i]) != 0)
        {
            perror("Erro criar sala multiplayer faster");
            exit(1);
        }
        pthread_detach(threadSala);
    }

    // idx 1: multiplayer normal (estado partilhado)
    {
        int idx = 1;
        struct SalaMultiplayer *s = &serverConfig->salaMultiplayer[idx];

        s->idSala = numeroTotalSalas + idx;
        s->clientesMax = 5;
        s->clienteMin = 1;
        s->nClientes = 0;

        s->hasWinner = false;
        s->winnerID = -1;

        s->head = 0;
        s->tail = 0;
        s->count = 0;
        s->emTurno = -1;

        s->jogo = jogosEsolucoes[0];

        strncpy(s->estadoAtual, s->jogo.jogo, NUMEROS_NO_JOGO);
        s->estadoAtual[NUMEROS_NO_JOGO] = '\0';

        strncpy(s->valoresCorretos, s->jogo.jogo, NUMEROS_NO_JOGO);
        s->valoresCorretos[NUMEROS_NO_JOGO] = '\0';

        pthread_mutex_init(&s->mtx, NULL);
        pthread_cond_init(&s->cv, NULL);

        // garantir clientes[] alocado também para o normal
        s->clientes = malloc(s->clientesMax * sizeof(struct ClienteConfig));
        memset(s->clientes, 0, s->clientesMax * sizeof(struct ClienteConfig));
    }
}

// ======================================================
// lógica principal de mensagens
// ======================================================

void receberMensagemETratarServer(char *buffer,
                                 int socketCliente,
                                 struct ClienteConfig clienteConfig,
                                 struct ServidorConfig *serverConfig)
{
    struct SalaSinglePlayer *salaAtualSIG = NULL;
    struct SalaMultiplayer *salaAtualMULFaster = NULL;
    struct SalaMultiplayer *salaAtualMUL = NULL;

    char *jogoADar = "";
    int nJogo = -1;
    int bytesRecebidos;
    int SalaID = -1;
    bool clienteDesconectado = false;

    sem_t *clientSem = malloc(sizeof(sem_t));
    if (!clientSem) { perror("Failed to allocate semaphore"); return; }
    if (sem_init(clientSem, 0, 0) != 0) { free(clientSem); perror("Failed to init sem"); return; }

    clienteConfig.sinalizarVerificaSala = clientSem;
    clienteConfig.socket = socketCliente;

    while (!clienteDesconectado && (bytesRecebidos = readSocket(socketCliente, buffer, BUF_SIZE - 1)) > 0)
    {
        buffer[bytesRecebidos] = '\0';

        // CORREÇÃO: não matar cliente por mensagens vazias
        if (so_espacos_ou_vazio(buffer)) {
            continue;
        }

        // guarda raw para log sem destruir com strtok
        char raw[BUF_SIZE];
        strncpy(raw, buffer, sizeof(raw) - 1);
        raw[sizeof(raw) - 1] = '\0';

        char bufferFinal[BUF_SIZE] = {0};
        snprintf(bufferFinal, BUF_SIZE, "[%s] Mensagem recebida: %s", getTempoHoraMinutoSegundoMs(), raw);
        logEventoServidor(bufferFinal);

        struct FormatoMensagens msgData = parseMensagem(buffer);
        if (!validarMensagemVazia(&msgData)) {
            // em vez de break, ignora e continua (senão cai tudo por um pacote mal formado)
            printf("Aviso: Mensagem inválida (ignorada)\n");
            continue;
        }

        // ======================================================
        // PEDIDO INICIAL (SEM_JOGO)
        // ======================================================
        if (strcmp(msgData.temJogo, "SEM_JOGO") == 0)
        {
            updateClientConfig(&clienteConfig, &msgData, jogoADar, nJogo, SalaID);

            // SIG
            if (strcmp(msgData.tipoJogo, "SIG") == 0)
            {
                salaAtualSIG = handleSinglePlayerFila(&clienteConfig, serverConfig);
                if (!salaAtualSIG)
                {
                    const char *filaCheia = "FILA CHEIA SINGLEPLAYER";
                    writeSocket(clienteConfig.socket, filaCheia, strlen(filaCheia));
                    clienteDesconectado = true;
                    break;
                }

                jogoADar = salaAtualSIG->jogo.jogo;
                nJogo = salaAtualSIG->jogo.idJogo;
                SalaID = salaAtualSIG->idSala;

                updateClientConfig(&clienteConfig, &msgData, jogoADar, nJogo, SalaID);
                bufferParaStructCliente(buffer, &clienteConfig);
                writeSocket(socketCliente, buffer, strlen(buffer));
                continue;
            }

            // MUL_FASTER
            if (strcmp(msgData.tipoJogo, "MUL_FASTER") == 0)
            {
                sem_post(&entradaPlayerMulPrimeiro);
                sem_wait(&capacidadeSalaMultiplayeFaster);

                sem_wait(&mutexSalaMultiplayerFaster);
                adicionarClienteSalaMultiplayerFaster(serverConfig, clienteConfig);
                sem_post(&mutexSalaMultiplayerFaster);

                salaAtualMULFaster = &serverConfig->salaMultiplayer[0];
                jogoADar = salaAtualMULFaster->jogo.jogo;
                nJogo = salaAtualMULFaster->jogo.idJogo;
                SalaID = salaAtualMULFaster->idSala;

                pthread_barrier_wait(&barreiraComecaTodos);
                sleep(1);

                updateClientConfig(&clienteConfig, &msgData, jogoADar, nJogo, SalaID);
                bufferParaStructCliente(buffer, &clienteConfig);
                writeSocket(socketCliente, buffer, strlen(buffer));
                continue;
            }

            // MUL normal
            if (strcmp(msgData.tipoJogo, "MUL") == 0)
            {
                salaAtualMUL = &serverConfig->salaMultiplayer[1];
                struct SalaMultiplayer *s = salaAtualMUL;

                pthread_mutex_lock(&s->mtx);

                if (s->nClientes >= s->clientesMax) {
                    pthread_mutex_unlock(&s->mtx);
                    const char *msg = "SALA_CHEIA";
                    writeSocket(socketCliente, msg, strlen(msg));
                    clienteDesconectado = true;
                    break;
                }

                clienteConfig.idCliente = atoi(msgData.idCliente);
                clienteConfig.socket = socketCliente;

                s->clientes[s->nClientes++] = clienteConfig;
                mp_enqueue(s, clienteConfig.idCliente);

                strcpy(clienteConfig.tipoJogo, "MUL");
                strcpy(clienteConfig.tipoResolucao, msgData.tipoResolucao);
                strcpy(clienteConfig.TemJogo, "COM_JOGO");

                clienteConfig.jogoAtual.idJogo = s->jogo.idJogo;
                clienteConfig.idSala = s->idSala;

                strncpy(clienteConfig.jogoAtual.jogo, s->estadoAtual, NUMEROS_NO_JOGO);
                clienteConfig.jogoAtual.jogo[NUMEROS_NO_JOGO] = '\0';

                strncpy(clienteConfig.jogoAtual.valoresCorretos, s->valoresCorretos, NUMEROS_NO_JOGO);
                clienteConfig.jogoAtual.valoresCorretos[NUMEROS_NO_JOGO] = '\0';

                strncpy(clienteConfig.jogoAtual.tempoInicio, getTempoHoraMinutoSegundoMs(), TEMPO_TAMANHO - 1);
                strncpy(clienteConfig.jogoAtual.tempoFinal, "0", TEMPO_TAMANHO - 1);

                clienteConfig.jogoAtual.resolvido = 0;
                clienteConfig.jogoAtual.numeroTentativas = atoi(msgData.numeroTentativas);

                bufferParaStructCliente(buffer, &clienteConfig);

                pthread_mutex_unlock(&s->mtx);

                writeSocket(socketCliente, buffer, strlen(buffer));
                continue;
            }
        }

        // ======================================================
        // COM_JOGO
        // ======================================================
        if (strcmp(msgData.temJogo, "COM_JOGO") == 0)
        {
            // SIG
            if (strcmp(msgData.tipoJogo, "SIG") == 0)
            {
                if (!salaAtualSIG) { printf("Erro: Cliente SIG sem sala atribuída\n"); break; }

                atualizarClientConfig(&clienteConfig, &msgData);

                char *logClienteEnviar = atualizaValoresCorretos(
                    clienteConfig.jogoAtual.jogo,
                    clienteConfig.jogoAtual.valoresCorretos,
                    salaAtualSIG->jogo.solucao,
                    &clienteConfig.jogoAtual.numeroTentativas
                );

                bool gameCompleted = verSeJogoAcabouEAtualizar(&clienteConfig, salaAtualSIG);

                memset(buffer, 0, BUF_SIZE);
                prepararRespostaJogo(buffer, &clienteConfig, logClienteEnviar);
                writeSocket(socketCliente, buffer, strlen(buffer));

                logQueEventoServidor(6, clienteConfig.idCliente, salaAtualSIG->idSala);

                free(logClienteEnviar);

                if (gameCompleted) break;
                continue;
            }

            // MUL_FASTER
            if (strcmp(msgData.tipoJogo, "MUL_FASTER") == 0)
            {
                if (!salaAtualMULFaster) { printf("Erro: Cliente sem sala atribuída\n"); break; }

                atualizarClientConfig(&clienteConfig, &msgData);

                char *logClienteEnviar = atualizaValoresCorretos(
                    clienteConfig.jogoAtual.jogo,
                    clienteConfig.jogoAtual.valoresCorretos,
                    salaAtualMULFaster->jogo.solucao,
                    &clienteConfig.jogoAtual.numeroTentativas
                );

                if (salaAtualMULFaster->hasWinner) {
                    free(logClienteEnviar);
                    continue;
                }

                bool gameCompleted = false;
                sem_wait(&mutexSalaMultiplayerFaster);
                gameCompleted = verSeJogoAcabouEAtualizarMultiplayerFaster(&clienteConfig, salaAtualMULFaster);
                sem_post(&mutexSalaMultiplayerFaster);

                memset(buffer, 0, BUF_SIZE);
                prepararRespostaJogo(buffer, &clienteConfig, logClienteEnviar);
                writeSocket(socketCliente, buffer, strlen(buffer));

                logQueEventoServidor(6, clienteConfig.idCliente, salaAtualMULFaster->idSala);

                free(logClienteEnviar);

                if (gameCompleted) sem_post(&salaAtualMULFaster->sinalizarVencedor);
                continue;
            }

            // MUL normal (fila + turno)
            if (strcmp(msgData.tipoJogo, "MUL") == 0)
            {
                if (!salaAtualMUL) salaAtualMUL = &serverConfig->salaMultiplayer[1];
                struct SalaMultiplayer *s = salaAtualMUL;

                int id = atoi(msgData.idCliente);

                pthread_mutex_lock(&s->mtx);

                mp_wait_turn(s, id);

                if (s->hasWinner) {
                    int w = s->winnerID;
                    pthread_mutex_unlock(&s->mtx);

                    char wmsg[32];
                    snprintf(wmsg, sizeof(wmsg), "WINNER|%d", w);
                    writeSocket(socketCliente, wmsg, strlen(wmsg));
                    break;
                }

                int dummyTent = 0;
                char *logClienteEnviar = atualizaValoresCorretos(
                    msgData.jogo,
                    s->valoresCorretos,
                    s->jogo.solucao,
                    &dummyTent
                );

                strncpy(s->estadoAtual, s->valoresCorretos, NUMEROS_NO_JOGO);
                s->estadoAtual[NUMEROS_NO_JOGO] = '\0';

                if (verificaResolvido(s->valoresCorretos, s->jogo.solucao)) {
                    s->hasWinner = true;
                    s->winnerID = id;
                    logQueEventoServidor(15, id, s->idSala);

                    pthread_mutex_lock(&numeroJogosResolvidosMutex);
                    numeroJogosResolvidos++;
                    pthread_mutex_unlock(&numeroJogosResolvidosMutex);
                }

                mp_rotate_after_play(s);

                strcpy(clienteConfig.tipoJogo, "MUL");
                strcpy(clienteConfig.TemJogo, "COM_JOGO");
                clienteConfig.idCliente = id;
                clienteConfig.idSala = s->idSala;
                clienteConfig.jogoAtual.idJogo = s->jogo.idJogo;

                strncpy(clienteConfig.jogoAtual.jogo, s->estadoAtual, NUMEROS_NO_JOGO);
                clienteConfig.jogoAtual.jogo[NUMEROS_NO_JOGO] = '\0';

                strncpy(clienteConfig.jogoAtual.valoresCorretos, s->valoresCorretos, NUMEROS_NO_JOGO);
                clienteConfig.jogoAtual.valoresCorretos[NUMEROS_NO_JOGO] = '\0';

                if (s->hasWinner) {
                    clienteConfig.jogoAtual.resolvido = 1;
                    strncpy(clienteConfig.jogoAtual.tempoFinal, getTempoHoraMinutoSegundoMs(), TEMPO_TAMANHO - 1);
                }

                memset(buffer, 0, BUF_SIZE);
                prepararRespostaJogo(buffer, &clienteConfig, logClienteEnviar);

                pthread_mutex_unlock(&s->mtx);

                writeSocket(socketCliente, buffer, strlen(buffer));

                free(logClienteEnviar);

                if (s->hasWinner) {
                    char wmsg[32];
                    snprintf(wmsg, sizeof(wmsg), "WINNER|%d", s->winnerID);
                    writeSocket(socketCliente, wmsg, strlen(wmsg));
                    break;
                }

                continue;
            }
        }
    }

    if (bytesRecebidos <= 0 && !clienteDesconectado) {
        pthread_mutex_lock(&clientesDesistiramMutex);
        clientesDesistiram++;
        pthread_mutex_unlock(&clientesDesistiramMutex);
    }

    // SIG cleanup
    if (salaAtualSIG)
    {
        pthread_mutex_lock(&salaAtualSIG->mutexSala);
        sem_post(&salaAtualSIG->jogadorFinalizou);

        if (salaAtualSIG->jogadorAResolver) {
            printf("Cliente %d não resolveu o jogo\n", clienteConfig.idCliente);
            logQueEventoServidor(13, clienteConfig.idCliente, salaAtualSIG->idSala);
        }

        salaAtualSIG->jogadorAResolver = false;
        salaAtualSIG->clienteAtual.idCliente = -1;
        salaAtualSIG->nClientes = 0;

        sem_wait(&salaAtualSIG->salaPronta);
        pthread_mutex_unlock(&salaAtualSIG->mutexSala);
    }

    // MUL_FASTER cleanup
    if (salaAtualMULFaster)
    {
        sem_wait(&jogoAcabouMultiplayerFaster);
        salaAtualMULFaster->nClientes--;

        if (salaAtualMULFaster->nClientes == 0) sem_post(&ultimoClienteSairSalaMultiplayerFaster);

        sem_post(&jogoAcabouMultiplayerFaster);
    }

    // MUL normal cleanup
    if (salaAtualMUL)
    {
        pthread_mutex_lock(&salaAtualMUL->mtx);
        mp_remove_player(salaAtualMUL, clienteConfig.idCliente);
        pthread_mutex_unlock(&salaAtualMUL->mtx);
    }

    pthread_mutex_lock(&clientesQueSairamMutex);
    clientesQueSairam++;
    pthread_mutex_unlock(&clientesQueSairamMutex);

    pthread_mutex_lock(&clientesAtuaisMutex);
    clientesAtuais--;
    pthread_mutex_unlock(&clientesAtuaisMutex);

    printf(COLOR_RED "Cliente %d saiu\n" COLOR_RESET, clienteConfig.idCliente);
    logQueEventoServidor(7, clienteConfig.idCliente, salaAtualSIG ? salaAtualSIG->idSala : 0);

    sem_destroy(clientSem);
    free(clientSem);
}

// ======================================================
// threads clientes (CORRIGIDA: mutex só para gerar ID)
// ======================================================

void *criaClienteThread(void *arg)
{
    struct ThreadCliente *args = (struct ThreadCliente *)arg;
    int socketCliente = args->socketCliente;

    // atribui ID rápido e liberta mutex logo
    pthread_mutex_lock(&mutexClienteID);
    idCliente++;
    int clientID = idCliente;
    pthread_mutex_unlock(&mutexClienteID);

    args->clienteId = clientID;

    char temp[BUF_SIZE] = {0};
    int bytesRecebidos;

    while ((bytesRecebidos = readSocket(socketCliente, temp, BUF_SIZE - 1)) > 0)
    {
        temp[bytesRecebidos] = '\0';

        if (so_espacos_ou_vazio(temp)) {
            memset(temp, 0, sizeof(temp));
            continue;
        }

        if (strstr(temp, "MANDA_ID") != NULL)
        {
            char out[32];
            snprintf(out, sizeof(out), "%d|", clientID);

            if (writeSocket(socketCliente, out, strlen(out)) < 0)
            {
                perror("Erro ao enviar ID");
                close(socketCliente);
                free(args);
                return NULL;
            }

            logQueEventoServidor(3, clientID, 0);
            break;
        }

        memset(temp, 0, sizeof(temp));
    }

    if (bytesRecebidos <= 0)
    {
        close(socketCliente);
        free(args);
        return NULL;
    }

    struct ClienteConfig clienteConfig = {0};
    receberMensagemETratarServer(temp, socketCliente, clienteConfig, args->server);

    close(socketCliente);
    free(args);
    return NULL;
}

// ======================================================
// socket server (accept sem mutex)
// ======================================================

void iniciarServidorSocket(struct ServidorConfig *server, struct Jogo jogosEsolucoes[])
{
    int socketServidor = socket(server->dominio, server->servico, server->protocolo);
    if (socketServidor == -1) { perror("Erro ao criar socket"); exit(1); }

    int opt = 1;
    if (setsockopt(socketServidor, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt failed");
        exit(1);
    }

    struct sockaddr_in enderecoServidor;
    enderecoServidor.sin_family = server->dominio;
    enderecoServidor.sin_port = htons(server->porta);
    enderecoServidor.sin_addr.s_addr = server->interface;

    if (bind(socketServidor, (struct sockaddr *)&enderecoServidor, sizeof(enderecoServidor)) == -1)
    {
        perror("Erro ao fazer bind");
        exit(1);
    }

    if (listen(socketServidor, server->backlog) == -1)
    {
        perror("Erro ao fazer listen");
        exit(1);
    }

    printf("==== Servidor Iniciado ====\n");
    printf("====== Porta: %d =======\n\n", server->porta);

    iniciarSalasJogoSinglePlayer(server, jogosEsolucoes);
    iniciarSalasJogoMultiplayer(server, jogosEsolucoes);

    while (1)
    {
        struct sockaddr_in enderecoCliente;
        int tamanhoEndereco = sizeof(enderecoCliente);

        int socketCliente = accept(socketServidor, (struct sockaddr *)&enderecoCliente, (socklen_t *)&tamanhoEndereco);
        if (socketCliente == -1)
        {
            perror("Erro ao aceitar conexão");
            continue;
        }

        pthread_mutex_lock(&clientesAtuaisMutex);
        clientesAtuais++;
        pthread_mutex_unlock(&clientesAtuaisMutex);

        struct ThreadCliente *args = malloc(sizeof(struct ThreadCliente));
        if (!args)
        {
            perror("Erro ao alocar memória para args-thread");
            close(socketCliente);
            continue;
        }
        memset(args, 0, sizeof(struct ThreadCliente));

        args->socketCliente = socketCliente;
        args->server = server;

        pthread_t thread;
        if (pthread_create(&thread, NULL, criaClienteThread, args) != 0)
        {
            perror("Failed to create thread");
            free(args);
            close(socketCliente);
            continue;
        }
        pthread_detach(thread);
    }

    close(socketServidor);
}

// ======================================================
// info server
// ======================================================

void* informacoesServidor(void* arg)
{
    (void)arg;
    sleep(5);
    while (1)
    {
        pthread_mutex_lock(&clientesAtuaisMutex);
        int ca = clientesAtuais;
        pthread_mutex_unlock(&clientesAtuaisMutex);

        pthread_mutex_lock(&clientesRejeitadosMutex);
        int cr = clientesRejeitados;
        pthread_mutex_unlock(&clientesRejeitadosMutex);

        pthread_mutex_lock(&clientesDesistiramMutex);
        int cd = clientesDesistiram;
        pthread_mutex_unlock(&clientesDesistiramMutex);

        pthread_mutex_lock(&numeroJogosResolvidosMutex);
        int nj = numeroJogosResolvidos;
        pthread_mutex_unlock(&numeroJogosResolvidosMutex);

        pthread_mutex_lock(&clientesQueSairamMutex);
        int cs = clientesQueSairam;
        pthread_mutex_unlock(&clientesQueSairamMutex);

        printf("\n[Sistema] Número de clientes atuais: %d\n", ca);
        printf("[Sistema] Número de clientes rejeitados: %d\n", cr);
        printf("[Sistema] Número de clientes que desistiram: %d\n", cd);
        printf("[Sistema] Número de jogos resolvidos: %d\n", nj);
        printf("[Sistema] Número de clientes que sairam: %d\n\n", cs);
        sleep(10);
    }
    return NULL;
}

// ======================================================
// main
// ======================================================

int main(int argc, char **argv)
{
    struct ServidorConfig serverConfig = {0};

    sem_init(&acessoLugares, 0, 1);

    if (argc < 2)
    {
        printf("Erro: Nome do ficheiro nao fornecido.\n");
        return 1;
    }

    if (strcmp(argv[1], CONFIGFILE) != 0)
    {
        printf("Nome do ficheiro incorreto\n");
        return 1;
    }

    pthread_t threadInformacoesServidor;
    pthread_create(&threadInformacoesServidor, NULL, informacoesServidor, &serverConfig);

    carregarConfigServidor(argv[1], &serverConfig);

    int numeroJogos = lerNumeroJogos(serverConfig.ficheiroJogosESolucoesCaminho);
    struct Jogo jogosEsolucoes[numeroJogos];
    serverConfig.numeroJogos = numeroJogos;

    carregarFicheiroJogosSolucoes(serverConfig.ficheiroJogosESolucoesCaminho, jogosEsolucoes);

    construtorServer(&serverConfig, AF_INET, SOCK_STREAM, 0, INADDR_ANY, serverConfig.porta, 5000, serverConfig.ficheiroJogosESolucoesCaminho);

    logQueEventoServidor(1, 0, 0);

    filaClientesSinglePlayer = criarFila(&serverConfig);
    if (!filaClientesSinglePlayer) {
        perror("Erro ao criar fila singleplayer");
        return 1;
    }

    iniciarServidorSocket(&serverConfig, jogosEsolucoes);

    free(filaClientesSinglePlayer->cliente);
    free(filaClientesSinglePlayer);
    return 0;
}
