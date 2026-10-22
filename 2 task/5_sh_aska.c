/* Обмен сообщениями между N терминалами через System V shared memory. */
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L

//_____________________________________________________BIBLIOTECS__________________________________________________________________________//
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ipc.h>
#include <sys/select.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <unistd.h>
//_________________________________________________________________________________________________________________________________________//


//_____________________________________________________PARAMETERS__________________________________________________________________________//
#define CHAT_KEY_FILE  "/tmp/it_homework_shm_chat.key"
#define CHAT_MAGIC     0x53484D43u
#define CHAT_VERSION   1u
#define MAX_USERS      32
#define MAX_MESSAGES   128
#define NAME_SIZE      32
#define TEXT_SIZE      256
#define CHAT_BOT       0

#define ERROR_IN_FUN   -1
#define ALL_RIGHT       1
//_________________________________________________________________________________________________________________________________________//


typedef struct
{
    char name[NAME_SIZE];
    pid_t pid;
} chat_user_t;


typedef struct
{
    char sender[NAME_SIZE];
    char recipient[NAME_SIZE];
    char text[TEXT_SIZE];
} chat_message_t;


typedef struct
{
    unsigned int magic;
    unsigned int version;
    unsigned int count;
    chat_user_t users[MAX_USERS];
    chat_message_t messages[MAX_MESSAGES];
} chat_data_t;


#ifdef __linux__
// Linux не объявляет этот тип в sys/sem.h, но semctl(SETVAL) принимает его.
union semun
{
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};
#endif


static volatile sig_atomic_t running = 1;
static int semaphore_id = ERROR_IN_FUN;
static int segment_id = ERROR_IN_FUN;
static chat_data_t* chat_data = NULL;
static char my_name[NAME_SIZE] = {0};


//_____________________________________________________SIGNALS_____________________________________________________________________________//
static void stop_chat(int signal_number)
{
    (void)signal_number;
    running = 0;
}
//_________________________________________________________________________________________________________________________________________//


//_____________________________________________________SYNCHRONIZATION_____________________________________________________________________//
static int lock_chat(void)
{
    struct sembuf operation = {0};
    operation.sem_num = 0;
    operation.sem_op = -1;       // P: ждём свободный семафор.
    operation.sem_flg = SEM_UNDO; // Ядро вернёт его при аварии процесса.

    while(semop(semaphore_id, &operation, 1) < 0)
    {
        if(errno != EINTR)
            return ERROR_IN_FUN;
    }

    return ALL_RIGHT;
}


static int unlock_chat(void)
{
    struct sembuf operation = {0};
    operation.sem_num = 0;
    operation.sem_op = 1;        // V: разрешаем следующему процессу работу с памятью.
    operation.sem_flg = SEM_UNDO;

    return semop(semaphore_id, &operation, 1) < 0 ? ERROR_IN_FUN : ALL_RIGHT;
}
//_________________________________________________________________________________________________________________________________________//


//_____________________________________________________USERS_______________________________________________________________________________//
static int user_is_online(const chat_user_t* user)
{
    if(user->pid <= 0)
        return 0;

    // EPERM означает, что процесс существует, хотя прав для сигнала нет.
    return kill(user->pid, 0) == 0 || errno == EPERM;
}


static int user_index(const char* name)
{
    for(int index = 0; index < MAX_USERS; ++index)
    {
        if(strcmp(chat_data->users[index].name, name) == 0)
            return index;
    }

    return ERROR_IN_FUN;
}


static int register_user(const char* name)
{
    if(lock_chat() < 0)
        return ERROR_IN_FUN;

    int index = user_index(name);

    if(index >= 0 && user_is_online(&chat_data->users[index]))
    {
        unlock_chat();
        errno = EEXIST;
        return ERROR_IN_FUN;
    }

    if(index < 0)
    {
        for(int candidate = 0; candidate < MAX_USERS; ++candidate)
        {
            if(!user_is_online(&chat_data->users[candidate]))
            {
                index = candidate;
                break;
            }
        }
    }

    if(index < 0)
    {
        unlock_chat();
        errno = ENOSPC;
        return ERROR_IN_FUN;
    }

    snprintf(chat_data->users[index].name, NAME_SIZE, "%s", name);
    chat_data->users[index].pid = getpid();

    return unlock_chat();
}


static void unregister_user(void)
{
    if(lock_chat() < 0)
        return;

    int index = user_index(my_name);

    if(index >= 0 && chat_data->users[index].pid == getpid())
    {
        chat_data->users[index].pid = 0;
        chat_data->users[index].name[0] = '\0';
    }

    unlock_chat();
}
//_________________________________________________________________________________________________________________________________________//


