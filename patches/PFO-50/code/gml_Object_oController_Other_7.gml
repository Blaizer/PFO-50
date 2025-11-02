steam_update();

if (!global.steamReady)
{
    if (steam_initialised())
    {
        if (steam_stats_ready())
        {
            global.steamReady = true;
        }
    }
}

if (steam_is_overlay_activated())
{
    global.steamOverlayActivated = true;
}
else
{
    global.steamOverlayActivated = false;
}

pfo_set_input_delay_favored_client_index(global.onlineFavoredPlayer >= 0 ? pfo_player_get_client_index(global.onlineFavoredPlayer) : -1);

if (global.onlineRunUpdate)
{
    pfo_update_time();
}

if (!pfo_update(global.onlineRunUpdate))
{
    show_message("Error: PFO.dll does not exist or failed to load.\n\nPlease make sure you have copied PFO.dll into the same folder as data.win.");
    game_end();
}

if (global.onlineRunUpdate)
{
    pfo_update_extra_input();
}

if (!global.paused && pfo_get_frame() > 10 && pfo_client_get_player_index() < 0 && !instance_exists(global.onlineSpectatorPauseMenu))
{
    with (oScreenHandler)
    {
        scrGetInput(0, GetInputType.Raw);

        if (pressStart)
        {
            global.onlineSpectatorPauseMenu = instance_create_depth(-32, -32, -16000, oPauseMenu, { menuType: 1 });

            with (global.onlineSpectatorPauseMenu)
            {
                persistent = true;
            }
        }
    }
}

global.onlineFavoredPlayer = -1;
global.onlineRunUpdate = true;
