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

    writer.Write(buffer_u64, read ? "randomize_state" : global.pfo_randomize_state);
    writer.Write(buffer_u32, read ? "random_seed"     : random_get_seed());
    writer.Write(buffer_s32, read ? "room"            : room);
    writer.Write(buffer_s32, read ? "instance_count"  : instance_count - instance_number(oOkay) - instance_number(oSaveIcon) - instance_exists(global.onlineSpectatorPauseMenu));

    writer.Write(buffer_s8,  read ? "curr_game_id" : global.currGameID);
    writer.Write(buffer_s8,  read ? "curr_file"    : global.currFile);
    writer.Write(buffer_s8,  read ? "num_players"  : global.numPlayers);
    writer.Write(buffer_s8,  read ? "paused"       : global.paused);
    writer.Write(buffer_s8,  read ? "attract_mode" : global.attractMode);
    writer.Write(buffer_s8,  read ? "language"     : global.language);
    writer.Write(buffer_s8,  read ? "half_time"    : global.halfTime);
    writer.Write(buffer_u64, read ? "rng_state_1"  : global.rng_state_1);
    writer.Write(buffer_u64, read ? "rng_state_2"  : global.rng_state_2);

    writer.Write(buffer_f64, read ? "current_time"               : global.pfo_current_time);
    writer.Write(buffer_f64, read ? "base_current_time"          : global.pfo_base_current_time);
    writer.Write(buffer_f64, read ? "time_stamp_incremental"     : global.timeStampIncremental)
    writer.Write(buffer_s16, read ? "attract_mode_library_timer" : global.attractModeLibraryTimer);
    writer.Write(buffer_s8,  read ? "online_simultaneous_turns"  : global.onlineSimultaneousTurns);

    var hasGameData = writer.Write(buffer_s8, read ? "has_game_data" : instance_exists(o14_Game));
    if (hasGameData)
    {
        writer.Write(buffer_f64, read ? "g14_cash_0" : o14_Game.cash[0]);
        writer.Write(buffer_f64, read ? "g14_cash_1" : o14_Game.cash[1]);
        writer.Write(buffer_f64, read ? "g14_cash_2" : o14_Game.cash[2]);
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
            var message = "Desync detected!\n\nDifference in GML values: " + string(differ.differences) + "\n\nOurs: " + string(differ.diff1) + "\n\nTheirs: " + string(differ.diff2);
            show_debug_message(message);
            show_message(message);
        }

        if (array_contains(differ.differences, "instance_count"))
        {
            show_debug_message("=== INSTANCE COUNTS ===");

            var map = ds_map_create();

            with (all)
            {
                if (!ds_map_exists(map, object_index))
                {
                    ds_map_add(map, object_index, 1);
                }
            }

            for (var obj = ds_map_find_first(map); !is_undefined(obj); obj = ds_map_find_next(map, obj))
            {
                show_debug_message(object_get_name(obj) + ": " + string(instance_number(obj)));
            }

            ds_map_destroy(map);

            show_debug_message("========================");
        }
    }
}
