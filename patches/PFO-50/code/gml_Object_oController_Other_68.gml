var param = async_load;

var type = ds_map_find_value(param, "type");

if (type == network_type_disconnect)
{
    var steamId = ds_map_find_value(param, "steam_id");
    var name = steam_get_user_persona_name_sync(steamId);
    if (!is_string(name) || name == "")
    {
        name = global.EXTERNAL_TEXT_ERROR;
    }

    alertMessageText = name + " Disconnected";
    alertMessageTimer = current_time + 5000;
}
