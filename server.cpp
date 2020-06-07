#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <strings.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#include <iostream>
#include <string>
#include <vector>
#include <fstream>

#include "common_types.hpp"

struct thread_data
{
    pthread_mutex_t *time_mutex;
    struct timeval *update_time;
    pthread_mutex_t *map_mutex;
    std::vector<std::vector<field_cells_type>> *map_s;
    int sockfd;
};

void *client_reciver(void *data)
{
    thread_data &prop = *((thread_data *)data); //присваивание в ссылку для удобства

    action_send input_act;                                                        //переменная для получения действий игрока
    int n = recv(prop.sockfd, (action_send *)&input_act, sizeof(action_send), 0); //получение первого действи
    while (n)                                                                     //если n==0 соединение разорвано
    {
        switch (input_act.action) //для возможности добавления новых действий
        {
        case move:
            pthread_mutex_lock(prop.map_mutex);
            //проверка не покидания пределов поля и свободности ячейки. не может быть меньше 0 беззнаковый тип
            if (input_act.to_x < (*prop.map_s)[0].size() && input_act.to_y < (*prop.map_s).size() && (*prop.map_s)[input_act.to_y][input_act.to_x] == empty)
            {
                (*prop.map_s)[input_act.from_y][input_act.from_x] = empty;
                (*prop.map_s)[input_act.to_y][input_act.to_x] = player;
                pthread_mutex_lock(prop.time_mutex);
                gettimeofday(prop.update_time, NULL);
                pthread_mutex_unlock(prop.time_mutex);
            }
            pthread_mutex_unlock(prop.map_mutex);
            break;

        default:
            break;
        }
        n = recv(prop.sockfd, (action_send *)&input_act, sizeof(action_send), 0);
    }
    return (void *)(0);
}

void *client_sender(void *data)
{
    thread_data &prop = *((thread_data *)data); //присваивание в ссылку для удобства
    //структура с временем последней отправленной клиенту версии
    struct timeval last_time;
    last_time.tv_sec = last_time.tv_usec = 0; //равна 0 для того чтобы по старту в первый раз произошла отправка

    //поиск свободной точки для игрока
    pthread_mutex_lock(prop.map_mutex);
    {
        bool flag_found = false;
        for (size_t i = 0; i < (*prop.map_s).size() && !flag_found; i++)
            for (size_t j = 0; j < (*prop.map_s)[i].size() && !flag_found; j++)
                if ((*prop.map_s)[i][j] == empty)
                {
                    (*prop.map_s)[i][j] = player;
                    flag_found = true;
                }
    }
    pthread_mutex_unlock(prop.map_mutex);
    pthread_mutex_lock(prop.time_mutex); //время меняется на каждое изменение поля
    gettimeofday(prop.update_time, NULL);
    pthread_mutex_unlock(prop.time_mutex);

    pthread_t reciver_start; //запуск потока получающего действия клиента
    pthread_create(&reciver_start, NULL, client_reciver, (void *)&prop);

    int n = 1;

    while (n)
    {
        pthread_mutex_lock(prop.time_mutex); //проверка обновления времени
        if (!memcmp(prop.update_time, &last_time, sizeof(timeval)))
        {
            pthread_mutex_unlock(prop.time_mutex);
            usleep(1);
            continue; //пропуск при отсутствии изменений
        }
        last_time = *prop.update_time; //обновление времени
        pthread_mutex_unlock(prop.time_mutex);
        prepare_message_data_send prep_m; //сообщение с размерами поля
        prep_m.type = field_type;
        pthread_mutex_lock(prop.map_mutex);
        prep_m.size = (*prop.map_s).size();
        prep_m.second_size = (*prop.map_s)[0].size();
        n = send(prop.sockfd, (prepare_message_data_send *)&prep_m, sizeof(prepare_message_data_send), 0);
        for (size_t i = 0; i < (*prop.map_s).size(); i++) //отправка поля построчно
            n = send(prop.sockfd, (field_cells_type *)(*prop.map_s)[i].data(), (*prop.map_s)[i].size() * sizeof(field_cells_type), 0);
        pthread_mutex_unlock(prop.map_mutex);
    }
    pthread_cancel(reciver_start); //хавершить поток отслеживающий действия клиента после обрыва соединения
    delete &prop;                  //удаление структуры с данными для потоков
    return (void *)(0);
}

