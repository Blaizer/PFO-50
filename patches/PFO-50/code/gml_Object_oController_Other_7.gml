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

global.onlineFavoredPlayer = -1;
global.onlineRunUpdate = true;
