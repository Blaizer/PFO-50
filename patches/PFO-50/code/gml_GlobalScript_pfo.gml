function pfo_start()
{
    global.pfo_randomize_state = int64(0);

    global.pfo_previous_gamespeed_fps = 0;
    global.pfo_base_current_time = 0;
    global.pfo_frames_at_current_gamespeed = 0;
    global.pfo_current_time = 0;

    global.pfo_input_command = int64(0);
    global.pfo_input_command_param = int64(0);
    global.pfo_input_command_to_send = int64(0);
    global.pfo_input_command_param_to_send = int64(0);
    global.pfo_input_command_future_send_frame = 0;

    for (var p = 0; p < real(PFO.MaxPlayers); p++)
    {
        global.pfo_input_command_each[p] = int64(0);
        global.pfo_input_command_param_each[p] = int64(0);
    }
}

function pfo_add_extra_input(futureFrame)
{
    if (argument_count == 2)
    {
        var flags = argument[1];
        var c = int64(0);

        if (global.pfo_input_command_to_send != int64(0))
        {
            if (LOG.LEVEL >= LOG.INFO) show_debug_message("Writing command to input flags: " + string(global.pfo_input_command_to_send) + " " + string(global.pfo_input_command_param_to_send) + " with future frame " + string(futureFrame) + " on frame " + string(pfo_get_frame()));
            c |= (global.pfo_input_command_to_send      & int64((1 << PFO.InputCommandBits)      - 1));
            c |= (global.pfo_input_command_param_to_send & int64((1 << PFO.InputCommandParamBits) - 1)) << PFO.InputCommandBits;
            global.pfo_input_command_future_send_frame = futureFrame;
        }

        return (flags << PFO.ExtraInputBits) | c;
    }
    else if (argument_count == 3)
    {
        return (argument[2] & int64((1 << PFO.InputCommandBits) - 1)) != int64(0);
    }
}

function pfo_remove_extra_input(flags)
{
    return flags >> PFO.ExtraInputBits;
}

function pfo_update_extra_input()
{
    global.pfo_input_command = int64(0);
    global.pfo_input_command_param = int64(0);

    for (var p = 0; p < real(PFO.MaxPlayers); p++)
    {
        global.pfo_input_command_each[p] = int64(0);
        global.pfo_input_command_param_each[p] = int64(0);
    }

    if (pfo_is_online())
    {
        for (var p = real(PFO.MaxPlayers) - 1; p >= 0; p--)
        {
            var c = pfo_player_get_input(p);

            var command      = c                           & int64((1 << int64(PFO.InputCommandBits)) - 1);
            var commandParam = (c >> PFO.InputCommandBits) & int64((1 << int64(PFO.InputCommandParamBits)) - 1);

            if (command != int64(0))
            {
                if (LOG.LEVEL >= LOG.INFO) show_debug_message("Read command from input flags for player " + string(p) + ": " + string(command) + " " + string(commandParam) + " on frame " + string(pfo_get_frame()));
                global.pfo_input_command = command;
                global.pfo_input_command_param = commandParam;
                global.pfo_input_command_each[p] = command;
                global.pfo_input_command_param_each[p] = commandParam;
            }
        }
    }

    global.pfo_input_command_to_send = int64(0);
    global.pfo_input_command_param_to_send = int64(0);
}

function pfo_send_command_in_progress()
{
    var ret = false;
    if (pfo_is_online() && pfo_get_frame() <= global.pfo_input_command_future_send_frame)
    {
        if (LOG.LEVEL >= LOG.VERBOSE) show_debug_message("Send command in progress until future frame " + string(global.pfo_input_command_future_send_frame) + " on frame " + string(pfo_get_frame()));
        ret = true;
    }
    return ret;
}

function pfo_send_command(command)
{
    var commandParam = argument_count > 1 ? argument[1] : int64(0);
    var p = argument_count > 2 ? argument[2] : 0;

    if (p >= 0 && pfo_is_online())
    {
        if (LOG.LEVEL >= LOG.INFO) show_debug_message("Set Command to send: " + string(command) + " " + string(commandParam) + " on frame " + string(pfo_get_frame()));
        global.pfo_input_command_to_send = command;
        global.pfo_input_command_param_to_send = commandParam;
        global.pfo_input_command_future_send_frame = pfo_get_frame(); // guarantees the command send will be in progress at least for this frame
    }
    else
    {
        p = argument_count > 2 ? argument[2] : -1;
        if (p >= 0)
        {
            global.pfo_input_command_each[p] = command;
            global.pfo_input_command_param_each[p] = commandParam;
        }
        else
        {
            global.pfo_input_command = command;
            global.pfo_input_command_param = commandParam;
        }
    }
}

function pfo_receive_command(command, param)
{
    var ret = false;

    var p = argument_count > 2 ? argument[2] : -1;
    var inputCommand;
    var commandParam;
    
    if (p >= 0)
    {
        inputCommand = global.pfo_input_command_each[p];
        commandParam = global.pfo_input_command_param_each[p];
    }
    else
    {
        inputCommand = global.pfo_input_command;
        commandParam = global.pfo_input_command_param;
    }

    if (inputCommand == command)
    {
        if (LOG.LEVEL >= LOG.INFO) show_debug_message("Received Command: " + string(inputCommand) + " " + string(commandParam) + " P" + string(p) + " on frame " + string(pfo_get_frame()));

        if (is_array(param))
        {
            param[0] = commandParam;
        }
        
        ret = true;
    }

    return ret;
}

function pfo_randomize()
{
    if (pfo_is_online())
    {
        global.pfo_randomize_state = (global.pfo_randomize_state * int64(0xd1342543de82ef95)) + int64(0x9e3779b97f4a7c15);
        random_set_seed((global.pfo_randomize_state >> int64(32)) & int64(0xffffffff));

        if (LOG.LEVEL >= LOG.VERBOSE) show_debug_message("Frame " + string(pfo_get_frame()) + ": Randomize: " + string(global.pfo_randomize_state));
    }
    else
    {
        randomize();
    }
}

function pfo_set_randomize_seed(seed)
{
    global.pfo_randomize_state = int64(seed);
}

function pfo_update_time()
{
    var _fps = pfo_game_get_speed(gamespeed_fps);

    if (global.pfo_previous_gamespeed_fps != _fps)
    {
        if (global.pfo_frames_at_current_gamespeed != 0)
        {
            global.pfo_base_current_time += global.pfo_frames_at_current_gamespeed * 1000.0 / global.pfo_previous_gamespeed_fps;
            global.pfo_frames_at_current_gamespeed = 0;
        }
        global.pfo_previous_gamespeed_fps = _fps;
    }

    global.pfo_frames_at_current_gamespeed++;
    global.pfo_current_time = round(global.pfo_base_current_time + global.pfo_frames_at_current_gamespeed * 1000.0 / _fps);
}

enum PFO
{
    InputCommandBits = 8,
    InputCommandParamBits = 8,
    ExtraInputBits = PFO.InputCommandBits + PFO.InputCommandParamBits,

    MaxPlayers = 2
}

enum LOG
{
    NONE = 0,
    INFO = 1,
    VERBOSE = 2,

    LEVEL = 1
}