//_____________________________________________________IPC_SETUP___________________________________________________________________________//
static int open_chat(void)
{
    // Файл нужен только для ftok; flock закрывает гонку при первом запуске.
    int key_fd = open(CHAT_KEY_FILE, O_RDWR | O_CREAT, 0600);

    if(key_fd < 0)
        return ERROR_IN_FUN;

    if(flock(key_fd, LOCK_EX) < 0)
    {
        close(key_fd);
        return ERROR_IN_FUN;
    }

    key_t memory_key = ftok(CHAT_KEY_FILE, 1);
    key_t semaphore_key = ftok(CHAT_KEY_FILE, 2);
    int new_semaphore = 0;
    int new_segment = 0;

    if(memory_key == (key_t)-1 || semaphore_key == (key_t)-1)
        goto fail;

    semaphore_id = semget(semaphore_key, 1, IPC_CREAT | IPC_EXCL | 0600);

    if(semaphore_id >= 0)
        new_semaphore = 1;
    else if(errno == EEXIST)
        semaphore_id = semget(semaphore_key, 1, 0600);

    if(semaphore_id < 0)
        goto fail;

    if(new_semaphore)
    {
        union semun argument = {0};
        argument.val = 1;  // Исходное состояние: доступ к памяти свободен.

        if(semctl(semaphore_id, 0, SETVAL, argument) < 0)
            goto fail;
    }

    segment_id = shmget(memory_key, sizeof(chat_data_t), IPC_CREAT | IPC_EXCL | 0600);

    if(segment_id >= 0)
        new_segment = 1;
    else if(errno == EEXIST)
        segment_id = shmget(memory_key, sizeof(chat_data_t), 0600);

    if(segment_id < 0)
        goto fail;

    chat_data = shmat(segment_id, NULL, 0);

    if(chat_data == (void*)-1)
    {
        chat_data = NULL;
        goto fail;
    }

    if(new_segment)
    {
        memset(chat_data, 0, sizeof(*chat_data));
        chat_data->magic = CHAT_MAGIC;
        chat_data->version = CHAT_VERSION;
    }

    if(chat_data->magic != CHAT_MAGIC ||
       chat_data->version != CHAT_VERSION ||
       chat_data->count > MAX_MESSAGES)
    {
        errno = EINVAL;
        goto fail;
    }

    flock(key_fd, LOCK_UN);
    close(key_fd);
    return ALL_RIGHT;

fail:
    if(chat_data != NULL)
    {
        shmdt(chat_data);
        chat_data = NULL;
    }

    flock(key_fd, LOCK_UN);
    close(key_fd);
    return ERROR_IN_FUN;
}


static int cleanup_chat(void)
{
    if(open_chat() < 0)
        return ERROR_IN_FUN;

    if(lock_chat() < 0)
        return ERROR_IN_FUN;

    // Не удаляем общие IPC-объекты, пока кто-либо пользуется чатом.
    for(int index = 0; index < MAX_USERS; ++index)
    {
        if(user_is_online(&chat_data->users[index]))
        {
            unlock_chat();
            shmdt(chat_data);
            chat_data = NULL;
            errno = EBUSY;
            return ERROR_IN_FUN;
        }
    }

    unlock_chat();
    shmdt(chat_data);
    chat_data = NULL;

    if(shmctl(segment_id, IPC_RMID, NULL) < 0)
        return ERROR_IN_FUN;

    return semctl(semaphore_id, 0, IPC_RMID) < 0 ? ERROR_IN_FUN : ALL_RIGHT;
}
//_________________________________________________________________________________________________________________________________________//


//_____________________________________________________MESSAGES____________________________________________________________________________//
static int add_message(const char* sender, const char* recipient, const char* text)
{
    if(chat_data->count >= MAX_MESSAGES)
        return ERROR_IN_FUN;

    chat_message_t* message = &chat_data->messages[chat_data->count++];
    snprintf(message->sender, NAME_SIZE, "%s", sender);
    snprintf(message->recipient, NAME_SIZE, "%s", recipient);
    snprintf(message->text, TEXT_SIZE, "%s", text);

    return ALL_RIGHT;
}


static int receive_messages(void)
{
    if(lock_chat() < 0)
        return ERROR_IN_FUN;

    chat_message_t received[MAX_MESSAGES] = {{0}};
    unsigned int received_count = 0;
    unsigned int index = 0;

    while(index < chat_data->count)
    {
        chat_message_t* message = &chat_data->messages[index];

        if(strcmp(message->recipient, my_name) != 0)
        {
            ++index;
            continue;
        }

        received[received_count++] = *message;

        // Удаляем доставленную запись и сохраняем порядок остальных.
        memmove(message, message + 1,
                (chat_data->count - index - 1) * sizeof(*message));
        --chat_data->count;
    }

    if(unlock_chat() < 0)
        return ERROR_IN_FUN;

    // Вывод выполняем после освобождения семафора.
    for(unsigned int message = 0; message < received_count; ++message)
        printf("От %s: %s\n", received[message].sender, received[message].text);

    if(received_count > 0)
        fflush(stdout);

    return ALL_RIGHT;
}


