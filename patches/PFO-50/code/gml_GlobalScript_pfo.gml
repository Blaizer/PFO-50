function pfo_start()
{
    global.pfo_inputCommand = int64(0);
    global.pfo_inputCommandParam = int64(0);
    global.pfo_inputCommandToSend = int64(0);
    global.pfo_inputCommandParamToSend = int64(0);
    global.pfo_inputCommandFutureSendFrame = 0;

    for (var p = 0; p < real(PFO.MaxPlayers); p++)
    {
        global.pfo_inputCommandEach[p] = int64(0);
        global.pfo_inputCommandParamEach[p] = int64(0);
    }
}

function pfo_add_extra_input(futureFrame)
{
    if (argument_count == 2)
    {
        var flags = argument[1];
        var c = int64(0);

        if (global.pfo_inputCommandToSend != int64(0))
        {
            if (LOG.LEVEL >= LOG.INFO) show_debug_message("Writing command to input flags: " + string(global.pfo_inputCommandToSend) + " " + string(global.pfo_inputCommandParamToSend) + " with future frame " + string(futureFrame) + " on frame " + string(pfo_get_frame()));
            c |= (global.pfo_inputCommandToSend      & int64((1 << PFO.InputCommandBits)      - 1));
            c |= (global.pfo_inputCommandParamToSend & int64((1 << PFO.InputCommandParamBits) - 1)) << PFO.InputCommandBits;
            global.pfo_inputCommandFutureSendFrame = futureFrame;
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
    global.pfo_inputCommand = int64(0);
    global.pfo_inputCommandParam = int64(0);

    for (var p = 0; p < real(PFO.MaxPlayers); p++)
    {
        global.pfo_inputCommandEach[p] = int64(0);
        global.pfo_inputCommandParamEach[p] = int64(0);
    }

    if (pfo_is_online())
    {
        for (var p = real(PFO.MaxPlayers) - 1; p >= 0; p--)
        {
            var c = pfo_get_input(p);

            var command      = c                           & int64((1 << int64(PFO.InputCommandBits)) - 1);
            var commandParam = (c >> PFO.InputCommandBits) & int64((1 << int64(PFO.InputCommandParamBits)) - 1);

            if (command != int64(0))
            {
                if (LOG.LEVEL >= LOG.INFO) show_debug_message("Read command from input flags for player " + string(p) + ": " + string(command) + " " + string(commandParam) + " on frame " + string(pfo_get_frame()));
                global.pfo_inputCommand = command;
                global.pfo_inputCommandParam = commandParam;
                global.pfo_inputCommandEach[p] = command;
                global.pfo_inputCommandParamEach[p] = commandParam;
            }
        }
    }

    global.pfo_inputCommandToSend = int64(0);
    global.pfo_inputCommandParamToSend = int64(0);
}

function pfo_send_command_in_progress()
{
    var ret = false;
    if (pfo_is_online() && pfo_get_frame() <= global.pfo_inputCommandFutureSendFrame)
    {
        if (LOG.LEVEL >= LOG.VERBOSE) show_debug_message("Send command in progress until future frame " + string(global.pfo_inputCommandFutureSendFrame) + " on frame " + string(pfo_get_frame()));
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
        global.pfo_inputCommandToSend = command;
        global.pfo_inputCommandParamToSend = commandParam;
        global.pfo_inputCommandFutureSendFrame = pfo_get_frame(); // guarantees the command send will be in progress at least for this frame
    }
    else
    {
        p = argument_count > 2 ? argument[2] : -1;
        if (p >= 0)
        {
            global.pfo_inputCommandEach[p] = command;
            global.pfo_inputCommandParamEach[p] = commandParam;
        }
        else
        {
            global.pfo_inputCommand = command;
            global.pfo_inputCommandParam = commandParam;
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
        inputCommand = global.pfo_inputCommandEach[p];
        commandParam = global.pfo_inputCommandParamEach[p];
    }
    else
    {
        inputCommand = global.pfo_inputCommand;
        commandParam = global.pfo_inputCommandParam;
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
