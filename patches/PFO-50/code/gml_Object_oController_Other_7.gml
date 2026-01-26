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

if (global.onlineRunUpdate)
{
    pfo_update_time();
}

pfo_update(global.onlineRunUpdate)

if (global.onlineRunUpdate)
{
    pfo_update_extra_input();
}

if (!global.paused && pfo_get_frame() > 10 && pfo_client_get_player_index() < 0 && !instance_exists(global.onlineSpectatorPauseMenu))
{
    with (oScreenHandler)
    {
        scrInputClear();
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

global.onlineRunUpdate = true;
