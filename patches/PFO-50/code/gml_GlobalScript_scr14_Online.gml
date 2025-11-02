function scr14_Sfx(arg0, arg1)
{
    if (numPlayers == 2 && (global.onlineSimultaneousTurns || pfo_is_online()) && pfo_client_get_player_index() == !currPlayer)
        exit;
    
    scrSfx(arg0, arg1);
}

function scr14_OnlineHideInfo()
{
    return numPlayers == 2 && state >= STATE_TURN_SETUP && state <= STATE_TURN_STABLE && currPlayer < numPlayers && !global.onlineSimultaneousTurns && pfo_is_online() && pfo_client_get_player_index() == !currPlayer;
}
