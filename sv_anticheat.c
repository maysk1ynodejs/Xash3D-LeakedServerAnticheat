#include "common.h"
#include "server.h"
#include "net_msg.h"

#define AC_MAX_WARN_COUNT    10
#define AC_SPEEDHACK_WINDOW  0.05f
#define AC_TRACE_FRAC_MIN    0.9f

cvar_t sv_block_nash3d    = { "sv_block_nash3d",    "0", FCVAR_SERVER, "block nash3d" };
cvar_t sv_block_ebash3d   = { "sv_block_ebash3d",   "0", FCVAR_SERVER, "block ebash3d" };
cvar_t sv_anticheat_debug = { "sv_anticheat_debug", "0", FCVAR_SERVER, "anti cheat debug" };
cvar_t sv_anti_wall       = { "sv_anti_wall",       "0", FCVAR_SERVER, "block wallhack" };
cvar_t sv_anti_aim        = { "sv_anti_aim",        "0", FCVAR_SERVER, "block aimbot" };
cvar_t sv_anti_speedhack  = { "sv_anti_speedhack",  "0", FCVAR_SERVER, "enable timing-based speedhack detection and reset (1 to enable, 0 to disable)" };
cvar_t sv_speedhack_kick  = { "sv_speedhack_kick",  "0", FCVAR_SERVER, "number of speedhack warns before automatic kick (0 to disable)" };
cvar_t sv_speedhack_re    = { "sv_speedhack_re",    "0", FCVAR_SERVER, "reset player position and velocity on speedhack detection (1 to enable, 0 to disable)" };

static const char *nash3d_names[] = {
    "nash3d",
    "nash3d_aim",
    "nash3d_speed",
    "nash3d_mult",
    "nash3d_friend_add",
    "nash3d_friend_list",
    "nash3d_friend_remove",
    NULL
};

static const char *ebash3d_names[] = {
    "ebash3d_aimbot",
    "ebash3d_oscs_bypass",
    NULL
};

static qboolean AC_MatchUserAgent( const char *useragent, const char **list )
{
    int i;

    if( !useragent || !list )
        return false;

    for( i = 0; list[i] != NULL; i++ )
    {
        if( !Q_strcasecmp( useragent, list[i] ) )
            return true;
    }

    return false;
}

static qboolean AC_CheckNash3d( sv_client_t *cl )
{
    const char *ua;

    if( sv_block_nash3d.value == 0.0f )
        return false;

    ua = cl->useragent;

    if( !Q_strncasecmp( ua, "nash3d", 6 ) )
        return true;

    return AC_MatchUserAgent( ua, nash3d_names );
}

static qboolean AC_CheckEbash3d( sv_client_t *cl )
{
    const char *ua;

    if( sv_block_ebash3d.value == 0.0f )
        return false;

    ua = cl->useragent;

    return AC_MatchUserAgent( ua, ebash3d_names );
}

void SV_AntiCheat_Init( void )
{
    Cvar_RegisterVariable( &sv_block_nash3d );
    Cvar_RegisterVariable( &sv_block_ebash3d );
    Cvar_RegisterVariable( &sv_anticheat_debug );
    Cvar_RegisterVariable( &sv_anti_wall );
    Cvar_RegisterVariable( &sv_anti_aim );
    Cvar_RegisterVariable( &sv_anti_speedhack );
    Cvar_RegisterVariable( &sv_speedhack_kick );
    Cvar_RegisterVariable( &sv_speedhack_re );

    MsgDev( D_INFO, "[AntiCheat] System initialized - Anti-Cheat by Ernyzas\n" );
}

void SV_AntiCheat_Shutdown( void )
{
    MsgDev( D_INFO, "[AntiCheat] System shutdown\n" );
}

void SV_AntiCheat_AllocClientData( sv_client_t *cl )
{
    if( !cl || !SV_IsValidClient( cl ) )
        return;

    if( cl->isBot || cl->local )
        return;

    if( cl->ac_data )
        return;

    cl->ac_data = Mem_Alloc( sv.mempool, sizeof( ac_clientdata_t ) );
    cl->ac_data->first_warn_time = sv.time;
    cl->ac_data->warn_count = 0;
    cl->ac_data->speedhack_flags = 0;
}

void SV_AntiCheat_FreeClientData( sv_client_t *cl )
{
    if( !cl || !cl->ac_data )
        return;

    Mem_Free( cl->ac_data );
    cl->ac_data = NULL;
}

static void AC_KickClient( sv_client_t *cl, const char *reason )
{
    SV_KickClient( cl, reason );
}

void SV_AntiCheat_CheckClient( sv_client_t *cl )
{
    if( !cl || !SV_IsValidClient( cl ) )
        return;

    if( cl->isBot || cl->local )
        return;

    if( sv_anticheat_debug.value != 0.0f )
        MsgDev( D_INFO, "[AntiCheat] Checking client %s\n", cl->name );

    if( AC_CheckNash3d( cl ) )
    {
        MsgDev( D_INFO, "[AntiCheat] %s kicked - nash3d\n", cl->name );
        AC_KickClient( cl, "Illegal client use original xash engine" );
        return;
    }

    if( AC_CheckEbash3d( cl ) )
    {
        MsgDev( D_INFO, "[AntiCheat] %s kicked - ebash3d\n", cl->name );
        AC_KickClient( cl, "Illegal client use original xash engine" );
        return;
    }
}

