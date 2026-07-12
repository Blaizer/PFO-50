function scr14_Sfx(arg0, arg1)
{
    if (numHumanPlayers >= 2 && !hotseat && (global.onlineSimultaneousTurns || pfo_is_online()))
    {
        var playerIndex = pfo_client_get_player_index();
        if (playerIndex != currPlayer && playerIndex >= 0 && playerIndex < 3 && !ai[playerIndex])
        {
            exit;
        }
    }
    
    scrSfx(arg0, arg1);
}

function scr14_OnlineHideInfo()
{
    if (numHumanPlayers >= 2 && !hotseat && state >= STATE_TURN_SETUP && state <= STATE_TURN_STABLE && currPlayer < 3 && !ai[currPlayer] && !global.onlineSimultaneousTurns && pfo_is_online())
    {
        var playerIndex = pfo_client_get_player_index();
        return playerIndex != currPlayer && playerIndex >= 0 && playerIndex < 3 && !ai[playerIndex];
    }

    return false;
}
