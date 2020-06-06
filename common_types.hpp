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
    int from_x, from_y, to_x, to_y;
};

enum data_type_send
{
    field_type = 0,
    player_list = 1,
    field_size = 2
};

struct prepare_message_data_send
{
    data_type_send type;
    size_t size, second_size;
};

#define field_cells_type uint8_t