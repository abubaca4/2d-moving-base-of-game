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
    pthread_mutex_t *time_map_mutex;
    struct timeval *update_map_time;
    pthread_mutex_t *time_player_mutex;
    struct timeval *update_player_time;
    pthread_mutex_t *map_mutex;
    std::vector<std::vector<field_cells_type>> *map_s;
    pthread_mutex_t *player_mutex;
    std::vector<player> *player_list;
    pthread_mutex_t *player_count_mutex;
    size_t *player_count_connected;
    int sockfd;
    size_t my_id;
};

void *client_sender(void *data)
{
    thread_data &prop = *((thread_data *)data); //присваивание в ссылку для удобства
    //структура с временем последней отправленной клиенту версии
    struct timeval last_time_map, last_time_player;
    last_time_map.tv_sec = last_time_map.tv_usec = 0; //равна 0 для того чтобы по старту в первый раз произошла отправка
    last_time_player = last_time_map;
    prepare_message_data_send prep_m;  //подготовительное сообщение с типом посылаемых данных
    prep_m.type = my_number_from_list; //отправка номера этого клиента
    prep_m.size = prop.my_id;
    int n = send(prop.sockfd, (prepare_message_data_send *)&prep_m, sizeof(prepare_message_data_send), 0);

    while (n)
    {
        pthread_mutex_lock(prop.time_map_mutex);                           //проверка обновления времени
        if (memcmp(prop.update_map_time, &last_time_map, sizeof(timeval))) //если карта обновилась
        {
            last_time_map = *prop.update_map_time; //обновление времени
            pthread_mutex_unlock(prop.time_map_mutex);
            prep_m.type = field_type;
            pthread_mutex_lock(prop.map_mutex);
            prep_m.size = (*prop.map_s).size();
            prep_m.second_size = (*prop.map_s)[0].size();
            n = send(prop.sockfd, (prepare_message_data_send *)&prep_m, sizeof(prepare_message_data_send), 0);
            for (size_t i = 0; i < (*prop.map_s).size(); i++) //отправка поля построчно
                n = send(prop.sockfd, (field_cells_type *)(*prop.map_s)[i].data(), (*prop.map_s)[i].size() * sizeof(field_cells_type), 0);
            pthread_mutex_unlock(prop.map_mutex);
        }
        else
            pthread_mutex_unlock(prop.time_map_mutex);
        pthread_mutex_lock(prop.time_player_mutex);
        if (memcmp(prop.update_player_time, &last_time_player, sizeof(timeval))) //если список игроков обновился
        {
            last_time_player = *prop.update_player_time;
            pthread_mutex_unlock(prop.time_player_mutex);
            prep_m.type = player_list;
            pthread_mutex_lock(prop.player_mutex);
            prep_m.size = (*prop.player_list).size();
            n = send(prop.sockfd, (prepare_message_data_send *)&prep_m, sizeof(prepare_message_data_send), 0);
            n = send(prop.sockfd, (player *)prop.player_list->data(), (*prop.player_list).size() * sizeof(player), 0);
            pthread_mutex_unlock(prop.player_mutex);
        }
        else
            pthread_mutex_unlock(prop.time_player_mutex);

        usleep(1);
    }
    return (void *)(0);
}

bool is_field_free(size_t x, size_t y, size_t my_id, std::vector<std::vector<field_cells_type>> &map, std::vector<player> &player_list)
{
    if (!(x < map[0].size() && y < map.size())) //проверка не покидания пределов поля и свободности ячейки. не может быть меньше 0 беззнаковый тип
        return false;

    if (map[y][x] == empty)
    {
        bool is_free = true;
        for (size_t k = 0; k < player_list.size() && is_free; k++)                                                   //проверка не занята ли клетка одним из игроков
            is_free = (k == my_id) || !player_list[k].is_alive || !(player_list[k].x == x && player_list[k].y == y); //занята самим игроком или проверяемый не активен или координаты не совпадают
        return is_free;
    }
    return false;
}