static int send_message(const char* recipient, const char* text)
{
    if(lock_chat() < 0)
        return ERROR_IN_FUN;

    int target = user_index(recipient);
    int online = target >= 0 && user_is_online(&chat_data->users[target]);
    int result = add_message(my_name, recipient, text);

    if(result == ALL_RIGHT && CHAT_BOT && !online)
    {
        char reply[TEXT_SIZE] = {0};

        snprintf(reply, sizeof(reply),
                 "Пользователь %s сейчас недоступен. Сообщение сохранено до его входа.",
                 recipient);

        if(add_message("bot", my_name, reply) < 0)
            fprintf(stderr, "Бот: %s\n", reply);
    }

    if(unlock_chat() < 0)
        return ERROR_IN_FUN;

    return result;
}
//_________________________________________________________________________________________________________________________________________//


//_____________________________________________________MAIN________________________________________________________________________________//
int main(int argc, char* argv[])
{
    if(argc == 2 && strcmp(argv[1], "--cleanup") == 0)
    {
        if(cleanup_chat() < 0)
        {
            perror("Очистка");
            return EXIT_FAILURE;
        }

        puts("Разделяемая память и семафор удалены.");
        return EXIT_SUCCESS;
    }

    if(argc != 2 || strlen(argv[1]) == 0 || strlen(argv[1]) >= NAME_SIZE ||
       strcmp(argv[1], "bot") == 0)
    {
        fprintf(stderr, "Использование: %s ИМЯ_ПОЛЬЗОВАТЕЛЯ | --cleanup\n", argv[0]);
        return EXIT_FAILURE;
    }

    for(const char* letter = argv[1]; *letter != '\0'; ++letter)
    {
        if(*letter == ' ' || *letter == '\t' || *letter == '\n')
        {
            fprintf(stderr, "Имя не должно содержать пробелы\n");
            return EXIT_FAILURE;
        }
    }

    snprintf(my_name, NAME_SIZE, "%s", argv[1]);

    if(open_chat() < 0)
    {
        perror("Открытие чата");
        return EXIT_FAILURE;
    }

    if(register_user(my_name) < 0)
    {
        perror("Регистрация пользователя");
        shmdt(chat_data);
        return EXIT_FAILURE;
    }

    signal(SIGINT, stop_chat);
    signal(SIGTERM, stop_chat);
    setvbuf(stdin, NULL, _IONBF, 0);

    printf("Вы: %s. Формат: ИМЯ_ПОЛУЧАТЕЛЯ текст. /exit — выход.\n", my_name);
    fflush(stdout);

    char line[TEXT_SIZE + NAME_SIZE + 2] = {0};

    while(running)
    {
        if(receive_messages() < 0)
        {
            perror("Получение");
            break;
        }

        fd_set input = {0};
        FD_ZERO(&input);
        FD_SET(STDIN_FILENO, &input);

        struct timeval timeout = {0, 200000};
        int ready = select(STDIN_FILENO + 1, &input, NULL, NULL, &timeout);

        if(ready < 0)
        {
            if(errno == EINTR)
                continue;

            perror("select");
            break;
        }

        if(ready == 0)
            continue;

        if(fgets(line, sizeof(line), stdin) == NULL)
            break;

        if(strcmp(line, "/exit\n") == 0)
            break;

        char* separator = strpbrk(line, " \t");

        if(separator == NULL || separator == line)
        {
            fprintf(stderr, "Введите: ИМЯ_ПОЛУЧАТЕЛЯ текст\n");
            continue;
        }

        *separator++ = '\0';

        while(*separator == ' ' || *separator == '\t')
            ++separator;

        size_t length = strlen(separator);

        if(length > 0 && separator[length - 1] == '\n')
            separator[--length] = '\0';

        if(strlen(line) >= NAME_SIZE || length == 0 || length >= TEXT_SIZE)
        {
            fprintf(stderr, "Имя до %d, текст до %d байт\n", NAME_SIZE - 1, TEXT_SIZE - 1);
            continue;
        }

        if(send_message(line, separator) < 0)
            fprintf(stderr, "Очередь заполнена или ошибка отправки\n");
    }

    unregister_user();
    shmdt(chat_data);

    return EXIT_SUCCESS;
}
//_________________________________________________________________________________________________________________________________________//
