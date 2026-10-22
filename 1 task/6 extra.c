/* Обмен сообщениями между терминалами через общий файл. */
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L

//_____________________________________________________BIBLIOTECS__________________________________________________________________________//
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
//_________________________________________________________________________________________________________________________________________//


//_____________________________________________________PARAMETERS__________________________________________________________________________//
#define CHAT_MAGIC      0x43484154u
#define CHAT_VERSION    1u
#define MAX_USER_ID     255
#define TEXT_SIZE       512
#define COMPACT_LIMIT   1024

#define ERROR_IN_FUN   -1
#define ALL_RIGHT       1
//_________________________________________________________________________________________________________________________________________//


typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint64_t next_id;
} chat_header_t;


typedef struct
{
    uint64_t id;
    uint32_t sender;
    uint32_t recipient;
    uint32_t delivered;
    char text[TEXT_SIZE];
} chat_message_t;


static volatile sig_atomic_t running = 1;


//_____________________________________________________SIGNALS_____________________________________________________________________________//
static void stop_chat(int signal_number)
{
    (void)signal_number;
    running = 0;
}
//_________________________________________________________________________________________________________________________________________//


//_____________________________________________________FILE_IO_____________________________________________________________________________//
static int read_exact(int fd, void* buffer, size_t size)
{
    size_t done = 0;

    while(done < size)
    {
        ssize_t amount = read(fd, (char*)buffer + done, size - done);

        if(amount == 0)
            return ERROR_IN_FUN;

        if(amount < 0)
        {
            if(errno == EINTR)
                continue;

            return ERROR_IN_FUN;
        }

        done += (size_t)amount;
    }

    return ALL_RIGHT;
}


static int write_exact(int fd, const void* buffer, size_t size)
{
    size_t done = 0;

    while(done < size)
    {
        ssize_t amount = write(fd, (const char*)buffer + done, size - done);

        if(amount < 0)
        {
            if(errno == EINTR)
                continue;

            return ERROR_IN_FUN;
        }

        done += (size_t)amount;
    }

    return ALL_RIGHT;
}
//_________________________________________________________________________________________________________________________________________//


//_____________________________________________________FILE_PROTOCOL_______________________________________________________________________//
static int compact_messages(int fd)
{
    off_t end = lseek(fd, 0, SEEK_END);

    if(end < (off_t)sizeof(chat_header_t))
        return ERROR_IN_FUN;

    size_t count = (size_t)(end - (off_t)sizeof(chat_header_t)) / sizeof(chat_message_t);

    if(count < COMPACT_LIMIT)
        return ALL_RIGHT;

    off_t target = (off_t)sizeof(chat_header_t);

    // Под общей блокировкой переносим только сообщения, ещё не полученные адресатом.
    for(size_t index = 0; index < count; ++index)
    {
        chat_message_t message = {0};
        off_t source = (off_t)sizeof(chat_header_t) + (off_t)(index * sizeof(message));

        if(lseek(fd, source, SEEK_SET) < 0 ||
           read_exact(fd, &message, sizeof(message)) < 0)
            return ERROR_IN_FUN;

        if(message.delivered != 0)
            continue;

        if(lseek(fd, target, SEEK_SET) < 0 ||
           write_exact(fd, &message, sizeof(message)) < 0)
            return ERROR_IN_FUN;

        target += (off_t)sizeof(message);
    }

    return ftruncate(fd, target) < 0 ? ERROR_IN_FUN : ALL_RIGHT;
}