void *client_reciver(void *data)
{
    thread_data &prop = *((thread_data *)data); //присваивание в ссылку для удобства
    //поиск свободной ячейки для игрока
    pthread_mutex_lock(prop.player_mutex);
    for (size_t k = 0; k < prop.player_list->size(); k++)
        if (!(*prop.player_list)[k].is_alive)
        {
            bool flag_found = false;
            prop.my_id = (*prop.player_list)[k].id;
            pthread_mutex_lock(prop.map_mutex);
            for (size_t i = 0; i < (*prop.map_s).size() && !flag_found; i++)
                for (size_t j = 0; j < (*prop.map_s)[i].size() && !flag_found; j++)
                    if (is_field_free(j, i, prop.my_id, *prop.map_s, *prop.player_list))
                    {
                        flag_found = true;
                        (*prop.player_list)[prop.my_id].x = j;
                        (*prop.player_list)[prop.my_id].y = i;
                    }
            pthread_mutex_unlock(prop.map_mutex);
            (*prop.player_list)[prop.my_id].is_alive = true;
            break;
        }
    pthread_mutex_unlock(prop.player_mutex);
    pthread_mutex_lock(prop.time_player_mutex); //время меняется на каждое изменение
    gettimeofday(prop.update_player_time, NULL);
    pthread_mutex_unlock(prop.time_player_mutex);

    pthread_t sender_start; //запуск потока отслеживающего изменения поля и отсылающего клиенту
    pthread_create(&sender_start, NULL, client_sender, (void *)&prop);

    action_send input_act;                                                        //переменная для получения действий игрока
    int n = recv(prop.sockfd, (action_send *)&input_act, sizeof(action_send), 0); //получение первого действи
    while (n)                                                                     //если n==0 соединение разорвано
    {
        switch (input_act.action) //для возможности добавления новых действий
        {
        case move:
            pthread_mutex_lock(prop.map_mutex);
            pthread_mutex_lock(prop.player_mutex);
            if (input_act.from_x == (*prop.player_list)[prop.my_id].x && input_act.from_y == (*prop.player_list)[prop.my_id].y && is_field_free(input_act.to_x, input_act.to_y, prop.my_id, *prop.map_s, *prop.player_list))
            {
                (*prop.player_list)[prop.my_id].x = input_act.to_x;
                (*prop.player_list)[prop.my_id].y = input_act.to_y;
                pthread_mutex_lock(prop.time_player_mutex);
                gettimeofday(prop.update_player_time, NULL);
                pthread_mutex_unlock(prop.time_player_mutex);
            }
            pthread_mutex_unlock(prop.player_mutex);
            pthread_mutex_unlock(prop.map_mutex);
            break;

        default:
            break;
        }
        n = recv(prop.sockfd, (action_send *)&input_act, sizeof(action_send), 0);
    }

    pthread_mutex_lock(prop.time_map_mutex); //закрыть все мьютексы чтобы не закрыть поток следящий за изменением в тот момент когда он имеет заблокированный мьютекс
    pthread_mutex_lock(prop.time_player_mutex);
    pthread_mutex_lock(prop.map_mutex);
    pthread_mutex_lock(prop.player_mutex);
    pthread_cancel(sender_start);                     //перед удалением данных для потока завершить поток отслеживания изменений
    (*prop.player_list)[prop.my_id].is_alive = false; //ячейка игрока более не занята
    gettimeofday(prop.update_player_time, NULL);
    pthread_mutex_unlock(prop.time_map_mutex);
    pthread_mutex_unlock(prop.time_player_mutex);
    pthread_mutex_unlock(prop.map_mutex);
    pthread_mutex_unlock(prop.player_mutex);

    pthread_mutex_lock(prop.player_count_mutex);
    (*prop.player_count_connected)--;
    pthread_mutex_unlock(prop.player_count_mutex);
    delete &prop; //удаление структуры с данными для потоков
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
    in.close();                    //закрытие файла
    in.open("player_collors.txt"); //файл с цветами игроков
    if (!in.is_open())             //выход если открытие файла не успешно
    {
        std::cout << "Collor file not found\n";
        exit(3);
    }
    size_t player_limit_count;
    in >> player_limit_count;
    std::vector<player> player_list(player_limit_count); //список игроков
    for (size_t i = 0; i < player_limit_count; i++)      //чтение цветов и заполнение полей по умолчанию
    {
        size_t r, g, b;
        in >> r >> g >> b;
        player_list[i].r = r;
        player_list[i].g = g;
        player_list[i].b = b;
        player_list[i].x = player_list[i].y = 0;
        player_list[i].id = i;
        player_list[i].is_alive = false; //соединён ли игрок
    }
    in.close();

    // время последнего обновления карты для детекции изменений
    struct timeval update_map_time;
    gettimeofday(&update_map_time, NULL);
    pthread_mutex_t time_map_mutex;
    pthread_mutex_init(&time_map_mutex, NULL);
    // время последнего изменения в списке игроков
    struct timeval update_player_time;
    gettimeofday(&update_player_time, NULL);
    pthread_mutex_t time_player_mutex;
    pthread_mutex_init(&time_player_mutex, NULL);
    //мьютекс для одновременного доступа к карте только 1 потока
    pthread_mutex_t map_mutex;
    pthread_mutex_init(&map_mutex, NULL);
    //мьютекс для одновременного доступа к списку игроков 1 потока
    pthread_mutex_t player_mutex;
    pthread_mutex_init(&player_mutex, NULL);

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
    //число подключённых игроков
    size_t player_count_connected = 0;
    //мьютекс для числа подключённых игроков
    pthread_mutex_t player_count_mutex;
    pthread_mutex_init(&player_count_mutex, NULL);

    while (true)
    {
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);
        //ожидание нового клиента
        int newsockfd = accept(sockfd, (struct sockaddr *)&cliaddr, &clilen);
        pthread_mutex_lock(&player_count_mutex);
        if (player_count_connected == player_limit_count)
        {
            pthread_mutex_unlock(&player_count_mutex);
            continue;
        }
        else
        {
            player_count_connected++;
            pthread_mutex_unlock(&player_count_mutex);
        }

        //сохранение данных необходимых для работы ротока, удаляются потоком
        thread_data *prop = new thread_data;
        prop->time_map_mutex = &time_map_mutex;
        prop->update_map_time = &update_map_time;
        prop->time_player_mutex = &time_player_mutex;
        prop->update_player_time = &update_player_time;
        prop->map_mutex = &map_mutex;
        prop->map_s = &map_s;
        prop->player_mutex = &player_mutex;
        prop->player_list = &player_list;
        prop->player_count_mutex = &player_count_mutex;
        prop->player_count_connected = &player_count_connected;
        prop->sockfd = newsockfd;
        // запуск потока для клиента, нет необходимости хранить, завершит себя сам по потере соединения
        pthread_t client_start;
        pthread_create(&client_start, NULL, client_reciver, (void *)prop);
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