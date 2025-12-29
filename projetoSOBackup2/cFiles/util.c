#define _XOPEN_SOURCE 700
#include "../headers/util.h"

int fecharFicheiro(FILE *file)
{
    if (file == NULL)
    {
        printf("Nenhum arquivo foi aberto.\n");
        return 1;
    }

    if (fclose(file) == EOF)
    {
        printf("Erro ao fechar o arquivo: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}

FILE *abrirFicheiroRead(char *filename)
{
    FILE *conf = fopen(filename, "r");
    if (conf == NULL)
    {
        printf("Ocorreu um erro na abertura do ficheiro: %s\n", strerror(errno));
        exit(1);
    }
    return conf;
}

int validarNomeFile(char *arquivoNome, char *padrao)
{
    regex_t regex;

    if (regcomp(&regex, padrao, REG_EXTENDED) != 0)
    {
        printf("Erro ao compilar a expressão regular.\n");
        return 0;
    }

    int resultado = regexec(&regex, arquivoNome, 0, NULL, 0);
    regfree(&regex);

    return (resultado == 0);
}

const char *getTempo()
{
    static char buffer[TEMPO_TAMANHO];
    time_t now = time(NULL);
    strftime(buffer, sizeof(buffer) - 1, "%d-%m-%Y %H:%M:%S", localtime(&now));
    return buffer;
}

const char *getTempoHoraMinutoSegundoMs()
{
    static char buffer[TEMPO_TAMANHO];
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm *tm_info = localtime(&ts.tv_sec);
    snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03ld",
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
             ts.tv_nsec / 1000000);

    return buffer;
}

/*
 * Parse "HH:MM:SS" (ou "HH:MM"), ignora ".mmm" se existir.
 */
time_t converterTempoStringParaTimeT(const char *tempo)
{
    if (!tempo) return (time_t)0;

    char tmp[32] = {0};
    strncpy(tmp, tempo, sizeof(tmp) - 1);

    char *dot = strchr(tmp, '.');
    if (dot) *dot = '\0';

    struct tm tmv;
    memset(&tmv, 0, sizeof(struct tm));

    if (!strptime(tmp, "%H:%M:%S", &tmv))
    {
        if (!strptime(tmp, "%H:%M", &tmv))
            return (time_t)0;
    }

    return mktime(&tmv);
}

ssize_t readSocket(int socket, void *buffer, size_t length)
{
    while (1)
    {
        ssize_t n = recv(socket, buffer, length, 0);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;

            if (errno == ECONNRESET || errno == EPIPE)
            {
                printf("[Sistema] Conexão encerrada pelo cliente\n");
                return -1;
            }
            return -1;
        }
        return n;
    }
}

ssize_t writeSocket(int socket, const void *buffer, size_t length)
{
    ssize_t written = 0;
    const char *ptr = (const char *)buffer;

    while (length > 0)
    {
        ssize_t n = send(socket, ptr, length, MSG_NOSIGNAL);
        if (n <= 0)
        {
            if (errno == EINTR)
                continue;

            if (errno == EPIPE || errno == ECONNRESET)
            {
                printf("[Sistema] Cliente desconectou abruptamente\n");
                return -1;
            }
            return -1;
        }

        written += n;
        ptr += n;
        length -= n;
    }
    return written;
}
