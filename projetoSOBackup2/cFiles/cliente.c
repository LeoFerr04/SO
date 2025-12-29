#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700

#include "../headers/cliente.h"
#include <stdint.h>     // uintptr_t
#include <unistd.h>     // usleep

// Aceita cliente1.conf até clienteN.conf
char *padrao = "./configs/cliente";

pthread_mutex_t mutexClienteLog = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutexSTDOUT     = PTHREAD_MUTEX_INITIALIZER;

/* =========================================================
   Helpers
   ========================================================= */

static int startsWith(const char *s, const char *prefix)
{
    if (!s || !prefix) return 0;
    size_t n = strlen(prefix);
    return strncmp(s, prefix, n) == 0;
}

/* =========================================================
   Config Cliente
   ========================================================= */

void carregarConfigCliente(char *nomeFicheiro, struct ClienteConfig *clienteConfig)
{
    FILE *config = abrirFicheiroRead(nomeFicheiro);
    if (config == NULL) {
        fprintf(stderr, "Erro: Falha ao abrir o ficheiro de configuração.\n");
        exit(1);
    }

    char buffer[BUF_SIZE];
    int linhaAtual = 0;

    while (fgets(buffer, BUF_SIZE, config) != NULL) {
        char *valor = strtok(buffer, "\n");
        if (valor == NULL) {
            perror("Erro: Linha vazia encontrada.\n");
            fecharFicheiro(config);
            exit(1);
        }

        switch (linhaAtual) {
            case 0: // tipoJogo
                strncpy(clienteConfig->tipoJogo, valor, INFO_SIZE - 1);
                clienteConfig->tipoJogo[INFO_SIZE - 1] = '\0';
                break;

            case 1: // tipoResolucao
                strncpy(clienteConfig->tipoResolucao, valor, INFO_SIZE - 1);
                clienteConfig->tipoResolucao[INFO_SIZE - 1] = '\0';
                break;

            case 2: // ipServidor
                strncpy(clienteConfig->ipServidor, valor, IP_SIZE - 1);
                clienteConfig->ipServidor[IP_SIZE - 1] = '\0';
                break;

            case 3: // porta
                clienteConfig->porta = atoi(valor);
                break;

            case 4: // numJogadoresASimular
                clienteConfig->numJogadoresASimular = atoi(valor);
                break;

            case 5: // tempoEntreTentativas
                clienteConfig->tempoEntreTentativas = atoi(valor);
                break;

            default:
                perror("Erro ao ler ficheiro de configuração.\n");
                fecharFicheiro(config);
                exit(1);
        }

        linhaAtual++;
    }

    if (linhaAtual < 4) {
        perror("Erro: Configuração incompleta\n");
        fecharFicheiro(config);
        exit(1);
    }

    fecharFicheiro(config);
}

/* =========================================================
   UI / Tabuleiro
   ========================================================= */

void imprimirTabuleiro(char *jogo)
{
    for (int i = 0; i < NUM_LINHAS; i++)
    {
        if (i % 3 == 0 && i != 0) printf("---------------------\n");
        for (int j = 0; j < NUM_LINHAS; j++)
        {
            if (j % 3 == 0 && j != 0) printf(" | ");
            printf("%c ", jogo[i * NUM_LINHAS + j]);
        }
        printf("\n");
    }
    printf("\n");
}

/* =========================================================
   Logs
   ========================================================= */

void logEventoCliente(const char *message, struct ClienteConfig *clienteConfig)
{
    char *str = "logs/clienteLog.txt";
    pthread_mutex_lock(&mutexClienteLog);

    FILE *file = fopen(str, "a");
    if (file == NULL)
    {
        perror("Erro ao abrir o ficheiro de log");
        pthread_mutex_unlock(&mutexClienteLog);
        return;
    }

    fprintf(file, "[Cliente ID: %u] %s\n", clienteConfig->idCliente, message);
    fclose(file);

    pthread_mutex_unlock(&mutexClienteLog);
}

