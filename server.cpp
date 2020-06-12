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
    std::vector<std::pair<size_t, size_t>> *start_points;
    pthread_mutex_t *players_top_mutex;
    std::vector<top_unit_count> *players_top;
    pthread_mutex_t *top_time_mutex;
    struct timeval *players_top_time;
    int sockfd;
    size_t my_id;
};

void *client_sender(void *data)
{
    thread_data &prop = *((thread_data *)data); //присваивание в ссылку для удобства
    //структура с временем последней отправленной клиенту версии
    struct timeval last_time_map, last_time_player, last_top_time;
    last_time_map.tv_sec = last_time_map.tv_usec = 0; //равна 0 для того чтобы по старту в первый раз произошла отправка
    last_time_player = last_time_map;
    pthread_mutex_lock(prop.top_time_mutex);
    last_top_time = *prop.players_top_time;
    pthread_mutex_unlock(prop.top_time_mutex);
    prepare_message_data_send prep_m;  //подготовительное сообщение с типом посылаемых данных
    prep_m.type = my_number_from_list; //отправка номера этого клиента
    prep_m.size = prop.my_id;
    int n = send(prop.sockfd, (prepare_message_data_send *)&prep_m, sizeof(prepare_message_data_send), MSG_NOSIGNAL);

    while (n)
    {
        pthread_mutex_lock(prop.time_map_mutex);                           //проверка обновления времени
        if (memcmp(prop.update_map_time, &last_time_map, sizeof(timeval))) //если карта обновилась
        {
            last_time_map = *prop.update_map_time; //обновление времени
            pthread_mutex_unlock(prop.time_map_mutex);
            prep_m.type = field_type;
            pthread_mutex_lock(prop.map_mutex);
            prep_m.size = prop.map_s->size();
            prep_m.second_size = prop.map_s->at(0).size();
            n = send(prop.sockfd, (prepare_message_data_send *)&prep_m, sizeof(prepare_message_data_send), MSG_NOSIGNAL);
            for (size_t i = 0; i < prop.map_s->size(); i++) //отправка поля построчно
                n = send(prop.sockfd, (field_cells_type *)prop.map_s->at(i).data(), prop.map_s->at(i).size() * sizeof(field_cells_type), MSG_NOSIGNAL);
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
            prep_m.size = prop.player_list->size();
            n = send(prop.sockfd, (prepare_message_data_send *)&prep_m, sizeof(prepare_message_data_send), MSG_NOSIGNAL);
            n = send(prop.sockfd, (player *)prop.player_list->data(), prop.player_list->size() * sizeof(player), MSG_NOSIGNAL);
            pthread_mutex_unlock(prop.player_mutex);
        }
        else
            pthread_mutex_unlock(prop.time_player_mutex);
        pthread_mutex_lock(prop.top_time_mutex);
        if (memcmp(prop.players_top_time, &last_top_time, sizeof(timeval)))
        {
            last_top_time = *prop.players_top_time;
            pthread_mutex_unlock(prop.top_time_mutex);
            prep_m.type = score;
            pthread_mutex_lock(prop.players_top_mutex);
            prep_m.size = prop.players_top->size();
            n = send(prop.sockfd, (prepare_message_data_send *)&prep_m, sizeof(prepare_message_data_send), MSG_NOSIGNAL);
            n = send(prop.sockfd, (top_unit_count *)prop.players_top->data(), prop.players_top->size() * sizeof(top_unit_count), MSG_NOSIGNAL);
            pthread_mutex_unlock(prop.players_top_mutex);
        }
        else
            pthread_mutex_unlock(prop.top_time_mutex);

        usleep(1);
    }
    return (void *)(0);
}

bool is_field_free_player(size_t x, size_t y, size_t my_id, std::vector<player> &player_list) //свободно ли поле от другого игрока
{
    bool is_free = true;
    for (size_t k = 0; k < player_list.size() && is_free; k++)                                                   //проверка не занята ли клетка одним из игроков
        is_free = (k == my_id) || !player_list[k].is_alive || !(player_list[k].x == x && player_list[k].y == y); //занята самим игроком или проверяемый не активен или координаты не совпадают
    return is_free;
}

