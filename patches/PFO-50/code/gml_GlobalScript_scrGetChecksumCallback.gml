function PFO_Writer(_buffer) constructor
{
    isReader = false;
    buffer = _buffer;

    static Write = function(type, value)
    {
        buffer_write(buffer, type, value);
        return value;
    }
}

function PFO_Differ(_buffer1, _buffer2) constructor
{
    isReader = true;
    buffer1 = _buffer1;
    buffer2 = _buffer2;
    diff1 = {};
    diff2 = {};
    differences = [];

    static Write = function(type, name)
    {
        var value1 = buffer_read(buffer1, type);
        var value2 = buffer_read(buffer2, type);
        variable_struct_set(diff1, name, value1);
        variable_struct_set(diff2, name, value2);

        if (value1 != value2 && !(is_nan(value1) && is_nan(value2)))
        {
            array_push(differences, name);
        }

        return value1;
    }
}

function scrSerializeChecksum(writer)
{
    var read = writer.isReader;

    writer.Write(buffer_u64, read ? "randomize_state" : global.pfo_randomizeState);
    writer.Write(buffer_u32, read ? "random_seed"     : random_get_seed());
    writer.Write(buffer_s32, read ? "room"            : room);
    writer.Write(buffer_s32, read ? "instance_count"  : instance_count - instance_number(oOkay) - instance_number(oSaveIcon));

    writer.Write(buffer_s8,  read ? "curr_file"    : global.currFile);
    writer.Write(buffer_s8,  read ? "curr_game"    : global.currGame);
    writer.Write(buffer_s8,  read ? "num_players"  : global.numPlayers);
    writer.Write(buffer_u8,  read ? "paused"       : global.paused);
    writer.Write(buffer_u8,  read ? "attract_mode" : global.attractMode);
    writer.Write(buffer_u64, read ? "rng_state_1"  : global.rng_state_1);
    writer.Write(buffer_u64, read ? "rng_state_2"  : global.rng_state_2);

    var hasGameData = writer.Write(buffer_u8, read ? "has_game_data" : instance_exists(o14_Game));
    if (hasGameData)
    {
        writer.Write(buffer_f64, read ? "game_14_cash_0" : o14_Game.cash[0]);
        writer.Write(buffer_f64, read ? "game_14_cash_1" : o14_Game.cash[1]);
        writer.Write(buffer_f64, read ? "game_14_cash_2" : o14_Game.cash[2]);
    }
}

function scrGetChecksumCallback()
{
    if (argument_count == 1)
    {
        var writer = new PFO_Writer(argument[0]);
        scrSerializeChecksum(writer);
    }
    else if (argument_count == 2)
    {
        var differ = new PFO_Differ(argument[0], argument[1]);
        scrSerializeChecksum(differ);

        if (array_length(differ.differences) > 0)
        {
            show_message("Desync detected!\n\nDifference in GML values: " + string(differ.differences) + "\n\nOurs: " + string(differ.diff1) + "\n\nTheirs: " + string(differ.diff2));
        }
    }
}
