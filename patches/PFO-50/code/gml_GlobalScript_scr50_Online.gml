function scr50_HasOnlinePlayers()
{
    return pfo_get_assigned_clients_count() >= 2;
}

function scr50_IsOnlineLocalPlayer(arg0)
{
    return pfo_get_assigned_clients_count() >= 2 && pfo_client_get_player_index() == (arg0 - 1);
}

function scr50_IsOnlineRemotePlayer(arg0)
{
    return pfo_get_assigned_clients_count() >= 2 && pfo_client_get_player_index() == !(arg0 - 1);
}
