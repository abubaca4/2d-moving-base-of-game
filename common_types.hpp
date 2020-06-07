//файл типов общих для сервера и клиента и как правило используемых для передачи
enum field
{
    empty = 0,
    wall = 1,
    player = 2
};

enum actions
{
    move = 0
};

struct action_send
{
    actions action;
    size_t from_x, from_y, to_x, to_y;
};

enum data_type_send
{
    field_type = 0,
    player_list = 1
};

struct prepare_message_data_send
{
    data_type_send type;
    size_t size, second_size;
};

#define field_cells_type uint8_t