static int receive_messages(int fd, uint32_t user_id)
{
    if(flock(fd, LOCK_EX) < 0)
        return ERROR_IN_FUN;

    int result = ERROR_IN_FUN;
    off_t end = lseek(fd, 0, SEEK_END);

    if(end < (off_t)sizeof(chat_header_t))
        goto finish;

    size_t count = (size_t)(end - (off_t)sizeof(chat_header_t)) / sizeof(chat_message_t);

    for(size_t index = 0; index < count; ++index)
    {
        chat_message_t message = {0};
        off_t position = (off_t)sizeof(chat_header_t) + (off_t)(index * sizeof(message));

        if(lseek(fd, position, SEEK_SET) < 0 ||
           read_exact(fd, &message, sizeof(message)) < 0)
            goto finish;

        if(message.recipient != user_id || message.delivered != 0)
            continue;

        printf("[№%llu] От %u: %s\n",
               (unsigned long long)message.id, message.sender, message.text);
        fflush(stdout);

        message.delivered = 1;

        if(lseek(fd, position, SEEK_SET) < 0 ||
           write_exact(fd, &message, sizeof(message)) < 0)
            goto finish;
    }

    result = compact_messages(fd);

finish:
    flock(fd, LOCK_UN);
    return result;
}


static int send_message(int fd, uint32_t sender, uint32_t recipient, const char* text)
{
    if(flock(fd, LOCK_EX) < 0)
        return ERROR_IN_FUN;

    int result = ERROR_IN_FUN;
    chat_header_t header = {0};

    if(lseek(fd, 0, SEEK_SET) < 0 ||
       read_exact(fd, &header, sizeof(header)) < 0)
        goto finish;

    chat_message_t message = {0};
    message.id = header.next_id++;
    message.sender = sender;
    message.recipient = recipient;
    snprintf(message.text, sizeof(message.text), "%s", text);

    if(lseek(fd, 0, SEEK_END) < 0 ||
       write_exact(fd, &message, sizeof(message)) < 0)
        goto finish;

    // Сохраняем следующий номер: после уплотнения ID сообщений не повторяются.
    if(lseek(fd, 0, SEEK_SET) < 0 ||
       write_exact(fd, &header, sizeof(header)) < 0 ||
       fsync(fd) < 0)
        goto finish;

    result = ALL_RIGHT;

finish:
    flock(fd, LOCK_UN);
    return result;
}


static int prepare_chat_file(int fd)
{
    if(flock(fd, LOCK_EX) < 0)
        return ERROR_IN_FUN;

    struct stat info = {0};
    chat_header_t header = {0};
    int result = ERROR_IN_FUN;

    if(fstat(fd, &info) < 0)
        goto finish;

    if(info.st_size == 0)
    {
        header.magic = CHAT_MAGIC;
        header.version = CHAT_VERSION;
        header.next_id = 1;
        result = write_exact(fd, &header, sizeof(header));
        goto finish;
    }

    if(info.st_size < (off_t)sizeof(header) ||
       (info.st_size - (off_t)sizeof(header)) % (off_t)sizeof(chat_message_t) != 0)
        goto finish;

    if(lseek(fd, 0, SEEK_SET) < 0 ||
       read_exact(fd, &header, sizeof(header)) < 0)
        goto finish;

    if(header.magic == CHAT_MAGIC && header.version == CHAT_VERSION)
        result = ALL_RIGHT;

finish:
    flock(fd, LOCK_UN);
    return result;
}
//_________________________________________________________________________________________________________________________________________//


//_____________________________________________________USER_ID_____________________________________________________________________________//
static int register_user(const char* chat_path, uint32_t user_id)
{
    size_t path_size = strlen(chat_path) + sizeof(".ids");
    char* lock_path = malloc(path_size);

    if(lock_path == NULL)
        return ERROR_IN_FUN;

    snprintf(lock_path, path_size, "%s.ids", chat_path);
    int id_fd = open(lock_path, O_RDWR | O_CREAT, 0600);
    free(lock_path);

    if(id_fd < 0)
        return ERROR_IN_FUN;

    // Родитель удерживает блокировку своего ID до закрытия id_fd.
    struct flock user_lock = {0};
    user_lock.l_type = F_WRLCK;
    user_lock.l_whence = SEEK_SET;
    user_lock.l_start = (off_t)user_id;
    user_lock.l_len = 1;

    if(fcntl(id_fd, F_SETLK, &user_lock) < 0)
    {
        close(id_fd);
        return ERROR_IN_FUN;
    }

    return id_fd;
}
//_________________________________________________________________________________________________________________________________________//