void *main_client_thread(void *port)
{
    // открытие файла карты
    std::ifstream in("map.txt");
    //размерности карты
    int lines, colonums;
    in >> lines >> colonums;
    if (!in.is_open()) //выход если открытие файла не успешно
    {
        std::cout << "Map file not found\n";
        exit(2);
    }
    //создание поля для игры
    std::vector<std::vector<field_cells_type>> map_s(lines, std::vector<field_cells_type>(colonums));
    {
        size_t temp; //переменная для чтения ячеек, отдельный блок для удаления после выхода
        for (size_t i = 0; i < map_s.size(); i++)
            for (size_t j = 0; j < map_s[i].size(); j++)
            {
                in >> temp; //чтение в темп так как чтение напрямую происходит в char виде
                map_s[i][j] = temp;
            }
    }
    in.close(); //закрытие файла
    // время последнего обновления карты для детекции изменений
    struct timeval update_time;
    gettimeofday(&update_time, NULL);
    pthread_mutex_t time_mutex;
    pthread_mutex_init(&time_mutex, NULL);
    //мьютекс для одновременного доступа к карте только 1 потока
    pthread_mutex_t map_mutex;
    pthread_mutex_init(&map_mutex, NULL);

    int sockfd;
    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(*((int *)port));
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        servaddr.sin_port = 0;
        if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
        {
            close(sockfd);
            exit(1);
        }
    }

    socklen_t servlen = sizeof(servaddr);
    listen(sockfd, 100);
    getsockname(sockfd, (struct sockaddr *)&servaddr, &servlen);
    std::cout << "Listening on port: " << ntohs(servaddr.sin_port) << std::endl;

    while (true)
    {
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);
        //ожидание нового клиента
        int newsockfd = accept(sockfd, (struct sockaddr *)&cliaddr, &clilen);
        //сохранение данных необходимых для работы ротока, удаляются потоком
        thread_data *prop = new thread_data;
        prop->sockfd = newsockfd;
        prop->map_mutex = &map_mutex;
        prop->map_s = &map_s;
        prop->time_mutex = &time_mutex;
        prop->update_time = &update_time;
        // запуск потока для клиента, нет необходимости хранить, завершит себя сам по потере соединения
        pthread_t client_start;
        pthread_create(&client_start, NULL, client_sender, (void *)prop);
    }
    return (void *)(0);
}

int main(int argc, char *argv[])
{
    // нужное ли число аргументов при запуске
    if (argc != 2)
    {
        std::cout << "No port given\n";
        return -1;
    }
    // порт из строки в число
    int port = atoi(argv[1]);
    // 0 если строка не число
    if (port == 0)
    {
        perror("Not correct port");
        return -1;
    }
    //поток для приёма новых соединений
    pthread_t main_thread;
    //создание потока и передача порта
    pthread_create(&main_thread, NULL, main_client_thread, (void *)&port);

    std::cout << "Ready to recive commands\n";
    bool done = false;
    std::string command;
    while (!done)
    {
        std::getline(std::cin, command); //чтение строки с командой
        if (command == "exit")
        {
            pthread_cancel(main_thread); //остановка потока
            exit(0);                     //выход
        }
        else if (command == "help")
        {
            std::cout << "Avalible commands:\n";
            std::cout << "exit - closes app\n";
            std::cout << "help - shows avalible commands\n";
        }
        else
        {
            std::cout << "Unknown command, please use help to get list of avalible commands\n";
        }
    }
    return 0;
}