void SV_AntiCheat_SpeedhackDetect( sv_client_t *cl, usercmd_t *ucmd )
{
    double server_time;
    double client_delta;
    double client_time;
    float  window;
    int    warn_limit;
    edict_t *ent;

    if( !cl || !ucmd )
        return;

    if( cl->isBot || cl->local )
        return;

    if( sv_anti_speedhack.value == 0.0f )
        return;

    server_time = sv.time;
    client_delta = (double)ucmd->msec / 1000.0;
    client_time = cl->ac_time_accum + client_delta;

    if( client_time > server_time )
    {
        if( !cl->ac_speedhack_warned && !cl->local )
        {
            MsgDev( D_INFO, "^3Warning: ^7%s time is faster than server time (speed hack?)\n", cl->name );

            cl->ac_warn_count++;
            cl->ac_speedhack_warned = true;

            warn_limit = (int)sv_speedhack_kick.value;
            if( warn_limit > 0 && cl->ac_warn_count > warn_limit )
            {
                AC_KickClient( cl, "Speed hacks aren't allowed on this server" );
                return;
            }
        }

        cl->ac_time_accum += client_delta;
        return;
    }

    cl->ac_speedhack_warned = false;
    cl->ac_time_accum = 0.0;

    if( sv_anti_speedhack.value == 0.0f )
        return;

    window = sv_speedhack_re.value != 0.0f ? AC_SPEEDHACK_WINDOW : 0.05f;

    if( client_time - server_time > window )
    {
        ent = cl->edict;

        if( !cl->ac_speedhack_active )
        {
            cl->ac_speedhack_active = true;
            cl->ac_first_warn_time = (float)server_time;
            cl->ac_warn_count = 0;
        }

        cl->ac_warn_count++;

        if( ent && sv_speedhack_re.value != 0.0f )
        {
            VectorCopy( cl->ac_safe_origin, ent->v.origin );
            VectorClear( ent->v.velocity );
            SV_LinkEdict( ent, false );
        }

        if( server_time - cl->ac_first_warn_time > sv_speedhack_kick.value && sv_speedhack_kick.value > 0.0f )
        {
            MsgDev( D_INFO, "^1Speed hacks aren't allowed ^2Anti Cheat by Ernyzas\n" );
            AC_KickClient( cl, "Speed hacks aren't allowed on this server" );
            return;
        }

        cl->ac_time_excess = window + server_time;
        cl->ac_time_accum = -client_delta;
        return;
    }

    if( cl->ac_speedhack_active )
        cl->ac_speedhack_active = false;

    if( ent = cl->edict )
        VectorCopy( ent->v.origin, cl->ac_safe_origin );
}

static qboolean AC_TraceVisible( const vec3_t from, const vec3_t to, edict_t *skip )
{
    trace_t tr;

    tr = SV_Move( from, vec3_origin, vec3_origin, to, MOVE_NOMONSTERS, skip );

    return tr.fraction >= AC_TRACE_FRAC_MIN;
}

qboolean SV_AntiCheat_FilterEntity( sv_client_t *cl, edict_t *ent )
{
    vec3_t  eye;
    vec3_t  ent_center;
    edict_t *cl_ent;
    vec3_t  mins, maxs;
    int     i, j, k;

    if( !cl || !ent )
        return true;

    cl_ent = cl->edict;
    if( !cl_ent )
        return true;

    if( cl_ent == ent )
        return true;

    if( sv_anti_wall.value == 0.0f && sv_anti_aim.value == 0.0f )
        return true;

    VectorAdd( cl_ent->v.origin, cl_ent->v.view_ofs, eye );

    VectorAdd( ent->v.absmin, ent->v.absmax, ent_center );
    VectorScale( ent_center, 0.5f, ent_center );

    if( sv_anti_wall.value > 0.0f )
    {
        vec3_t corners[8];
        qboolean visible = false;

        VectorCopy( ent->v.absmin, mins );
        VectorCopy( ent->v.absmax, maxs );

        for( i = 0; i < 2 && !visible; i++ )
        {
            for( j = 0; j < 2 && !visible; j++ )
            {
                for( k = 0; k < 2 && !visible; k++ )
                {
                    vec3_t corner;
                    corner[0] = i ? maxs[0] : mins[0];
                    corner[1] = j ? maxs[1] : mins[1];
                    corner[2] = k ? maxs[2] : mins[2];

                    if( AC_TraceVisible( eye, corner, cl_ent ) )
                        visible = true;
                }
            }
        }

        if( !visible )
        {
            if( AC_TraceVisible( eye, ent_center, cl_ent ) )
                visible = true;
        }

        if( !visible )
            return false;
    }

    if( sv_anti_aim.value > 0.0f )
    {
        if( ent->v.health > 0.0f && cl_ent->v.health > 0.0f )
        {
            if( !AC_TraceVisible( eye, ent_center, cl_ent ) )
                return false;
        }
    }

    return true;
}

void SV_AntiCheat_OnClientConnect( sv_client_t *cl )
{
    if( !cl )
        return;

    SV_AntiCheat_AllocClientData( cl );
    SV_AntiCheat_CheckClient( cl );
}

void SV_AntiCheat_OnClientDisconnect( sv_client_t *cl )
{
    if( !cl )
        return;

    SV_AntiCheat_FreeClientData( cl );
}

void SV_AntiCheat_Frame( sv_client_t *cl, usercmd_t *ucmd )
{
    if( !cl || !ucmd )
        return;

    if( cl->state != cs_spawned )
        return;

    SV_AntiCheat_SpeedhackDetect( cl, ucmd );
    SV_AntiCheat_CheckClient( cl );
}
