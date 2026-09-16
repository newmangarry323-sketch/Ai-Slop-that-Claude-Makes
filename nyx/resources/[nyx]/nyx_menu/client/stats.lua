--[[ ==========================================================================
     nyx_menu — kill / death tracking
     --------------------------------------------------------------------------
     Feeds the K / D / ratio capsule in the top bar.

     An honest caveat, because it affects how much you should trust the numbers:
     GTA has no server-side notion of "player A killed player B". The victim's
     client is the only thing that knows who did it, so the victim reports it.
     A modified client can therefore lie about its own deaths and about who got
     the kill. The server rate-limits the claim and verifies the named killer is
     a real connected player, which stops the lazy version of the abuse, but
     these are scoreboard numbers, not an audit log. Do not attach anything
     valuable — payouts, ranks, bans — to them without your own checks.
     ========================================================================== ]]

if not NyxMenu.Stats.enabled then return end

local stats = { kills = 0, deaths = 0 }

--- @return number|nil server id of the killing player, if it was a player
local function resolveKiller(victimPed)
    local killer = GetPedSourceOfDeath(victimPed)
    if not killer or killer == 0 or killer == victimPed then return nil end
    if not DoesEntityExist(killer) then return nil end

    -- Run over by a car: the source of death is the vehicle, so step to whoever
    -- was driving it.
    if IsEntityAVehicle(killer) then
        local driver = GetPedInVehicleSeat(killer, -1)
        if not driver or driver == 0 then return nil end
        killer = driver
    end

    if not IsEntityAPed(killer) then return nil end

    local playerIdx = NetworkGetPlayerIndexFromPed(killer)
    if playerIdx == -1 then return nil end

    local serverId = GetPlayerServerId(playerIdx)
    if serverId == GetPlayerServerId(PlayerId()) then return nil end  -- suicide
    return serverId
end

CreateThread(function()
    local wasDead = false

    while true do
        Wait(400)

        local ped = PlayerPedId()
        local dead = IsPedDeadOrDying(ped, true)

        -- Rising edge only: IsPedDeadOrDying stays true for the whole time the
        -- player is on the floor, and we want one report per death.
        if dead and not wasDead then
            local killer = resolveKiller(ped)
            if killer or NyxMenu.Stats.countEnvironmentDeaths then
                TriggerServerEvent('nyx_menu:died', killer)
            end
        end

        wasDead = dead
    end
end)

RegisterNetEvent('nyx_menu:stats', function(payload)
    stats.kills = payload.kills or 0
    stats.deaths = payload.deaths or 0
    TriggerEvent('nyx_menu:statsChanged', stats)
end)

function NyxMenu.GetStats()
    local ratio = stats.deaths > 0 and (stats.kills / stats.deaths) or stats.kills
    return {
        kills = stats.kills,
        deaths = stats.deaths,
        ratio = ('%.2f'):format(ratio)
    }
end

exports('GetStats', function() return NyxMenu.GetStats() end)