void logQueEventoCliente(int numero, struct ClienteConfig clienteConfig)
{
    switch (numero)
    {
        case 1:  logEventoCliente("Cliente comecou o programa", &clienteConfig); break;
        case 3:  logEventoCliente("Cliente conectou-se ao servidor", &clienteConfig); break;
        case 4:  logEventoCliente("Cliente enviou uma mensagem ao servidor", &clienteConfig); break;
        case 5:  logEventoCliente("Cliente recebeu uma resposta do servidor", &clienteConfig); break;
        case 6:  logEventoCliente("Cliente desconectou-se do servidor", &clienteConfig); break;
        case 7:  logEventoCliente("[ERRO]", &clienteConfig); break;
        case 8:  logEventoCliente("Cliente resolveu jogo", &clienteConfig); break;
        case 9:  logEventoCliente("Fila singleplayer cheia, jogador saiu", &clienteConfig); break;
        case 10: logEventoCliente("Cliente desistiu de resolver o jogo", &clienteConfig); break;
        default: logEventoCliente("Evento desconhecido", &clienteConfig); break;
    }
}

/* =========================================================
   Construtor + Socket
   ========================================================= */

void construtorCliente(int dominio, unsigned int porta, __u_long interface, struct ClienteConfig *clienteConfig)
{
    strcpy(clienteConfig->TemJogo, "SEM_JOGO");
    clienteConfig->jogoAtual.resolvido = false;
    clienteConfig->jogoAtual.numeroTentativas = 0;
    clienteConfig->jogoAtual.idJogo = 0;
    clienteConfig->idSala = -1;

    memset(clienteConfig->jogoAtual.jogo, '0', NUMEROS_NO_JOGO);
    clienteConfig->jogoAtual.jogo[NUMEROS_NO_JOGO] = '\0';

    memset(clienteConfig->jogoAtual.valoresCorretos, '0', NUMEROS_NO_JOGO);
    clienteConfig->jogoAtual.valoresCorretos[NUMEROS_NO_JOGO] = '\0';

    strcpy(clienteConfig->jogoAtual.tempoInicio, "0");
    strcpy(clienteConfig->jogoAtual.tempoFinal, "0");

    clienteConfig->dominio = dominio;
    clienteConfig->porta   = porta;
    clienteConfig->interface = interface;
}

void iniciarClienteSocket(struct ClienteConfig *clienteConfig)
{
    clienteConfig->socket = socket(clienteConfig->dominio, SOCK_STREAM, 0);
    if (clienteConfig->socket == -1) {
        perror("Erro ao criar socket");
        exit(1);
    }

    struct sockaddr_in enderecoServidor;
    enderecoServidor.sin_family = clienteConfig->dominio;
    enderecoServidor.sin_port   = htons(clienteConfig->porta);

    if (inet_pton(clienteConfig->dominio, clienteConfig->ipServidor, &enderecoServidor.sin_addr) <= 0)
    {
        perror("Erro ao converter IP do servidor");
        close(clienteConfig->socket);
        exit(1);
    }

    logQueEventoCliente(1, *clienteConfig);

    int umaVez = 1;
    while (connect(clienteConfig->socket, (struct sockaddr *)&enderecoServidor, sizeof(enderecoServidor)) == -1)
    {
        if (umaVez) {
            perror("Erro ao conectar ao servidor");
            printf("Tentando conectar ao servidor...\n");
            umaVez = 0;
        }
        usleep(200 * 1000);
    }

    logQueEventoCliente(3, *clienteConfig);

    // pedir ID
    char *mandaID = "MANDA_ID";
    if (writeSocket(clienteConfig->socket, mandaID, strlen(mandaID)) < 0) {
        perror("Erro ao enviar MANDA_ID");
        logQueEventoCliente(7, *clienteConfig);
        close(clienteConfig->socket);
        return;
    }

    char recebeIDCliente[BUF_SIZE] = {0};
    ssize_t r = readSocket(clienteConfig->socket, recebeIDCliente, BUF_SIZE - 1);
    if (r <= 0) {
        perror("Erro a receber ID do servidor");
        logQueEventoCliente(7, *clienteConfig);
        close(clienteConfig->socket);
        return;
    }
    recebeIDCliente[r] = '\0';

    if (strstr(recebeIDCliente, "|") != NULL) {
        clienteConfig->idCliente = atoi(strtok(recebeIDCliente, "|"));
    } else {
        clienteConfig->idCliente = atoi(recebeIDCliente);
    }

    // loop principal
    mandarETratarMSG(clienteConfig);

    close(clienteConfig->socket);
}

/* =========================================================
   Tentativas
   ========================================================= */