//_____________________________________________________MAIN________________________________________________________________________________//
int main(int argc, char* argv[])
{
    if(argc != 3)
    {
        fprintf(stderr, "Использование: %s ID_1_255 ФАЙЛ_СООБЩЕНИЙ\n", argv[0]);
        return EXIT_FAILURE;
    }

    char* end_ptr = NULL;
    long parsed_id = strtol(argv[1], &end_ptr, 10);

    if(*argv[1] == '\0' || *end_ptr != '\0' ||
       parsed_id < 1 || parsed_id > MAX_USER_ID)
    {
        fprintf(stderr, "ID должен быть числом от 1 до %d\n", MAX_USER_ID);
        return EXIT_FAILURE;
    }

    uint32_t user_id = (uint32_t)parsed_id;
    int fd = open(argv[2], O_RDWR | O_CREAT, 0600);

    if(fd < 0)
    {
        perror("open");
        return EXIT_FAILURE;
    }

    if(prepare_chat_file(fd) < 0)
    {
        fprintf(stderr, "Файл чата повреждён или имеет другой формат\n");
        close(fd);
        return EXIT_FAILURE;
    }

    int id_fd = register_user(argv[2], user_id);

    if(id_fd < 0)
    {
        fprintf(stderr, "Пользователь %u уже запущен или блокировка недоступна\n", user_id);
        close(fd);
        return EXIT_FAILURE;
    }

    signal(SIGINT, stop_chat);
    signal(SIGTERM, stop_chat);

    pid_t child = fork();

    if(child < 0)
    {
        perror("fork");
        close(id_fd);
        close(fd);
        return EXIT_FAILURE;
    }

    if(child == 0)
    {
        // Ребёнку нужен отдельный дескриптор: flock привязан к открытому файлу.
        close(id_fd);
        close(fd);
        fd = open(argv[2], O_RDWR);

        if(fd < 0)
        {
            perror("open");
            _exit(EXIT_FAILURE);
        }

        while(running)
        {
            if(receive_messages(fd, user_id) < 0)
            {
                perror("Чтение чата");
                _exit(EXIT_FAILURE);
            }

            struct timespec pause_time = {0, 200000000L};
            nanosleep(&pause_time, NULL);
        }

        close(fd);
        _exit(EXIT_SUCCESS);
    }

    printf("Ваш ID: %u. Формат: ID_ПОЛУЧАТЕЛЯ текст. /exit — выход.\n", user_id);
    fflush(stdout);

    char line[TEXT_SIZE + 32] = {0};

    while(running && fgets(line, sizeof(line), stdin) != NULL)
    {
        if(strcmp(line, "/exit\n") == 0)
            break;

        char* separator = NULL;
        long target = strtol(line, &separator, 10);

        if(separator == line || (*separator != ' ' && *separator != '\t') ||
           target < 1 || target > MAX_USER_ID)
        {
            fprintf(stderr, "Введите: ID_1_255 текст\n");
            continue;
        }

        while(*separator == ' ' || *separator == '\t')
            ++separator;

        size_t length = strlen(separator);

        if(length > 0 && separator[length - 1] == '\n')
            separator[--length] = '\0';

        if(length == 0 || length >= TEXT_SIZE)
        {
            fprintf(stderr, "Текст должен содержать 1..%d байт\n", TEXT_SIZE - 1);
            continue;
        }

        if(send_message(fd, user_id, (uint32_t)target, separator) < 0)
        {
            perror("Отправка");
            break;
        }
    }

    kill(child, SIGTERM);
    waitpid(child, NULL, 0);
    close(id_fd);
    close(fd);

    return EXIT_SUCCESS;
}
//_________________________________________________________________________________________________________________________________________//
