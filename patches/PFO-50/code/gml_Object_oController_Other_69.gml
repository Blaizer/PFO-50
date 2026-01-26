var param = async_load;
LOG_DEBUG("Steam Async Event: " + json_encode(param));

var eventType = ds_map_find_value(param, "event_type");
if (eventType == "lobby_join_requested")
{
    var lobbyId = ds_map_find_value(param, "lobby_id");

    if (is_int64(lobbyId) && lobbyId != int64(0))
    {
        if (pfo_is_online())
        {
            scrOnlineCleanup(true);
        }
        else
        {
            scrUnpause();
            scrCloseProfile();

            global.attractModeLibraryTimer = 0;

            if (room != rmLibrary)
            {
                scrExitToLibrary();
                scrClearCheats();
            }
        }

        global.roomPrev = rmLibrary;
        global.SKIP_INTRO = lobbyId;
        room_goto(rmLibrary);
    }
}