void *client_reciver(void *data)
{
    thread_data &prop = *((thread_data *)data); //присваивание в ссылку для удобства
    //поиск свободной ячейки для игрока
    pthread_mutex_lock(prop.player_mutex);
    for (size_t k = 0; k < prop.player_list->size(); k++)
        if (!prop.player_list->at(k).is_alive)
        {
            prop.my_id = prop.player_list->at(k).id;
            prop.player_list->at(prop.my_id).x = prop.start_points->at(prop.my_id).second;
            prop.player_list->at(prop.my_id).y = prop.start_points->at(prop.my_id).first;
            prop.player_list->at(prop.my_id).is_alive = true;
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
        pthread_mutex_lock(prop.player_mutex);
        if (input_act.from_x == prop.player_list->at(prop.my_id).x && input_act.from_y == prop.player_list->at(prop.my_id).y) //правильно ли текущее местоположение игрока что знает клиент
        {
            pthread_mutex_unlock(prop.player_mutex);

            switch (input_act.action) //для возможности добавления новых действий
            {
            case move:
                if (!(input_act.to_x < prop.map_s->at(0).size() && input_act.to_y < prop.map_s->size())) //не выходит ли точка назначения за пределы карты
                    break;
                pthread_mutex_lock(prop.map_mutex);
                switch (prop.map_s->at(input_act.to_y)[input_act.to_x]) //в зависимости от того в какую точку идёт игрок
                {
                case empty:
                case door_open:
                    pthread_mutex_lock(prop.player_mutex);
                    if (is_field_free_player(input_act.to_x, input_act.to_y, prop.my_id, *prop.player_list)) //если поле свободно от другого игрока
                    {
                        prop.player_list->at(prop.my_id).x = input_act.to_x;
                        prop.player_list->at(prop.my_id).y = input_act.to_y;
                        pthread_mutex_lock(prop.time_player_mutex);
                        gettimeofday(prop.update_player_time, NULL);
                        pthread_mutex_unlock(prop.time_player_mutex);
                    }
                    pthread_mutex_unlock(prop.player_mutex);
                    break;
                case trap:
                    prop.map_s->at(input_act.to_y)[input_act.to_x] = trap_on;
                    pthread_mutex_lock(prop.time_map_mutex);
                    gettimeofday(prop.update_map_time, NULL);
                    pthread_mutex_unlock(prop.time_map_mutex);
                case trap_on:
                    pthread_mutex_lock(prop.player_mutex);
                    prop.player_list->at(prop.my_id).x = prop.start_points->at(prop.my_id).second;
                    prop.player_list->at(prop.my_id).y = prop.start_points->at(prop.my_id).first;
                    pthread_mutex_unlock(prop.player_mutex);
                    pthread_mutex_lock(prop.time_player_mutex);
                    gettimeofday(prop.update_player_time, NULL);
                    pthread_mutex_unlock(prop.time_player_mutex);
                    break;

                default:
                    break;
                }
                pthread_mutex_unlock(prop.map_mutex);
                break;

            case doorAction:
                pthread_mutex_lock(prop.player_mutex);
                pthread_mutex_lock(prop.map_mutex);
                if (is_field_free_player(input_act.to_x, input_act.to_y, prop.my_id, *prop.player_list) && (prop.map_s->at(input_act.to_y)[input_act.to_x] == door_open || prop.map_s->at(input_act.to_y)[input_act.to_x] == door_lock))
                {
                    prop.map_s->at(input_act.to_y)[input_act.to_x] = prop.map_s->at(input_act.to_y)[input_act.to_x] == door_open ? door_lock : door_open;
                    pthread_mutex_lock(prop.time_map_mutex);
                    gettimeofday(prop.update_map_time, NULL);
                    pthread_mutex_unlock(prop.time_map_mutex);
                }
                pthread_mutex_unlock(prop.player_mutex);
                pthread_mutex_unlock(prop.map_mutex);
                break;

            default:
                break;
            }
        }
        else
            pthread_mutex_unlock(prop.player_mutex);
        n = recv(prop.sockfd, (action_send *)&input_act, sizeof(action_send), 0);
    }

    pthread_mutex_lock(prop.time_map_mutex); //закрыть все мьютексы чтобы не закрыть поток следящий за изменением в тот момент когда он имеет заблокированный мьютекс
    pthread_mutex_lock(prop.time_player_mutex);
    pthread_mutex_lock(prop.map_mutex);
    pthread_mutex_lock(prop.player_mutex);
    pthread_cancel(sender_start);                      //перед удалением данных для потока завершить поток отслеживания изменений
    prop.player_list->at(prop.my_id).is_alive = false; //ячейка игрока более не занята
    gettimeofday(prop.update_player_time, NULL);
    pthread_mutex_unlock(prop.time_map_mutex);
    pthread_mutex_unlock(prop.time_player_mutex);
    pthread_mutex_unlock(prop.map_mutex);
    pthread_mutex_unlock(prop.player_mutex);

    pthread_mutex_lock(prop.player_count_mutex);
    (*prop.player_count_connected)--;
    pthread_mutex_unlock(prop.player_count_mutex);
    close(prop.sockfd);
    delete &prop; //удаление структуры с данными для потоков
    return (void *)(0);
}

void *main_client_thread(void *port)
{
    // открытие файла карты
    std::ifstream in("map.txt");
    //размерности карты
    size_t lines, colonums;
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
    size_t start_points_count;
    in >> start_points_count;
    std::vector<std::pair<size_t, size_t>> start_points(start_points_count);
    for (size_t i = 0; i < start_points_count; i++) // считывание стартовых точек
        in >> start_points[i].first >> start_points[i].second;
    in.close();                    //закрытие файла
    in.open("player_collors.txt"); //файл с цветами игроков
    if (!in.is_open())             //выход если открытие файла не успешно
    {
        std::cout << "Collor file not found\n";
        exit(3);
    }
    size_t player_collors_limit_count;
    in >> player_collors_limit_count;
    std::vector<player> player_list(player_collors_limit_count); //список игроков
    for (size_t i = 0; i < player_collors_limit_count; i++)      //чтение цветов и заполнение полей по умолчанию
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

    size_t player_limit_count = std::min(player_collors_limit_count, start_points_count);
    //топ игроков по счёту
    std::vector<top_unit_count> players_top(player_limit_count, 0);
    pthread_mutex_t players_top_mutex;
    pthread_mutex_init(&players_top_mutex, NULL);
    struct timeval players_top_time;
    gettimeofday(&players_top_time, NULL);
    pthread_mutex_t top_time_mutex;
    pthread_mutex_init(&top_time_mutex, NULL);

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

    *((int *)port) = sockfd = socket(PF_INET, SOCK_STREAM, 0);
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
            close(newsockfd);
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
        prop->start_points = &start_points;
        prop->players_top_mutex = &players_top_mutex;
        prop->players_top = &players_top;
        prop->top_time_mutex = &top_time_mutex;
        prop->players_top_time = &players_top_time;
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
            close(port);                 //закрытие сокета
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