void tentarSolucaoParcial(char tentativaAtual[], char valoresCorretos[])
{
    for (int i = 0; i < (int)strlen(tentativaAtual); i++)
    {
        if ((tentativaAtual[i] != '0') && (tentativaAtual[i] != valoresCorretos[i]))
        {
            char numero = tentativaAtual[i];
            tentativaAtual[i] = (char)((int)numero + 1);
            break;
        }
        else if (tentativaAtual[i] == '0')
        {
            tentativaAtual[i] = '1';
            break;
        }
    }
}

void tentarSolucaoCompleta(char tentativaAtual[], char valoresCorretos[])
{
    for (int i = 0; i < (int)strlen(tentativaAtual); i++)
    {
        if ((tentativaAtual[i] != '0') && (tentativaAtual[i] != valoresCorretos[i]))
        {
            char numero = tentativaAtual[i];
            tentativaAtual[i] = (char)((int)numero + 1);
        }
        else if (tentativaAtual[i] == '0')
        {
            tentativaAtual[i] = '1';
        }
    }
}

/* =========================================================
   Protocolo: format / parse
   ========================================================= */

void formatarMensagemJogo(char *buffer, const struct ClienteConfig *clienteConfig)
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

bool parseMensagemJogo(const char *buffer, struct ClienteConfig *clienteConfig)
{
    char *temp = strdup(buffer);
    if (!temp) return false;

    char *idCliente = strtok(temp, "|");
    char *tipoJogo = strtok(NULL, "|");
    char *tipoResolucao = strtok(NULL, "|");
    char *temJogo = strtok(NULL, "|");
    char *idJogo = strtok(NULL, "|");
    char *idSala = strtok(NULL, "|");
    char *jogo = strtok(NULL, "|");
    char *valoresCorretos = strtok(NULL, "|");
    char *tempoInicio = strtok(NULL, "|");
    char *tempoFinal = strtok(NULL, "|");
    char *resolvido = strtok(NULL, "|");
    char *numeroTentativas = strtok(NULL, "|");

    bool ok = (idCliente && tipoJogo && tipoResolucao && temJogo &&
               idJogo && idSala && jogo && valoresCorretos &&
               tempoInicio && tempoFinal && resolvido && numeroTentativas);

    if (ok) {
        clienteConfig->idCliente = atoi(idCliente);
        (void)tipoJogo;
        (void)tipoResolucao;

        strcpy(clienteConfig->TemJogo, temJogo);
        clienteConfig->jogoAtual.idJogo = atoi(idJogo);
        clienteConfig->idSala = atoi(idSala);

        strncpy(clienteConfig->jogoAtual.jogo, jogo, NUMEROS_NO_JOGO);
        clienteConfig->jogoAtual.jogo[NUMEROS_NO_JOGO] = '\0';

        strncpy(clienteConfig->jogoAtual.valoresCorretos, valoresCorretos, NUMEROS_NO_JOGO);
        clienteConfig->jogoAtual.valoresCorretos[NUMEROS_NO_JOGO] = '\0';

        strncpy(clienteConfig->jogoAtual.tempoInicio, tempoInicio, TEMPO_TAMANHO - 1);
        clienteConfig->jogoAtual.tempoInicio[TEMPO_TAMANHO - 1] = '\0';

        strncpy(clienteConfig->jogoAtual.tempoFinal, tempoFinal, TEMPO_TAMANHO - 1);
        clienteConfig->jogoAtual.tempoFinal[TEMPO_TAMANHO - 1] = '\0';

        clienteConfig->jogoAtual.resolvido = atoi(resolvido);
        clienteConfig->jogoAtual.numeroTentativas = atoi(numeroTentativas);
    }

    free(temp);
    return ok;
}

/* =========================================================
   Envio
   ========================================================= */

bool enviarPedidoJogo(struct ClienteConfig *clienteConfig)
{
    char buffer[BUF_SIZE] = {0};
    formatarMensagemJogo(buffer, clienteConfig);

    if (writeSocket(clienteConfig->socket, buffer, BUF_SIZE) < 0) {
        perror("Erro ao enviar pedido");
        logQueEventoCliente(7, *clienteConfig);
        return false;
    }

    char logbuf[2 * BUF_SIZE];
    snprintf(logbuf, sizeof(logbuf), "[%s] Mensagem enviada: %s", getTempoHoraMinutoSegundoMs(), buffer);
    logEventoCliente(logbuf, clienteConfig);
    return true;
}

