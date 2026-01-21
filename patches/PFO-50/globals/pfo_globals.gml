#macro LOG_LEVEL PFO_LogLevel.Info
#macro DEBUG:LOG_LEVEL PFO_LogLevel.Debug
#macro LOG pfo_show_debug_message
#macro DEBUG:LOG show_debug_message
#macro LOG_VERBOSE if (LOG_LEVEL <= PFO_LogLevel.Verbose) LOG
#macro LOG_DEBUG if (LOG_LEVEL <= PFO_LogLevel.Debug) LOG
#macro LOG_INFO if (LOG_LEVEL <= PFO_LogLevel.Info) LOG

enum PFO_LogLevel
{
    Verbose,
    Debug,
    Info
}

enum PFO_OnlineState
{
    Offline,
    Online,
    Disconnecting
}

enum PFO_InputDelayMode
{
    Automatic,
    Manual
}
