function scrFunction(objectId, func)
{
    return { objectId: objectId, func: func, defaultValue: argument_count > 2 ? argument[2] : -1 };
}

function scrCallFunction(func)
{
    with (func.objectId)
    {
        return script_execute(func.func);
    }

    return func.defaultValue;
}

function scr01_GetFavoredPlayer()
{
    if (numPlayers == 1)
        return 0;

    if (state == 2 && alarm[0] < blackTransitionLength && !firstSpawn && numLives > 0)
        return (currPlayer + 1) % numPlayers;

    if (state >= 0 && state <= 2)
        return currPlayer;

    return 0;
}

function scr08_GetFavoredPlayer()
{
    if (state != MGSTATE_INACTIVE)
        return -1;

    return 0;
}

function scr13_GetFavoredPlayer()
{
    if (numPlayers == 1)
        return 0;

    if (futureState == STATE_TURN_READY || futureState == STATE_TURN_GO || (state == STATE_LEVEL_PREVIEW && subState >= 2))
        return switchSides ? (player % numPlayers) : (player - 1);

    if (futureState == STATE_TURN_END)
        return switchSides ? (player - 1) : (player % numPlayers);

    return -1;
}

function scr14_GetFavoredPlayer()
{
    if (numHumanPlayers < 2 || hotseat)
        return 0;

    if (state >= STATE_TURN_MAIN && state <= STATE_TURN_STABLE && !ai[currPlayer])
    {
        if (!global.onlineSimultaneousTurns)
            return currPlayer;

        if (currPlayer == pfo_client_get_player_index())
            return -2;
    }

    return -1;
}

function scr20_GetFavoredPlayer()
{
    if (numPlayers == 1)
        return 0;

    if (futureState == STATE_PLAYER_HANDOFF)
        return (cp + 1) % numPlayers;

    return cp;
}

function scr23_GetFavoredPlayer()
{
    if (global.numPlayers == 1)
        return 0;

    if ((state == STATE_CHARACTER_SELECT && subState == 2) || (state == STATE_STAGE_POST && global.g23_currStage < (array_length(stageSequence) - 1)))
        return 0;

    if (state == STATE_STAGE_INTRO || state == STATE_GAMEPLAY)
    {
        for (var i = 0; i < global.numPlayers; i++)
        {
            if (player[i].myTurn)
                return i;
        }

        return 0;
    }

    return -1;
}

function scr29_GetFavoredPlayer()
{
    if (numPlayers == 1)
        return 0;

    if (futureState == STATE_RESPAWN && !firstSpawn)
        return (currPlayer + 1) % numPlayers;

    if (futureState == STATE_LEVEL_PLAY || futureState == STATE_RESPAWN)
        return currPlayer;

    return 0;
}

function scr30_GetFavoredPlayer()
{
    if (numPlayers == 1)
        return 0;

    if ((state == STATE_2P_GAMBLE && subState >= 3) || (futureState >= STATE_PREP_FIGHT && futureState <= STATE_GAME_OVER))
        return controller - 1;

    return -1;
}

function scr33_GetFavoredPlayer()
{
    if (numPlayers == 1)
        return 0;

    if ((state == STATE_MAP && subState == 8 && stateTimer > 0) || (state == STATE_PLAY && subState == 10 && stateTimer > 2))
        return player % numPlayers;

    if ((mode == MODE_SKIRMISH) ? (state == STATE_PLAY || futureState == STATE_MATCH_OVER) : (state == STATE_WAR_SETUP || (futureState >= STATE_MAP_GEN && futureState <= STATE_MATCH_OVER)))
        return player - 1;

    if (mode == MODE_SKIRMISH && (state == STATE_LEVEL_GEN || state == STATE_ARMY_SETUP))
        return attacker - 1;

    return -1;
}

function scr36_GetFavoredPlayer()
{
    if (numPlayers == 1)
        return 0;

    if (futureState == STATE_START_PARTY && time[0] != NUM_DAYS)
        return (cp + 1) % numPlayers;

    return cp;
}

function scr50_GetFavoredPlayer()
{
    if (numPlayers == 1 || hotseat)
        return 0;

    if (futureState == STATE_END_TURN)
        return player % numPlayers;

    if ((controller == player && (futureState >= STATE_START_TURN && futureState <= STATE_END_TURN)) || futureState == STATE_END_COMBAT || (state == STATE_LEVEL_GEN && subState == 2))
        return player - 1;

    return -1;
}