bool enviarTentativa(struct ClienteConfig *clienteConfig)
{
    char buffer[BUF_SIZE] = {0};
    formatarMensagemJogo(buffer, clienteConfig);

    if (writeSocket(clienteConfig->socket, buffer, BUF_SIZE) < 0) {
        perror("Erro ao enviar tentativa");
        logQueEventoCliente(7, *clienteConfig);
        return false;
    }

    char logbuf[2 * BUF_SIZE];
    snprintf(logbuf, sizeof(logbuf), "[%s] Mensagem enviada: %s", getTempoHoraMinutoSegundoMs(), buffer);
    logEventoCliente(logbuf, clienteConfig);
    return true;
}

/* =========================================================
   Lógica do cliente
   ========================================================= */

static void atualizarTentativa(struct ClienteConfig *clienteConfig)
{
    if (strcmp(clienteConfig->tipoResolucao, "COMPLET") == 0) {
        tentarSolucaoCompleta(clienteConfig->jogoAtual.jogo, clienteConfig->jogoAtual.valoresCorretos);
        clienteConfig->jogoAtual.numeroTentativas++;
    }
    else if (strcmp(clienteConfig->tipoResolucao, "PARCIAL") == 0) {
        tentarSolucaoParcial(clienteConfig->jogoAtual.jogo, clienteConfig->jogoAtual.valoresCorretos);
        clienteConfig->jogoAtual.numeroTentativas++;
    }
}

static bool processarEstadoJogo(struct ClienteConfig *clienteConfig)
{
    pthread_mutex_lock(&mutexSTDOUT);
    printf("Cliente ID:%d\n", clienteConfig->idCliente);
    printf("Sala ID:%d\n", clienteConfig->idSala);
    printf("\nTentativa %d:\n\n", clienteConfig->jogoAtual.numeroTentativas);
    imprimirTabuleiro(clienteConfig->jogoAtual.jogo);
    pthread_mutex_unlock(&mutexSTDOUT);

    atualizarTentativa(clienteConfig);

    struct timespec tempo = {
        .tv_sec  = clienteConfig->tempoEntreTentativas / 1000,
        .tv_nsec = (clienteConfig->tempoEntreTentativas % 1000) * 1000000
    };
    nanosleep(&tempo, NULL);

    return enviarTentativa(clienteConfig);
}

static void imprimirResultadoFinal(struct ClienteConfig *clienteConfig)
{
    pthread_mutex_lock(&mutexSTDOUT);
    printf("Cliente ID:%d\n", clienteConfig->idCliente);
    printf("Tentativa: %d\n\n", clienteConfig->jogoAtual.numeroTentativas);
    imprimirTabuleiro(clienteConfig->jogoAtual.jogo);
    printf("Jogo resolvido!\n");
    printf("Resolvido em %d tentativas\n", clienteConfig->jogoAtual.numeroTentativas);
    printf("Hora de inicio: %s\n", clienteConfig->jogoAtual.tempoInicio);
    printf("Hora de fim: %s\n", clienteConfig->jogoAtual.tempoFinal);
    pthread_mutex_unlock(&mutexSTDOUT);
}

bool desistirDeResolver(void)
{
    unsigned int seed = (unsigned int)(time(NULL) ^ (uintptr_t)pthread_self());
    int r = rand_r(&seed) % 100;
    return (r < 2);
}

