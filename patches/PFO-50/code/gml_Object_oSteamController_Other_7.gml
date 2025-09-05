steam_update();

if (!global.steamReady)
{
    if (steam_initialised())
    {
        if (steam_stats_ready())
            global.steamReady = true;
    }
}

if (steam_is_overlay_activated())
    global.steamOverlayActivated = true;
else
    global.steamOverlayActivated = false;

if (pfo_is_online())
{
    for (var i = 0; i < 10; i++)
    {
        if (keyboard_check_pressed(ord("0") + i))
        {
            if (keyboard_check(vk_shift))
            {
                pfo_request_input_delay_change(PFO_InputDelayMode.ManualSelf, int64(i));
            }
            else
            {
                pfo_request_input_delay_change(PFO_InputDelayMode.ManualAll, int64(i));
            }
        }
    }

    if (keyboard_check_pressed(vk_backspace))
    {
        pfo_request_input_delay_change(PFO_InputDelayMode.AutomaticShared);
        pfo_send_command(Command.SetFavoredPlayer, int64(15));
    }

    if (keyboard_check_pressed(189))
    {
        pfo_send_command(Command.SetFavoredPlayer, int64(pfo_get_online_player_index()));
    }
    else if (keyboard_check_pressed(187))
    {
        pfo_send_command(Command.SetFavoredPlayer, int64(pfo_get_online_player_index() == 1 ? 0 : 1));
    }

    // var arg = [];
    // if (pfo_receive_command(Command.SetFavoredPlayer, arg))
    // {
    //     pfo_set_input_delay_favored_player_index(arg[0] == int64(15) ? -1 : arg[0]);
    // }

    pfo_set_input_delay_favored_player_index(global.onlineFavoredPlayer);
    global.onlineFavoredPlayer = -1;
}

if (!pfo_update())
{
    show_message("Error: PFO.dll does not exist or failed to load.\n\nPlease make sure you have copied PFO.dll into the same folder as data.win.");
    game_end();
}

pfo_update_extra_input();

enum Command
{
    None     = 0,
    
    Back     = 1,
    Unpause  = 2,

    SetFavoredPlayer = 3,
}

enum PFO_InputDelayMode
{
    ManualAll          = 0,
    ManualSelf         = 1,
    AutomaticShared    = 2,
    AutomaticExclusive = 3,
}