void mandarETratarMSG(struct ClienteConfig *clienteConfig)
{
    char buffer[BUF_SIZE];
    ssize_t bytesRead;
    int jogadoresEmFalta = 0;

    if (!enviarPedidoJogo(clienteConfig)) return;

    while ((bytesRead = readSocket(clienteConfig->socket, buffer, BUF_SIZE - 1)) > 0)
    {
        buffer[bytesRead] = '\0';

        if (strcmp(buffer, "FILA CHEIA SINGLEPLAYER") == 0) {
            pthread_mutex_lock(&mutexSTDOUT);
            printf("Cliente ID:%d\n", clienteConfig->idCliente);
            printf("Fila singleplayer está cheia\n");
            pthread_mutex_unlock(&mutexSTDOUT);
            logQueEventoCliente(9, *clienteConfig);
            break;
        }

        if (startsWith(buffer, "ENTROU_FASTER|")) {
            pthread_mutex_lock(&mutexSTDOUT);
            sscanf(buffer, "ENTROU_FASTER|%d", &jogadoresEmFalta);
            printf("\nCliente ID: %d\n", clienteConfig->idCliente);
            if (jogadoresEmFalta == 0) printf("Começando jogo...\n\n");
            else printf("Aguardando restantes %d jogadores\n\n", jogadoresEmFalta);
            pthread_mutex_unlock(&mutexSTDOUT);
            continue;
        }

        if (startsWith(buffer, "WINNER|")) {
            int winnerID;
            sscanf(buffer, "WINNER|%d", &winnerID);

            pthread_mutex_lock(&mutexSTDOUT);
            printf("Cliente ID:%d\n", clienteConfig->idCliente);
            if (winnerID == clienteConfig->idCliente) printf("\nVocê venceu o jogo!\n\n");
            else printf("\nVocê perdeu o jogo!\n\n");
            pthread_mutex_unlock(&mutexSTDOUT);
            break;
        }

        if (strcmp(buffer, "JOGO_CANCELADO") == 0) {
            pthread_mutex_lock(&mutexSTDOUT);
            printf("Cliente ID: %d\n", clienteConfig->idCliente);
            printf("Jogo multiplayer faster cancelado\n");
            pthread_mutex_unlock(&mutexSTDOUT);
            break;
        }

        if (!parseMensagemJogo(buffer, clienteConfig)) {
            continue;
        }

        char logbuf[2 * BUF_SIZE];
        snprintf(logbuf, sizeof(logbuf), "[%s] Mensagem recebida: %s", getTempoHoraMinutoSegundoMs(), buffer);
        logEventoCliente(logbuf, clienteConfig);

        if (strcmp(clienteConfig->TemJogo, "COM_JOGO") == 0)
        {
            if (desistirDeResolver()) {
                pthread_mutex_lock(&mutexSTDOUT);
                printf("Cliente ID:%d\n", clienteConfig->idCliente);
                printf("Desistiu de resolver o jogo\n");
                pthread_mutex_unlock(&mutexSTDOUT);
                logQueEventoCliente(10, *clienteConfig);
                break;
            }

            if (!clienteConfig->jogoAtual.resolvido) {
                if (!processarEstadoJogo(clienteConfig)) break;
            } else {
                imprimirResultadoFinal(clienteConfig);
                logQueEventoCliente(8, *clienteConfig);
                break;
            }
        }
    }

    if (bytesRead == 0) logQueEventoCliente(6, *clienteConfig);
    else if (bytesRead < 0) logQueEventoCliente(7, *clienteConfig);
}

/* =========================================================
   Thread jogador
   ========================================================= */

static void* jogadorThread(void* arg)
{
    struct ClienteConfig* config = (struct ClienteConfig*)arg;

    construtorCliente(AF_INET, config->porta, INADDR_ANY, config);
    iniciarClienteSocket(config);

    pthread_exit(NULL);
}

/* =========================================================
   MAIN
   ========================================================= */

int main(int argc, char **argv)
{
    struct ClienteConfig clienteConfig = {0};

    if (argc < 2) {
        printf("Erro: Nome do ficheiro de configuracao nao fornecido.\n");
        return 1;
    }

    if (!validarNomeFile(argv[1], padrao)) {
        printf("Nome do ficheiro de configuracao incorreto: %s\n", argv[1]);
        return 1;
    }

    carregarConfigCliente(argv[1], &clienteConfig);

    int numJogadores = clienteConfig.numJogadoresASimular;

    pthread_t* threads = calloc(numJogadores, sizeof(pthread_t));
    struct ClienteConfig* configsJogadores = calloc(numJogadores, sizeof(struct ClienteConfig));

    if (!threads || !configsJogadores) {
        perror("Erro na alocação de memória");
        free(threads);
        free(configsJogadores);
        return 1;
    }

    srand((unsigned int)time(NULL));

    for (int i = 0; i < numJogadores; i++)
    {
        configsJogadores[i] = clienteConfig;
        configsJogadores[i].idCliente = 0; // servidor dá ID real

        int result = pthread_create(&threads[i], NULL, jogadorThread, &configsJogadores[i]);

        usleep((rand() % 9501) + 5000);

        if (result != 0) {
            fprintf(stderr, "Erro ao criar thread: %s\n", strerror(result));
            for (int j = 0; j < i; j++) pthread_join(threads[j], NULL);
            free(threads);
            free(configsJogadores);
            return 1;
        }
    }

    for (int i = 0; i < numJogadores; i++) {
        pthread_join(threads[i], NULL);
        printf("Jogador terminou. (%d/%d)\n", i + 1, numJogadores);
    }

    free(threads);
    free(configsJogadores);

    printf("Todos os jogadores terminaram.\n");
    return 0;
}
