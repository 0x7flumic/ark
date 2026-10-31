local existing = rawget(_G, "__arkhan")
if type(existing) == "table" and existing.version == "0.4.0-selective" and
   type(existing.nativeUiFrame) == "function" and
   type(existing.autofarmFrame) == "function" and
   type(existing.autoquestFrame) == "function" then
    return "ARKHAN_BOOTSTRAP_ALREADY_READY"
end

local Arkhan = existing or {}
Arkhan.version = "0.4.0-selective"
Arkhan.startedAt = os.time()
Arkhan.modules = Arkhan.modules or {}

local function safeType(value)
    local ok, result = pcall(type, value)
    return ok and result or "error"
end

local function loadUtils()
    local ok, value = pcall(require, "Common.Utils.Utils")
    if ok and type(value) == "table" then return value, nil end
    local firstError = tostring(value)
    ok, value = pcall(require, "Common/Utils/Utils")
    if ok and type(value) == "table" then return value, nil end
    return nil, firstError .. " | " .. tostring(value)
end



local function clampInteger(value, minimum, maximum, fallback)
    value = tonumber(value)
    if not value then return fallback end
    return math.max(minimum, math.min(maximum, math.floor(value)))
end

local function safeRead(object, key)
    if object == nil then return nil end
    local ok, value = pcall(function() return object[key] end)
    if ok then return value end
    return nil
end

local function safeCall(method, receiver, ...)
    if type(method) ~= "function" then return false, nil end
    return pcall(method, receiver, ...)
end

local function scalar(value, limit)
    if value == nil then return nil end
    if type(value) == "number" or type(value) == "boolean" then return value end
    local ok, rendered = pcall(tostring, value)
    if not ok then return nil end
    limit = limit or 160
    if #rendered > limit then return rendered:sub(1, limit) .. "…" end
    return rendered
end

local function requireTable(name)
    local ok, value = pcall(require, name)
    if ok and type(value) == "table" then return value, nil end
    return nil, scalar(value, 220) or "require-failed"
end

local function callGlobal(runtimePg, name, ...)
    local method = safeRead(runtimePg, name)
    if type(method) ~= "function" then return false, nil end
    local ok, result = pcall(method, ...)
    if ok then return true, result end
    return pcall(method, runtimePg, ...)
end

local function localizedText(runtimePg, key)
    if key == nil or key == "" then return "" end
    local ok, value = callGlobal(runtimePg, "getLocalizationText", key)
    if not ok or value == nil then return "" end
    return scalar(value, 220) or ""
end

local function readAxis(position, index, lower, upper)
    local value = safeRead(position, index)
    if tonumber(value) then return tonumber(value) end
    value = safeRead(position, lower)
    if tonumber(value) then return tonumber(value) end
    return tonumber(safeRead(position, upper))
end

local function positionOf(entity)
    local ok, position = safeCall(safeRead(entity, "getPosition"), entity)
    if not ok or position == nil then return nil end
    local x = readAxis(position, 1, "x", "X")
    local y = readAxis(position, 2, "y", "Y")
    local z = readAxis(position, 3, "z", "Z")
    if x == nil or y == nil or z == nil then return nil end
    return { x = x, y = y, z = z }
end

local function distanceBetween(left, right)
    local dx, dy, dz = left.x - right.x, left.y - right.y, left.z - right.z
    return math.sqrt(dx * dx + dy * dy + dz * dz)
end

local function predicate(utils, name, ...)
    local method = safeRead(utils, name)
    if type(method) ~= "function" then return nil, "missing" end
    local ok, value = pcall(method, ...)
    if not ok then return nil, scalar(value, 120) or "error" end
    return value == true, nil
end

local function callBool(entity, name)
    local ok, value = safeCall(safeRead(entity, name), entity)
    return ok and value == true
end

local function classTypeOf(entity)
    local ok, value = safeCall(safeRead(entity, "getClassType"), entity)
    return ok and (scalar(value, 80) or "") or ""
end

local function configHints(entity)
    local ok, config = safeCall(safeRead(entity, "getConfigData"), entity)
    if not ok or config == nil then return {}, "" end
    local hints = {}
    for _, key in ipairs({ "name", "displayName", "actorType", "npcType", "petPrototypeId", "speciesId", "modelId", "interactionType", "label" }) do
        local value = scalar(safeRead(config, key), 120)
        if value ~= nil and value ~= "" then hints[key] = value end
    end
    return hints, safeType(config)
end

local function entityTemplateId(entity)
    if entity == nil then return nil end
    for _, key in ipairs({ "templateId", "configId", "prototypeId", "staticId" }) do
        local value = tonumber(safeRead(entity, key))
        if value then return value end
    end
    local ok, config = safeCall(safeRead(entity, "getConfigData"), entity)
    if ok and config then
        for _, key in ipairs({ "templateId", "id", "configId", "prototypeId" }) do
            local value = tonumber(safeRead(config, key))
            if value then return value end
        end
    end
    return nil
end

local function classify(utils, origin, runtimePg, entity)
    if entity == origin then return "controlled", {} end
    if entity == safeRead(runtimePg, "me") then return "player", {} end
    if entityTemplateId(entity) == 11042 then return "chest", {} end
    local checks = {
        { "isCollectItem", "collect-item" }, { "isChest", "chest" },
        { "isPlayerPet", "player-pet" }, { "isNpc", "npc" },
        { "isCreation", "creation" }, { "isInteractor", "interactor" },
        { "isPet", "pet" }
    }
    for _, check in ipairs(checks) do
        if predicate(utils, check[1], entity) == true then return check[2], {} end
    end
    if predicate(utils, "isPuppet", entity) == true then
        if origin and predicate(utils, "isEnemy", origin, entity) == true then return "enemy-puppet", {} end
        return "puppet", {}
    end
    return "other", {}
end

local function displayName(runtimePg, entity, hints)
    local ok, name = safeCall(safeRead(entity, "getName"), entity)
    if not ok or name == nil or name == "" then
        name = safeRead(entity, "name") or safeRead(entity, "displayName") or
               (hints and (hints.name or hints.displayName))
    end
    local key = scalar(name, 120)
    if key == nil then return "", nil end
    local localized = localizedText(runtimePg, key)
    if localized ~= "" and localized ~= tostring(key) then return localized, key end
    return tostring(key), key
end

local function unwrapCurrentBddValue(value)
    if type(value) ~= "table" or safeRead(value, "_BddData_") ~= true then return value end
    return rawget(value, "__value") or value
end

local function ensureBddStringKey(key)
    local addresses = rawget(_G, "__BDD_str2addr")
    if type(addresses) ~= "table" then return false end
    if rawget(addresses, key) ~= nil then return true end
    local add = safeRead(rawget(_G, "bdd"), "add_str")
    if type(add) ~= "function" then return false end
    local ok, addr = pcall(add, key)
    if not ok or addr == nil then return false end
    rawset(addresses, key, addr)
    local reverse = rawget(_G, "__BDD_addr2str")
    if type(reverse) == "table" then rawset(reverse, addr, key) end
    return true
end

local function bddUserdataIterator(value)
    if type(value) ~= "userdata" then return nil end
    local ok, meta = pcall(getmetatable, value)
    if not ok or type(meta) ~= "table" then return nil end
    if safeRead(value, "_BddData_") == true and meta == rawget(_G, "__BDD_metatable") then
        local nextBdd = rawget(_G, "bddnext")
        if type(nextBdd) == "function" then
            return function(state, key) return nextBdd(state, key) end, value, nil
        end
    end
    local iterator = rawget(meta, "__pairs")
    if type(iterator) ~= "function" then return nil end
    local pairOk, nextFn, state, key = pcall(iterator, value)
    if pairOk and type(nextFn) == "function" then return nextFn, state, key end
    return nil
end

local function luaTableIterator(value)
    local ok, keys = safeCall(safeRead(value, "GetKeys"), value)
    if not ok or keys == nil then return nil end
    local enumOk, iterator = safeCall(safeRead(keys, "GetEnumerator"), keys)
    if not enumOk or iterator == nil then return nil end
    local nextFn = safeRead(iterator, "MoveNext")
    local getter = safeRead(value, "get_Item")
    if type(nextFn) ~= "function" or type(getter) ~= "function" then return nil end
    local done = false
    return function()
        if done then return nil end
        local movedOk, moved = safeCall(nextFn, iterator)
        if not movedOk or moved ~= true then
            done = true
            safeCall(safeRead(iterator, "Dispose"), iterator)
            return nil
        end
        local key = safeRead(iterator, "Current")
        if key == nil then return nil end
        local itemOk, item = safeCall(getter, value, key)
        return key, itemOk and item or nil
    end
end

local function arkhanTeleportVector(x, y, z)
    local vectorType = rawget(_G, "Vector3")
    if vectorType == nil then return nil end
    local new = safeRead(vectorType, "New")
    if type(new) == "function" then
        local ok, vector = pcall(new, x, y, z)
        if ok and vector ~= nil then return vector end
    end
    local ok, vector = pcall(vectorType, x, y, z)
    if ok then return vector end
    return nil
end

local function arkhanTeleportPawn(runtimePg, target, stopDistance)
    if runtimePg == nil or target == nil then return false, "runtime/target unavailable" end
    local pawn = safeRead(runtimePg, "pawn") or safeRead(runtimePg, "me")
    if pawn == nil then return false, "controlled pawn unavailable" end
    local origin = positionOf(pawn)
    if origin == nil then return false, "controlled pawn position unavailable" end
    local targetX, targetY, targetZ = tonumber(target.x), tonumber(target.y), tonumber(target.z)
    if targetX == nil or targetY == nil or targetZ == nil then return false, "invalid destination" end
    local dx, dz = origin.x - targetX, origin.z - targetZ
    local length = math.sqrt(dx * dx + dz * dz)
    local distance = math.max(0, tonumber(stopDistance) or 1.25)
    if length > 0.001 and distance > 0 then
        targetX = targetX + dx / length * distance
        targetZ = targetZ + dz / length * distance
    end
    targetY = targetY + 0.15
    local move = safeRead(pawn, "forceSetPosEx")
    if type(move) == "function" then
        local ok = safeCall(move, pawn, targetX, targetY, targetZ, true)
        if ok then return true end
    end
    move = safeRead(pawn, "forceSetPos")
    if type(move) == "function" then
        local vector = arkhanTeleportVector(targetX, targetY, targetZ)
        if vector ~= nil then
            local ok = safeCall(move, pawn, vector, true)
            if ok then return true end
        end
    end
    move = safeRead(pawn, "setPositionEx")
    if type(move) == "function" then
        local constants = select(1, requireTable("Common.Const.Const"))
        local reasons = safeRead(constants, "AgentTransformReasonConst")
        local reason = safeRead(reasons, "TeleportFromLua")
        if reason ~= nil then
            local ok = safeCall(move, pawn, targetX, targetY, targetZ, reason)
            if ok then return true end
        end
    end
    return false, "no supported pawn teleport surface"
end

function Arkhan.registerModule(name, module)
    if type(name) ~= "string" or type(module) ~= "table" then return false, "invalid-module" end
    if Arkhan.modules[name] and type(Arkhan.modules[name].shutdown) == "function" then
        pcall(Arkhan.modules[name].shutdown, Arkhan.modules[name])
    end
    Arkhan.modules[name] = module
    return true
end

local RuntimeFeatures = {
    name = "runtime-features",
    state = "ready",
    tickInterval = 0.20,
    timerId = nil,
    tickCount = 0,
    lastTickError = "",
    enabled = {
        infiniteStamina = false,
        noFallDamage = false,
        speedBoost = false,
        autoCollect = false,
        autoChest = false,
        autoQuickCapture = false,
        autoDialogueSkip = false,
        autoCutsceneSkip = false,
        fovOverride = false,
        interconnectionGodMode = false,
        interconnectionNoCooldown = false,
        interconnectionNoCost = false,
        interconnectionDamage = false,
        interconnectionAttackSpeed = false,
        interconnectionAttackFurious = false,
        interconnectionAttackEnhance = false,
        interconnectionAttackHunt = false,
        interconnectionCaptureBoost = false,
        interconnectionInfiUltimate = false
    },
    overloadReliefEnabled = false,
    settings = {
        collectCooldown = 0.45,
        collectRange = 30,
        chestCooldown = 0.75,
        chestRange = 18,
        quickCaptureCooldown = 0.75,
        dialogueSkipCooldown = 0.20,
        cutsceneSkipCooldown = 0.35,
        quickCaptureMinPercent = 100,
        speedMultiplier = 1.50,
        fov = 80,
        interconnectionDamageMultiplier = 2.00,
        interconnectionAttackSpeedMultiplier = 1.50
    },
    lastRun = {
        autoCollect = -999,
        autoChest = -999,
        autoQuickCapture = -999,
        autoDialogueSkip = -999,
        autoCutsceneSkip = -999
    },
    stats = {
        infiniteStamina = { applied = 0, released = 0, errors = 0, last = "idle" },
        noFallDamage = { applied = 0, released = 0, errors = 0, last = "idle" },
        speedBoost = { applied = 0, released = 0, errors = 0, last = "idle" },
        overloadRelief = { requests = 0, errors = 0, last = "idle" },
        autoCollect = {
            actions = 0, requests = 0, rejected = 0, positionRejected = 0, probes = 0,
            learnedHits = 0, localTriggerHits = 0, suppressedBubbles = 0, errors = 0,
            lastSuccessDistance = 0, maxSuccessDistance = 0, last = "idle"
        },
        autoChest = {
            actions = 0, requests = 0, rejected = 0, positionRejected = 0, probes = 0,
            learnedHits = 0, localTriggerHits = 0, suppressedBubbles = 0, skippedCosted = 0, errors = 0,
            lastSuccessDistance = 0, maxSuccessDistance = 0, last = "idle"
        },
        autoQuickCapture = { actions = 0, skippedThreshold = 0, errors = 0, last = "idle" },
        autoDialogueSkip = { actions = 0, errors = 0, last = "idle" },
        autoCutsceneSkip = { actions = 0, errors = 0, last = "idle" },
        fovOverride = { applied = 0, released = 0, errors = 0, last = "idle" },
        interconnection = {
            active = false, controlling = false, enters = 0, exits = 0, swaps = 0,
            abilities = 0, castRewrites = 0, castRpcSeen = 0, buffRequests = 0,
            errors = 0, petId = "0", templateId = "0",
            ultimateCooldownRate = -1, ultimateCooldownMax = -1, ultimateSp = -1, ultimateSpMax = -1,
            last = "idle"
        },
    },
    staminaOwner = nil,
    staminaPriorLocked = nil,
    fallSpace = nil,
    fallPrior = nil,
    speedEntries = {},
    cameraOwner = nil,
    cameraPriorFov = nil,
    lastCutsceneCtrl = nil,
    interactionPending = nil,
    interactionLearning = {
        autoCollect = { targets = {}, profiles = {}, nextProbeAt = 0, lastPruneAt = 0 },
        autoChest = { targets = {}, profiles = {}, nextProbeAt = 0, lastPruneAt = 0 }
    },
    bubbleFilterInstalled = false,
    bubbleFilterOriginal = nil,
    bubbleFilterWrapper = nil,
    interconnection = {
        pet = nil,
        petActorId = nil,
        player = nil,
        playerCaptureBuffSent = false,
        lastPlayerCaptureBuffAt = -999,
        invincibleBuffSent = false,
        lastInvincibleBuffAt = -999,
        featureBuffSent = {},
        lastBuffRefreshAt = {},
        ultimateBuffSent = false,
        ultimateBuffActorId = nil,
        ultimateRecyclePending = false,
        ultimateLastRecycleAt = -999,
        castingEnergyPassiveSent = false,
        castingEnergyPassiveConfirmed = false,
        castingEnergyPassiveLastRequestAt = -999,
        castingEnergyPassiveMissingSince = nil,
        castingEnergyActorId = nil,
        castingEnergyLastFillAt = -999
    },
    playerGodMode = { owner = nil, buffSent = false, lastRequestAt = -999 },
    overloadRelief = { lastRequestByBuff = {} },
}

local featureNames = {
    infiniteStamina = true,
    noFallDamage = true,
    speedBoost = true,
    autoCollect = true,
    autoChest = true,
    autoQuickCapture = true,
    autoDialogueSkip = true,
    autoCutsceneSkip = true,
    fovOverride = true,
    interconnectionGodMode = true,
    interconnectionNoCooldown = true,
    interconnectionNoCost = true,
    interconnectionDamage = true,
    interconnectionAttackSpeed = true,
    interconnectionAttackFurious = true,
    interconnectionAttackEnhance = true,
    interconnectionAttackHunt = true,
    interconnectionCaptureBoost = true,
    interconnectionInfiUltimate = true
}

local function featureNow()
    local Time = select(1, requireTable("Core.Common.Time"))
    if Time then
        local value = tonumber(safeRead(Time, "realtimeSinceStartup")) or tonumber(safeRead(Time, "realSecondCache"))
        if value then return value end
        local method = safeRead(Time, "getRealSecond")
        local ok, result = safeCall(method, Time)
        if ok and tonumber(result) then return tonumber(result) end
    end
    return os.clock()
end

local function featureError(stats, message)
    stats.errors = (tonumber(stats.errors) or 0) + 1
    stats.last = "error: " .. tostring(scalar(message, 180) or "unknown")
end

local function allEntities(runtimePg)
    local entityMgr = runtimePg and runtimePg.global and runtimePg.global.entityMgr
    local getAll = entityMgr and safeRead(entityMgr, "getAllEntities")
    if type(getAll) ~= "function" then return nil, "entity-manager-not-ready" end
    local ok, entities = pcall(getAll)
    if not ok then ok, entities = safeCall(getAll, entityMgr) end
    if not ok or type(entities) ~= "table" then return nil, "entity-list-not-ready" end
    return entities, nil
end

local function entityInteractionKey(entity)
    local id = scalar(safeRead(entity, "id"), 96)
    if id ~= nil then return tostring(id) end
    local globalId = scalar(safeRead(entity, "globalId"), 96)
    if globalId ~= nil and tostring(globalId) ~= "" then return tostring(globalId) end
    local staticId = scalar(safeRead(entity, "staticId"), 96)
    if staticId ~= nil then return tostring(staticId) end
    return tostring(entity)
end

local function entityProfileKey(featureName, entity)
    local actionPrototypeId = scalar(safeRead(entity, "actionPrototypeId"), 48) or "0"
    local templateId = scalar(safeRead(entity, "templateId"), 48)
        or scalar(safeRead(entity, "configId"), 48)
        or scalar(safeRead(entity, "prototypeId"), 48)
        or "0"
    return tostring(featureName) .. ":" .. tostring(actionPrototypeId) .. ":" .. tostring(templateId)
end

local function entityServerSafeRange(entity)
    local base = 0
    local method = safeRead(entity, "getInteractiveDist")
    if type(method) == "function" then
        local ok, value = safeCall(method, entity)
        if ok then base = math.max(base, tonumber(value) or 0) end
    end

    local cfgOk, cfg = safeCall(safeRead(entity, "getConfigData"), entity)
    if cfgOk and cfg ~= nil then base = math.max(base, tonumber(safeRead(cfg, "interactiveDist")) or 0) end

    local actionPrototypeId = tonumber(safeRead(entity, "actionPrototypeId"))
    local InteractData = select(1, requireTable("Data.interact_data"))
    local data = actionPrototypeId and InteractData and InteractData[actionPrototypeId]
    if data ~= nil then
        base = math.max(base, tonumber(safeRead(data, "interactiveDist")) or 0)
        base = math.max(base, tonumber(safeRead(data, "interactiveIconDist")) or 0)
    end

    if base <= 0 then return nil, nil, nil end
    local Const = select(1, requireTable("Common.Const.Const"))
    local tolerance = tonumber(Const and safeRead(Const, "SERVER_POS_CHECK_TOLERANCE")) or 1
    return math.max(0, base + tolerance), base, tolerance
end

local function entityTriggerValid(entity)
    local triggerId = safeRead(entity, "triggerId")
    local model = safeRead(entity, "eModel")
    local check = model and safeRead(model, "CheckTriggerDistanceValid")
    if triggerId == nil or type(check) ~= "function" then return nil end
    local ok, value = safeCall(check, model, triggerId)
    if not ok then return nil end
    return value == true
end

local function interactionCenterOf(entity)
    local base = positionOf(entity)
    if base == nil then return nil, "entity-center" end

    local offsetMethod = safeRead(entity, "getInteractLocalOffset")
    if type(offsetMethod) ~= "function" then return base, "entity-center" end
    local offsetOk, localOffset = safeCall(offsetMethod, entity)
    if not offsetOk or localOffset == nil then return base, "entity-center" end

    local rotationMethod = safeRead(entity, "getRotation")
    local rotationOk, rotation = safeCall(rotationMethod, entity)
    local mul = rotation and safeRead(rotation, "MulVec3")
    if not rotationOk or rotation == nil or type(mul) ~= "function" then return base, "entity-center" end

    local worldOffsetOk, worldOffset = safeCall(mul, rotation, localOffset)
    if not worldOffsetOk or worldOffset == nil then return base, "entity-center" end
    local ox = readAxis(worldOffset, 1, "x", "X")
    local oy = readAxis(worldOffset, 2, "y", "Y")
    local oz = readAxis(worldOffset, 3, "z", "Z")
    if ox == nil or oy == nil or oz == nil then return base, "entity-center" end
    return { x = base.x + ox, y = base.y + oy, z = base.z + oz }, "interaction-offset"
end

local function interactionDistanceOf(entity, pawnPos)
    local entityPos = positionOf(entity)
    if entityPos == nil then return nil, nil, "unavailable" end
    local interactionPos, source = interactionCenterOf(entity)
    if interactionPos == nil then interactionPos, source = entityPos, "entity-center" end
    return distanceBetween(pawnPos, interactionPos), distanceBetween(pawnPos, entityPos), source
end

local function learningProfile(learning, signature, featureName)
    local profile = learning.profiles[signature]
    if profile ~= nil then return profile end
    local initialStep = featureName == "autoChest" and 5.0 or 2.5
    profile = {
        safeExtra = 0,
        step = initialStep,
        minStep = 0.75,
        maxStep = featureName == "autoChest" and 8.0 or 4.0,
        minRejectedExtra = nil,
        successes = 0,
        rejects = 0,
        probes = 0,
        nextProbeAt = 0,
        lastOutcome = "new"
    }
    learning.profiles[signature] = profile
    return profile
end

local function learningTarget(learning, key, signature, now)
    local target = learning.targets[key]
    if target == nil then
        target = {
            signature = signature,
            lastSeen = now,
            nextTryAt = 0,
            retryBelow = nil,
            successes = 0,
            rejects = 0,
            maxSuccessDistance = 0,
            minRejectedDistance = nil
        }
        learning.targets[key] = target
    else
        target.signature = signature
        target.lastSeen = now
    end
    return target
end

local function pruneInteractionLearning(learning, now)
    if now - (tonumber(learning.lastPruneAt) or 0) < 10 then return end
    learning.lastPruneAt = now
    for key, target in pairs(learning.targets) do
        if now - (tonumber(target.lastSeen) or now) > 120 then learning.targets[key] = nil end
    end
end

local function learningSummary(learning)
    local profileCount, targetCount, maxSafeExtra = 0, 0, 0
    for _, profile in pairs(learning.profiles or {}) do
        profileCount = profileCount + 1
        maxSafeExtra = math.max(maxSafeExtra, tonumber(profile.safeExtra) or 0)
    end
    for _ in pairs(learning.targets or {}) do targetCount = targetCount + 1 end
    return profileCount, targetCount, maxSafeExtra
end

local function profileProbeLimit(profile, safeRange)
    if safeRange == nil then return nil end
    local limit = safeRange + math.max(0, tonumber(profile.safeExtra) or 0) + math.max(0.5, tonumber(profile.step) or 1)
    local rejectedExtra = tonumber(profile.minRejectedExtra)
    if rejectedExtra and profile.rejects >= 2 then
        limit = math.min(limit, safeRange + math.max(0, rejectedExtra - 0.5))
    end
    return limit
end

local function directEntityCandidate(runtimePg, classifierName, range, rejectCostedChest, learning, now, featureName)
    local entities, entityError = allEntities(runtimePg)
    if not entities then return nil, entityError end
    local Utils, utilsError = loadUtils()
    if not Utils then return nil, "utils-not-ready: " .. tostring(utilsError or "missing") end
    local classifier = safeRead(Utils, classifierName)
    if type(classifier) ~= "function" then return nil, "classifier-not-ready: " .. classifierName end
    local pawnPos = positionOf(runtimePg and (runtimePg.pawn or runtimePg.me))
    if not pawnPos then return nil, "controlled-position-not-ready" end

    pruneInteractionLearning(learning, now)
    local best = nil
    local bestRank, bestScore = nil, nil

    for _, entity in pairs(entities) do
        local classOk, isMatch = pcall(classifier, entity)
        if classOk and isMatch == true then
            local distance, centerDistance, distanceSource = interactionDistanceOf(entity, pawnPos)
            if distance and distance <= range then
                local allowed = true
                local canMethod = safeRead(entity, "checkCanInteract")
                if type(canMethod) == "function" then
                    local canOk, canValue = safeCall(canMethod, entity, nil)
                    allowed = canOk and canValue == true
                end
                if allowed and rejectCostedChest then
                    local configOk, config = safeCall(safeRead(entity, "getConfigData"), entity)
                    if configOk and config ~= nil then
                        local showDetail = safeRead(config, "showDetail")
                        local needItem = safeRead(config, "needItem")
                        if showDetail or (type(needItem) == "table" and tonumber(needItem[1] or 0) ~= 0) then
                            allowed = false
                        end
                    end
                end

                if allowed then
                    local key = entityInteractionKey(entity)
                    local signature = entityProfileKey(featureName, entity)
                    local profile = learningProfile(learning, signature, featureName)
                    local target = learningTarget(learning, key, signature, now)
                    local safeRange, baseRange, tolerance = entityServerSafeRange(entity)
                    local triggerValid = entityTriggerValid(entity)
                    local extra = safeRange and math.max(0, distance - safeRange) or nil
                    local retryBlocked = false
                    if target.retryBelow ~= nil then
                        retryBlocked = distance >= tonumber(target.retryBelow) or now < tonumber(target.nextTryAt or 0)
                    elseif now < tonumber(target.nextTryAt or 0) then
                        retryBlocked = true
                    end

                    if not retryBlocked then
                        local rank, mode, score
                        if triggerValid == true then
                            rank, mode, score = 0, "local-trigger", distance
                        elseif safeRange and distance <= safeRange + 0.05 then
                            rank, mode, score = 1, "official-safe", distance
                        elseif safeRange and distance <= safeRange + math.max(0, tonumber(profile.safeExtra) or 0) + 0.05 then
                            rank, mode, score = 2, "learned-safe", distance
                        else
                            local probeLimit = profileProbeLimit(profile, safeRange)
                            local probeReady = now >= (tonumber(learning.nextProbeAt) or 0)
                                and now >= (tonumber(profile.nextProbeAt) or 0)
                            if probeReady and probeLimit and distance <= probeLimit + 0.05 then
                                rank, mode = 3, "adaptive-probe"
                                score = extra or distance
                            end
                        end

                        if rank ~= nil and (best == nil or rank < bestRank or (rank == bestRank and score < bestScore)) then
                            bestRank, bestScore = rank, score
                            best = {
                                entity = entity,
                                key = key,
                                signature = signature,
                                profile = profile,
                                target = target,
                                distance = distance,
                                centerDistance = centerDistance,
                                distanceSource = distanceSource,
                                safeRange = safeRange,
                                baseRange = baseRange,
                                tolerance = tolerance,
                                extra = extra,
                                triggerValid = triggerValid,
                                mode = mode
                            }
                        end
                    end
                end
            end
        end
    end
    return best, nil
end

function RuntimeFeatures:anyEnabled()
    for _, value in pairs(self.enabled) do
        if value == true then return true end
    end
    return self.overloadReliefEnabled == true
end

function RuntimeFeatures:ensureTimer()
    if self.timerId ~= nil then return true end
    local TimerManager, timerError = requireTable("Core.Timer.TimerManager")
    if not TimerManager or type(TimerManager.addRepeatTimer) ~= "function" then
        self.lastTickError = "timer-manager-not-ready: " .. tostring(timerError or "missing-addRepeatTimer")
        return false
    end
    local ok, timerId = pcall(TimerManager.addRepeatTimer, self.tickInterval, function()
        local runOk, runError = pcall(function() self:tick() end)
        if not runOk then self.lastTickError = tostring(scalar(runError, 220) or "feature-tick-failed") end
    end)
    if not ok or timerId == nil then
        self.lastTickError = "feature-timer-create-failed: " .. tostring(scalar(timerId, 180) or "nil")
        return false
    end
    self.timerId = timerId
    self.lastTickError = ""
    return true
end

function RuntimeFeatures:stopTimerIfIdle()
    if self:anyEnabled() or self.timerId == nil then return end
    local TimerManager = select(1, requireTable("Core.Timer.TimerManager"))
    if TimerManager and type(TimerManager.removeTimer) == "function" then pcall(TimerManager.removeTimer, self.timerId) end
    self.timerId = nil
end

function RuntimeFeatures:installInteractionBubbleFilter()
    if self.bubbleFilterInstalled then return true end
    local ClientUtils = select(1, requireTable("Utils.ClientUtils"))
    if type(ClientUtils) ~= "table" then return false end
    local original = safeRead(ClientUtils, "showBubbleMessage")
    if type(original) ~= "function" then return false end

    local function wrapper(noticeId, ...)
        local pending = RuntimeFeatures.interactionPending
        local code = tonumber(noticeId)
        if pending ~= nil and (code == 1002 or code == 1026) then
            local stats = RuntimeFeatures.stats[pending.feature]
            if type(stats) == "table" then stats.suppressedBubbles = (tonumber(stats.suppressedBubbles) or 0) + 1 end
            return
        end
        return original(noticeId, ...)
    end

    self.bubbleFilterOriginal = original
    self.bubbleFilterWrapper = wrapper
    ClientUtils.showBubbleMessage = wrapper
    self.bubbleFilterInstalled = true
    return true
end

function RuntimeFeatures:restoreInteractionBubbleFilter()
    if not self.bubbleFilterInstalled then return end
    local ClientUtils = select(1, requireTable("Utils.ClientUtils"))
    if type(ClientUtils) == "table" and safeRead(ClientUtils, "showBubbleMessage") == self.bubbleFilterWrapper then
        ClientUtils.showBubbleMessage = self.bubbleFilterOriginal
    end
    self.bubbleFilterInstalled = false
    self.bubbleFilterOriginal = nil
    self.bubbleFilterWrapper = nil
end

function RuntimeFeatures:releaseStamina()
    local owner = self.staminaOwner
    if owner ~= nil then
        local setter = safeRead(owner, "setStaminaLocked")
        if type(setter) == "function" then
            local restore = self.staminaPriorLocked == true
            local ok, err = safeCall(setter, owner, restore)
            if ok then
                self.stats.infiniteStamina.released = self.stats.infiniteStamina.released + 1
                self.stats.infiniteStamina.last = restore and "restored prior lock" or "released"
            else featureError(self.stats.infiniteStamina, err) end
        end
    end
    self.staminaOwner = nil
    self.staminaPriorLocked = nil
end

function RuntimeFeatures:tickInfiniteStamina()
    local runtimePg = rawget(_G, "pg")
    local owner = runtimePg and runtimePg.me
    if owner == nil then self.stats.infiniteStamina.last = "waiting for pg.me"; return false end
    if self.staminaOwner ~= owner then
        self:releaseStamina()
        self.staminaOwner = owner
        self.staminaPriorLocked = safeRead(owner, "staminaLocked") == true
    end
    if safeRead(owner, "staminaLocked") == true then self.stats.infiniteStamina.last = "locked"; return false end
    local ok, result = safeCall(safeRead(owner, "setStaminaLocked"), owner, true)
    if not ok then featureError(self.stats.infiniteStamina, result or "setStaminaLocked unavailable"); return false end
    self.stats.infiniteStamina.applied = self.stats.infiniteStamina.applied + 1
    self.stats.infiniteStamina.last = "locked at current stamina"
    return true
end

function RuntimeFeatures:releaseNoFall()
    if type(self.fallSpace) == "table" then rawset(self.fallSpace, "checkImmuneFallDamage", self.fallPrior) end
    if self.fallSpace ~= nil then
        self.stats.noFallDamage.released = self.stats.noFallDamage.released + 1
        self.stats.noFallDamage.last = "restored"
    end
    self.fallSpace, self.fallPrior = nil, nil
end

function RuntimeFeatures:tickNoFallDamage()
    local runtimePg = rawget(_G, "pg")
    local space = runtimePg and runtimePg.me and safeRead(runtimePg.me, "space") or (runtimePg and runtimePg.space)
    if type(space) ~= "table" then self.stats.noFallDamage.last = "space is not patchable"; return false end
    if self.fallSpace ~= space then
        self:releaseNoFall()
        self.fallSpace = space
        self.fallPrior = rawget(space, "checkImmuneFallDamage")
    end
    if rawget(space, "checkImmuneFallDamage") ~= self._noFallFunction then
        self._noFallFunction = self._noFallFunction or function() return true end
        rawset(space, "checkImmuneFallDamage", self._noFallFunction)
        self.stats.noFallDamage.applied = self.stats.noFallDamage.applied + 1
    end
    self.stats.noFallDamage.last = "immune to fall damage"
    return true
end

local SPEED_RATIO_ATTRIBUTE_NAMES = {
    "speed_ratio_walk_v",
    "speed_ratio_run_v",
    "speed_ratio_sprint_v",
    "speed_ratio_dash_v",
    "speed_ratio_swim_v",
    "speed_ratio_swim_fast_v",
    "speed_ratio_swim_dash_v",
    "speed_ratio_glide_v",
    "speed_ratio_climb_v",
    "speed_ratio_climb_sprint_v",
    "speed_ratio_climb_dash_v",
    "speed_ratio_fly_v",
    "speed_ratio_fly_sprint_v"
}

function RuntimeFeatures:releaseSpeed()
    local entries = self.speedEntries
    local restored = false
    if type(entries) == "table" then
        for _, entry in ipairs(entries) do
            local attr = entry and entry.attr
            local prior = entry and entry.prior
            if attr ~= nil and type(prior) == "table" then
                local setter = safeRead(attr, "setAttribute")
                if type(setter) == "function" then
                    for id, value in pairs(prior) do
                        safeCall(setter, attr, id, value, true)
                    end
                end
                restored = true
            end
        end
    end
    if restored then
        self.stats.speedBoost.released = self.stats.speedBoost.released + 1
        self.stats.speedBoost.last = "restored"
    end
    self.speedEntries = {}
end

function RuntimeFeatures:tickSpeedBoost()
    local runtimePg = rawget(_G, "pg")
    local AttributeConst = select(1, requireTable("Common.Const.AttributeConst"))
    if runtimePg == nil or AttributeConst == nil then
        self.stats.speedBoost.last = "speed attributes not ready"
        return false
    end

    local targets, seenAttrs = {}, {}
    local function addSpeedTarget(label, owner)
        local attr = owner and safeRead(owner, "actorCombatAttribute")
        if attr ~= nil and not seenAttrs[attr] then
            seenAttrs[attr] = true
            targets[#targets + 1] = { label = label, attr = attr }
        end
    end
    addSpeedTarget("me", safeRead(runtimePg, "me"))
    addSpeedTarget("pawn", safeRead(runtimePg, "pawn"))
    if #targets == 0 then
        self.stats.speedBoost.last = "actorCombatAttribute not ready"
        return false
    end

    local ids = {}
    for _, name in ipairs(SPEED_RATIO_ATTRIBUTE_NAMES) do
        local id = safeRead(AttributeConst, name)
        if id ~= nil then ids[#ids + 1] = { name = name, id = id } end
    end
    if #ids == 0 then
        self.stats.speedBoost.last = "movement ratio ids unavailable"
        return false
    end

    local entries = self.speedEntries
    local needsRebuild = type(entries) ~= "table" or #entries ~= #targets
    if not needsRebuild then
        for _, target in ipairs(targets) do
            local found = false
            for _, entry in ipairs(entries) do
                if entry.attr == target.attr then found = true; break end
            end
            if not found then needsRebuild = true; break end
        end
    end

    if needsRebuild then
        self:releaseSpeed()
        entries = {}
        for _, target in ipairs(targets) do
            local getter = safeRead(target.attr, "getAttribValue")
            local setter = safeRead(target.attr, "setAttribute")
            if type(getter) == "function" and type(setter) == "function" then
                local entry = { label = target.label, attr = target.attr, prior = {} }
                for _, spec in ipairs(ids) do
                    local ok, value = safeCall(getter, target.attr, spec.id)
                    value = ok and tonumber(value) or nil
                    if value ~= nil then entry.prior[spec.id] = value end
                end
                if next(entry.prior) ~= nil then entries[#entries + 1] = entry end
            end
        end
        self.speedEntries = entries
    end

    if #entries == 0 then
        self.stats.speedBoost.last = "movement ratios unavailable on current actor"
        return false
    end

    local delta = math.max(0, math.min(2, (tonumber(self.settings.speedMultiplier) or 1) - 1))
    local changed, ratioCount = false, 0
    for _, entry in ipairs(entries) do
        local attr = entry.attr
        local setter = safeRead(attr, "setAttribute")
        local getter = safeRead(attr, "getAttribValue")
        if type(setter) == "function" and type(getter) == "function" then
            for id, base in pairs(entry.prior) do
                ratioCount = ratioCount + 1
                local wanted = base + delta
                local ok, current = safeCall(getter, attr, id)
                if ok and math.abs((tonumber(current) or 0) - wanted) > 0.0001 then
                    local setOk, setResult = safeCall(setter, attr, id, wanted, true)
                    if not setOk then featureError(self.stats.speedBoost, setResult); return false end
                    changed = true
                end
            end
        end
    end

    if changed then self.stats.speedBoost.applied = self.stats.speedBoost.applied + 1 end
    self.stats.speedBoost.last = string.format("%.2fx movement ratio (%d ratios / %d actor%s)",
        tonumber(self.settings.speedMultiplier) or 1, ratioCount, #entries, #entries == 1 and "" or "s")
    return changed
end

function RuntimeFeatures:releaseFov()
    local camera = self.cameraOwner
    if camera ~= nil and self.cameraPriorFov ~= nil then
        local ok = pcall(function() camera.fieldOfView = self.cameraPriorFov end)
        if ok then self.stats.fovOverride.released = self.stats.fovOverride.released + 1 end
    end
    self.cameraOwner, self.cameraPriorFov = nil, nil
end

function RuntimeFeatures:tickFovOverride()
    local runtimePg = rawget(_G, "pg")
    local camera = runtimePg and runtimePg.global and runtimePg.global.cameraMgr and safeRead(runtimePg.global.cameraMgr, "worldCameraInst")
    if camera == nil then self.stats.fovOverride.last = "world camera not ready"; return false end
    if self.cameraOwner ~= camera then
        self:releaseFov()
        self.cameraOwner = camera
        self.cameraPriorFov = tonumber(safeRead(camera, "fieldOfView"))
    end
    local wanted = math.max(45, math.min(120, tonumber(self.settings.fov) or 80))
    local current = tonumber(safeRead(camera, "fieldOfView"))
    if current and math.abs(current - wanted) < 0.05 then self.stats.fovOverride.last = string.format("FOV %.0f", wanted); return false end
    local ok, err = pcall(function() camera.fieldOfView = wanted end)
    if not ok then featureError(self.stats.fovOverride, err); return false end
    self.stats.fovOverride.applied = self.stats.fovOverride.applied + 1
    self.stats.fovOverride.last = string.format("FOV %.0f", wanted)
    return true
end

local INTERCONNECTION_INVINCIBLE_BUFF = 230307
local EGG_HASTE_OVERLOAD_BUFF_ID = 6003038

local INTERCONNECTION_ATTACK_ALL = {
    10020, 10024, 2050004, 2102308, 2107101, 2107102, 2108101, 2113101, 4000002,
    6003009, 20110020, 91191011, 270020002, 921200102, 2061000011, 912810102,
    20120010, 910210301, 2122201101
}

local INTERCONNECTION_FEATURE_BUFFS = {
    { key = "interconnectionNoCost", ids = { 11999 }, label = "No Cost" },
    { key = "interconnectionDamage", ids = { 2113305 }, label = "Guaranteed Crit" },

    { key = "interconnectionAttackSpeed", ids = INTERCONNECTION_ATTACK_ALL, label = "ATK Speed" },

    { key = "interconnectionAttackFurious", ids = { 2061000011 }, label = "Furious ATK" },
    { key = "interconnectionAttackEnhance", ids = { 4000002 }, label = "Enhance ATK" },
    { key = "interconnectionAttackHunt", ids = { 6003009 }, label = "Hunt ATK" },
    { key = "interconnectionCaptureBoost", ids = { 11140 }, label = "Capture Rate" },

}

local INTERCONNECTION_CASTING_ENERGY_PASSIVE_BUFF = 4004081
local INTERCONNECTION_CASTING_ENERGY_STACK_BUFF = 4004082
local INTERCONNECTION_CASTING_ENERGY_MAX_LAYER = 12
local INTERCONNECTION_CASTING_ENERGY_DURATION = 3600.0
local INTERCONNECTION_CASTING_ENERGY_ADDS_PER_TICK = 12
local INTERCONNECTION_CASTING_ENERGY_REFILL_INTERVAL = 0.35
local INTERCONNECTION_CASTING_ENERGY_PASSIVE_RETRY_INTERVAL = 0.75
local INTERCONNECTION_CASTING_ENERGY_PASSIVE_MISSING_DEBOUNCE = 0.15

local INTERCONNECTION_INFI_ULTIMATE_BUFF = 4001101
local INTERCONNECTION_INFI_ULTIMATE_DURATION = 3600.0

local INTERCONNECTION_CAPTURE_BOOST_BUFF = 11140
local INTERCONNECTION_CAPTURE_BOOST_DURATION = 2.5

function RuntimeFeatures:interconnectionAnyEnabled()
    if self.enabled.interconnectionGodMode == true or self.enabled.interconnectionNoCooldown == true or
       self.enabled.interconnectionInfiUltimate == true then return true end
    for _, def in ipairs(INTERCONNECTION_FEATURE_BUFFS) do
        if self.enabled[def.key] == true then return true end
    end
    return false
end

function RuntimeFeatures:sendInterconnectionRpc(pet, name, ...)
    local sender = safeRead(pet, "serverMsgNoGC")
    if type(sender) ~= "function" then return false, "serverMsgNoGC-missing" end
    return safeCall(sender, pet, name, ...)
end

function RuntimeFeatures:setOverloadReliefEnabled(enabled)
    self.overloadReliefEnabled = enabled == true
    if self:anyEnabled() then
        if not self:ensureTimer() then
            self.overloadReliefEnabled = false
            return false, self.lastTickError ~= "" and self.lastTickError or "feature-timer-not-ready"
        end
    else
        self:stopTimerIfIdle()
    end
    return true, self.overloadReliefEnabled and "enabled" or "disabled"
end

function RuntimeFeatures:tickOverloadRelief()
    local stats = self.stats.overloadRelief
    local runtimePg = rawget(_G, "pg")
    local player = runtimePg and safeRead(runtimePg, "me")
    if player == nil then stats.last = "waiting for pg.me"; return false end

    local inDungeonFn = safeRead(player, "isInGrabEggDungeon")
    local inDungeonOk, inDungeon = safeCall(inDungeonFn, player)
    local space = safeRead(player, "space")
    local isGrabEggFn = safeRead(space, "isGrabEgg")
    local isGrabEggOk, isGrabEgg = safeCall(isGrabEggFn, space)
    if not inDungeonOk or inDungeon ~= true or not isGrabEggOk or isGrabEgg ~= true then
        stats.last = "waiting for Egg Haste"
        return false
    end

    local loadLevel = tonumber(safeRead(player, "curLoadLevel"))

    local AbilityConst = select(1, requireTable("Common.Const.AbilityConst"))
    local stunTag = tonumber(safeRead(AbilityConst, "BUFF_TAG_STUN")) or 3
    local targets, seenTargets = {}, {}
    local function addTarget(label, owner)
        if owner ~= nil and not seenTargets[owner] then
            seenTargets[owner] = true
            targets[#targets + 1] = { label = label, owner = owner }
        end
    end
    addTarget("player", player)
    addTarget("Aniimo", safeRead(runtimePg, "pawn"))
    local controlledAniimo = self:controlledAniimo()
    addTarget("Aniimo", controlledAniimo)

    local now, activeTargets, sent = featureNow(), 0, 0
    for _, target in ipairs(targets) do
        local owner = target.owner
        local actorBuff = safeRead(owner, "actorBuff")
        local hasTagFn = safeRead(actorBuff, "hasTag")
        local hasTagOk, hasStunTag = safeCall(hasTagFn, actorBuff, stunTag)
        local exactBuffPresent = false
        local findBuffFn = safeRead(actorBuff, "findBuffByTemplateId")
        local findOk, foundBuffs = safeCall(findBuffFn, actorBuff, EGG_HASTE_OVERLOAD_BUFF_ID)
        if findOk and safeType(foundBuffs) == "table" and #foundBuffs > 0 then
            exactBuffPresent = true
        end
        if not exactBuffPresent then
            local buffMap = safeRead(actorBuff, "buffMap")
            if safeType(buffMap) == "table" then
                for _, buff in pairs(buffMap) do
                    local buffData = safeRead(buff, "buffData")
                    if tonumber(safeRead(buffData, "templateId")) == EGG_HASTE_OVERLOAD_BUFF_ID then
                        exactBuffPresent = true
                        break
                    end
                end
            end
        end

        local overloadActive = exactBuffPresent or
            (loadLevel ~= nil and loadLevel >= 6 and hasTagOk and hasStunTag == true)
        if overloadActive then
            activeTargets = activeTargets + 1
            local actorId = safeRead(owner, "actorId")
            local requestKey = tostring(actorId or owner) .. ":" .. tostring(EGG_HASTE_OVERLOAD_BUFF_ID)
            local lastAt = tonumber(self.overloadRelief.lastRequestByBuff[requestKey]) or -999
            if now - lastAt >= 0.80 then
                local callOk = self:sendInterconnectionRpc(owner, "RPC_CS_AIRemoveBuff", EGG_HASTE_OVERLOAD_BUFF_ID)
                if callOk then
                    self.overloadRelief.lastRequestByBuff[requestKey] = now
                    stats.requests = (tonumber(stats.requests) or 0) + 1
                    sent = sent + 1
                else
                    stats.errors = (tonumber(stats.errors) or 0) + 1
                end
            end
        end
    end

    if sent > 0 then
        stats.last = string.format("Overload removal requested for %d target%s", sent, sent == 1 and "" or "s")
    elseif activeTargets > 0 then
        stats.last = "Overload removal on cooldown"
    else
        stats.last = "waiting for Overload STUN"
    end
    return sent > 0
end

function RuntimeFeatures:releasePlayerGodMode()
    local state = self.playerGodMode
    if state.owner ~= nil and state.buffSent then
        self:sendInterconnectionRpc(state.owner, "RPC_CS_AIRemoveBuff", INTERCONNECTION_INVINCIBLE_BUFF)
    end
    state.owner = nil
    state.buffSent = false
    state.lastRequestAt = -999
end

function RuntimeFeatures:tickPlayerGodMode(enabled)
    local state = self.playerGodMode
    if enabled ~= true then
        self:releasePlayerGodMode()
        return false
    end

    local runtimePg = rawget(_G, "pg")
    local player = runtimePg and safeRead(runtimePg, "me")
    if player == nil then
        self:releasePlayerGodMode()
        return false
    end
    local playerClass = tostring(classTypeOf(player) or "")
    if playerClass:lower():find("clientpet", 1, true) then
        self:releasePlayerGodMode()
        return false
    end

    if state.owner ~= player then
        self:releasePlayerGodMode()
        state.owner = player
    end

    local now = featureNow()
    if now - (tonumber(state.lastRequestAt) or -999) < 1.0 then return false end
    local ok = self:sendInterconnectionRpc(player, "RPC_CS_AIAddBuff", INTERCONNECTION_INVINCIBLE_BUFF, 2.5)
    if not ok then return false end
    state.buffSent = true
    state.lastRequestAt = now
    return true
end

function RuntimeFeatures:releasePlayerCaptureBoost()
    local state = self.interconnection
    local player = state.player
    if player ~= nil and state.playerCaptureBuffSent then
        self:sendInterconnectionRpc(player, "RPC_CS_AIRemoveBuff", INTERCONNECTION_CAPTURE_BOOST_BUFF)
    end
    state.playerCaptureBuffSent = false
    state.lastPlayerCaptureBuffAt = -999
    state.player = nil
end

function RuntimeFeatures:tickPlayerCaptureBoost()
    local runtimePg = rawget(_G, "pg")
    local player = runtimePg and safeRead(runtimePg, "me")
    if player == nil then return false, "capture-boost-player-not-ready" end

    local playerClass = tostring(classTypeOf(player) or type(player))
    if playerClass:lower():find("clientpet", 1, true) then
        return false, "capture-boost-resolution-returned-clientpet"
    end

    local state = self.interconnection
    if state.player ~= player then
        self:releasePlayerCaptureBoost()
        state.player = player
    end

    local now = featureNow()
    if now - (tonumber(state.lastPlayerCaptureBuffAt) or -999) < 1.0 then return false end
    local ok, err = self:sendInterconnectionRpc(player, "RPC_CS_AIAddBuff", INTERCONNECTION_CAPTURE_BOOST_BUFF, INTERCONNECTION_CAPTURE_BOOST_DURATION)
    if not ok then return false, "capture-boost-player-rpc: " .. tostring(err) end

    state.playerCaptureBuffSent = true
    state.lastPlayerCaptureBuffAt = now
    self.stats.interconnection.buffRequests = (tonumber(self.stats.interconnection.buffRequests) or 0) + 1
    local playerId = scalar(safeRead(player, "id"), 64) or scalar(safeRead(player, "actorId"), 64) or "?"
    self.stats.interconnection.last = string.format("capture buff %d refresh target=%s class=%s",
        INTERCONNECTION_CAPTURE_BOOST_BUFF, tostring(playerId), playerClass)
    return true
end

local function interconnectionActorId(entity)
    if entity == nil then return nil, nil end
    local raw = safeRead(entity, "actorId")
    local rendered = scalar(raw, 48)
    if rendered == nil or tostring(rendered) == "0" then return nil, raw end
    return tostring(rendered), raw
end

local function interconnectionMainPet(entity, me)
    if entity == nil or entity == me then return false end
    local actorId = interconnectionActorId(entity)
    if actorId == nil then return false end
    return safeRead(entity, "isMainPet") == true
end

function RuntimeFeatures:removeInterconnectionBuffs(pet, preserveUltimateBuff, preserveCastingEnergy)
    local state = self.interconnection
    if pet ~= nil then
        if state.invincibleBuffSent then self:sendInterconnectionRpc(pet, "RPC_CS_AIRemoveBuff", INTERCONNECTION_INVINCIBLE_BUFF) end
        if state.ultimateBuffSent and preserveUltimateBuff ~= true then
            self:sendInterconnectionRpc(pet, "RPC_CS_AIRemoveBuff", INTERCONNECTION_INFI_ULTIMATE_BUFF)
        end
        if state.castingEnergyPassiveSent and preserveCastingEnergy ~= true then
            self:sendInterconnectionRpc(pet, "RPC_CS_AIRemoveBuff", INTERCONNECTION_CASTING_ENERGY_STACK_BUFF)
            self:sendInterconnectionRpc(pet, "RPC_CS_AIRemoveBuff", INTERCONNECTION_CASTING_ENERGY_PASSIVE_BUFF)
        end
        for buffId in pairs(state.featureBuffSent or {}) do
            self:sendInterconnectionRpc(pet, "RPC_CS_AIRemoveBuff", buffId)
        end
    end
    state.invincibleBuffSent = false
    state.lastInvincibleBuffAt = -999
    if preserveUltimateBuff ~= true then
        state.ultimateBuffSent = false
        state.ultimateBuffActorId = nil
        state.ultimateRecyclePending = false
        state.ultimateLastRecycleAt = -999
    end
    if preserveCastingEnergy ~= true then
        state.castingEnergyPassiveSent = false
        state.castingEnergyPassiveConfirmed = false
        state.castingEnergyPassiveLastRequestAt = -999
        state.castingEnergyPassiveMissingSince = nil
        state.castingEnergyActorId = nil
        state.castingEnergyLastFillAt = -999
    end
    state.featureBuffSent = {}
    state.lastBuffRefreshAt = {}
end

function RuntimeFeatures:releaseInterconnection(reason, preserveUltimateBuff, preserveCastingEnergy)
    local state = self.interconnection
    local hadPet = state.pet ~= nil
    local oldPet = state.pet
    self:removeInterconnectionBuffs(oldPet, preserveUltimateBuff, preserveCastingEnergy)
    self:releasePlayerCaptureBoost()
    state.pet = nil
    state.petActorId = nil
    local stats = self.stats.interconnection
    stats.active = false
    stats.controlling = false
    stats.abilities = 0
    stats.ultimateCooldownRate = -1
    stats.ultimateCooldownMax = -1
    stats.ultimateSp = -1
    stats.ultimateSpMax = -1
    if hadPet then stats.exits = (tonumber(stats.exits) or 0) + 1 end
    stats.last = tostring(reason or "released")
end

function RuntimeFeatures:controlledAniimo()
    local runtimePg = rawget(_G, "pg")
    local owner = runtimePg and safeRead(runtimePg, "me")
    if owner == nil then return nil, "waiting-pg.me" end

    local game = runtimePg and safeRead(runtimePg, "game")
    local controller = game and safeRead(game, "controller")
    local controllerPawn = controller and safeRead(controller, "pawn")
    if interconnectionMainPet(controllerPawn, owner) then return controllerPawn, nil end

    local pawn = runtimePg and safeRead(runtimePg, "pawn")
    if interconnectionMainPet(pawn, owner) then return pawn, nil end

    local isControlling = safeRead(owner, "isControllingPet")
    local controlOk, controlling = safeCall(isControlling, owner)
    if not controlOk or controlling ~= true then return nil, "not-controlling-aniimo" end

    local getPet = safeRead(owner, "getCurPetEntity")
    if type(getPet) ~= "function" then return nil, "controlled-aniimo-pawn-unavailable" end
    local petOk, pet = safeCall(getPet, owner)
    if not petOk or not interconnectionMainPet(pet, owner) then return nil, "controlled-aniimo-not-ready" end
    return pet, nil
end

function RuntimeFeatures:countInterconnectionAbilities(pet)
    local map = safeRead(pet, "abilityMap")
    if type(map) ~= "table" then return 0 end
    local count = 0
    for _ in pairs(map) do count = count + 1 end
    return count
end

local function interconnectionFindBuff(entity, buffId)
    if entity == nil then return nil end

    local target = entity
    local runtimePg = rawget(_G, "pg")
    local actorIdRaw = safeRead(entity, "actorId")
    local getByActorId = runtimePg and safeRead(runtimePg, "getEntityByActorId")
    if type(getByActorId) == "function" and actorIdRaw ~= nil then
        local ok, canonical = pcall(getByActorId, actorIdRaw)
        if ok and canonical ~= nil then target = canonical end
    end

    local function findOn(owner)
        local actorBuff = owner and safeRead(owner, "actorBuff")
        local finder = actorBuff and safeRead(actorBuff, "findOneBuffByTemplateId")
        if type(finder) ~= "function" then return nil end
        local ok, buff = safeCall(finder, actorBuff, buffId)
        return ok and buff or nil
    end

    local buff = findOn(target)
    if buff == nil and target ~= entity then buff = findOn(entity) end
    return buff
end

local function interconnectionBuffLayer(entity, buffId)
    local buff = interconnectionFindBuff(entity, buffId)
    if buff == nil then return 0 end
    local buffData = safeRead(buff, "buffData")
    return math.max(0, tonumber(buffData and safeRead(buffData, "layer")) or 0)
end

function RuntimeFeatures:updateUltimateReadback(pet)
    local stats = self.stats.interconnection
    stats.ultimateCooldownRate = -1
    stats.ultimateCooldownMax = -1
    stats.ultimateSp = -1
    stats.ultimateSpMax = -1
    if pet == nil then return end

    if self.enabled.interconnectionNoCooldown == true then
        stats.ultimateCooldownRate = interconnectionBuffLayer(pet, INTERCONNECTION_CASTING_ENERGY_STACK_BUFF)
        stats.ultimateCooldownMax = INTERCONNECTION_CASTING_ENERGY_MAX_LAYER
    end

    if self.enabled.interconnectionInfiUltimate == true then
        local getSp = safeRead(pet, "getSp")
        local getMaxSp = safeRead(pet, "getMaxSp")
        local okSp, sp = safeCall(getSp, pet)
        local okMax, spMax = safeCall(getMaxSp, pet)
        if okSp and tonumber(sp) then stats.ultimateSp = tonumber(sp) end
        if okMax and tonumber(spMax) then stats.ultimateSpMax = tonumber(spMax) end

        if stats.ultimateSp < 0 or stats.ultimateSpMax < 0 then
            local combat = safeRead(pet, "actorCombatAttribute")
            if combat ~= nil then
                if stats.ultimateSp < 0 then
                    local ok, value = safeCall(safeRead(combat, "getSp"), combat)
                    if ok and tonumber(value) then stats.ultimateSp = tonumber(value) end
                end
                if stats.ultimateSpMax < 0 then
                    local ok, value = safeCall(safeRead(combat, "getMaxSp"), combat)
                    if ok and tonumber(value) then stats.ultimateSpMax = tonumber(value) end
                end
            end
        end
    end
end

function RuntimeFeatures:tickInterconnection()
    local stats = self.stats.interconnection
    local pet, reason = self:controlledAniimo()
    if pet == nil then
        if self.interconnection.pet ~= nil then
            self:releaseInterconnection(reason,
                self.enabled.interconnectionInfiUltimate == true,
                self.enabled.interconnectionNoCooldown == true)
        else
            if not self.enabled.interconnectionCaptureBoost then
                stats.last = reason
            end
            stats.controlling = false
            stats.ultimateCooldownRate = -1
            stats.ultimateCooldownMax = -1
            stats.ultimateSp = -1
            stats.ultimateSpMax = -1
        end
        return false
    end
    stats.controlling = true
    local petActorId = tostring(scalar(safeRead(pet, "actorId"), 64) or scalar(safeRead(pet, "id"), 64) or "0")
    if self.interconnection.petActorId ~= petActorId then
        local hadPet = self.interconnection.petActorId ~= nil
        if hadPet then self:releaseInterconnection("controlled-aniimo-changed", false, false) end
        self.interconnection.petActorId = petActorId
        self.interconnection.pet = pet
        stats.controlling = true
        if hadPet then stats.swaps = (tonumber(stats.swaps) or 0) + 1 else stats.enters = (tonumber(stats.enters) or 0) + 1 end
    else
        self.interconnection.pet = pet
    end
    stats.active = true
    stats.petId = petActorId
    stats.templateId = tostring(scalar(safeRead(pet, "templateId"), 64) or scalar(safeRead(pet, "configId"), 64) or "0")
    stats.abilities = self:countInterconnectionAbilities(pet)

    local now = featureNow()
    if self.enabled.interconnectionGodMode then
        if now - (tonumber(self.interconnection.lastInvincibleBuffAt) or -999) >= 1.0 then
            local ok, err = self:sendInterconnectionRpc(pet, "RPC_CS_AIAddBuff", INTERCONNECTION_INVINCIBLE_BUFF, 2.5)
            if ok then
                self.interconnection.invincibleBuffSent = true
                self.interconnection.lastInvincibleBuffAt = now
                stats.buffRequests = (tonumber(stats.buffRequests) or 0) + 1
            else featureError(stats, err) end
        end
    elseif self.interconnection.invincibleBuffSent then
        self:sendInterconnectionRpc(pet, "RPC_CS_AIRemoveBuff", INTERCONNECTION_INVINCIBLE_BUFF)
        self.interconnection.invincibleBuffSent = false
    end

    if self.enabled.interconnectionInfiUltimate == true then
        if self.interconnection.ultimateRecyclePending == true then
            if now - (tonumber(self.interconnection.ultimateLastRecycleAt) or -999) >= 0.15 then
                local ok, err = self:sendInterconnectionRpc(
                    pet, "RPC_CS_AIAddBuff",
                    INTERCONNECTION_INFI_ULTIMATE_BUFF,
                    INTERCONNECTION_INFI_ULTIMATE_DURATION)
                if ok then
                    self.interconnection.ultimateBuffSent = true
                    self.interconnection.ultimateBuffActorId = petActorId
                    self.interconnection.ultimateRecyclePending = false
                    stats.buffRequests = (tonumber(stats.buffRequests) or 0) + 1
                    stats.last = string.format("buff %d re-added after SP=0 (Infi Ultimate)",
                        INTERCONNECTION_INFI_ULTIMATE_BUFF)
                else
                    featureError(stats, err)
                end
            end
        elseif self.interconnection.ultimateBuffSent ~= true or self.interconnection.ultimateBuffActorId ~= petActorId then
            local ok, err = self:sendInterconnectionRpc(
                pet, "RPC_CS_AIAddBuff",
                INTERCONNECTION_INFI_ULTIMATE_BUFF,
                INTERCONNECTION_INFI_ULTIMATE_DURATION)
            if ok then
                self.interconnection.ultimateBuffSent = true
                self.interconnection.ultimateBuffActorId = petActorId
                self.interconnection.ultimateRecyclePending = false
                self.interconnection.ultimateLastRecycleAt = now
                stats.buffRequests = (tonumber(stats.buffRequests) or 0) + 1
                stats.last = string.format("buff %d persistent add (Infi Ultimate)",
                    INTERCONNECTION_INFI_ULTIMATE_BUFF)
            else
                featureError(stats, err)
            end
        else
            local currentSp = tonumber(stats.ultimateSp)
            local currentSpMax = tonumber(stats.ultimateSpMax)
            if currentSp ~= nil and currentSpMax ~= nil and currentSpMax > 0 and
               currentSp >= 0 and currentSp <= 0.0001 and
               now - (tonumber(self.interconnection.ultimateLastRecycleAt) or -999) >= 0.50 then
                local ok, err = self:sendInterconnectionRpc(
                    pet, "RPC_CS_AIRemoveBuff", INTERCONNECTION_INFI_ULTIMATE_BUFF)
                if ok then
                    self.interconnection.ultimateBuffSent = false
                    self.interconnection.ultimateRecyclePending = true
                    self.interconnection.ultimateLastRecycleAt = now
                    stats.buffRequests = (tonumber(stats.buffRequests) or 0) + 1
                    stats.last = string.format("buff %d removed for SP=0 recycle (Infi Ultimate)",
                        INTERCONNECTION_INFI_ULTIMATE_BUFF)
                else
                    featureError(stats, err)
                end
            end
        end
    elseif self.interconnection.ultimateBuffSent == true or self.interconnection.ultimateRecyclePending == true then
        if self.interconnection.ultimateBuffSent == true then
            self:sendInterconnectionRpc(pet, "RPC_CS_AIRemoveBuff", INTERCONNECTION_INFI_ULTIMATE_BUFF)
        end
        self.interconnection.ultimateBuffSent = false
        self.interconnection.ultimateBuffActorId = nil
        self.interconnection.ultimateRecyclePending = false
        self.interconnection.ultimateLastRecycleAt = -999
        stats.last = string.format("buff %d removed", INTERCONNECTION_INFI_ULTIMATE_BUFF)
    end

    if self.enabled.interconnectionNoCooldown == true then
        local state = self.interconnection

        if state.castingEnergyActorId ~= petActorId then
            state.castingEnergyPassiveSent = false
            state.castingEnergyPassiveConfirmed = false
            state.castingEnergyPassiveLastRequestAt = -999
            state.castingEnergyPassiveMissingSince = nil
            state.castingEnergyActorId = petActorId
            state.castingEnergyLastFillAt = -999
        end

        local passiveObserved = interconnectionFindBuff(pet, INTERCONNECTION_CASTING_ENERGY_PASSIVE_BUFF) ~= nil
        if passiveObserved then
            state.castingEnergyPassiveSent = true
            state.castingEnergyPassiveConfirmed = true
            state.castingEnergyPassiveMissingSince = nil
        else
            local shouldAddPassive = false
            local readdReason = nil
            if state.castingEnergyPassiveConfirmed == true then
                if state.castingEnergyPassiveMissingSince == nil then
                    state.castingEnergyPassiveMissingSince = now
                elseif now - (tonumber(state.castingEnergyPassiveMissingSince) or now) >=
                       INTERCONNECTION_CASTING_ENERGY_PASSIVE_MISSING_DEBOUNCE then
                    shouldAddPassive = true
                    readdReason = "confirmed passive disappeared"
                end
            elseif state.castingEnergyPassiveSent ~= true then
                shouldAddPassive = true
                readdReason = "initial passive add"
            elseif now - (tonumber(state.castingEnergyPassiveLastRequestAt) or -999) >=
                   INTERCONNECTION_CASTING_ENERGY_PASSIVE_RETRY_INTERVAL then
                shouldAddPassive = true
                readdReason = "passive sync retry"
            end

            if shouldAddPassive then
                local ok, err = self:sendInterconnectionRpc(
                    pet, "RPC_CS_AIAddBuff",
                    INTERCONNECTION_CASTING_ENERGY_PASSIVE_BUFF,
                    INTERCONNECTION_CASTING_ENERGY_DURATION)
                if ok then
                    state.castingEnergyPassiveSent = true
                    state.castingEnergyPassiveConfirmed = false
                    state.castingEnergyPassiveLastRequestAt = now
                    state.castingEnergyPassiveMissingSince = nil
                    state.castingEnergyLastFillAt = -999
                    stats.buffRequests = (tonumber(stats.buffRequests) or 0) + 1
                    stats.last = string.format("buff %d add | %s",
                        INTERCONNECTION_CASTING_ENERGY_PASSIVE_BUFF, tostring(readdReason or "watchdog"))
                else
                    featureError(stats, err)
                end
            elseif state.castingEnergyPassiveConfirmed ~= true then
                stats.last = "waiting for Casting Energy passive server sync"
            end
        end

        passiveObserved = interconnectionFindBuff(pet, INTERCONNECTION_CASTING_ENERGY_PASSIVE_BUFF) ~= nil
        local currentLayer = interconnectionBuffLayer(pet, INTERCONNECTION_CASTING_ENERGY_STACK_BUFF)
        if passiveObserved and currentLayer < INTERCONNECTION_CASTING_ENERGY_MAX_LAYER and
           now - (tonumber(state.castingEnergyLastFillAt) or -999) >= INTERCONNECTION_CASTING_ENERGY_REFILL_INTERVAL then
            local missing = INTERCONNECTION_CASTING_ENERGY_MAX_LAYER - currentLayer
            local requestCount = math.min(INTERCONNECTION_CASTING_ENERGY_ADDS_PER_TICK, missing)
            local sent = 0
            for _ = 1, requestCount do
                local ok, err = self:sendInterconnectionRpc(
                    pet, "RPC_CS_AIAddBuff",
                    INTERCONNECTION_CASTING_ENERGY_STACK_BUFF,
                    INTERCONNECTION_CASTING_ENERGY_DURATION)
                if not ok then
                    featureError(stats, err)
                    break
                end
                sent = sent + 1
                stats.buffRequests = (tonumber(stats.buffRequests) or 0) + 1
            end
            state.castingEnergyLastFillAt = now
            if sent > 0 then
                stats.last = string.format("Casting Energy refill x%d | observed %d/%d",
                    sent, currentLayer, INTERCONNECTION_CASTING_ENERGY_MAX_LAYER)
            end
        end
    elseif self.interconnection.castingEnergyPassiveSent == true or
           self.interconnection.castingEnergyPassiveConfirmed == true then
        self:sendInterconnectionRpc(pet, "RPC_CS_AIRemoveBuff", INTERCONNECTION_CASTING_ENERGY_STACK_BUFF)
        self:sendInterconnectionRpc(pet, "RPC_CS_AIRemoveBuff", INTERCONNECTION_CASTING_ENERGY_PASSIVE_BUFF)
        self.interconnection.castingEnergyPassiveSent = false
        self.interconnection.castingEnergyPassiveConfirmed = false
        self.interconnection.castingEnergyPassiveLastRequestAt = -999
        self.interconnection.castingEnergyPassiveMissingSince = nil
        self.interconnection.castingEnergyActorId = nil
        self.interconnection.castingEnergyLastFillAt = -999
        stats.last = "No Cooldown Casting Energy buffs removed"
    end

    local desired = {}
    for _, def in ipairs(INTERCONNECTION_FEATURE_BUFFS) do
        if def.key ~= "interconnectionCaptureBoost" and self.enabled[def.key] then
            for _, buffId in ipairs(def.ids or {}) do
                desired[buffId] = def.label
            end
        end
    end

    for buffId, label in pairs(desired) do
        local lastAt = tonumber(self.interconnection.lastBuffRefreshAt[buffId]) or -999
        if now - lastAt >= 1.0 then
            local ok, err = self:sendInterconnectionRpc(pet, "RPC_CS_AIAddBuff", buffId, 2.5)
            if ok then
                self.interconnection.featureBuffSent[buffId] = true
                self.interconnection.lastBuffRefreshAt[buffId] = now
                stats.buffRequests = (tonumber(stats.buffRequests) or 0) + 1
                stats.last = string.format("buff %d refresh (%s)", buffId, tostring(label or "group"))
            else
                featureError(stats, err)
            end
        end
    end

    local removeIds = {}
    for buffId in pairs(self.interconnection.featureBuffSent) do
        if desired[buffId] == nil then removeIds[#removeIds + 1] = buffId end
    end
    for _, buffId in ipairs(removeIds) do
        self:sendInterconnectionRpc(pet, "RPC_CS_AIRemoveBuff", buffId)
        self.interconnection.featureBuffSent[buffId] = nil
        self.interconnection.lastBuffRefreshAt[buffId] = nil
        stats.last = string.format("buff %d removed", buffId)
    end

    self:updateUltimateReadback(pet)

    if stats.last == "idle" or string.find(stats.last, "active pet=", 1, true) == 1 then
        stats.last = string.format("active pet=%s abilities=%d", stats.petId, tonumber(stats.abilities) or 0)
    end
    return true
end

function RuntimeFeatures:tickAutoQuickCapture(now)
    if now - self.lastRun.autoQuickCapture < self.settings.quickCaptureCooldown then return false end
    self.lastRun.autoQuickCapture = now
    local runtimePg = rawget(_G, "pg")
    local interaction = runtimePg and runtimePg.game and runtimePg.game.interaction
    if not interaction then self.stats.autoQuickCapture.last = "waiting for InteractionSystem"; return false end
    local refresh = safeRead(interaction, "tryRefreshQuickCaptureInteract")
    if type(refresh) == "function" then pcall(refresh, interaction) end
    local targetId = safeRead(interaction, "quickCaptureEntId")
    local unit = safeRead(interaction, "quickCaptureUnit")
    if targetId == nil or unit == nil then self.stats.autoQuickCapture.last = "no official quick-capture target"; return false end
    local canOk, canInteract = safeCall(safeRead(unit, "canInteractive"), unit)
    if not canOk or canInteract ~= true then self.stats.autoQuickCapture.last = "official unit not interactive"; return false end
    local invalidMethod = safeRead(unit, "getInvalidInteractData")
    if type(invalidMethod) == "function" then
        local invalidOk, invalid = safeCall(invalidMethod, unit)
        if not invalidOk then featureError(self.stats.autoQuickCapture, invalid); return false
        elseif invalid ~= nil then self.stats.autoQuickCapture.last = "official quick-capture validation rejected"; return false end
    end
    local entityOk, entity = callGlobal(runtimePg, "getEntity", targetId)
    if not entityOk or entity == nil then self.stats.autoQuickCapture.last = "target entity disappeared"; return false end
    local ballOk, itemId = safeCall(safeRead(unit, "getBallItemId"), unit)
    local CatchProbContext = select(1, requireTable("Common.Utils.CatchProbContext"))
    if not ballOk or itemId == nil or not CatchProbContext or type(CatchProbContext.clientGet) ~= "function" then
        self.stats.autoQuickCapture.last = "capture probability surface not ready"; return false
    end
    local probOk, context = pcall(CatchProbContext.clientGet, entity, itemId)
    if not probOk or context == nil then featureError(self.stats.autoQuickCapture, context or "CatchProbContext.clientGet failed"); return false end
    local finalPercent = (tonumber(safeRead(context, "finalProb")) or 0) * 100
    if finalPercent + 0.0001 < self.settings.quickCaptureMinPercent then
        self.stats.autoQuickCapture.skippedThreshold = self.stats.autoQuickCapture.skippedThreshold + 1
        self.stats.autoQuickCapture.last = string.format("skipped %.1f%% < %d%%", finalPercent, self.settings.quickCaptureMinPercent)
        return false
    end
    local actionOk, actionResult = safeCall(safeRead(unit, "doInteract"), unit)
    if not actionOk then featureError(self.stats.autoQuickCapture, actionResult or "quick-capture doInteract failed"); return false end
    self.stats.autoQuickCapture.actions = self.stats.autoQuickCapture.actions + 1
    self.stats.autoQuickCapture.last = string.format("requested target=%s rate=%.1f%%", tostring(scalar(targetId, 80) or "?"), finalPercent)
    return true
end

function RuntimeFeatures:tickDirectEntityAction(now, featureName, classifierName, range, cooldown, label, rejectCostedChest)
    if self.interactionPending ~= nil then
        local pendingAge = now - (tonumber(self.interactionPending.startedAt) or now)
        if pendingAge < 5.0 then
            self.stats[featureName].last = string.format("waiting for %s response %.1fs", tostring(self.interactionPending.label or "interaction"), pendingAge)
            return false
        end
        self.interactionPending = nil
    end
    if now - self.lastRun[featureName] < cooldown then return false end
    self.lastRun[featureName] = now

    local stats = self.stats[featureName]
    local runtimePg = rawget(_G, "pg")
    local learning = self.interactionLearning[featureName]
    if type(learning) ~= "table" then
        learning = { targets = {}, profiles = {}, nextProbeAt = 0, lastPruneAt = 0 }
        self.interactionLearning[featureName] = learning
    end

    local candidate, scanError = directEntityCandidate(runtimePg, classifierName, range, rejectCostedChest, learning, now, featureName)
    if scanError then stats.last = scanError; return false end
    if candidate == nil then
        local profiles, targets, maxExtra = learningSummary(learning)
        stats.adaptiveProfiles, stats.adaptiveTargets, stats.maxLearnedExtra = profiles, targets, maxExtra
        stats.last = string.format("no eligible %s within %.0fm (adaptive frontier)", label, range)
        return false
    end

    local entity = candidate.entity
    local Const = select(1, requireTable("Common.Const.Const"))
    local NoticeDef = select(1, requireTable("Common.NoticeDef"))
    local player = runtimePg and runtimePg.me
    local startInteract = player and safeRead(player, "startInteract")
    local entityId = safeRead(entity, "id")
    local actionPrototypeId = safeRead(entity, "actionPrototypeId")
    local interactionType = Const and (featureName == "autoCollect" and safeRead(Const, "IACT_IP_COLLECT_ITEM") or safeRead(Const, "IACT_TP_CHEST"))
    if type(startInteract) ~= "function" or entityId == nil or actionPrototypeId == nil or interactionType == nil then
        stats.last = label .. " interaction surface not ready"
        return false
    end

    self:installInteractionBubbleFilter()

    local key = candidate.key
    local profile = candidate.profile
    local target = candidate.target
    local requestDistance = tonumber(candidate.distance) or -1
    local requestCenterDistance = tonumber(candidate.centerDistance) or requestDistance
    local requestSafeRange = tonumber(candidate.safeRange)
    local requestExtra = tonumber(candidate.extra) or 0
    local mode = candidate.mode or "direct"

    self.interactionPending = {
        feature = featureName,
        key = key,
        signature = candidate.signature,
        label = label,
        startedAt = now,
        distance = requestDistance,
        centerDistance = requestCenterDistance,
        mode = mode
    }

    stats.requests = (tonumber(stats.requests) or 0) + 1
    stats.lastMeasuredDistance = requestDistance
    stats.lastEntityCenterDistance = requestCenterDistance
    stats.lastOfficialRange = requestSafeRange or 0
    stats.lastDistanceSource = candidate.distanceSource or "entity-center"
    stats.lastMode = mode
    if mode == "adaptive-probe" then
        stats.probes = (tonumber(stats.probes) or 0) + 1
        profile.probes = (tonumber(profile.probes) or 0) + 1
        profile.nextProbeAt = now + 1.5
        learning.nextProbeAt = now + 1.0
    elseif mode == "learned-safe" then
        stats.learnedHits = (tonumber(stats.learnedHits) or 0) + 1
    elseif mode == "local-trigger" then
        stats.localTriggerHits = (tonumber(stats.localTriggerHits) or 0) + 1
    end

    local profiles, targets, maxExtra = learningSummary(learning)
    stats.adaptiveProfiles, stats.adaptiveTargets, stats.maxLearnedExtra = profiles, targets, maxExtra
    stats.last = string.format("%s %s %.2fm%s [%s]", mode, label, requestDistance,
        requestSafeRange and string.format(" official<=%.2fm", requestSafeRange) or "",
        tostring(candidate.distanceSource or "center"))

    local function onResult(ret, retArgs)
        if self.interactionPending and self.interactionPending.key == key then self.interactionPending = nil end
        local resultNow = featureNow()
        local code = tonumber(ret) or -999999
        local successCode = tonumber(NoticeDef and safeRead(NoticeDef, "SUCCESS")) or 0

        if code == successCode then
            stats.actions = (tonumber(stats.actions) or 0) + 1
            stats.lastSuccessDistance = requestDistance
            stats.maxSuccessDistance = math.max(tonumber(stats.maxSuccessDistance) or 0, requestDistance)
            stats.lastSuccessCenterDistance = requestCenterDistance
            target.successes = (tonumber(target.successes) or 0) + 1
            target.maxSuccessDistance = math.max(tonumber(target.maxSuccessDistance) or 0, requestDistance)
            target.retryBelow = nil
            target.nextTryAt = 0

            profile.successes = (tonumber(profile.successes) or 0) + 1
            if requestSafeRange and requestExtra > (tonumber(profile.safeExtra) or 0) then
                profile.safeExtra = requestExtra
            end
            profile.step = math.min(tonumber(profile.maxStep) or 4.0,
                math.max(tonumber(profile.minStep) or 0.75, (tonumber(profile.step) or 1.0) * 1.20 + 0.15))
            profile.nextProbeAt = resultNow + 0.35
            profile.lastOutcome = string.format("success %.2fm", requestDistance)

            local pCount, tCount, learned = learningSummary(learning)
            stats.adaptiveProfiles, stats.adaptiveTargets, stats.maxLearnedExtra = pCount, tCount, learned
            stats.last = string.format("success %s %.2fm (%s, learned +%.2fm)", label, requestDistance, mode,
                tonumber(profile.safeExtra) or 0)

            if featureName == "autoCollect" then
                local refresh = safeRead(entity, "refreshCollectItemOpened")
                if type(refresh) == "function" then pcall(refresh, entity) end
            elseif featureName == "autoChest" then
                local facade = rawget(_G, "facade")
                if facade ~= nil then
                    local send = safeRead(facade, "sendLuaEvent") or safeRead(facade, "SendLuaEvent")
                    if type(send) == "function" then pcall(send, facade, tostring(safeRead(entity, "staticId") or "") .. "chestOpen") end
                end
                local petEvent = player and safeRead(player, "executePetAdditiveAIEvent")
                if type(petEvent) == "function" then pcall(petEvent, player, "MasterOpenChestTrigger", { chestId = entityId }) end
            end
            return
        end

        stats.rejected = (tonumber(stats.rejected) or 0) + 1
        target.rejects = (tonumber(target.rejects) or 0) + 1
        local positionCode = tonumber(NoticeDef and safeRead(NoticeDef, "ERROR_POSITION_VALID")) or 1002
        local distanceCode = tonumber(NoticeDef and safeRead(NoticeDef, "ERROR_INVALID_DISTANCE")) or 1026
        if code == positionCode or code == distanceCode then
            stats.positionRejected = (tonumber(stats.positionRejected) or 0) + 1
            target.minRejectedDistance = target.minRejectedDistance and math.min(target.minRejectedDistance, requestDistance) or requestDistance

            local retryBelow
            if requestSafeRange and requestDistance > requestSafeRange then
                retryBelow = requestSafeRange + (requestDistance - requestSafeRange) * 0.50
            elseif requestSafeRange then
                retryBelow = math.max(0.5, requestSafeRange - 0.25)
            else
                retryBelow = math.max(1, requestDistance * 0.65)
            end
            target.retryBelow = retryBelow
            target.nextTryAt = resultNow + 6.0

            profile.rejects = (tonumber(profile.rejects) or 0) + 1
            if requestSafeRange then
                profile.minRejectedExtra = profile.minRejectedExtra and math.min(profile.minRejectedExtra, requestExtra) or requestExtra
                if requestExtra <= (tonumber(profile.safeExtra) or 0) + 0.10 then
                    profile.safeExtra = math.max(0, requestExtra - 0.50)
                end
            end
            profile.step = math.max(tonumber(profile.minStep) or 0.75, (tonumber(profile.step) or 1.0) * 0.60)
            profile.nextProbeAt = resultNow + 6.0
            profile.lastOutcome = string.format("reject %d %.2fm", code, requestDistance)
            learning.nextProbeAt = resultNow + 3.0

            local pCount, tCount, learned = learningSummary(learning)
            stats.adaptiveProfiles, stats.adaptiveTargets, stats.maxLearnedExtra = pCount, tCount, learned
            stats.last = string.format("range rejected %s %.2fm; retry below %.2fm; frontier +%.2fm", label,
                requestDistance, retryBelow, tonumber(profile.safeExtra) or 0)
        else
            target.nextTryAt = resultNow + 3.0
            target.retryBelow = math.max(0, requestDistance * 0.80)
            profile.lastOutcome = string.format("server %d", code)
            stats.last = string.format("server rejected %s code=%d at %.2fm", label, code, requestDistance)
        end
    end

    local callOk, callErr = safeCall(startInteract, player, interactionType, entityId, actionPrototypeId, {}, onResult)
    if not callOk then
        self.interactionPending = nil
        featureError(stats, callErr or (label .. " startInteract failed"))
        return false
    end
    return true
end

function RuntimeFeatures:tickAutoCollect(now)
    return self:tickDirectEntityAction(now, "autoCollect", "isCollectItem", self.settings.collectRange, self.settings.collectCooldown, "collect", false)
end

function RuntimeFeatures:tickAutoChest(now)
    return self:tickDirectEntityAction(now, "autoChest", "isChest", self.settings.chestRange, self.settings.chestCooldown, "chest", true)
end

function RuntimeFeatures:tickAutoDialogueSkip(now)
    if now - self.lastRun.autoDialogueSkip < self.settings.dialogueSkipCooldown then return false end
    self.lastRun.autoDialogueSkip = now
    local runtimePg = rawget(_G, "pg")
    local dialogue = runtimePg and runtimePg.game and runtimePg.game.dialogue
    local running = dialogue and safeRead(dialogue, "dialogueRunningList")
    if type(running) ~= "table" then self.stats.autoDialogueSkip.last = "no dialogue running"; return false end
    for dialogueId, cmd in pairs(running) do
        local canOk, canSkip = safeCall(safeRead(cmd, "canSkip"), cmd)
        if canOk and canSkip == true then
            local skipOk, isSkipping = safeCall(safeRead(cmd, "isSkipping"), cmd)
            if not (skipOk and isSkipping == true) then
                local actionOk, actionResult = safeCall(safeRead(cmd, "startSkip"), cmd)
                if not actionOk then featureError(self.stats.autoDialogueSkip, actionResult or "startSkip failed"); return false end
                if actionResult ~= false then
                    self.stats.autoDialogueSkip.actions = self.stats.autoDialogueSkip.actions + 1
                    self.stats.autoDialogueSkip.last = "skip requested dialogue=" .. tostring(scalar(dialogueId, 80) or "?")
                    return true
                end
            end
        end
    end
    self.stats.autoDialogueSkip.last = "waiting for skippable dialogue"
    return false
end

function RuntimeFeatures:tickAutoCutsceneSkip(now)
    if now - self.lastRun.autoCutsceneSkip < self.settings.cutsceneSkipCooldown then return false end
    self.lastRun.autoCutsceneSkip = now
    local runtimePg = rawget(_G, "pg")
    local ui = runtimePg and runtimePg.global and runtimePg.global.ui
    local UIConst = select(1, requireTable("Const.UIConst"))
    local uid = UIConst and safeRead(UIConst, "UI_ID_SKIP_PANEL")
    if not ui or uid == nil then self.stats.autoCutsceneSkip.last = "skip-panel surface not ready"; return false end
    local openOk, isOpen = safeCall(safeRead(ui, "checkUIOpen"), ui, uid)
    if not openOk or isOpen ~= true then self.lastCutsceneCtrl = nil; self.stats.autoCutsceneSkip.last = "no cutscene skip panel"; return false end
    local ctrlOk, ctrl = safeCall(safeRead(ui, "tryGetCtrlByUid"), ui, uid)
    if not ctrlOk or ctrl == nil then self.stats.autoCutsceneSkip.last = "skip panel controller unavailable"; return false end
    if safeRead(ctrl, "canSkip") ~= true then self.stats.autoCutsceneSkip.last = "waiting for cutscene skip permission"; return false end
    if self.lastCutsceneCtrl == ctrl then self.stats.autoCutsceneSkip.last = "skip already requested for active panel"; return false end
    local info = safeRead(ctrl, "info")
    local skipFunc = info and safeRead(info, "skipFunc")
    if type(skipFunc) ~= "function" then self.stats.autoCutsceneSkip.last = "skip callback unavailable"; return false end
    local startSkipFunc = safeRead(info, "startSkipFunc")
    if type(startSkipFunc) == "function" then
        local startOk, startErr = pcall(startSkipFunc)
        if not startOk then featureError(self.stats.autoCutsceneSkip, startErr); return false end
    end
    self.lastCutsceneCtrl = ctrl
    local dismiss = safeRead(ctrl, "dismiss")
    if type(dismiss) == "function" then safeCall(dismiss, ctrl) end
    local skipOk, skipErr = pcall(skipFunc)
    if not skipOk then self.lastCutsceneCtrl = nil; featureError(self.stats.autoCutsceneSkip, skipErr); return false end
    self.stats.autoCutsceneSkip.actions = self.stats.autoCutsceneSkip.actions + 1
    self.stats.autoCutsceneSkip.last = "cutscene skip callback requested"
    return true
end

function RuntimeFeatures:tick()
    self.tickCount = self.tickCount + 1
    local now = featureNow()
    if self.enabled.infiniteStamina then
        local ok, err = pcall(self.tickInfiniteStamina, self)
        if not ok then featureError(self.stats.infiniteStamina, err) end
    elseif self.staminaOwner ~= nil then self:releaseStamina() end
    if self.enabled.noFallDamage then
        local ok, err = pcall(self.tickNoFallDamage, self)
        if not ok then featureError(self.stats.noFallDamage, err) end
    elseif self.fallSpace ~= nil then self:releaseNoFall() end
    if self.enabled.speedBoost then
        local ok, err = pcall(self.tickSpeedBoost, self)
        if not ok then featureError(self.stats.speedBoost, err) end
    elseif type(self.speedEntries) == "table" and #self.speedEntries > 0 then self:releaseSpeed() end
    if self.overloadReliefEnabled then
        local ok, err = pcall(self.tickOverloadRelief, self)
        if not ok then featureError(self.stats.overloadRelief, err) end
    end
    if self.enabled.fovOverride then
        local ok, err = pcall(self.tickFovOverride, self)
        if not ok then featureError(self.stats.fovOverride, err) end
    elseif self.cameraOwner ~= nil then self:releaseFov() end
    if self.enabled.interconnectionCaptureBoost then
        local ok, result, detail = pcall(self.tickPlayerCaptureBoost, self)
        if not ok then featureError(self.stats.interconnection, result)
        elseif result == false and detail ~= nil then self.stats.interconnection.last = tostring(detail) end
    elseif self.interconnection.playerCaptureBuffSent then
        self:releasePlayerCaptureBoost()
    end
    if self:interconnectionAnyEnabled() then
        local ok, err = pcall(self.tickInterconnection, self)
        if not ok then featureError(self.stats.interconnection, err) end
    elseif self.interconnection.pet ~= nil then self:releaseInterconnection("all-interconnection-features-off") end


    local quickActed = false
    if self.enabled.autoQuickCapture then
        local ok, result = pcall(self.tickAutoQuickCapture, self, now)
        if ok then quickActed = result == true else featureError(self.stats.autoQuickCapture, result) end
    end
    local collectActed = false
    if self.enabled.autoCollect and not quickActed then
        local ok, result = pcall(self.tickAutoCollect, self, now)
        if ok then collectActed = result == true else featureError(self.stats.autoCollect, result) end
    end
    if self.enabled.autoChest and not quickActed and not collectActed then
        local ok, result = pcall(self.tickAutoChest, self, now)
        if not ok then featureError(self.stats.autoChest, result) end
    end
    if self.enabled.autoDialogueSkip then
        local ok, result = pcall(self.tickAutoDialogueSkip, self, now)
        if not ok then featureError(self.stats.autoDialogueSkip, result) end
    end
    if self.enabled.autoCutsceneSkip then
        local ok, result = pcall(self.tickAutoCutsceneSkip, self, now)
        if not ok then featureError(self.stats.autoCutsceneSkip, result) end
    end
end

function RuntimeFeatures:applySettings(quickCaptureMinPercent, collectRange, chestRange, speedMultiplier, fov,
                                  interconnectionDamageMultiplier, interconnectionAttackSpeedMultiplier)
    if quickCaptureMinPercent ~= nil then self.settings.quickCaptureMinPercent = clampInteger(quickCaptureMinPercent, 0, 100, self.settings.quickCaptureMinPercent) end
    if collectRange ~= nil then self.settings.collectRange = clampInteger(collectRange, 3, 250, self.settings.collectRange) end
    if chestRange ~= nil then self.settings.chestRange = clampInteger(chestRange, 3, 250, self.settings.chestRange) end
    if speedMultiplier ~= nil then self.settings.speedMultiplier = math.max(1, math.min(3, tonumber(speedMultiplier) or self.settings.speedMultiplier)) end
    if fov ~= nil then self.settings.fov = clampInteger(fov, 45, 120, self.settings.fov) end
    if interconnectionDamageMultiplier ~= nil then
        self.settings.interconnectionDamageMultiplier = math.floor(math.max(2, math.min(5,
            tonumber(interconnectionDamageMultiplier) or self.settings.interconnectionDamageMultiplier)) + 0.5)
    end
    if interconnectionAttackSpeedMultiplier ~= nil then
        self.settings.interconnectionAttackSpeedMultiplier = math.max(1, math.min(3, tonumber(interconnectionAttackSpeedMultiplier) or self.settings.interconnectionAttackSpeedMultiplier))
    end
end

function RuntimeFeatures:setFeature(name, enabled, quickCaptureMinPercent, collectRange, chestRange, speedMultiplier, fov,
                               interconnectionDamageMultiplier, interconnectionAttackSpeedMultiplier)
    if featureNames[name] ~= true then return false, "unknown-feature" end
    self:applySettings(quickCaptureMinPercent, collectRange, chestRange, speedMultiplier, fov,
                       interconnectionDamageMultiplier, interconnectionAttackSpeedMultiplier)
    local wasEnabled = self.enabled[name] == true
    self.enabled[name] = enabled == true
    if name == "infiniteStamina" and not self.enabled[name] then self:releaseStamina() end
    if name == "noFallDamage" and not self.enabled[name] then self:releaseNoFall() end
    if name == "speedBoost" and not self.enabled[name] then self:releaseSpeed() end
    if name == "fovOverride" and not self.enabled[name] then self:releaseFov() end
    if name == "interconnectionCaptureBoost" and not self.enabled[name] then self:releasePlayerCaptureBoost() end
    if name == "autoCutsceneSkip" and not self.enabled[name] then self.lastCutsceneCtrl = nil end
    if not self:interconnectionAnyEnabled() and self.interconnection.pet ~= nil then self:releaseInterconnection("disabled") end
    if self:anyEnabled() then
        if not self:ensureTimer() then
            for key in pairs(self.enabled) do self.enabled[key] = false end
            self:releaseStamina(); self:releaseNoFall(); self:releaseSpeed(); self:releaseFov(); self:releaseInterconnection("timer-failed"); self.lastCutsceneCtrl = nil
            return false, self.lastTickError ~= "" and self.lastTickError or "feature-timer-not-ready"
        end
    else self:stopTimerIfIdle() end
    return true, self.enabled[name] and "enabled" or "disabled"
end

function RuntimeFeatures:applyNativeControls(c)
    self:applySettings(c.quickCaptureMinPercent, c.collectRange, c.chestRange, c.speedMultiplier, c.fov,
                       c.interconnectionDamageMultiplier, c.interconnectionAttackSpeedMultiplier)
    for name in pairs(featureNames) do self.enabled[name] = c[name] == true end
    if not self.enabled.interconnectionCaptureBoost and self.interconnection.playerCaptureBuffSent then self:releasePlayerCaptureBoost() end
    if not self.enabled.infiniteStamina and self.staminaOwner ~= nil then self:releaseStamina() end
    if not self.enabled.noFallDamage and self.fallSpace ~= nil then self:releaseNoFall() end
    if not self.enabled.speedBoost and type(self.speedEntries) == "table" and #self.speedEntries > 0 then self:releaseSpeed() end
    if not self.enabled.fovOverride and self.cameraOwner ~= nil then self:releaseFov() end
    if not self.enabled.autoCutsceneSkip then self.lastCutsceneCtrl = nil end
    if not self:interconnectionAnyEnabled() and self.interconnection.pet ~= nil then self:releaseInterconnection("native-controls-off") end
    if self:anyEnabled() then return self:ensureTimer() end
    self:stopTimerIfIdle()
    return true
end

function RuntimeFeatures:shutdown()
    for key in pairs(self.enabled) do self.enabled[key] = false end
    self:releasePlayerGodMode()
    self:releaseStamina(); self:releaseNoFall(); self:releaseSpeed(); self:releaseFov(); self:releaseInterconnection("shutdown"); self.lastCutsceneCtrl = nil
    self.interactionPending = nil
    self.interactionLearning = {
        autoCollect = { targets = {}, profiles = {}, nextProbeAt = 0, lastPruneAt = 0 },
        autoChest = { targets = {}, profiles = {}, nextProbeAt = 0, lastPruneAt = 0 }
    }
    self:restoreInteractionBubbleFilter()
    local TimerManager = select(1, requireTable("Core.Timer.TimerManager"))
    if self.timerId ~= nil and TimerManager and type(TimerManager.removeTimer) == "function" then pcall(TimerManager.removeTimer, self.timerId) end
    self.timerId = nil
end

Arkhan.registerModule("runtimeFeatures", RuntimeFeatures)

function Arkhan.playerGodModeFrame(enabled)
    local module = Arkhan.modules.runtimeFeatures
    if type(module) ~= "table" or type(module.tickPlayerGodMode) ~= "function" then return false end
    local ok, result = pcall(module.tickPlayerGodMode, module, enabled == true)
    return ok and result == true
end

function Arkhan.playerOverloadReliefFrame(enabled)
    local module = Arkhan.modules.runtimeFeatures
    if type(module) ~= "table" or type(module.setOverloadReliefEnabled) ~= "function" then return false end
    local ok, result = pcall(module.setOverloadReliefEnabled, module, enabled == true)
    return ok and result == true
end

Arkhan.nativeControlRevision = Arkhan.nativeControlRevision or -1

local function protocolText(value)
    value = tostring(value or "")
    value = value:gsub("[\t\r\n]", " ")
    if #value > 120 then value = value:sub(1, 120) end
    return value
end

local function espKindEnabled(kind, controls)
    if kind == "collect-item" then return controls.espCollect end
    if kind == "chest" then return controls.espChest end
    if kind == "enemy-puppet" then return controls.espEnemy end
    if kind == "puppet" or kind == "pet" or kind == "player-pet" then return controls.espCreature end
    if kind == "npc" then return controls.espNpc end
    return false
end

local function isCreatureKind(kind)
    return kind == "enemy-puppet" or kind == "puppet" or kind == "pet" or kind == "player-pet"
end

local function hasLabel(mask, flag)
    mask = math.max(0, math.floor(tonumber(mask) or 0))
    return math.floor(mask / flag) % 2 == 1
end

local function creatureLabelAllowed(mask, controls)
    mask = math.max(0, math.floor(tonumber(mask) or 0))
    local known = hasLabel(mask, 1) or hasLabel(mask, 2) or hasLabel(mask, 4) or
                  hasLabel(mask, 8) or hasLabel(mask, 16) or hasLabel(mask, 32)
    if not known then return controls.espCreatureNormal == true end
    return (hasLabel(mask, 1) and controls.espCreatureShiny == true) or
           (hasLabel(mask, 2) and controls.espCreatureBoss == true) or
           (hasLabel(mask, 4) and controls.espCreatureElite == true) or
           (hasLabel(mask, 8) and controls.espCreatureVariant == true) or
           (hasLabel(mask, 16) and controls.espCreatureMagic == true) or
           (hasLabel(mask, 32) and controls.espCreatureRainbow == true)
end

local function trimText(value)
    value = tostring(value or "")
    return (value:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function nameListMatches(name, listText)
    local lowerName = string.lower(tostring(name or ""))
    local any = false
    for token in tostring(listText or ""):gmatch("[^,;]+") do
        token = string.lower(trimText(token))
        if token ~= "" then
            any = true
            if lowerName:find(token, 1, true) then return true, true end
        end
    end
    return false, any
end

local function speciesFilterAllowed(name, controls)
    local mode = math.max(0, math.min(2, math.floor(tonumber(controls.espSpeciesFilterMode) or 0)))
    if mode == 0 then return true end
    local matched, any = nameListMatches(name, controls.espSpeciesFilter)
    if not any then return mode ~= 1 end
    if mode == 1 then return matched end
    return not matched
end

local function espHealth(entity)
    local curHp = tonumber(safeRead(entity, "curHp"))
    local maxHp = tonumber(safeRead(entity, "maxHp"))
    if curHp == nil then
        local ok, value = safeCall(safeRead(entity, "getHp"), entity)
        if ok then curHp = tonumber(value) end
    end
    if maxHp == nil then
        local ok, value = safeCall(safeRead(entity, "getMaxHp"), entity)
        if ok then maxHp = tonumber(value) end
    end
    if curHp == nil or maxHp == nil or curHp ~= curHp or maxHp ~= maxHp or maxHp <= 0 then
        return -1, -1
    end
    return math.max(0, curHp), maxHp
end

local function espHeightPosition(entity, basePosition)
    if entity == nil or basePosition == nil then return nil end
    local eModel = safeRead(entity, "eModel")
    local height = tonumber(eModel and safeRead(eModel, "height")) or 0
    if height <= 0 then height = tonumber(safeRead(entity, "bodyHeight")) or 0 end
    if height <= 0 then return nil end

    local x = tonumber(safeRead(basePosition, "x"))
    local y = tonumber(safeRead(basePosition, "y"))
    local z = tonumber(safeRead(basePosition, "z"))
    if x == nil or y == nil or z == nil then return nil end

    local Vector3Type = rawget(_G, "Vector3")
    if Vector3Type == nil then
        local CS = rawget(_G, "CS")
        Vector3Type = CS and CS.UnityEngine and CS.UnityEngine.Vector3
    end
    if Vector3Type == nil then return nil end
    local newFn = safeRead(Vector3Type, "New")
    if type(newFn) == "function" then
        local ok, value = pcall(newFn, x, y + height, z)
        if ok and value ~= nil then return value end
    end
    local ok, value = pcall(Vector3Type, x, y + height, z)
    if ok and value ~= nil then return value end
    return nil
end

local function screenSize()
    local Screen = rawget(_G, "Screen")
    if Screen == nil then
        local CS = rawget(_G, "CS")
        Screen = CS and CS.UnityEngine and CS.UnityEngine.Screen
    end
    return tonumber(safeRead(Screen, "width")) or 1920, tonumber(safeRead(Screen, "height")) or 1080
end

local function appendAdaptiveFeatureProtocol(lines)
    local runtimeFeatures = Arkhan.modules.runtimeFeatures
    if type(runtimeFeatures) ~= "table" then return end
    for _, name in ipairs({ "autoCollect", "autoChest" }) do
        local stats = runtimeFeatures.stats and runtimeFeatures.stats[name] or {}
        local learning = runtimeFeatures.interactionLearning and runtimeFeatures.interactionLearning[name]
        local profiles, targets, maxExtra = 0, 0, 0
        if type(learning) == "table" then profiles, targets, maxExtra = learningSummary(learning) end
        lines[#lines + 1] = string.format(
            "F\t%s\t%d\t%d\t%d\t%d\t%d\t%.3f\t%.3f\t%.3f\t%d\t%d\t%d\t%s",
            name,
            tonumber(stats.actions) or 0,
            tonumber(stats.requests) or 0,
            tonumber(stats.rejected) or 0,
            tonumber(stats.positionRejected) or 0,
            tonumber(stats.probes) or 0,
            tonumber(stats.lastSuccessDistance) or 0,
            tonumber(stats.maxSuccessDistance) or 0,
            tonumber(maxExtra) or 0,
            profiles,
            targets,
            tonumber(stats.suppressedBubbles) or 0,
            protocolText(stats.last or "idle"))
    end
end

local function appendInterconnectionProtocol(lines)
    local runtimeFeatures = Arkhan.modules.runtimeFeatures
    local stats = type(runtimeFeatures) == "table" and runtimeFeatures.stats and runtimeFeatures.stats.interconnection or {}
    lines[#lines + 1] = string.format(
        "I\t%d\t%d\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%s\t%.6f\t%.6f\t%.3f\t%.3f",
        stats.active == true and 1 or 0,
        stats.controlling == true and 1 or 0,
        protocolText(stats.petId or "0"),
        protocolText(stats.templateId or "0"),
        tonumber(stats.abilities) or 0,
        tonumber(stats.castRewrites) or 0,
        tonumber(stats.castRpcSeen) or 0,
        tonumber(stats.buffRequests) or 0,
        tonumber(stats.errors) or 0,
        protocolText(stats.last or "idle"),
        tonumber(stats.ultimateCooldownRate) or -1,
        tonumber(stats.ultimateCooldownMax) or -1,
        tonumber(stats.ultimateSp) or -1,
        tonumber(stats.ultimateSpMax) or -1)
end

local function appendMobVacuumProtocol(lines)
    local vacuum = Arkhan.modules.mobVacuum
    local stats = type(vacuum) == "table" and vacuum.stats or {}
    lines[#lines + 1] = string.format(
        "MV\t%d\t%d\t%d\t%.2f\t%d\t%d\t%d\t%d\t%s",
        type(vacuum) == "table" and (vacuum.enabled == true or vacuum.stun == true) and 1 or 0,
        type(vacuum) == "table" and tonumber(vacuum.radius) or 100,
        type(vacuum) == "table" and tonumber(vacuum.maxMobs) or 12,
        type(vacuum) == "table" and tonumber(vacuum.holdDistance) or 4.0,
        tonumber(stats.candidates) or 0,
        tonumber(stats.active) or 0,
        tonumber(stats.moved) or 0,
        tonumber(stats.errors) or 0,
        protocolText(stats.last or "waiting"))
end

local function appendAutofarmProtocol(lines)
    local farm = Arkhan.modules.autofarm
    local stats = type(farm) == "table" and farm.stats or {}
    lines[#lines + 1] = string.format(
        "AF\t%d\t%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%s\t%.2f\t%d\t%d\t%d\t%s\t%s",
        type(farm) == "table" and farm.enabled == true and 1 or 0,
        type(farm) == "table" and farm.dataLoaded == true and 1 or 0,
        protocolText(type(farm) == "table" and farm.state or "idle"),
        tonumber(stats.selectedTotal) or 0,
        tonumber(stats.remaining) or 0,
        tonumber(stats.collected) or 0,
        tonumber(stats.absent) or 0,
        tonumber(stats.deferred) or 0,
        tonumber(stats.liveNearby) or 0,
        tonumber(stats.currentIndex) or 0,
        protocolText(stats.currentKind or "none"),
        protocolText(stats.currentId or "0"),
        tonumber(stats.targetDistance) or -1,
        tonumber(stats.teleports) or 0,
        tonumber(stats.interactions) or 0,
        tonumber(stats.errors) or 0,
        protocolText(stats.ySource or "none"),
        protocolText(stats.last or "waiting"))
end

local CaptureAssistant = Arkhan.captureAssistant or {
    enabled = false,
    autoThrow = false,
    fastThrow = true,
    skipCameraTransition = true,
    throwAnimSpeed = 2.0,
    ballSpeed = 2.0,
    recoveryDelay = 0.30,
    mode = 0,
    state = "idle",
    targetActorId = "0",
    targetName = "",
    distance = 0,
    chance = -1,
    itemId = 0,
    throws = 0,
    errors = 0,
    last = "idle",
    targetSince = 0,
    nextThrowAt = 0,
    enteredCatchMode = false,
    savedCatchDistance = nil,
    savedCameraDistance = nil,
    currentEntity = nil,
    targetCooldowns = {},
    preparedBall = nil,
    animationBoosted = false,
    boostedBall = nil,
    savedPlayableSpeed = nil,
    appliedPlayableSpeed = nil,
    animationRestoreAt = 0
}
CaptureAssistant.targetCooldowns = type(CaptureAssistant.targetCooldowns) == "table" and CaptureAssistant.targetCooldowns or {}
Arkhan.captureAssistant = CaptureAssistant

local function captureNow()
    local Time = select(1, requireTable("Core.Common.Time"))
    return tonumber(Time and safeRead(Time, "realSecondCache")) or os.clock()
end

local function captureActorId(entity)
    return scalar(safeRead(entity, "actorId"), 96) or scalar(safeRead(entity, "id"), 96) or "0"
end

local function captureRestoreAnimation(me)
    local state = CaptureAssistant
    if not state.animationBoosted then return end
    local restored = false
    local applyAnimSpeed = me and safeRead(me, "applyAnimSpeed")
    if type(applyAnimSpeed) == "function" then
        local ok = safeCall(applyAnimSpeed, me)
        restored = ok == true
    end
    if not restored then
        local eModel = me and safeRead(me, "eModel")
        local saved = tonumber(state.savedPlayableSpeed)
        if eModel and saved then
            restored = pcall(function() eModel.PlayableSpeed = saved end)
        end
    end
    state.animationBoosted = false
    state.boostedBall = nil
    state.savedPlayableSpeed = nil
    state.appliedPlayableSpeed = nil
    state.animationRestoreAt = 0
end

local function captureMaybeRestoreAnimation(me, context, now, force)
    local state = CaptureAssistant
    if not state.animationBoosted then return end
    local currentBall = context and safeRead(context, "ballEnt") or nil
    local ballChanged = state.boostedBall ~= nil and currentBall ~= state.boostedBall
    if force == true or context == nil or ballChanged or (tonumber(now) or 0) >= (tonumber(state.animationRestoreAt) or 0) then
        captureRestoreAnimation(me)
    end
end

local function capturePrepareBall(ballEnt, now)
    local state = CaptureAssistant
    if ballEnt == nil then return end
    if state.preparedBall == ballEnt then return end
    state.preparedBall = ballEnt

    if state.fastThrow then
        local createTime = tonumber(safeRead(ballEnt, "createTime"))
        if createTime then
            pcall(function() ballEnt.createTime = math.min(createTime, (tonumber(now) or captureNow()) - 1.0) end)
        end
    end

    local multiplier = math.max(1.0, math.min(4.0, tonumber(state.ballSpeed) or 1.0))
    if multiplier > 1.001 then
        local data = safeRead(ballEnt, "ballData")
        local maxV = tonumber(data and safeRead(data, "maxV"))
        if type(data) == "table" and maxV then
            local clone = {}
            for k, v in pairs(data) do clone[k] = v end
            clone.maxV = maxV * multiplier
            pcall(function() ballEnt.ballData = clone end)
        end
    end
end

local function captureBoostThrowAnimation(me, ballEnt, now)
    local state = CaptureAssistant
    captureRestoreAnimation(me)
    local multiplier = math.max(1.0, math.min(4.0, tonumber(state.throwAnimSpeed) or 1.0))
    if multiplier <= 1.001 then return end
    local eModel = me and safeRead(me, "eModel")
    local current = tonumber(eModel and safeRead(eModel, "PlayableSpeed"))
    if eModel == nil or current == nil then return end
    local applied = current * multiplier
    local ok = pcall(function() eModel.PlayableSpeed = applied end)
    if not ok then return end
    state.animationBoosted = true
    state.boostedBall = ballEnt
    state.savedPlayableSpeed = current
    state.appliedPlayableSpeed = applied
    state.animationRestoreAt = (tonumber(now) or captureNow()) + 2.0
end

local function captureSetCatchPresentation(cameraGroup, enabled)
    if cameraGroup == nil then return false end
    local fn = safeRead(cameraGroup, "enableCatch")
    if type(fn) ~= "function" then return false end
    local ok = safeCall(fn, cameraGroup, enabled == true)
    return ok == true
end

local function isWildCaptureEntity(Utils, entity, kind)
    local isWild = Utils and safeRead(Utils, "isWildPuppet")
    if type(isWild) == "function" then
        local ok, result = pcall(isWild, entity)
        if ok then return result == true end
    end
    if not isCreatureKind(kind) then return false end
    return safeRead(entity, "masterPlayer") == nil and safeRead(entity, "master") == nil
end

local function selectedNormalBall(runtimePg, Utils)
    local itemId
    local ClientCaptureUtils = select(1, requireTable("Utils.ClientCaptureUtils"))
    if ClientCaptureUtils and type(ClientCaptureUtils.getEnterCatchModeItemId) == "function" then
        local ok, value = pcall(ClientCaptureUtils.getEnterCatchModeItemId)
        if ok then itemId = tonumber(value) end
    end
    if not itemId then
        local hud = runtimePg and runtimePg.global and runtimePg.global.ui and runtimePg.global.ui.hudV2
        if hud then
            local ok, value = safeCall(safeRead(hud, "getCurSelectPropId"), hud)
            if ok then itemId = tonumber(value) end
        end
    end
    if not itemId then return nil, "no usable Aniicapsula selected" end

    local castItemData = select(1, requireTable("Data.cast_item_data"))
    local itemId2Cast = Utils and safeRead(Utils, "itemId2CastItemId")
    if type(castItemData) ~= "table" or type(itemId2Cast) ~= "function" then
        return nil, "capture item data not ready"
    end
    local ok, castId = pcall(itemId2Cast, itemId)
    local ballData = ok and castItemData[tonumber(castId)] or nil
    if not ballData or safeRead(ballData, "proxy") ~= "CatchBall" then
        return nil, "select a normal Aniicapsula (CatchBall)"
    end
    return itemId
end

local function ensureCaptureInfo(row, itemId)
    if row == nil or row.entity == nil or itemId == nil then return false, -1 end
    if row.captureItemId == itemId and row.captureChecked then
        return row.catchable == true, tonumber(row.captureChance) or -1
    end
    row.captureChecked = true
    row.captureItemId = itemId
    row.catchable = false
    row.captureChance = -1
    local CatchProbContext = select(1, requireTable("Common.Utils.CatchProbContext"))
    if not CatchProbContext or type(CatchProbContext.clientGet) ~= "function" then return false, -1 end
    local ok, context = pcall(CatchProbContext.clientGet, row.entity, itemId)
    if not ok or context == nil then return false, -1 end
    row.catchable = safeRead(context, "canCatch") == true
    row.captureChance = math.max(0, math.min(100, (tonumber(safeRead(context, "finalProb")) or 0) * 100))
    return row.catchable, row.captureChance
end

local function espCreatureRowAllowed(row, controls, itemId)
    if not row or not isCreatureKind(row.kind) then return false end
    if not espKindEnabled(row.kind, controls) then return false end
    if not creatureLabelAllowed(row.labelMask, controls) then return false end
    if not speciesFilterAllowed(row.name, controls) then return false end
    if controls.espCatchableOnly == true or (tonumber(controls.espMinCapturePercent) or 0) > 0 then
        local catchable, chance = ensureCaptureInfo(row, itemId)
        if controls.espCatchableOnly == true and not catchable then return false end
        if (tonumber(controls.espMinCapturePercent) or 0) > 0 and
           (chance < 0 or chance + 0.0001 < (tonumber(controls.espMinCapturePercent) or 0)) then return false end
    end
    return true
end

local function captureRelease(runtimePg, reason)
    local state = CaptureAssistant
    local me = runtimePg and runtimePg.me
    captureMaybeRestoreAnimation(me, me and safeRead(me, "currentContext"), captureNow(), true)
    local cameraGroup = runtimePg and runtimePg.game and runtimePg.game.camera and runtimePg.game.camera.playerCameraMode
    local catchCamera = cameraGroup and safeRead(cameraGroup, "catchCamera")
    if catchCamera and state.currentEntity ~= nil then
        local currentTarget = safeRead(catchCamera, "target")
        if currentTarget == state.currentEntity then pcall(safeRead(catchCamera, "setTarget"), catchCamera, nil) end
    end
    if catchCamera then
        if state.savedCatchDistance ~= nil then pcall(function() catchCamera.distance = state.savedCatchDistance end) end
        local cameraMode = safeRead(catchCamera, "cameraMode")
        if cameraMode and state.savedCameraDistance ~= nil then pcall(function() cameraMode.distance = state.savedCameraDistance end) end
    end
    if state.enteredCatchMode and runtimePg and runtimePg.me and runtimePg.me.isInCatchMode and runtimePg.me:isInCatchMode() then
        local context = safeRead(runtimePg.me, "currentContext")
        if context and safeRead(context, "className") == "CameraThrowContext" then
            local controller = runtimePg.game and runtimePg.game.controller
            local setMode = controller and safeRead(controller, "setCatchModeEnable")
            if type(setMode) == "function" then pcall(setMode, controller, false) end
        end
    end
    state.enteredCatchMode = false
    state.savedCatchDistance = nil
    state.savedCameraDistance = nil
    state.currentEntity = nil
    state.preparedBall = nil
    state.targetActorId = "0"
    state.targetName = ""
    state.distance = 0
    state.chance = -1
    state.itemId = 0
    state.targetSince = 0
    state.nextThrowAt = 0
    state.state = reason or "idle"
    state.last = reason or "idle"
end

local function captureSelectTarget(controls, candidates, itemId, runtimePg, now)
    local mode = math.max(0, math.min(3, math.floor(tonumber(controls.captureTargetMode) or 0)))
    local minChance = math.max(0, math.min(100, tonumber(controls.captureMinPercent) or 0))
    now = tonumber(now) or captureNow()
    local markedActorId
    if mode == 3 then
        local lockHelper = runtimePg and runtimePg.game and runtimePg.game.controller and safeRead(runtimePg.game.controller, "lockHelper")
        markedActorId = scalar(lockHelper and safeRead(lockHelper, "forceLockActorId"), 96)
        if markedActorId == nil or tostring(markedActorId) == "0" then return nil, "no game-locked target" end
        markedActorId = tostring(markedActorId)
    end

    for _, row in ipairs(candidates) do
        if row.wild and row.distance <= (tonumber(controls.captureRange) or 50) then
            local cooldownUntil = tonumber(CaptureAssistant.targetCooldowns[tostring(row.actorId or "0")]) or 0
            local allowed = cooldownUntil <= now
            if mode == 1 then
                allowed = allowed and espCreatureRowAllowed(row, controls, itemId)
            elseif mode == 2 then
                local matched, any = nameListMatches(row.name, controls.captureSpecies)
                allowed = allowed and any and matched
            elseif mode == 3 then
                allowed = allowed and tostring(row.actorId) == markedActorId
            end
            if allowed then
                local catchable, chance = ensureCaptureInfo(row, itemId)
                if catchable and chance + 0.0001 >= minChance then return row end
            end
        end
    end
    if mode == 2 and trimText(controls.captureSpecies) == "" then return nil, "species list is empty" end
    return nil, "no eligible Aniimo"
end

local function captureAssistantTick(controls, candidates, runtimePg, Utils)
    local state = CaptureAssistant
    state.enabled = controls.captureAssistant == true
    state.autoThrow = controls.captureAutoThrow == true
    state.fastThrow = controls.captureFastThrow == true
    state.skipCameraTransition = controls.captureSkipCameraTransition == true
    state.throwAnimSpeed = math.max(1.0, math.min(4.0, tonumber(controls.captureThrowAnimSpeed) or 2.0))
    state.ballSpeed = math.max(1.0, math.min(4.0, tonumber(controls.captureBallSpeed) or 2.0))
    state.recoveryDelay = math.max(0.10, math.min(2.0, tonumber(controls.captureRecoveryDelay) or 0.30))
    state.mode = math.max(0, math.min(3, math.floor(tonumber(controls.captureTargetMode) or 0)))
    if not state.enabled then
        if state.currentEntity ~= nil or state.enteredCatchMode then captureRelease(runtimePg, "disabled")
        else state.state = "disabled"; state.last = "disabled" end
        state.targetCooldowns = {}
        return
    end

    local ok, err = pcall(function()
        local now = captureNow()
        local me = runtimePg and runtimePg.me
        if me == nil then state.state = "waiting-player"; state.last = "player not ready"; return end
        local itemId, ballError = selectedNormalBall(runtimePg, Utils)
        if not itemId then state.state = "waiting-ball"; state.last = ballError or "normal Aniicapsula unavailable"; return end
        state.itemId = itemId

        for actorId, untilTime in pairs(state.targetCooldowns) do
            if tonumber(untilTime) == nil or tonumber(untilTime) <= now then state.targetCooldowns[actorId] = nil end
        end

        local liveContext = safeRead(me, "currentContext")
        captureMaybeRestoreAnimation(me, liveContext, now, false)

        local throwingFn = safeRead(me, "isCaptureThrowing")
        if type(throwingFn) == "function" then
            local throwStateOk, throwing = safeCall(throwingFn, me)
            if throwStateOk and throwing == true then
                state.state = "throwing"
                state.last = "normal throw in progress"
                return
            end
        end

        local row, targetError = captureSelectTarget(controls, candidates, itemId, runtimePg, now)
        if not row then
            captureRelease(runtimePg, targetError or "no eligible Aniimo")
            return
        end

        local actorId = tostring(row.actorId or "0")
        if state.targetActorId ~= actorId then
            state.targetActorId = actorId
            state.targetName = row.name or "Aniimo"
            state.targetSince = now
            state.currentEntity = row.entity
        end
        state.distance = row.distance or 0
        state.chance = row.captureChance or -1

        local controller = runtimePg.game and runtimePg.game.controller
        local inCatch = false
        local inCatchFn = safeRead(me, "isInCatchMode")
        if type(inCatchFn) == "function" then
            local catchOk, value = safeCall(inCatchFn, me)
            inCatch = catchOk and value == true
        end
        if not inCatch then
            local setMode = controller and safeRead(controller, "setCatchModeEnable")
            if type(setMode) ~= "function" then state.state = "waiting-context"; state.last = "normal catch-mode controller not ready"; return end
            local enterOk, entered = safeCall(setMode, controller, true)
            if not enterOk or entered == false then state.state = "waiting-context"; state.last = "normal catch mode rejected"; return end
            state.enteredCatchMode = true
            state.targetSince = now
            state.state = "entering-catch"
            state.last = "entered normal capture mode"
            return
        end

        local context = safeRead(me, "currentContext")
        if context == nil or safeRead(context, "className") ~= "CameraThrowContext" then
            state.state = "context-blocked"
            state.last = "current capture context is not CameraThrowContext"
            return
        end

        local cameraGroup = runtimePg.game and runtimePg.game.camera and runtimePg.game.camera.playerCameraMode
        if state.skipCameraTransition then captureSetCatchPresentation(cameraGroup, true) end
        local catchCamera = cameraGroup and safeRead(cameraGroup, "catchCamera")
        if catchCamera == nil then state.state = "waiting-camera"; state.last = "catch camera not ready"; return end
        local cameraMode = safeRead(catchCamera, "cameraMode")
        if state.savedCatchDistance == nil then state.savedCatchDistance = tonumber(safeRead(catchCamera, "distance")) end
        if state.savedCameraDistance == nil and cameraMode then state.savedCameraDistance = tonumber(safeRead(cameraMode, "distance")) end
        local desiredDistance = math.max(5, tonumber(controls.captureRange) or 50) + 2
        pcall(function()
            catchCamera.distance = math.max(tonumber(catchCamera.distance) or 0, desiredDistance)
            if cameraMode then cameraMode.distance = math.max(tonumber(cameraMode.distance) or 0, desiredDistance) end
        end)
        local aimOk, aimResult = safeCall(safeRead(catchCamera, "setTarget"), catchCamera, row.entity)
        if not aimOk then state.state = "aim-error"; state.last = tostring(scalar(aimResult, 160) or "catchCamera:setTarget failed"); return end
        state.currentEntity = row.entity

        local aimSettle = state.fastThrow and 0.0 or 0.25
        if now - (tonumber(state.targetSince) or now) < aimSettle then
            state.state = "aiming"
            state.last = "normal catch camera locking target"
            return
        end

        if not state.autoThrow then
            state.state = "ready"
            state.last = "aim locked; manual throw"
            return
        end
        if now < (tonumber(state.nextThrowAt) or 0) then
            state.state = "cooldown"
            state.last = "waiting between throws"
            return
        end
        local ballEnt = safeRead(context, "ballEnt")
        if ballEnt == nil then state.state = "waiting-ball"; state.last = "waiting held Aniicapsula"; return end
        capturePrepareBall(ballEnt, now)
        local readyFn = safeRead(ballEnt, "isBallReady")
        if type(readyFn) == "function" then
            local readyOk, ready = safeCall(readyFn, ballEnt)
            if not readyOk or ready ~= true then state.state = "waiting-ball"; state.last = "Aniicapsula not ready"; return end
        end

        local throwFn = safeRead(me, "captureThrow")
        if type(throwFn) ~= "function" then state.state = "throw-error"; state.last = "captureThrow unavailable"; return end
        captureBoostThrowAnimation(me, ballEnt, now)
        local throwOk, throwResult = safeCall(throwFn, me)
        if not throwOk then
            captureMaybeRestoreAnimation(me, context, now, true)
            state.errors = (tonumber(state.errors) or 0) + 1
            state.state = "throw-error"
            state.last = tostring(scalar(throwResult, 160) or "normal captureThrow failed")
            state.nextThrowAt = now + state.recoveryDelay
            return
        end
        if state.skipCameraTransition then
            captureSetCatchPresentation(cameraGroup, false)
        end
        state.throws = (tonumber(state.throws) or 0) + 1
        state.targetCooldowns[actorId] = now + 5.0
        state.nextThrowAt = now + state.recoveryDelay
        state.state = "throw-dispatched"
        state.last = string.format("normal throw -> %s (%.1f%%) | anim %.2fx | ball %.2fx | recovery %.2fs",
            tostring(row.name or "Aniimo"), tonumber(row.captureChance) or 0, state.throwAnimSpeed, state.ballSpeed, state.recoveryDelay)
    end)
    if not ok then
        state.errors = (tonumber(state.errors) or 0) + 1
        state.state = "error"
        state.last = tostring(scalar(err, 180) or "capture-assistant error")
    end
end

local function appendCaptureAssistantProtocol(lines)
    local s = CaptureAssistant
    lines[#lines + 1] = string.format(
        "C\t%d\t%d\t%d\t%s\t%s\t%s\t%.2f\t%.2f\t%d\t%d\t%d\t%s",
        s.enabled == true and 1 or 0,
        s.autoThrow == true and 1 or 0,
        tonumber(s.mode) or 0,
        protocolText(s.state or "idle"),
        protocolText(s.targetActorId or "0"),
        protocolText(s.targetName or ""),
        tonumber(s.distance) or 0,
        tonumber(s.chance) or -1,
        tonumber(s.itemId) or 0,
        tonumber(s.throws) or 0,
        tonumber(s.errors) or 0,
        protocolText(s.last or "idle"))
end


local EggHaste = {
    inMode = false,
    liveSpace = false,
    context = "outside",
    controllingEgg = false,
    fastCarry = false,
    controllerReady = false,
    spaceType = 0,
    sceneId = 0,
    controller = nil,
    controllerBase = nil,
    robEggClass = nil,
    robEggVtbl = nil,
    originalBasicControlInfo = nil,
    patchEnabled = false,
    patchMoveMultiplier = 1.0,
    patchDashMultiplier = 1.0,
    lastSetupKey = nil,
    lastSetupEggId = nil,
    originalFastCarryRatio = nil,
    appliedFastCarryRatio = nil,
    sprintRatioClass = nil,
    sprintRatioVtbl = nil,
    originalSprintRatio = nil,
    fastCarryPatchEnabled = false,
    fastCarryRequestedRatio = 1.4,
    fastCarryBaselineRatio = 1.4,
    eggSpeedMultiplier = 1.0,
    dashMultiplier = 1.0,
    fastCarryRatio = 1.4,
    liveEggs = 0, syncedEggs = 0, cachedEggNests = 0,
    viewFogRemoved = false,
    fullMapRevealed = false,
    fogClass = nil,
    playerFogClass = nil,
    originalPreloadFogViewLimit = nil,
    originalRefreshFogViewLimit = nil,
    originalSetSightOfViewRange = nil,
    originalShouldRevealAllMapFog = nil,
    viewFogPatchEnabled = false,
    mapFogPatchEnabled = false,
    last = "waiting for Egg Haste"
}

local function tryWrite(object, key, value)
    if object == nil then return false end
    return pcall(function() object[key] = value end)
end

local function resolveEggHasteContext(runtimePg)
    local me = runtimePg and safeRead(runtimePg, "me")
    local meSpace = me and safeRead(me, "space") or nil
    local pgSpace = runtimePg and safeRead(runtimePg, "space") or nil

    local Const = select(1, requireTable("Common.Const.Const"))
    local pvpScene = tonumber(Const and safeRead(Const, "ROB_EGG_SCENE_ID")) or 3004
    local pveScene = tonumber(Const and safeRead(Const, "ROB_EGG_SCENE_CLIP_ID")) or 3005
    local robEggType = tonumber(Const and safeRead(Const, "SPACE_TYPE_ROBEGG")) or 12
    local robEggUndergroundType = tonumber(Const and safeRead(Const, "SPACE_TYPE_ROBEGG_UNDERGROUND")) or 100

    local function isActiveEggHasteSpace(candidate)
        if candidate == nil then return false end
        local candidateType = tonumber(safeRead(candidate, "spaceType")) or 0
        if candidateType == robEggType or candidateType == robEggUndergroundType then
            return true
        end
        local isGrabEgg = safeRead(candidate, "isGrabEgg")
        if type(isGrabEgg) == "function" then
            local ok, value = safeCall(isGrabEgg, candidate)
            if ok and value == true then return true end
        end
        local candidateScene = tonumber(
            safeRead(candidate, "sceneId") or safeRead(candidate, "masterDungeonSceneId"))
        return candidateScene == pvpScene or candidateScene == pveScene
    end
    local space
    if isActiveEggHasteSpace(pgSpace) then
        space = pgSpace
    elseif isActiveEggHasteSpace(meSpace) then
        space = meSpace
    else
        space = meSpace or pgSpace
    end

    local anchors = {}
    local function anchor(name)
        anchors[#anchors + 1] = name
    end

    local liveSpace = false
    local spaceType = tonumber(space and safeRead(space, "spaceType")) or 0
    local isGrabEgg = space and safeRead(space, "isGrabEgg")
    if type(isGrabEgg) == "function" then
        local ok, value = safeCall(isGrabEgg, space)
        if ok and value == true then
            liveSpace = true
            anchor("space-method")
        end
    end
    if spaceType == robEggType or spaceType == robEggUndergroundType then
        liveSpace = true
        anchor("space-type")
    end

    local sceneId = tonumber(space and (safeRead(space, "sceneId") or safeRead(space, "masterDungeonSceneId"))) or 0

    local global = runtimePg and safeRead(runtimePg, "global")
    local sceneMgr = global and safeRead(global, "scene")
    local targetSceneId = tonumber(sceneMgr and (
        safeRead(sceneMgr, "targetSceneId") or safeRead(sceneMgr, "currentSceneId") or safeRead(sceneMgr, "sceneId"))) or 0
    if targetSceneId == pvpScene or targetSceneId == pveScene then
        liveSpace = true
        anchor("target-scene")
    end

    local dungeonSceneId = 0
    local inDungeonSession = false
    local inTeamRoom = false

    if me ~= nil then
        local getDungeonSceneId = safeRead(me, "getDungeonSceneId")
        if type(getDungeonSceneId) == "function" then
            local ok, value = safeCall(getDungeonSceneId, me)
            if ok then dungeonSceneId = tonumber(value) or 0 end
        end

        local method = safeRead(me, "isInGrabEggDungeon")
        if type(method) == "function" then
            local ok, value = safeCall(method, me)
            inDungeonSession = ok and value == true
            if inDungeonSession then anchor("player-dungeon") end
        end
        method = safeRead(me, "isInGrabEggTeamRoom")
        if type(method) == "function" then
            local ok, value = safeCall(method, me)
            inTeamRoom = ok and value == true
            if inTeamRoom then anchor("team-room") end
        end

        local getTeam = safeRead(me, "getCurTeamInfo")
        if type(getTeam) == "function" then
            local ok, teamInfo = safeCall(getTeam, me)
            if ok and teamInfo ~= nil then
                local teamSceneId = tonumber(safeRead(teamInfo, "dungeonSceneId")) or 0
                if dungeonSceneId == 0 then dungeonSceneId = teamSceneId end
            end
        end
    end

    if dungeonSceneId == pvpScene or dungeonSceneId == pveScene then
        inDungeonSession = true
        anchor("dungeon-id")
    end

    local ui = global and safeRead(global, "ui")
    local hud = ui and safeRead(ui, "hudV2")
    local hudRobEgg = false
    if hud ~= nil then
        for _, layoutName in ipairs({"MD", "LD", "RD", "LM", "RM", "LU", "RU", "MU"}) do
            local layout = safeRead(hud, layoutName)
            local component = layout and safeRead(layout, "robEggSprite")
            if component ~= nil and safeRead(component, "isRobEgg") == true then
                hudRobEgg = true
                break
            end
        end
    end
    if hudRobEgg then
        liveSpace = true
        anchor("robegg-hud")
    end

    local game = runtimePg and safeRead(runtimePg, "game")
    local grabEgg = game and safeRead(game, "grabEgg")
    local timerLive = false
    if grabEgg ~= nil then
        local curEndTime = tonumber(safeRead(grabEgg, "curEndTime"))
        local Time = select(1, requireTable("Core.Common.Time"))
        local now = tonumber(Time and safeRead(Time, "secondCache"))
        if curEndTime ~= nil and now ~= nil and curEndTime > now then
            timerLive = true
            liveSpace = true
            anchor("escape-timer")
        end
    end

    local modeUiOpen = false
    local checkUiOpen = ui and safeRead(ui, "checkUIOpen")
    if type(checkUiOpen) == "function" then
        local UIConst = select(1, requireTable("Const.UIConst"))
        local uiId = tonumber(UIConst and safeRead(UIConst, "UI_ID_GRAB_EGGS_MODE")) or 806
        local ok, value = safeCall(checkUiOpen, ui, uiId)
        modeUiOpen = ok and value == true
        if modeUiOpen then anchor("mode-ui") end
    end

    local active = liveSpace or inDungeonSession or modeUiOpen
    local context = "outside"
    if liveSpace then context = "dungeon"
    elseif inTeamRoom then context = "team-room"
    elseif inDungeonSession then context = "GrabEgg-session"
    elseif modeUiOpen then context = "mode-ui" end

    if sceneId == 0 and targetSceneId ~= 0 then sceneId = targetSceneId end
    if sceneId == 0 and dungeonSceneId ~= 0 then sceneId = dungeonSceneId end

    local detector = string.format(
        "anchors=%s space=%d target=%d dungeon=%d hud=%d timer=%d",
        #anchors > 0 and table.concat(anchors, "+") or "none",
        spaceType, targetSceneId, dungeonSceneId, hudRobEgg and 1 or 0, timerLive and 1 or 0)
    return active, liveSpace, context, space, sceneId, detector
end

local function restoreEggController()
    local controller = EggHaste.controller
    local base = EggHaste.controllerBase
    if controller ~= nil and type(base) == "table" then
        for key, value in pairs(base) do
            tryWrite(controller, key, value)
        end
    end
    EggHaste.controller = nil
    EggHaste.controllerBase = nil
    EggHaste.controllerReady = false
end

local function cacheEggController(controller)
    if controller == nil then
        restoreEggController()
        return nil
    end
    if EggHaste.controller ~= controller then
        restoreEggController()
        EggHaste.controller = controller
        EggHaste.controllerBase = {
            moveForce = tonumber(safeRead(controller, "moveForce")),
            maxVelocity = tonumber(safeRead(controller, "maxVelocity")),
            maxVelocityInAir = tonumber(safeRead(controller, "maxVelocityInAir")),
            dashImpulseRatio = tonumber(safeRead(controller, "dashImpulseRatio")),
            accelerationDuration = tonumber(safeRead(controller, "accelerationDuration")),
            decelerationDuration = tonumber(safeRead(controller, "decelerationDuration"))
        }
    end
    EggHaste.controllerReady = EggHaste.controllerBase ~= nil and
        EggHaste.controllerBase.moveForce ~= nil and EggHaste.controllerBase.maxVelocity ~= nil
    return EggHaste.controller
end

local function currentEggController(runtimePg)
    local me = runtimePg and runtimePg.me
    if me == nil then return nil, nil end
    local controlling = false
    local check = safeRead(me, "isControllingEgg")
    if type(check) == "function" then
        local ok, value = safeCall(check, me)
        controlling = ok and value == true
    else
        controlling = tonumber(safeRead(me, "controlEggId")) ~= nil and tonumber(safeRead(me, "controlEggId")) ~= 0
    end
    EggHaste.controllingEgg = controlling
    if not controlling then return nil, nil end

    local egg
    local getEgg = safeRead(me, "getCurControllingEgg")
    if type(getEgg) == "function" then
        local ok, value = safeCall(getEgg, me)
        if ok then egg = value end
    end
    if egg == nil then
        local eggId = safeRead(me, "controlEggId")
        local ok, value = callGlobal(runtimePg, "getEntity", eggId)
        if ok then egg = value end
    end
    local eModel = egg and safeRead(egg, "eModel")
    local component = eModel and safeRead(eModel, "physicControllerComponent")
    local controller = component and safeRead(component, "realPhysicController")
    return controller, egg
end

local function installEggControlPatch(moveMultiplier, dashMultiplier)
    local RobEggClass = select(1, requireTable("Entities.SpaceEntities.ClientRobSpaceEgg"))
    if type(RobEggClass) ~= "table" then return false end
    if EggHaste.originalBasicControlInfo == nil then
        local getVtbl = safeRead(RobEggClass, "getVtbl")
        local vtbl = nil
        if type(getVtbl) == "function" then
            local ok, value = safeCall(getVtbl, RobEggClass)
            if ok and type(value) == "table" then vtbl = value end
        end
        if type(vtbl) ~= "table" then return false end
        local original = rawget(vtbl, "getBasicControlInfo") or safeRead(RobEggClass, "getBasicControlInfo")
        if type(original) ~= "function" then return false end
        EggHaste.robEggClass = RobEggClass
        EggHaste.robEggVtbl = vtbl
        EggHaste.originalBasicControlInfo = original
        vtbl.getBasicControlInfo = function(self)
            local info = EggHaste.originalBasicControlInfo(self)
            if type(info) ~= "table" or EggHaste.patchEnabled ~= true then return info end
            local out = {}
            for index, value in ipairs(info) do out[index] = value end
            local moveMul = EggHaste.patchMoveMultiplier or 1.0
            local dashMul = EggHaste.patchDashMultiplier or 1.0
            if tonumber(out[1]) then out[1] = out[1] * moveMul end
            if tonumber(out[2]) then out[2] = out[2] * moveMul end
            if tonumber(out[3]) then out[3] = out[3] * dashMul end
            if tonumber(out[5]) then out[5] = out[5] * moveMul end
            if tonumber(out[6]) then out[6] = out[6] / math.sqrt(moveMul) end
            if tonumber(out[7]) then out[7] = out[7] / math.sqrt(moveMul) end
            return out
        end
    end
    EggHaste.patchMoveMultiplier = moveMultiplier or 1.0
    EggHaste.patchDashMultiplier = dashMultiplier or 1.0
    EggHaste.patchEnabled = true
    return true
end

local function restoreEggControlPatch()
    if EggHaste.robEggVtbl ~= nil and EggHaste.originalBasicControlInfo ~= nil then
        pcall(function() EggHaste.robEggVtbl.getBasicControlInfo = EggHaste.originalBasicControlInfo end)
    end
    EggHaste.robEggClass = nil
    EggHaste.robEggVtbl = nil
    EggHaste.originalBasicControlInfo = nil
    EggHaste.patchEnabled = false
    EggHaste.patchMoveMultiplier = 1.0
    EggHaste.patchDashMultiplier = 1.0
    EggHaste.lastSetupKey = nil
    EggHaste.lastSetupEggId = nil
end

local function refreshEggControllerFromLua(runtimePg, egg, configKey)
    if egg == nil or runtimePg == nil or runtimePg.me == nil then return false end
    local eggId = tostring(safeRead(egg, "id") or safeRead(egg, "actorId") or "0")
    if EggHaste.lastSetupKey == configKey and EggHaste.lastSetupEggId == eggId then return true end
    local eModel = safeRead(egg, "eModel")
    local setup = eModel and safeRead(eModel, "SetupController")
    local playerModel = safeRead(runtimePg.me, "eModel")
    local Const = select(1, requireTable("Common.Const.Const"))
    local componentIndex = Const and safeRead(Const, "COMPONENT_PHYSIC_CONTROLLER") or 35
    if type(setup) ~= "function" or playerModel == nil then return false end
    local ok = safeCall(setup, eModel, componentIndex, playerModel, 0)
    if ok then
        EggHaste.lastSetupKey = configKey
        EggHaste.lastSetupEggId = eggId
        return true
    end
    return false
end

local function fastCarryBaselineRatio()
    local SysConfigData = select(1, requireTable("Data.sys_config_data"))
    return tonumber(SysConfigData and safeRead(SysConfigData, "FAST_CARRY_EGG_SPEED_RATIO")) or 1.4
end

local function installFastCarryRatioPatch(runtimePg, requestedRatio)
    local Attr = select(1, requireTable("Common.Ability.Attribute.ActorCombatAttribute"))
    if type(Attr) ~= "table" then return false end
    if EggHaste.originalSprintRatio == nil then
        local getVtbl = safeRead(Attr, "getVtbl")
        local vtbl = nil
        if type(getVtbl) == "function" then
            local ok, value = safeCall(getVtbl, Attr)
            if ok and type(value) == "table" then vtbl = value end
        end
        if type(vtbl) ~= "table" then return false end
        local original = rawget(vtbl, "getSprintSpeedRatio") or safeRead(Attr, "getSprintSpeedRatio")
        if type(original) ~= "function" then return false end
        EggHaste.sprintRatioClass = Attr
        EggHaste.sprintRatioVtbl = vtbl
        EggHaste.originalSprintRatio = original
        vtbl.getSprintSpeedRatio = function(self)
            local ratio = EggHaste.originalSprintRatio(self)
            if EggHaste.fastCarryPatchEnabled ~= true then return ratio end
            local entity = safeRead(self, "entity")
            local check = entity and safeRead(entity, "FAST_CARRY_EGG_ST")
            local fast = false
            if type(check) == "function" then
                local ok, value = safeCall(check, entity)
                fast = ok and value == true
            end
            if not fast then return ratio end
            local baseline = tonumber(EggHaste.fastCarryBaselineRatio) or 1.4
            local target = tonumber(EggHaste.fastCarryRequestedRatio) or baseline
            if math.abs(baseline) < 0.0001 then return ratio end
            return ratio * (target / baseline)
        end
    end
    EggHaste.fastCarryBaselineRatio = fastCarryBaselineRatio()
    EggHaste.fastCarryRequestedRatio = math.max(1.0, math.min(3.0, tonumber(requestedRatio) or EggHaste.fastCarryBaselineRatio))
    EggHaste.fastCarryPatchEnabled = true
    EggHaste.fastCarryRatio = EggHaste.fastCarryRequestedRatio
    local me = runtimePg and safeRead(runtimePg, "me")
    local refresh = me and safeRead(me, "onMotionAttrChange")
    if type(refresh) == "function" then pcall(refresh, me) end
    return true
end

local function restoreFastCarryRatio(runtimePg)
    local hadPatch = EggHaste.originalSprintRatio ~= nil
    EggHaste.fastCarryPatchEnabled = false
    if EggHaste.sprintRatioVtbl ~= nil and EggHaste.originalSprintRatio ~= nil then
        pcall(function() EggHaste.sprintRatioVtbl.getSprintSpeedRatio = EggHaste.originalSprintRatio end)
    end
    EggHaste.sprintRatioClass = nil
    EggHaste.sprintRatioVtbl = nil
    EggHaste.originalSprintRatio = nil
    EggHaste.fastCarryBaselineRatio = 1.4
    EggHaste.fastCarryRequestedRatio = 1.4
    EggHaste.fastCarryRatio = 1.4
    EggHaste.originalFastCarryRatio = nil
    EggHaste.appliedFastCarryRatio = nil
    if hadPatch then
        local me = runtimePg and safeRead(runtimePg, "me")
        local refresh = me and safeRead(me, "onMotionAttrChange")
        if type(refresh) == "function" then pcall(refresh, me) end
    end
end

local function eggHasteFogClass()
    if type(EggHaste.fogClass) == "table" then return EggHaste.fogClass end
    local class = select(1, requireTable("Entities.SpaceEntities.ClientRobEggUnderGroundPlace"))
    if type(class) == "table" then EggHaste.fogClass = class end
    return class
end

local function eggHastePlayerFogClass()
    if type(EggHaste.playerFogClass) == "table" then return EggHaste.playerFogClass end
    local class = select(1, requireTable("Entities.SpaceEntities.PlayerComponent.ClientGrabEggComponent"))
    if type(class) == "table" then EggHaste.playerFogClass = class end
    return class
end

local function eggHasteClassVtbl(class)
    if type(class) ~= "table" then return nil end
    local getVtbl = safeRead(class, "getVtbl")
    if type(getVtbl) ~= "function" then return nil end
    local ok, vtbl = safeCall(getVtbl, class)
    if ok and type(vtbl) == "table" then return vtbl end
    return nil
end

local function eggHasteNotifyMapFogRefresh()
    local facadeObject = rawget(_G, "facade")
    if facadeObject == nil then return false end
    local MessageName = select(1, requireTable("Const.MessageName"))
    local message = MessageName and safeRead(MessageName, "GRAB_EGG_NOVICE_PROTECTION_CHANGED")
    local send = safeRead(facadeObject, "SendMessageCommand")
    if message == nil or type(send) ~= "function" then return false end
    local ok = safeCall(send, facadeObject, message)
    return ok == true
end

local function restoreEggHasteViewFog(runtimePg)
    if not EggHaste.viewFogPatchEnabled then
        EggHaste.viewFogRemoved = false
        return
    end
    local class = eggHasteFogClass()
    local vtbl = eggHasteClassVtbl(class)
    if type(vtbl) == "table" then
        if EggHaste.originalPreloadFogViewLimit ~= nil then rawset(vtbl, "preloadFogViewLimit", EggHaste.originalPreloadFogViewLimit) end
        if EggHaste.originalRefreshFogViewLimit ~= nil then rawset(vtbl, "refreshFogViewLimit", EggHaste.originalRefreshFogViewLimit) end
    end
    local playerClass = eggHastePlayerFogClass()
    local playerVtbl = eggHasteClassVtbl(playerClass)
    if type(playerVtbl) == "table" and EggHaste.originalSetSightOfViewRange ~= nil then
        rawset(playerVtbl, "setSightOfViewRange", EggHaste.originalSetSightOfViewRange)
    end
    EggHaste.viewFogPatchEnabled = false
    EggHaste.viewFogRemoved = false

    local space = runtimePg and safeRead(runtimePg, "space")
    if space == nil then
        local me = runtimePg and safeRead(runtimePg, "me")
        space = me and safeRead(me, "space") or nil
    end
    local refresh = space and safeRead(space, "refreshFogViewLimit")
    if type(refresh) == "function" then safeCall(refresh, space) end
end

local function installEggHasteViewFog(runtimePg)
    local class = eggHasteFogClass()
    local vtbl = eggHasteClassVtbl(class)
    if type(vtbl) ~= "table" then return false, "fog vtbl unavailable" end
    if not EggHaste.viewFogPatchEnabled then
        local preload = rawget(vtbl, "preloadFogViewLimit") or safeRead(class, "preloadFogViewLimit")
        local refresh = rawget(vtbl, "refreshFogViewLimit") or safeRead(class, "refreshFogViewLimit")
        if type(preload) ~= "function" or type(refresh) ~= "function" then return false, "fog methods unavailable" end
        EggHaste.originalPreloadFogViewLimit = preload
        EggHaste.originalRefreshFogViewLimit = refresh
        rawset(vtbl, "preloadFogViewLimit", function(self) return end)
        rawset(vtbl, "refreshFogViewLimit", function(self) return end)
        EggHaste.viewFogPatchEnabled = true

        local playerClass = eggHastePlayerFogClass()
        local playerVtbl = eggHasteClassVtbl(playerClass)
        if type(playerVtbl) == "table" then
            local setSight = rawget(playerVtbl, "setSightOfViewRange") or safeRead(playerClass, "setSightOfViewRange")
            if type(setSight) == "function" then
                EggHaste.originalSetSightOfViewRange = setSight
                rawset(playerVtbl, "setSightOfViewRange", function(self, range, force, useAfterViewLimit)
                    if (tonumber(range) or 0) > 0 then
                        local currentPg = rawget(_G, "pg")
                        local global = currentPg and safeRead(currentPg, "global")
                        local cameraMgr = global and safeRead(global, "cameraMgr")
                        local unload = cameraMgr and safeRead(cameraMgr, "UnloadSightOfViewEffect")
                        if type(unload) == "function" then safeCall(unload, cameraMgr, true) end
                        return
                    end
                    return EggHaste.originalSetSightOfViewRange(self, range, force, useAfterViewLimit)
                end)
            end
        end
    end

    local space = runtimePg and safeRead(runtimePg, "space")
    if space == nil then
        local me = runtimePg and safeRead(runtimePg, "me")
        space = me and safeRead(me, "space") or nil
    end
    local stopRefresh = space and safeRead(space, "stopRefreshFogVisualParams")
    if type(stopRefresh) == "function" then safeCall(stopRefresh, space) end

    local global = runtimePg and safeRead(runtimePg, "global")
    local cameraMgr = global and safeRead(global, "cameraMgr")
    local unload = cameraMgr and safeRead(cameraMgr, "UnloadSightOfViewEffect")
    if type(unload) ~= "function" then return false, "camera fog API unavailable" end
    local ok = safeCall(unload, cameraMgr, true)
    EggHaste.viewFogRemoved = ok == true
    return EggHaste.viewFogRemoved, EggHaste.viewFogRemoved and "removed" or "unload failed"
end

local function restoreEggHasteMapFog()
    if not EggHaste.mapFogPatchEnabled then
        EggHaste.fullMapRevealed = false
        return
    end
    local class = eggHasteFogClass()
    local vtbl = eggHasteClassVtbl(class)
    if type(vtbl) == "table" and EggHaste.originalShouldRevealAllMapFog ~= nil then
        rawset(vtbl, "shouldRevealAllMapFog", EggHaste.originalShouldRevealAllMapFog)
    end
    EggHaste.mapFogPatchEnabled = false
    EggHaste.fullMapRevealed = false
    eggHasteNotifyMapFogRefresh()
end

local function installEggHasteMapFog()
    local class = eggHasteFogClass()
    local vtbl = eggHasteClassVtbl(class)
    if type(vtbl) ~= "table" then return false, "fog vtbl unavailable" end
    if not EggHaste.mapFogPatchEnabled then
        local original = rawget(vtbl, "shouldRevealAllMapFog") or safeRead(class, "shouldRevealAllMapFog")
        if type(original) ~= "function" then return false, "map fog method unavailable" end
        EggHaste.originalShouldRevealAllMapFog = original
        rawset(vtbl, "shouldRevealAllMapFog", function(self, mapSceneId)
            if mapSceneId ~= nil then
                local runtimePg = rawget(_G, "pg")
                local game = runtimePg and safeRead(runtimePg, "game")
                local map = game and safeRead(game, "map")
                local convert = map and safeRead(map, "convertSceneId")
                if type(convert) ~= "function" then return false end
                local okCurrent, currentMapScene = safeCall(convert, map, safeRead(self, "sceneId"))
                local okTarget, targetMapScene = safeCall(convert, map, mapSceneId)
                if not okCurrent or not okTarget or currentMapScene ~= targetMapScene then return false end
            end
            return true
        end)
        EggHaste.mapFogPatchEnabled = true
    end
    EggHaste.fullMapRevealed = true
    eggHasteNotifyMapFogRefresh()
    return true, "revealed"
end

local function eggHasteFogTick(controls, runtimePg)
    local active = controls.eggHasteEnabled == true and EggHaste.liveSpace == true
    if active and controls.eggHasteRemoveViewFog == true then
        if not EggHaste.viewFogPatchEnabled or not EggHaste.viewFogRemoved then installEggHasteViewFog(runtimePg) end
    else
        restoreEggHasteViewFog(runtimePg)
    end

    if active and controls.eggHasteRevealFullMap == true then
        if not EggHaste.mapFogPatchEnabled or not EggHaste.fullMapRevealed then installEggHasteMapFog() end
    else
        restoreEggHasteMapFog()
    end
end

local function eggHasteTick(controls, runtimePg)
    local contextActive, liveSpace, contextName, space, sceneHint, contextDetector = resolveEggHasteContext(runtimePg)
    EggHaste.inMode = contextActive
    EggHaste.liveSpace = liveSpace
    EggHaste.context = contextName
    EggHaste.contextDetector = contextDetector or "anchors=none"
    local me = runtimePg and runtimePg.me
    EggHaste.fastCarry = me ~= nil and safeRead(me, "isInFastCarryEggState") == true or false
    EggHaste.controllingEgg = false

    if controls.eggHasteEnabled ~= true or EggHaste.inMode ~= true then
        restoreEggControlPatch()
        restoreEggController()
        restoreFastCarryRatio(runtimePg)
        restoreEggHasteViewFog(runtimePg)
        restoreEggHasteMapFog()
        EggHaste.liveSpace = false
        EggHaste.context = "outside"
        EggHaste.last = controls.eggHasteEnabled == true and ("waiting for Egg Haste / GrabEgg context | " .. tostring(EggHaste.contextDetector)) or "Egg Haste tools disabled"
        return
    end

    if controls.eggHasteFastCarryBoost == true then
        installFastCarryRatioPatch(runtimePg, controls.eggHasteFastCarryRatio)
    else
        restoreFastCarryRatio(runtimePg)
    end

    local moveMul = controls.eggHasteEggMovement == true and math.max(1.0, math.min(4.0, tonumber(controls.eggHasteEggSpeedMultiplier) or 1.5)) or 1.0
    local dashMul = controls.eggHasteDashBoost == true and math.max(1.0, math.min(4.0, tonumber(controls.eggHasteDashMultiplier) or 1.5)) or 1.0
    if controls.eggHasteEggMovement == true or controls.eggHasteDashBoost == true then
        installEggControlPatch(moveMul, dashMul)
    else
        restoreEggControlPatch()
    end

    local controller, currentEgg = currentEggController(runtimePg)
    local setupReady = false
    if currentEgg ~= nil then
        setupReady = refreshEggControllerFromLua(runtimePg, currentEgg, string.format("%.3f/%.3f", moveMul, dashMul))
    end
    controller = cacheEggController(controller)
    EggHaste.controllerReady = EggHaste.controllerReady or setupReady
    if controller ~= nil and EggHaste.controllerBase ~= nil then
        local base = EggHaste.controllerBase
        if base.moveForce then tryWrite(controller, "moveForce", base.moveForce * moveMul) end
        if base.maxVelocity then tryWrite(controller, "maxVelocity", base.maxVelocity * moveMul) end
        if base.maxVelocityInAir then tryWrite(controller, "maxVelocityInAir", base.maxVelocityInAir * moveMul) end
        if base.dashImpulseRatio then tryWrite(controller, "dashImpulseRatio", base.dashImpulseRatio * dashMul) end
        if base.accelerationDuration then tryWrite(controller, "accelerationDuration", base.accelerationDuration / math.sqrt(moveMul)) end
        if base.decelerationDuration then tryWrite(controller, "decelerationDuration", base.decelerationDuration / math.sqrt(moveMul)) end
    end

    if controls.eggHasteNoDashCooldown == true and me ~= nil and EggHaste.controllingEgg then
        tryWrite(me, "eggModeDashCD", nil)
    end

    if EggHaste.controllingEgg then
        EggHaste.last = EggHaste.controllerReady and "Egg controller active" or "controlling egg; physics controller not ready"
    elseif EggHaste.fastCarry then
        EggHaste.last = "Fast Carry active"
    elseif not EggHaste.liveSpace then
        EggHaste.last = "Egg Haste context: " .. tostring(EggHaste.context) .. " | " .. tostring(EggHaste.contextDetector)
    else
        EggHaste.last = "Egg Haste dungeon active | " .. tostring(EggHaste.contextDetector)
    end
end

local function eggHasteKind(entity, Utils, Const)
    if entity == nil then return nil end
    if safeRead(entity, "isRobSpaceEgg") == true then return "egg-haste-egg" end
    if safeRead(entity, "isGrabEggTransfer") == true then return "egg-haste-transfer" end
    if safeRead(entity, "isResourceBox") == true then
        local subtype = tonumber(safeRead(entity, "subType"))
        local types = Const and safeRead(Const, "ROB_EGG_LOOT_TYPE")
        local nest = types and (tonumber(safeRead(types, "EGG_NEST")) or 4) or 4
        local boxPuppet = types and (tonumber(safeRead(types, "BOX_PUPPET")) or 6) or 6
        local resource = types and (tonumber(safeRead(types, "RESOURCE_BOX")) or 1) or 1
        local deathBox = types and (tonumber(safeRead(types, "DEATH_BOX")) or 2) or 2
        local playerBag = types and (tonumber(safeRead(types, "PLAYER_BAG")) or 3) or 3
        local breakable = types and (tonumber(safeRead(types, "BREAKABLE")) or 5) or 5
        if subtype == nest or subtype == boxPuppet then return "egg-haste-nest" end
        if subtype == resource then return "egg-haste-resource" end
        if subtype == deathBox or subtype == playerBag then return "egg-haste-bag" end
        if subtype == breakable then return "egg-haste-breakable" end
        return "egg-haste-resource"
    end
    if safeRead(entity, "isCollectItem") == true then return "egg-haste-loot" end
    return nil
end

local function eggHasteKindEnabled(kind, controls)
    if kind == "egg-haste-egg" then return controls.eggHasteEspEggs == true end
    if kind == "egg-haste-nest" then return controls.eggHasteEspNests == true end
    if kind == "egg-haste-resource" then return controls.eggHasteEspResources == true end
    if kind == "egg-haste-loot" then return controls.eggHasteEspLoot == true end
    if kind == "egg-haste-bag" then return controls.eggHasteEspBags == true end
    if kind == "egg-haste-transfer" then return controls.eggHasteEspTransfers == true end
    if kind == "egg-haste-breakable" then return controls.eggHasteEspBreakables == true end
    return false
end

local eggHasteQualityNames = {
    [1] = "White", [2] = "Green", [3] = "Blue",
    [4] = "Purple", [5] = "Gold", [6] = "Rainbow"
}

local function eggHasteQualityForEntity(entity, chestData)
    local templateId = tonumber(safeRead(entity, "templateId"))
    if templateId == nil then return nil end

    local config = nil
    local getConfigData = safeRead(entity, "getConfigData")
    local configOk, configValue = safeCall(getConfigData, entity)
    if configOk and configValue ~= nil then config = configValue end
    if config == nil and chestData ~= nil then config = safeRead(chestData, templateId) end

    local quality = tonumber(safeRead(config, "quality"))
    if quality and quality >= 1 and quality <= 6 and quality == math.floor(quality) then return quality end
    return nil
end

local function eggHasteQualityAllowed(mask, quality)
    mask = math.max(0, math.floor(tonumber(mask) or 127))
    local bitIndex = quality and quality >= 1 and quality <= 6 and quality == math.floor(quality) and (quality - 1) or 6
    return math.floor(mask / (2 ^ bitIndex)) % 2 == 1
end

local function eggHasteFallbackName(kind)
    if kind == "egg-haste-egg" then return "Egg" end
    if kind == "egg-haste-nest" then return "Egg Nest" end
    if kind == "egg-haste-resource" then return "Resource Box" end
    if kind == "egg-haste-loot" then return "Loot" end
    if kind == "egg-haste-bag" then return "Player / Death Bag" end
    if kind == "egg-haste-transfer" then return "Transfer Station" end
    if kind == "egg-haste-breakable" then return "Breakable" end
    return "Egg Haste"
end





local Autofarm = {
    name = "autofarm",
    enabled = false,
    includeChests = true,
    includeAmber = true,
    scanRadius = 500,
    emptyConfirmRadius = 180,
    teleportDelay = 0.90,
    settleDelay = 1.50,
    lastResetRequest = 0,
    lastTickAt = -999,
    lastTeleportAt = -999,
    dataLoaded = false,
    dataError = nil,
    targets = {},
    state = "idle",
    done = {},
    deferredUntil = {},
    currentIndex = 0,
    currentLastSeenAt = -999,
    currentMissingScans = 0,
    currentStartedAt = -999,
    seedIndex = 0,
    seedSettleUntil = -999,
    seedEmptyScans = 0,
    stats = {
        selectedTotal = 0, remaining = 0, collected = 0, absent = 0, deferred = 0,
        liveNearby = 0, currentIndex = 0, currentKind = "none", currentId = "0",
        targetDistance = -1, teleports = 0, interactions = 0, errors = 0,
        ySource = "none", last = "idle"
    }
}

local function autofarmXZDistance(left, right)
    if left == nil or right == nil then return math.huge end
    local dx = (tonumber(left.x) or 0) - (tonumber(right.x) or 0)
    local dz = (tonumber(left.z) or 0) - (tonumber(right.z) or 0)
    return math.sqrt(dx * dx + dz * dz)
end

local function autofarmDataPath()
    local fallback = "Arkhan\\Runtime\\autofarm_targets.lua"
    local CS = rawget(_G, "CS")
    if CS == nil then return fallback end
    local ok, path = pcall(function()
        local System = CS.System
        local IO = System and System.IO
        local Path = IO and IO.Path
        local AppDomain = System and System.AppDomain
        local domain = AppDomain and AppDomain.CurrentDomain
        local baseDirectory = domain and domain.BaseDirectory
        local baseText = baseDirectory ~= nil and tostring(baseDirectory) or ""
        if Path ~= nil and baseText ~= "" then
            return Path.Combine(baseText, "Arkhan", "Runtime", "autofarm_targets.lua")
        end
        local UnityEngine = CS.UnityEngine
        local Application = UnityEngine and UnityEngine.Application
        local dataPath = Application and Application.dataPath
        local dataText = dataPath ~= nil and tostring(dataPath) or ""
        if Path ~= nil and dataText ~= "" then
            local gameRoot = Path.GetDirectoryName(dataText)
            if gameRoot ~= nil and tostring(gameRoot) ~= "" then
                return Path.Combine(tostring(gameRoot), "Arkhan", "Runtime", "autofarm_targets.lua")
            end
        end
        return nil
    end)
    if ok and type(path) == "string" and path ~= "" then return path end
    return fallback
end

local function autofarmExecuteDataset(path)
    local loadfileFn = rawget(_G, "loadfile")
    if type(loadfileFn) == "function" then
        local ok, chunk, err = pcall(loadfileFn, path)
        if ok and type(chunk) == "function" then
            local runOk, result = pcall(chunk)
            if runOk and type(result) == "table" then return result, nil end
            if not runOk then return nil, tostring(result) end
        elseif ok and err ~= nil then
        end
    end

    local CS = rawget(_G, "CS")
    if CS ~= nil then
        local ok, result = pcall(function()
            local File = CS.System and CS.System.IO and CS.System.IO.File
            if File == nil then error("System.IO.File unavailable") end
            local text = File.ReadAllText(path)
            local loader = rawget(_G, "load") or rawget(_G, "loadstring")
            if type(loader) ~= "function" then error("Lua text loader unavailable") end
            local chunk, loadError
            local loadOk, loadedA, loadedB = pcall(loader, tostring(text), "@Arkhan/Runtime/autofarm_targets.lua")
            if loadOk then chunk, loadError = loadedA, loadedB end
            if type(chunk) ~= "function" then
                local retryOk, retryChunk, retryError = pcall(loader, tostring(text))
                if retryOk then chunk, loadError = retryChunk, retryError end
            end
            if type(chunk) ~= "function" then error(tostring(loadError or "dataset load failed")) end
            local runOk, value = pcall(chunk)
            if not runOk then error(tostring(value)) end
            return value
        end)
        if ok and type(result) == "table" then return result, nil end
        if not ok then return nil, tostring(result) end
    end
    return nil, "dataset file loader unavailable"
end

function Autofarm:loadData()
    if self.dataLoaded and type(self.targets) == "table" and #self.targets > 0 then return true end
    local path = autofarmDataPath()
    local data, err = autofarmExecuteDataset(path)
    if type(data) ~= "table" or type(data.targets) ~= "table" or #data.targets == 0 then
        self.dataLoaded = false
        self.dataError = tostring(err or "dataset invalid")
        self.stats.last = "dataset error: " .. self.dataError
        return false
    end
    self.targets = data.targets
    self.dataLoaded = true
    self.dataError = nil
    self.stats.last = string.format("dataset ready: %d anchors", #self.targets)
    return true
end

function Autofarm:isSelected(target)
    if type(target) ~= "table" then return false end
    if target.kind == "amber" then return self.includeAmber == true end
    return self.includeChests == true
end

function Autofarm:resetProgress(reason)
    self.done = {}
    self.deferredUntil = {}
    self.currentIndex = 0
    self.currentMissingScans = 0
    self.currentLastSeenAt = -999
    self.currentStartedAt = -999
    self.seedIndex = 0
    self.seedSettleUntil = -999
    self.seedEmptyScans = 0
    self.state = self.enabled and "scan" or "idle"
    self.stats.collected = 0
    self.stats.absent = 0
    self.stats.deferred = 0
    self.stats.teleports = 0
    self.stats.interactions = 0
    self.stats.errors = 0
    self.stats.last = "progress reset: " .. tostring(reason or "manual")
end

function Autofarm:refreshProgress(now)
    local selected, remaining, collected, absent, deferred = 0, 0, 0, 0, 0
    for index, target in ipairs(self.targets or {}) do
        if self:isSelected(target) then
            selected = selected + 1
            local state = self.done[index]
            if state == "collected" then collected = collected + 1
            elseif state == "absent" then absent = absent + 1
            elseif state == nil then
                local deferUntil = tonumber(self.deferredUntil[index]) or 0
                if deferUntil > now then deferred = deferred + 1 else remaining = remaining + 1 end
            end
        end
    end
    self.stats.selectedTotal = selected
    self.stats.remaining = remaining
    self.stats.collected = collected
    self.stats.absent = absent
    self.stats.deferred = deferred
end

local function autofarmEntityTemplateId(entity)
    return entityTemplateId(entity)
end

function Autofarm:liveKind(entity)
    if autofarmEntityTemplateId(entity) == 11042 then return "amber" end
    return "chest"
end

function Autofarm:nearestDatasetIndex(position, preferredKind, maximumDistance, now, allowAbsent)
    local bestIndex, bestDistance
    maximumDistance = tonumber(maximumDistance) or 55
    for index, target in ipairs(self.targets or {}) do
        local progress = self.done[index]
        local eligibleProgress = progress == nil or (allowAbsent == true and progress == "absent")
        if self:isSelected(target) and eligibleProgress then
            local deferUntil = tonumber(self.deferredUntil[index]) or 0
            if deferUntil <= now and (preferredKind == nil or target.kind == preferredKind) then
                local distance = autofarmXZDistance(position, target)
                if distance <= maximumDistance and (bestDistance == nil or distance < bestDistance) then
                    bestIndex, bestDistance = index, distance
                end
            end
        end
    end
    return bestIndex, bestDistance
end

function Autofarm:scanLive(runtimePg, originPos, now)
    local output = {}
    local entities, entityError = allEntities(runtimePg)
    if type(entities) ~= "table" then
        return output, entityError or "entity-list-not-ready"
    end
    local utils = select(1, loadUtils())
    if type(utils) ~= "table" then return output, "Utils unavailable" end
    local origin = runtimePg and (safeRead(runtimePg, "pawn") or safeRead(runtimePg, "me"))
    local byIndex = {}
    for _, entity in pairs(entities) do
        if entity ~= origin and entity ~= safeRead(runtimePg, "me") then
            local kind = classify(utils, origin, runtimePg, entity)
            if kind == "chest" then
                local pos = positionOf(entity)
                if pos ~= nil then
                    local playerDistance = autofarmXZDistance(originPos, pos)
                    if playerDistance <= self.scanRadius then
                        local liveKind = self:liveKind(entity)
                        if (liveKind == "amber" and self.includeAmber) or (liveKind == "chest" and self.includeChests) then
                            local index, anchorDistance = self:nearestDatasetIndex(pos, liveKind, 60, now, false)
                            if index == nil then
                                index, anchorDistance = self:nearestDatasetIndex(pos, liveKind, 60, now, true)
                                if index ~= nil and self.done[index] == "absent" then
                                    self.done[index] = nil
                                    self.stats.last = string.format("revived %s anchor %s after live ESP confirmation",
                                        tostring(liveKind), tostring(self.targets[index] and self.targets[index].id or index))
                                end
                            end
                            if index ~= nil then
                                local row = {
                                    index = index, entity = entity, pos = pos, distance = playerDistance,
                                    anchorDistance = anchorDistance or 0, kind = liveKind
                                }
                                local prior = byIndex[index]
                                if prior == nil or row.anchorDistance < prior.anchorDistance then byIndex[index] = row end
                            end
                        end
                    end
                end
            end
        end
    end
    for _, row in pairs(byIndex) do output[#output + 1] = row end
    table.sort(output, function(a, b) return (a.distance or math.huge) < (b.distance or math.huge) end)
    return output, nil
end

function Autofarm:findLiveForCurrent(live)
    for _, row in ipairs(live or {}) do
        if row.index == self.currentIndex then return row end
    end
    return nil
end

function Autofarm:chooseSeed(originPos, now)
    local nearest = {}
    for index, target in ipairs(self.targets or {}) do
        if self:isSelected(target) and self.done[index] == nil then
            local deferUntil = tonumber(self.deferredUntil[index]) or 0
            if deferUntil <= now then
                nearest[#nearest + 1] = { index = index, target = target, distance = autofarmXZDistance(originPos, target) }
            end
        end
    end
    if #nearest == 0 then return nil end
    table.sort(nearest, function(a, b) return a.distance < b.distance end)

    local limit = math.min(#nearest, 24)
    local best, bestScore
    for candidateIndex = 1, limit do
        local candidate = nearest[candidateIndex]
        local density = 0
        for index, target in ipairs(self.targets or {}) do
            if self:isSelected(target) and self.done[index] == nil and index ~= candidate.index then
                local deferUntil = tonumber(self.deferredUntil[index]) or 0
                if deferUntil <= now and autofarmXZDistance(candidate.target, target) <= 220 then
                    density = density + 1
                end
            end
        end
        local score = candidate.distance - math.min(density, 12) * 8.0
        if bestScore == nil or score < bestScore then
            best, bestScore = candidate, score
        end
    end
    return best
end

local function autofarmVector3(x, y, z)
    local Vector3Type = rawget(_G, "Vector3")
    if Vector3Type == nil then
        local CS = rawget(_G, "CS")
        Vector3Type = CS and CS.UnityEngine and CS.UnityEngine.Vector3
    end
    if Vector3Type == nil then return nil end
    local newFn = safeRead(Vector3Type, "New")
    if type(newFn) == "function" then
        local ok, value = pcall(newFn, x, y, z)
        if ok and value ~= nil then return value end
    end
    local ok, value = pcall(Vector3Type, x, y, z)
    return ok and value or nil
end

function Autofarm:resolveSeedY(runtimePg, target, originPos)
    local entities = select(1, allEntities(runtimePg))
    local nearestY, nearestDistance
    if type(entities) == "table" then
        for _, entity in pairs(entities) do
            local pos = positionOf(entity)
            if pos ~= nil then
                local distance = autofarmXZDistance(target, pos)
                if distance <= 90 and (nearestDistance == nil or distance < nearestDistance) then
                    nearestY, nearestDistance = pos.y, distance
                end
            end
        end
    end
    if nearestY ~= nil then return nearestY + 1.0, "nearby-entity" end

    local CS = rawget(_G, "CS")
    local UnityEngine = CS and CS.UnityEngine
    local Physics = UnityEngine and UnityEngine.Physics
    if Physics ~= nil then
        local originY = math.max((tonumber(originPos and originPos.y) or 0) + 800, 1200)
        local rayOrigin = autofarmVector3(target.x, originY, target.z)
        local rayDown = autofarmVector3(0, -1, 0)
        local raycastAll = safeRead(Physics, "RaycastAll")
        if rayOrigin ~= nil and rayDown ~= nil and type(raycastAll) == "function" then
            local ok, hits = pcall(raycastAll, rayOrigin, rayDown, 4000.0)
            if ok and hits ~= nil then
                local length = tonumber(safeRead(hits, "Length")) or tonumber(safeRead(hits, "length")) or 0
                local bestY
                for index = 0, math.max(0, length - 1) do
                    local hit = safeRead(hits, index)
                    local point = hit and safeRead(hit, "point")
                    local y = point and tonumber(safeRead(point, "y"))
                    if y ~= nil and (bestY == nil or y > bestY) then bestY = y end
                end
                if bestY ~= nil then return bestY + 1.0, "terrain-raycast" end
            end
        end
    end

    return (tonumber(originPos and originPos.y) or 0) + 3.0, "current-y-fallback"
end

function Autofarm:moveTo(runtimePg, position, stopDistance, ySource, now)
    if now - self.lastTeleportAt < self.teleportDelay then return false, "teleport cooldown" end
    local ok, reason = arkhanTeleportPawn(runtimePg, position, stopDistance)
    if ok then
        self.lastTeleportAt = now
        self.stats.teleports = (tonumber(self.stats.teleports) or 0) + 1
        self.stats.ySource = tostring(ySource or "runtime")
        return true, nil
    end
    self.stats.errors = (tonumber(self.stats.errors) or 0) + 1
    self.stats.last = "teleport failed: " .. tostring(reason or "unknown")
    return false, reason
end

function Autofarm:beginLiveTarget(row, runtimePg, now)
    local target = self.targets[row.index]
    if target == nil then return false end
    self.currentIndex = row.index
    self.currentLastSeenAt = now
    self.currentMissingScans = 0
    self.currentStartedAt = now
    self.state = "target-live"
    self.stats.currentIndex = row.index
    self.stats.currentKind = tostring(target.kind or row.kind or "chest")
    self.stats.currentId = tostring(target.id or "0")
    self.stats.targetDistance = tonumber(row.distance) or -1
    local ok = self:moveTo(runtimePg, row.pos, 2.0, "live-entity", now)
    if ok then
        self.stats.last = string.format("live target %s %s -> exact runtime Y", self.stats.currentKind, self.stats.currentId)
    end
    return true
end

function Autofarm:markCurrentCollected(now)
    if self.currentIndex > 0 and self.done[self.currentIndex] == nil then
        self.done[self.currentIndex] = "collected"
    end
    self.stats.last = string.format("target %s disappeared from ESP classifier; collected", tostring(self.stats.currentId or "?"))
    self.currentIndex = 0
    self.currentMissingScans = 0
    self.currentStartedAt = -999
    self.stats.currentIndex = 0
    self.stats.currentKind = "none"
    self.stats.currentId = "0"
    self.stats.targetDistance = -1
    self.state = "scan"
    self:refreshProgress(now)
end

function Autofarm:deferCurrent(now, reason)
    if self.currentIndex > 0 then self.deferredUntil[self.currentIndex] = now + 60.0 end
    self.stats.last = "target deferred 60s: " .. tostring(reason or "stalled")
    self.currentIndex = 0
    self.currentMissingScans = 0
    self.currentStartedAt = -999
    self.stats.currentIndex = 0
    self.stats.currentKind = "none"
    self.stats.currentId = "0"
    self.stats.targetDistance = -1
    self.state = "scan"
    self:refreshProgress(now)
end

function Autofarm:pruneConfirmedEmpty(originPos, now)
    local count = 0
    for index, target in ipairs(self.targets or {}) do
        if self:isSelected(target) and self.done[index] == nil then
            local deferUntil = tonumber(self.deferredUntil[index]) or 0
            if deferUntil <= now and autofarmXZDistance(originPos, target) <= self.emptyConfirmRadius then
                self.done[index] = "absent"
                count = count + 1
            end
        end
    end
    self.stats.last = string.format("stable empty scan: %d JSON anchors confirmed absent inside %dm", count, self.emptyConfirmRadius)
    self:refreshProgress(now)
    return count
end

function Autofarm:interactCurrent(now)
    local runtimeFeatures = Arkhan.modules.runtimeFeatures
    if type(runtimeFeatures) ~= "table" or type(runtimeFeatures.tickDirectEntityAction) ~= "function" then
        self.stats.errors = (tonumber(self.stats.errors) or 0) + 1
        self.stats.last = "RuntimeFeatures chest interaction unavailable"
        return false
    end
    local ok, acted = pcall(runtimeFeatures.tickDirectEntityAction, runtimeFeatures, now, "autoChest", "isChest", 24, 0.75, "autofarm chest", true)
    if not ok then
        self.stats.errors = (tonumber(self.stats.errors) or 0) + 1
        self.stats.last = "interaction error: " .. tostring(acted)
        return false
    end
    if acted == true then self.stats.interactions = (tonumber(self.stats.interactions) or 0) + 1 end
    return acted == true
end

function Autofarm:tick()
    local now = featureNow()
    if now - self.lastTickAt < 0.12 then return end
    self.lastTickAt = now

    if not self.enabled then
        self.state = "idle"
        self.stats.liveNearby = 0
        self:refreshProgress(now)
        return
    end
    if not self:loadData() then
        self.state = "dataset-error"
        self.stats.errors = math.max(1, tonumber(self.stats.errors) or 0)
        return
    end
    if not self.includeChests and not self.includeAmber then
        self.state = "paused-no-types"
        self.stats.last = "select Chests and/or Lumin Amber"
        self:refreshProgress(now)
        return
    end

    local runtimePg = rawget(_G, "pg")
    local pawn = runtimePg and (safeRead(runtimePg, "pawn") or safeRead(runtimePg, "me"))
    local originPos = positionOf(pawn)
    if runtimePg == nil or pawn == nil or originPos == nil then
        self.state = "waiting-runtime"
        self.stats.last = "waiting for controlled pawn"
        return
    end

    self:refreshProgress(now)
    if self.stats.selectedTotal <= 0 then
        self.state = "paused-no-types"
        return
    end

    local live, scanError = self:scanLive(runtimePg, originPos, now)
    self.stats.liveNearby = #live
    if scanError ~= nil and #live == 0 then
        self.stats.last = "scan: " .. tostring(scanError)
    end

    if self.currentIndex > 0 then
        local current = self:findLiveForCurrent(live)
        if current ~= nil then
            self.currentLastSeenAt = now
            self.currentMissingScans = 0
            self.stats.currentIndex = self.currentIndex
            self.stats.targetDistance = tonumber(current.distance) or -1
            local target = self.targets[self.currentIndex]
            self.stats.currentKind = tostring(target and target.kind or current.kind or "chest")
            self.stats.currentId = tostring(target and target.id or "0")

            if current.distance > 6.0 and now - self.lastTeleportAt >= self.teleportDelay then
                self:moveTo(runtimePg, current.pos, 2.0, "live-entity", now)
            end
            if current.distance <= 12.0 or now - self.currentStartedAt >= 0.25 then
                self:interactCurrent(now)
            end
            if now - self.currentStartedAt > 20.0 then
                self:deferCurrent(now, "still classified after 20s")
            else
                self.state = "waiting-disappear"
            end
            return
        end

        self.currentMissingScans = self.currentMissingScans + 1
        if self.currentMissingScans >= 5 and now - self.currentLastSeenAt >= 0.80 then
            self:markCurrentCollected(now)
        else
            self.state = "verify-disappear"
        end
        return
    end

    if self.state == "seed-settle" then
        if #live > 0 then
            self.seedEmptyScans = 0
            self:beginLiveTarget(live[1], runtimePg, now)
            return
        end
        if now < self.seedSettleUntil then
            self.stats.last = string.format("waiting for world streaming near anchor %d", self.seedIndex)
            return
        end
        self.seedEmptyScans = self.seedEmptyScans + 1
        if self.seedEmptyScans >= 3 then
            self:pruneConfirmedEmpty(originPos, now)
            self.seedIndex = 0
            self.seedEmptyScans = 0
            self.state = "scan"
        end
        return
    end

    if #live > 0 then
        self:beginLiveTarget(live[1], runtimePg, now)
        return
    end

    local seed = self:chooseSeed(originPos, now)
    if seed == nil then
        if self.stats.remaining <= 0 and self.stats.deferred <= 0 then
            self.state = "complete"
            self.stats.last = string.format("route complete: %d collected, %d absent", self.stats.collected, self.stats.absent)
        else
            self.state = "waiting-deferred"
            self.stats.last = "waiting for deferred targets to become eligible again"
        end
        return
    end

    local y, ySource = self:resolveSeedY(runtimePg, seed.target, originPos)
    local position = { x = tonumber(seed.target.x) or 0, y = y, z = tonumber(seed.target.z) or 0 }
    local ok, reason = self:moveTo(runtimePg, position, 0.0, ySource, now)
    if ok then
        self.seedIndex = seed.index
        self.seedSettleUntil = now + self.settleDelay
        self.seedEmptyScans = 0
        self.state = "seed-settle"
        self.stats.currentIndex = seed.index
        self.stats.currentKind = tostring(seed.target.kind or "chest")
        self.stats.currentId = tostring(seed.target.id or "0")
        self.stats.targetDistance = tonumber(seed.distance) or -1
        self.stats.last = string.format("seed hop -> %s %s (%.0fm); waiting %.1fs for streaming",
            self.stats.currentKind, self.stats.currentId, tonumber(seed.distance) or 0, self.settleDelay)
    else
        self.deferredUntil[seed.index] = now + 10.0
        self.stats.last = "seed teleport failed; deferred 10s: " .. tostring(reason or "unknown")
        self.state = "scan"
    end
end

function Autofarm:frame(enabled, includeChests, includeAmber, scanRadius, emptyConfirmRadius, teleportDelayMs, settleDelayMs, resetRequest)
    self.enabled = enabled == true
    self.includeChests = includeChests == true
    self.includeAmber = includeAmber == true
    self.scanRadius = math.max(100, math.min(800, math.floor(tonumber(scanRadius) or 500)))
    self.emptyConfirmRadius = math.max(25, math.min(self.scanRadius, math.floor(tonumber(emptyConfirmRadius) or 180)))
    self.teleportDelay = math.max(0.25, math.min(5.0, (tonumber(teleportDelayMs) or 900) / 1000.0))
    self.settleDelay = math.max(0.50, math.min(8.0, (tonumber(settleDelayMs) or 1500) / 1000.0))

    local request = math.max(0, math.floor(tonumber(resetRequest) or 0))
    if request ~= self.lastResetRequest then
        self.lastResetRequest = request
        self:resetProgress("native request")
    end

    if not self.dataLoaded then pcall(self.loadData, self) end
    local ok, err = pcall(self.tick, self)
    if not ok then
        self.stats.errors = (tonumber(self.stats.errors) or 0) + 1
        self.stats.last = "autofarm tick error: " .. tostring(err)
        self.state = "error"
    end
end

function Autofarm:shutdown()
    self.enabled = false
    self.state = "idle"
    self.currentIndex = 0
end

Arkhan.registerModule("autofarm", Autofarm)

function Arkhan.autofarmFrame(enabled, includeChests, includeAmber, scanRadius, emptyConfirmRadius, teleportDelayMs, settleDelayMs, resetRequest)
    local module = Arkhan.modules.autofarm
    if type(module) ~= "table" or type(module.frame) ~= "function" then return false end
    local ok = pcall(module.frame, module, enabled, includeChests, includeAmber, scanRadius, emptyConfirmRadius,
                     teleportDelayMs, settleDelayMs, resetRequest)
    return ok == true
end


local MobVacuum = {
    name = "mob-vacuum",
    enabled = false,
    stun = false,
    radius = 100,
    maxMobs = 12,
    holdDistance = 4.0,
    repositionTolerance = 1.5,
    driftTolerance = 0.35,
    stunHeight = 9.0,
    stunInterval = 0.50,
    stunEntityCooldown = 8.0,
    anchor = nil,
    assignments = {},
    stunLastAt = {},
    wasEnabled = false,
    wasStun = false,
    nextStunAt = -999,
    lastTickAt = -999,
    stats = {
        candidates = 0, active = 0, moved = 0, errors = 0,
        last = "idle"
    }
}

local function mobVacuumVector3(x, y, z)
    local Vector3Type = rawget(_G, "Vector3")
    if Vector3Type == nil then
        local CS = rawget(_G, "CS")
        Vector3Type = CS and CS.UnityEngine and CS.UnityEngine.Vector3
    end
    if Vector3Type == nil then return nil end
    local newFn = safeRead(Vector3Type, "New")
    if type(newFn) == "function" then
        local ok, value = pcall(newFn, x, y, z)
        if ok and value ~= nil then return value end
    end
    local ok, value = pcall(Vector3Type, x, y, z)
    return ok and value or nil
end

local function mobVacuumPlayerBasis(pawn)
    local forwardX, forwardZ = 0, 1
    local getRotation = safeRead(pawn, "getRotation")
    local okRot, rotation = safeCall(getRotation, pawn)
    if okRot and rotation ~= nil then
        local mul = safeRead(rotation, "MulVec3")
        local localForward = mobVacuumVector3(0, 0, 1)
        if type(mul) == "function" and localForward ~= nil then
            local okForward, worldForward = safeCall(mul, rotation, localForward)
            if okForward and worldForward ~= nil then
                local x = tonumber(safeRead(worldForward, "x")) or tonumber(safeRead(worldForward, 1))
                local z = tonumber(safeRead(worldForward, "z")) or tonumber(safeRead(worldForward, 3))
                local mag = x and z and math.sqrt(x * x + z * z) or 0
                if mag > 0.001 then forwardX, forwardZ = x / mag, z / mag end
            end
        end
    end
    return forwardX, forwardZ, -forwardZ, forwardX
end

local function mobVacuumPredicate(utils, name, ...)
    local value = predicate(utils, name, ...)
    return value == true
end

local function mobVacuumEligible(utils, pawn, runtimePg, entity)
    if entity == nil or entity == pawn or entity == safeRead(runtimePg, "me") then return false end
    if safeRead(entity, "destroyed") == true or callBool(entity, "isDead") then return false end

    if safeRead(entity, "isNpcEntity") == true or safeRead(entity, "isBot") == true then return false end
    if mobVacuumPredicate(utils, "isNpc", entity) or
       mobVacuumPredicate(utils, "isPetNpc", entity) or
       mobVacuumPredicate(utils, "isPeopleNpc", entity) then
        return false
    end
    if mobVacuumPredicate(utils, "isPlayerPet", entity) or
       mobVacuumPredicate(utils, "isPet", entity) or
       mobVacuumPredicate(utils, "isBotPet", entity) or
       mobVacuumPredicate(utils, "isHomePet", entity) or
       mobVacuumPredicate(utils, "isCarryPet", entity) or
       mobVacuumPredicate(utils, "isCreatePlenty", entity) then
        return false
    end

    if mobVacuumPredicate(utils, "isBoss", entity) or
       mobVacuumPredicate(utils, "isElite", entity) or
       mobVacuumPredicate(utils, "isSemanticallyBoss", entity) then
        return false
    end

    if not mobVacuumPredicate(utils, "isPuppet", entity) then return false end
    if not mobVacuumPredicate(utils, "isWildPuppet", entity) then return false end
    if pawn ~= nil and not mobVacuumPredicate(utils, "isEnemy", pawn, entity) then return false end

    local getConfigData = safeRead(entity, "getConfigData")
    local okConfig, config = safeCall(getConfigData, entity)
    if okConfig and config ~= nil then
        local npcType = tonumber(safeRead(config, "npcType"))
        local Const = rawget(_G, "Const")
        local npcTypes = Const and safeRead(Const, "NPC_TYPE")
        local npcNone = npcTypes and tonumber(safeRead(npcTypes, "None"))
        if npcType ~= nil and npcNone ~= nil and npcType ~= npcNone then return false end
    end

    return true
end

local function mobVacuumEntityKey(entity, pos)
    if entity ~= nil then
        local actorId = safeRead(entity, "actorId")
        if actorId ~= nil and tostring(actorId) ~= "0" and tostring(actorId) ~= "" then
            return "actor:" .. tostring(actorId)
        end
        local entityId = safeRead(entity, "id") or safeRead(entity, "entityId")
        if entityId ~= nil and tostring(entityId) ~= "0" and tostring(entityId) ~= "" then
            return "entity:" .. tostring(entityId)
        end
    end
    if pos ~= nil then
        return string.format("pos:%.1f:%.1f:%.1f", tonumber(pos.x) or 0, tonumber(pos.y) or 0, tonumber(pos.z) or 0)
    end
    return tostring(entity)
end

local function mobVacuumDistance3(a, b)
    if a == nil or b == nil then return math.huge end
    local dx = (tonumber(a.x) or 0) - (tonumber(b.x) or 0)
    local dy = (tonumber(a.y) or 0) - (tonumber(b.y) or 0)
    local dz = (tonumber(a.z) or 0) - (tonumber(b.z) or 0)
    return math.sqrt(dx * dx + dy * dy + dz * dz)
end

local function mobVacuumTargetPoint(anchor, slot, maxSlots, distance)
    local halfSpan = math.rad(distance < 2.0 and 17.5 or 35.0)
    local angle = 0
    if slot > 1 then
        local rank = math.floor(slot / 2)
        local side = (slot % 2 == 0) and -1 or 1
        local maxRank = math.max(1, math.ceil((math.max(1, maxSlots) - 1) / 2))
        angle = side * (rank / maxRank) * halfSpan
    end
    local ca, sa = math.cos(angle), math.sin(angle)
    local dirX = anchor.forwardX * ca + anchor.rightX * sa
    local dirZ = anchor.forwardZ * ca + anchor.rightZ * sa
    return {
        x = anchor.x + dirX * distance,
        y = anchor.y + 0.10,
        z = anchor.z + dirZ * distance,
        dirX = dirX,
        dirZ = dirZ
    }
end

local function mobVacuumStunPoint(anchor, slot, maxSlots, distance, height)
    local target = mobVacuumTargetPoint(anchor, slot, maxSlots, distance)
    target.y = anchor.y + (tonumber(height) or 9.0)
    return target
end

function MobVacuum:captureAnchor(pawn, originPos, reason)
    local forwardX, forwardZ, rightX, rightZ = mobVacuumPlayerBasis(pawn)
    self.anchor = {
        x = tonumber(originPos.x) or 0,
        y = tonumber(originPos.y) or 0,
        z = tonumber(originPos.z) or 0,
        forwardX = forwardX, forwardZ = forwardZ,
        rightX = rightX, rightZ = rightZ,
        holdDistance = self.holdDistance,
        maxMobs = self.maxMobs,
        reason = tostring(reason or "anchor")
    }
end

function MobVacuum:playerDistanceFromAnchor(originPos)
    return mobVacuumDistance3(originPos, self.anchor)
end

function MobVacuum:assignSlots(candidates, active)
    local activeKeys = {}
    for index = 1, active do activeKeys[candidates[index].key] = true end
    for key, _ in pairs(self.assignments) do
        if not activeKeys[key] then self.assignments[key] = nil end
    end

    local used = {}
    for key, slot in pairs(self.assignments) do
        if activeKeys[key] and slot >= 1 and slot <= self.maxMobs then used[slot] = true
        else self.assignments[key] = nil end
    end
    for index = 1, active do
        local row = candidates[index]
        if self.assignments[row.key] == nil then
            for slot = 1, self.maxMobs do
                if not used[slot] then
                    self.assignments[row.key] = slot
                    used[slot] = true
                    break
                end
            end
        end
    end
end

function MobVacuum:moveEntity(entity, target, forceReposition)
    local current = positionOf(entity)
    local repositioned = forceReposition == true or current == nil or mobVacuumDistance3(current, target) > self.driftTolerance
    if repositioned then
        local vector = mobVacuumVector3(target.x, target.y, target.z)
        if vector == nil then return false, "Vector3 unavailable", false end

        local setPosition = safeRead(entity, "setPosition")
        local okPos, moved = safeCall(setPosition, entity, vector)
        if not okPos then
            local setPositionEx = safeRead(entity, "setPositionEx")
            okPos, moved = safeCall(setPositionEx, entity, target.x, target.y, target.z)
        end
        if not okPos or moved == false then return false, "setPosition failed", false end
    end

    local direction = mobVacuumVector3(target.dirX, 0, target.dirZ)
    local eModel = safeRead(entity, "eModel")
    local setDirection = eModel and safeRead(eModel, "SetDirection")
    local Const = rawget(_G, "Const")
    local componentMotion = Const and tonumber(safeRead(Const, "COMPONENT_MOTION"))
    local faced = false
    if direction ~= nil and type(setDirection) == "function" and componentMotion ~= nil then
        local okDir = safeCall(setDirection, eModel, componentMotion, direction, true)
        faced = okDir == true
    end
    if not faced then
        local Quaternion = rawget(_G, "Quaternion")
        local lookRotation = Quaternion and safeRead(Quaternion, "LookRotation")
        local up = rawget(_G, "Vector3") and safeRead(rawget(_G, "Vector3"), "up")
        if direction ~= nil and type(lookRotation) == "function" and up ~= nil then
            local okQ, rotation = pcall(lookRotation, direction, up)
            if okQ and rotation ~= nil then
                local setRotation = safeRead(entity, "setRotation")
                safeCall(setRotation, entity, rotation, true)
            end
        end
    end
    return true, nil, repositioned
end

function MobVacuum:tick()
    local now = featureNow()
    local tickInterval = self.stun and self.stunInterval or 0.10
    if now - self.lastTickAt < tickInterval then return end
    self.lastTickAt = now

    self.stats.candidates = 0
    self.stats.active = 0
    self.stats.moved = 0
    local modeActive = self.enabled or self.stun
    if not modeActive then
        if self.wasEnabled or self.wasStun then
            self.anchor = nil
            self.assignments = {}
        end
        self.wasEnabled = false
        self.wasStun = false
        self.nextStunAt = -999
        self.stunLastAt = {}
        self.stats.last = "disabled"
        return
    end

    local runtimePg = rawget(_G, "pg")
    local pawn = runtimePg and (safeRead(runtimePg, "pawn") or safeRead(runtimePg, "me"))
    local originPos = positionOf(pawn)
    if runtimePg == nil or pawn == nil or originPos == nil then
        self.stats.last = "waiting for controlled pawn"
        return
    end

    local modeChanged = self.wasEnabled ~= self.enabled or self.wasStun ~= self.stun
    if not self.wasEnabled and not self.wasStun then
        self.anchor = nil
        self.assignments = {}
    end
    if modeChanged then
        self.nextStunAt = now
        self.stunLastAt = {}
        self.anchor = nil
        self.assignments = {}
    end
    self.wasEnabled = self.enabled
    self.wasStun = self.stun

    local anchorReason = nil
    local playerDelta = self:playerDistanceFromAnchor(originPos)
    if self.anchor == nil then
        anchorReason = "enabled"
    elseif playerDelta > self.repositionTolerance then
        anchorReason = string.format("player moved %.2fm", playerDelta)
    elseif math.abs((tonumber(self.anchor.holdDistance) or 0) - self.holdDistance) > 0.001 or
           tonumber(self.anchor.maxMobs) ~= self.maxMobs then
        anchorReason = "layout changed"
    end
    if anchorReason ~= nil then
        self:captureAnchor(pawn, originPos, anchorReason)
        playerDelta = 0
    end

    local entities, entityError = allEntities(runtimePg)
    local utils = select(1, loadUtils())
    if type(entities) ~= "table" or type(utils) ~= "table" then
        self.stats.errors = (tonumber(self.stats.errors) or 0) + 1
        self.stats.last = tostring(entityError or "Utils unavailable")
        return
    end

    local candidates = {}
    for _, entity in pairs(entities) do
        if mobVacuumEligible(utils, pawn, runtimePg, entity) then
            local pos = positionOf(entity)
            if pos ~= nil then
                local distance = distanceBetween(originPos, pos)
                if distance <= self.radius then
                    candidates[#candidates + 1] = {
                        entity = entity, distance = distance,
                        key = mobVacuumEntityKey(entity, pos)
                    }
                end
            end
        end
    end
    table.sort(candidates, function(a, b) return a.distance < b.distance end)
    self.stats.candidates = #candidates

    local active = math.min(#candidates, self.maxMobs)
    self:assignSlots(candidates, active)
    local moved, errors = 0, 0
    if self.stun then
        if now >= self.nextStunAt then
            self.nextStunAt = now + self.stunInterval
            for index = 1, active do
                local row = candidates[index]
                local lastLaunchAt = tonumber(self.stunLastAt[row.key]) or -999
                if now - lastLaunchAt >= self.stunEntityCooldown then
                    local slot = self.assignments[row.key] or index
                    local target = mobVacuumStunPoint(self.anchor, slot, self.maxMobs, self.holdDistance, self.stunHeight)
                    local ok, _, repositioned = self:moveEntity(row.entity, target, true)
                    if ok then
                        self.stunLastAt[row.key] = now
                        if repositioned then moved = moved + 1 end
                    else
                        errors = errors + 1
                    end
                end
            end
        end
        self.stats.active = active
        self.stats.moved = moved
        if errors > 0 then self.stats.errors = (tonumber(self.stats.errors) or 0) + errors end
        self.stats.last = string.format(
            "stun mode | launched %d/%d regular wild mobs | player %.2f/%.2fm",
            moved, active, tonumber(playerDelta) or 0, self.repositionTolerance)
    else
        for index = 1, active do
            local row = candidates[index]
            local slot = self.assignments[row.key] or index
            local target = mobVacuumTargetPoint(self.anchor, slot, self.maxMobs, self.holdDistance)
            local ok, _, repositioned = self:moveEntity(row.entity, target)
            if ok then
                if repositioned then moved = moved + 1 end
            else
                errors = errors + 1
            end
        end
        self.stats.active = active
        self.stats.moved = moved
        if errors > 0 then self.stats.errors = (tonumber(self.stats.errors) or 0) + errors end
        self.stats.last = string.format(
            "world anchor locked | holding %d/%d regular wild mobs | corrected %d | player %.2f/%.2fm",
            active, self.maxMobs, moved, tonumber(playerDelta) or 0, self.repositionTolerance)
    end
end

function MobVacuum:setStun(stunEnabled)
    self.stun = stunEnabled == true
    if self.stun then self.enabled = false end
end

function MobVacuum:frame(enabled, radius, maxMobs, holdDistance, repositionTolerance)
    self.enabled = enabled == true and not self.stun
    self.radius = math.max(1, math.min(500, math.floor(tonumber(radius) or 100)))
    self.maxMobs = math.max(1, math.min(32, math.floor(tonumber(maxMobs) or 12)))
    self.holdDistance = math.max(0.5, math.min(10.0, tonumber(holdDistance) or 4.0))
    self.repositionTolerance = math.max(0.25, math.min(10.0, tonumber(repositionTolerance) or 1.5))
    local ok, err = pcall(self.tick, self)
    if not ok then
        self.stats.errors = (tonumber(self.stats.errors) or 0) + 1
        self.stats.last = "vacuum tick error: " .. tostring(err)
    end
end

function MobVacuum:shutdown()
    self.enabled = false
    self.stun = false
    self.wasEnabled = false
    self.wasStun = false
    self.nextStunAt = -999
    self.stunLastAt = {}
    self.anchor = nil
    self.assignments = {}
    self.stats.active = 0
    self.stats.last = "shutdown"
end

Arkhan.registerModule("mobVacuum", MobVacuum)

function Arkhan.mobVacuumModeFrame(stunEnabled)
    local module = Arkhan.modules.mobVacuum
    if type(module) ~= "table" or type(module.setStun) ~= "function" then return false end
    local ok = pcall(module.setStun, module, stunEnabled)
    return ok == true
end

function Arkhan.mobVacuumFrame(enabled, radius, maxMobs, holdDistance, repositionTolerance)
    local module = Arkhan.modules.mobVacuum
    if type(module) ~= "table" or type(module.frame) ~= "function" then return false end
    local ok = pcall(module.frame, module, enabled, radius, maxMobs, holdDistance, repositionTolerance)
    return ok == true
end

do
local Autoquest = Arkhan.autoquest or {
    name = "autoquest",
    enabled = false,
    selectedQuestId = 0,
    resolvedQuestId = 0,
    lastTeleportRequest = 0,
    lastTargetKey = nil,
    lastTeleportedKey = nil,
    lastInteractionKey = nil,
    nextActionAt = 0,
    activeQuests = {},
    selectedTitle = "",
    objective = "",
    state = "waiting",
    manualFeedback = "",
    targetDistance = -1,
    targetPosition = nil,
    targetKey = nil,
    targetSceneId = nil,
    petForms = {},
    habitats = {},
    petFormsOwned = 0,
    petFormsRequired = 0,
    habitatCacheKey = nil,
    habitatIndex = {},
    habitatRouteCacheAt = -1,
    habitatRouteCacheKey = nil,
    habitatRouteCache = {},
    pendingHabitatTeleport = nil,
    teleports = 0
}
Arkhan.autoquest = Autoquest

local function autoquestDataEntries(value)
    local valueType = type(value)
    if valueType == "table" then
        local current, source = unwrapCurrentBddValue(value)
        if current ~= value then value, valueType = current, type(current) end
    end
    local iterator, iteratorState, key
    if valueType == "userdata" then
        iterator, iteratorState, key = bddUserdataIterator(value)
        if iterator == nil then iterator = luaTableIterator(value) end
    end
    if iterator == nil then
        local ok, tableIterator, tableState, firstKey = pcall(pairs, value)
        if ok and type(tableIterator) == "function" then
            iterator, iteratorState, key = tableIterator, tableState, firstKey
        end
    end
    if type(iterator) ~= "function" then return nil, "table iterator unavailable" end
    local entries = {}
    while true do
        if type(key) == "string" then ensureBddStringKey(key) end
        local ok, nextKey, item = pcall(iterator, iteratorState, key)
        if not ok then return nil, tostring(nextKey or "table iteration failed") end
        if nextKey == nil then break end
        key = nextKey
        entries[#entries + 1] = { key = nextKey, value = item }
    end
    return entries
end

local AutoquestRuntime = (function()
local function autoquestQuestConfig(questUtils, questId)
    local getter = safeRead(questUtils, "getQuestConfig")
    if type(getter) ~= "function" then return nil end
    local ok, config = pcall(getter, questId)
    return ok and config or nil
end

local function autoquestQuestData(questUtils, questId, acceptedById)
    if acceptedById and acceptedById[questId] then return acceptedById[questId] end
    local getter = safeRead(questUtils, "getQuestData")
    if type(getter) ~= "function" then return nil end
    local ok, data = pcall(getter, questId)
    return ok and data or nil
end

local function autoquestRootQuestId(questUtils, questId)
    local getter = safeRead(questUtils, "getRootQuestId")
    if type(getter) == "function" then
        local ok, rootId = pcall(getter, questId)
        if ok and tonumber(rootId) and tonumber(rootId) > 0 then return tonumber(rootId) end
    end
    return tonumber(questId) or 0
end

local function autoquestTrackedQuestId(questUtils, player)
    local getPageTraceQuestId = safeRead(questUtils, "getPageTraceQuestId")
    if type(getPageTraceQuestId) == "function" then
        local ok, value = pcall(getPageTraceQuestId)
        if ok and tonumber(value) ~= nil then
            local trackedId = tonumber(value) or 0
            if trackedId > 0 then return trackedId end

            local temporaryId = tonumber(safeRead(player, "curTraceTempQuest"))
            if temporaryId and temporaryId > 0 then return temporaryId end
            return 0
        end
    end

    local getTracingQuest = safeRead(questUtils, "getTracingQuest")
    if type(getTracingQuest) == "function" then
        local ok, questData = pcall(getTracingQuest)
        local questId = ok and tonumber(safeRead(questData, "configId")) or nil
        if questId and questId > 0 then return questId end
    end
    local temporaryId = tonumber(safeRead(player, "curTraceTempQuest"))
    if temporaryId and temporaryId > 0 then return temporaryId end
    return tonumber(safeRead(player, "curTraceQuest")) or 0
end

local function autoquestQuestTitle(runtimePg, questUtils, questId)
    questId = tonumber(questId) or 0
    if questId <= 0 then return "" end

    local function resolveTitle(value)
        if value == nil then return "" end
        local localized = localizedText(runtimePg, value)
        if localized ~= "" then return localized end
        local plain = scalar(value, 120)
        if plain ~= nil and plain ~= "" and plain ~= "0" then return plain end
        return ""
    end

    local getName = safeRead(questUtils, "getQuestName")
    if type(getName) == "function" then
        local ok, titleKey = pcall(getName, questId)
        if ok then
            local title = resolveTitle(titleKey)
            if title ~= "" then return title end
        end
    end

    local config = autoquestQuestConfig(questUtils, questId)
    for _, field in ipairs({ "title", "questName", "name" }) do
        local title = resolveTitle(safeRead(config, field))
        if title ~= "" then return title end
    end

    local getCurtainName = safeRead(questUtils, "getCurtainQuestName")
    if type(getCurtainName) == "function" then
        local ok, value = pcall(getCurtainName, questId)
        if ok then
            local title = resolveTitle(value)
            if title ~= "" then return title end
        end
    end
    return "Quest " .. tostring(questId)
end

local function autoquestIsManualCommit(questUtils, questId)
    local check = safeRead(questUtils, "isQuestManualCommit")
    if type(check) ~= "function" then return false end
    local ok, result = pcall(check, questId)
    return ok and result == true
end

local function autoquestHudObjectiveSet(questUtils, rootQuestId)
    local getter = safeRead(questUtils, "getReceivedQuestObjectives")
    if type(getter) ~= "function" then return nil, false end

    local ok, objectives = pcall(getter, rootQuestId, false)
    if not ok or objectives == nil then return nil, false end
    local entries = autoquestDataEntries(objectives)
    if entries == nil then return nil, false end

    local allowed = {}
    for _, entry in ipairs(entries) do
        local objective = entry.value
        local candidateQuestId = tonumber(safeRead(objective, "questId"))
        local objectiveId = tonumber(safeRead(objective, "objId"))
        local objectiveConfig = safeRead(objective, "objConfig")
        local pathId = tonumber(safeRead(objectiveConfig, "pathfindingID"))
        local showPath = safeRead(objectiveConfig, "showPath")
        local isFinished = safeRead(objective, "isFined") == true
        if candidateQuestId and candidateQuestId > 0 and objectiveId and objectiveId > 0 and
           not isFinished and pathId and pathId > 0 and
           (showPath == true or tonumber(showPath) == 1) then
            allowed[tostring(candidateQuestId) .. ":" .. tostring(objectiveId)] = objective
        end
    end
    return allowed, true
end

local autoquestDescendantQuestCache = {}

local function autoquestCollectDescendantQuestIds(questUtils, rootQuestId)
    rootQuestId = tonumber(rootQuestId)
    if rootQuestId == nil then return {} end
    local cached = autoquestDescendantQuestCache[rootQuestId]
    if cached ~= nil then return cached end

    local result, seen, pending = {}, {}, { tonumber(rootQuestId) }
    local cursor = 1
    local complete = true
    while cursor <= #pending do
        local questId = tonumber(pending[cursor])
        cursor = cursor + 1
        if questId and not seen[questId] then
            seen[questId] = true
            result[#result + 1] = questId
            local config = autoquestQuestConfig(questUtils, questId)
            if config == nil then complete = false end
            local subQuests = safeRead(config, "subQuests")
            local children = safeRead(subQuests, "all")
            local entries = children ~= nil and autoquestDataEntries(children) or nil
            if children ~= nil and entries == nil then complete = false end
            for _, entry in ipairs(entries or {}) do
                local childId = tonumber(entry.value)
                if childId and not seen[childId] then pending[#pending + 1] = childId end
            end
        end
    end
    if complete then autoquestDescendantQuestCache[rootQuestId] = result end
    return result
end

local function autoquestPlayerHasTemplateId(player, templateId)
    local petStats = safeRead(player, "petStatsInfo")
    local hasTemplate = safeRead(petStats, "hasTemplateId")
    if type(hasTemplate) == "function" then
        local ok, result = safeCall(hasTemplate, petStats, templateId)
        if ok and tonumber(result) == 1 then return true end
    end
    local tempPets = safeRead(player, "tempPets")
    local entries = tempPets ~= nil and autoquestDataEntries(tempPets) or nil
    for _, entry in ipairs(entries or {}) do
        if tonumber(safeRead(entry.value, "templateId")) == templateId then return true end
    end
    return false
end

local function autoquestReadPetForms(runtimePg, questUtils, rootQuestId, acceptedById, receivedState, completedState)
    local petData = select(1, requireTable("Data.pet_data"))
    local utils = select(1, requireTable("Common.Utils.Utils"))
    local player = runtimePg and safeRead(runtimePg, "me")
    local byTemplateId = {}
    local questIds = autoquestCollectDescendantQuestIds(questUtils, rootQuestId)

    for _, childQuestId in ipairs(questIds) do
        local childData = autoquestQuestData(questUtils, childQuestId, acceptedById)
        local childState = tonumber(safeRead(childData, "state"))
        if childData ~= nil and (receivedState == nil or childState == nil or childState == receivedState or childState == completedState) then
            local config = autoquestQuestConfig(questUtils, childQuestId)
            local objectives = safeRead(config, "objectives")
            local objectiveIds = safeRead(config, "objectivesIDs")
            local objectiveEntries = objectiveIds ~= nil and autoquestDataEntries(objectiveIds) or nil
            if objectiveEntries == nil then objectiveEntries = autoquestDataEntries(objectives) end

            for _, objectiveEntry in ipairs(objectiveEntries or {}) do
                local objectiveId = tonumber(objectiveEntry.value) or tonumber(objectiveEntry.key)
                local objectiveConfig = objectiveId and safeRead(objectives, objectiveId) or objectiveEntry.value
                local params = safeRead(objectiveConfig, "paramVals")
                local templateId = tonumber(safeRead(params, 2))
                if safeRead(params, 1) == "HAS_PET_ID" and templateId and templateId > 0 and
                   (safeRead(params, 4) == "=" or safeRead(params, 4) == nil) and
                   (tonumber(safeRead(params, 5)) or 1) == 1 then
                    local objectiveData = safeRead(safeRead(childData, "objectives"), objectiveId)
                    local complete = safeRead(objectiveData, "isComplete") == true or
                        autoquestPlayerHasTemplateId(player, templateId)
                    local petConfig = petData and safeRead(petData, templateId)
                    local name = localizedText(runtimePg, safeRead(petConfig, "name"))
                    if name == "" then name = "Aniimo" end
                    local prototypeId = tonumber(safeRead(petConfig, "petPrototypeId")) or 0
                    if prototypeId == 0 and utils and type(safeRead(utils, "getPetPetPrototypeId")) == "function" then
                        local ok, value = pcall(utils.getPetPetPrototypeId, templateId)
                        if ok then prototypeId = tonumber(value) or 0 end
                    end
                    local previous = byTemplateId[templateId]
                    if previous == nil then
                        byTemplateId[templateId] = {
                            templateId = templateId,
                            missing = not complete,
                            name = name,
                            prototypeId = prototypeId
                        }
                    elseif complete then
                        previous.missing = false
                    end
                end
            end
        end
    end

    local forms = {}
    for _, form in pairs(byTemplateId) do forms[#forms + 1] = form end
    table.sort(forms, function(left, right)
        if left.missing ~= right.missing then return left.missing end
        return left.templateId < right.templateId
    end)
    return forms
end

local function autoquestNumericList(value)
    local result = {}
    local entries = value ~= nil and autoquestDataEntries(value) or nil
    for _, entry in ipairs(entries or {}) do
        local itemId = tonumber(entry.value)
        if itemId == nil and entry.value == true then itemId = tonumber(entry.key) end
        if itemId ~= nil then result[#result + 1] = itemId end
    end
    return result
end

local autoquestHabitatFields = {
    petList = "Normal",
    aurora = "Aurora",
    rain = "Rain",
    snow = "Snow",
    thunder = "Thunder",
    sun = "Sun"
}

local function autoquestBuildHabitatIndex(self, forms, mapBlockConfigData, smallAreaIndex)
    local prototypes, prototypeSet = {}, {}
    for _, form in ipairs(forms) do
        if form.missing and form.prototypeId > 0 and not prototypeSet[form.prototypeId] then
            prototypeSet[form.prototypeId] = true
            prototypes[#prototypes + 1] = form.prototypeId
        end
    end
    table.sort(prototypes)
    local signature = tostring(mapBlockConfigData) .. ":" .. table.concat(prototypes, ",")
    if self.habitatCacheKey == signature then return self.habitatIndex end

    local byPrototype, dedupe = {}, {}
    for _, prototypeId in ipairs(prototypes) do byPrototype[prototypeId] = {} end
    local areaEntries = smallAreaIndex ~= nil and autoquestDataEntries(smallAreaIndex) or nil
    for _, areaEntry in ipairs(areaEntries or {}) do
        local smallAreaId = tonumber(areaEntry.key) or tonumber(areaEntry.value)
        local blockConfig = smallAreaId and safeRead(mapBlockConfigData, smallAreaId)
        if blockConfig ~= nil then
            local mapAreaId = tonumber(safeRead(blockConfig, "mapAreaId")) or 0
            local areaName = localizedText(rawget(_G, "pg"), safeRead(blockConfig, "areaName"))
            if areaName == "" then areaName = "Area " .. tostring(smallAreaId) end
            for fieldName, condition in pairs(autoquestHabitatFields) do
                local ids = autoquestNumericList(safeRead(blockConfig, fieldName))
                for _, itemId in ipairs(ids) do
                    local list = byPrototype[itemId]
                    if list ~= nil then
                        local dedupeKey = tostring(itemId) .. ":" .. tostring(smallAreaId)
                        local habitat = dedupe[dedupeKey]
                        if habitat == nil then
                            habitat = {
                                prototypeId = itemId,
                                smallAreaId = smallAreaId,
                                mapAreaId = mapAreaId,
                                areaName = areaName,
                                conditions = {}
                            }
                            dedupe[dedupeKey] = habitat
                            list[#list + 1] = habitat
                        end
                        habitat.conditions[condition] = true
                    end
                end
            end
        end
    end
    for _, list in pairs(byPrototype) do
        table.sort(list, function(left, right) return left.smallAreaId < right.smallAreaId end)
    end
    self.habitatCacheKey = signature
    self.habitatIndex = byPrototype
    return byPrototype
end

local function autoquestAreaScenes(sceneDistributeAreaData, wantedMapAreas)
    local scenesByArea = {}
    local sceneEntries = sceneDistributeAreaData ~= nil and autoquestDataEntries(sceneDistributeAreaData) or nil
    for _, sceneEntry in ipairs(sceneEntries or {}) do
        local sceneId = tonumber(sceneEntry.key)
        if sceneId then
            local areaIds = autoquestNumericList(sceneEntry.value)
            for _, areaId in ipairs(areaIds) do
                if wantedMapAreas[areaId] then
                    local list = scenesByArea[areaId] or {}
                    scenesByArea[areaId] = list
                    list[#list + 1] = sceneId
                end
            end
        end
    end
    return scenesByArea
end

local function autoquestReadTeleportMarks(runtimePg, mapSystem, mapAreaId, sceneIds, mapBlockConfigData,
        defaultMarkData, mapConst, currentPosition, currentMainSceneId)
    local marks, seen = {}, {}
    local getSceneMarks = safeRead(mapSystem, "getSceneMarkInfoByMarkId")
    local convertScene = safeRead(mapSystem, "convertSceneId")
    local getMarkStatus = safeRead(mapSystem, "getMarkStatus")
    local inWhichBlock = safeRead(mapSystem, "inWhichBlock")
    local sceneMarkCache = safeRead(mapSystem, "cacheAllSceneMarkPointData")
    local lockedStatus = tonumber(safeRead(mapConst, "MAP_MARK_STATUS_LOCKED")) or 1

    for _, sceneId in ipairs(sceneIds) do
        if type(getSceneMarks) == "function" then
            local markData = safeRead(sceneMarkCache, sceneId)
            if markData == nil then
                safeCall(getSceneMarks, mapSystem, sceneId)
                markData = safeRead(sceneMarkCache, sceneId)
            end
            if markData ~= nil then
                local markEntries = autoquestDataEntries(markData) or {}
                for _, markEntry in ipairs(markEntries) do
                    local markId = tonumber(markEntry.key)
                    local markInfo = markEntry.value
                    local configId = tonumber(safeRead(markInfo, "markConfigId"))
                    local markConfig = configId and safeRead(defaultMarkData, configId)
                    local infoPage = tonumber(safeRead(markInfo, "infoPage")) or tonumber(safeRead(markConfig, "infoPage"))
                    local show = safeRead(markInfo, "ShoworNot")
                    if show == nil then show = safeRead(markInfo, "showOrNot") end
                    local realSceneId = tonumber(safeRead(markInfo, "realSceneId")) or sceneId
                    local markType = tonumber(safeRead(markInfo, "markType")) or 0
                    local markPosition = safeRead(markInfo, "markPosition")
                    local isShown = show == true or tonumber(show) == 1
                    if markId and markInfo and infoPage == 2 and isShown and markPosition and not seen[tostring(realSceneId) .. ":" .. tostring(markId)] then
                        local mainSceneId = realSceneId
                        if type(convertScene) == "function" then
                            local convertOk, converted = safeCall(convertScene, mapSystem, realSceneId)
                            if convertOk and tonumber(converted) then mainSceneId = tonumber(converted) end
                        end
                        local status = nil
                        if type(getMarkStatus) == "function" then
                            local statusOk, value = safeCall(getMarkStatus, mapSystem, markId, markType, realSceneId)
                            if statusOk then status = tonumber(value) end
                        end
                        if status and status > lockedStatus then
                            local smallAreaId = nil
                            if type(inWhichBlock) == "function" then
                                local blockOk, blockId = safeCall(inWhichBlock, mapSystem, realSceneId, false, markPosition)
                                if blockOk then smallAreaId = tonumber(blockId) end
                            end
                            local markBlock = smallAreaId and safeRead(mapBlockConfigData, smallAreaId)
                            local markMapAreaId = tonumber(safeRead(markInfo, "largeAreaId")) or
                                tonumber(safeRead(markInfo, "belongAreaId")) or tonumber(safeRead(markBlock, "mapAreaId")) or 0
                            if markMapAreaId == mapAreaId then
                                local markX = readAxis(markPosition, 1, "x", "X")
                                local markY = readAxis(markPosition, 2, "y", "Y")
                                local markZ = readAxis(markPosition, 3, "z", "Z")
                                local distance = math.huge
                                if markX and markY and markZ and currentPosition and currentMainSceneId == mainSceneId then
                                    distance = distanceBetween(currentPosition, { x = markX, y = markY, z = markZ })
                                end
                                local nameKey = safeRead(markInfo, "infoTitle") or safeRead(markInfo, "txt") or safeRead(markInfo, "name")
                                local markName = localizedText(runtimePg, nameKey)
                                if markName == "" then markName = "TP " .. tostring(markId) end
                                marks[#marks + 1] = {
                                    markId = markId,
                                    markRealSceneId = realSceneId,
                                    teleportSceneId = mainSceneId,
                                    smallAreaId = smallAreaId or 0,
                                    name = markName,
                                    distance = distance
                                }
                                seen[tostring(realSceneId) .. ":" .. tostring(markId)] = true
                            end
                        end
                    end
                end
            end
        end
    end
    return marks
end

local function autoquestBuildHabitatRows(self, runtimePg, forms, now)
    local mapBlockConfigData = select(1, requireTable("Data.map_block_config_data"))
    local smallAreaIndex = select(1, requireTable("Data.map_small_area_id_to_index"))
    local mapAreaData = select(1, requireTable("Data.map_area_config_data"))
    local sceneDistribute = select(1, requireTable("Data.scene_distribute_area_data"))
    local sceneData = select(1, requireTable("Data.scene_data"))
    local sceneSeamless = select(1, requireTable("Data.scene_seamless_data"))
    local defaultMarkData = select(1, requireTable("Data.default_map_mark_data"))
    local mapConst = select(1, requireTable("Common.Const.Const"))
    local mapSystem = safeRead(safeRead(runtimePg, "game"), "map")
    if mapBlockConfigData == nil or smallAreaIndex == nil or mapSystem == nil then return {} end

    local habitatIndex = autoquestBuildHabitatIndex(self, forms, mapBlockConfigData, smallAreaIndex)
    local formsKeyParts, wantedMapAreas = {}, {}
    for _, form in ipairs(forms) do
        if form.missing and form.prototypeId > 0 then
            formsKeyParts[#formsKeyParts + 1] = tostring(form.templateId)
            for _, habitat in ipairs(habitatIndex[form.prototypeId] or {}) do
                if habitat.mapAreaId > 0 then wantedMapAreas[habitat.mapAreaId] = true end
            end
        end
    end
    table.sort(formsKeyParts)
    local routeKey = table.concat(formsKeyParts, ",") .. ":" .. tostring(sceneDistribute)
    if self.habitatRouteCacheKey == routeKey and now - (self.habitatRouteCacheAt or -1) < 5 then
        return self.habitatRouteCache
    end

    local scenesByArea = autoquestAreaScenes(sceneDistribute, wantedMapAreas)
    local areaRoutes = {}
    local map = mapSystem
    local me = safeRead(runtimePg, "me")
    local playerSpace = safeRead(me, "space")
    local currentSceneId = tonumber(safeRead(playerSpace, "sceneId"))
    local currentMainSceneId = currentSceneId
    local convertScene = safeRead(map, "convertSceneId")
    if currentSceneId and type(convertScene) == "function" then
        local convertedOk, converted = safeCall(convertScene, map, currentSceneId)
        if convertedOk and tonumber(converted) then currentMainSceneId = tonumber(converted) end
    end
    local currentPosition = positionOf(safeRead(runtimePg, "pawn") or safeRead(runtimePg, "me"))

    for mapAreaId in pairs(wantedMapAreas) do
        local scenes, seenScenes = {}, {}
        for _, sceneId in ipairs(scenesByArea[mapAreaId] or {}) do
            local rootSceneId = sceneId
            if type(convertScene) == "function" then
                local rootOk, rootValue = safeCall(convertScene, map, sceneId)
                if rootOk and tonumber(rootValue) then rootSceneId = tonumber(rootValue) end
            end
            if not seenScenes[rootSceneId] then
                seenScenes[rootSceneId] = true
                scenes[#scenes + 1] = rootSceneId
            end
            local rootData = safeRead(sceneData, rootSceneId) or safeRead(sceneData, sceneId)
            local overlays = safeRead(rootData, "mapDisplayOverlayScenes")
            for _, overlayEntry in ipairs(overlays ~= nil and autoquestDataEntries(overlays) or {}) do
                local overlayId = tonumber(overlayEntry.value) or tonumber(overlayEntry.key)
                if overlayId and not seenScenes[overlayId] then
                    seenScenes[overlayId] = true
                    scenes[#scenes + 1] = overlayId
                end
            end
            local seamless = safeRead(sceneSeamless, rootSceneId)
            local seamlessGroup = safeRead(seamless, "seamlessGroup")
            for _, seamlessEntry in ipairs(seamlessGroup ~= nil and autoquestDataEntries(seamlessGroup) or {}) do
                local seamlessId = tonumber(seamlessEntry.key) or tonumber(seamlessEntry.value)
                if seamlessId and not seenScenes[seamlessId] then
                    seenScenes[seamlessId] = true
                    scenes[#scenes + 1] = seamlessId
                end
            end
        end
        local marks = autoquestReadTeleportMarks(runtimePg, map, mapAreaId, scenes, mapBlockConfigData,
            defaultMarkData, mapConst, currentPosition, currentMainSceneId)
        areaRoutes[mapAreaId] = marks
    end

    local output = {}
    for _, form in ipairs(forms) do
        if form.missing then
            for _, habitat in ipairs(habitatIndex[form.prototypeId] or {}) do
                local conditions = {}
                for condition in pairs(habitat.conditions) do conditions[#conditions + 1] = condition end
                table.sort(conditions)
                local conditionText = table.concat(conditions, " / ")
                local candidates, exact = {}, {}
                for _, mark in ipairs(areaRoutes[habitat.mapAreaId] or {}) do
                    if mark.smallAreaId == habitat.smallAreaId then exact[#exact + 1] = mark else candidates[#candidates + 1] = mark end
                end
                local useFallback = #exact == 0
                local selected = useFallback and candidates or exact
                table.sort(selected, function(left, right)
                    if left.distance ~= right.distance then return left.distance < right.distance end
                    return left.markId < right.markId
                end)
                local areaConfig = mapAreaData and safeRead(mapAreaData, habitat.mapAreaId)
                local regionName = localizedText(runtimePg, safeRead(areaConfig, "areaName"))
                local fullAreaName = habitat.areaName
                if regionName ~= "" and regionName ~= habitat.areaName then fullAreaName = fullAreaName .. " · " .. regionName end
                if #selected == 0 then
                    output[#output + 1] = {
                        templateId = form.templateId,
                        smallAreaId = habitat.smallAreaId,
                        areaName = fullAreaName,
                        condition = conditionText,
                        markId = 0,
                        markRealSceneId = 0,
                        teleportSceneId = 0,
                        markName = "",
                        areaFallback = false
                    }
                else
                    local count = math.min(#selected, 3)
                    for index = 1, count do
                        local mark = selected[index]
                        output[#output + 1] = {
                            templateId = form.templateId,
                            smallAreaId = habitat.smallAreaId,
                            areaName = fullAreaName,
                            condition = conditionText,
                            markId = mark.markId,
                            markRealSceneId = mark.markRealSceneId,
                            teleportSceneId = mark.teleportSceneId,
                            markName = mark.name,
                            areaFallback = useFallback
                        }
                    end
                end
            end
        end
    end
    self.habitatRouteCacheKey = routeKey
    self.habitatRouteCacheAt = now
    self.habitatRouteCache = output
    return output
end

local function autoquestFindHabitatRoute(habitats, templateId, markId, realSceneId)
    local fallback = nil
    for _, route in ipairs(habitats or {}) do
        if route.templateId == templateId and route.markId ~= 0 then
            if route.markId == markId and route.markRealSceneId == realSceneId then return route end
            if fallback == nil then fallback = route end
        end
    end
    if markId == nil or markId == 0 then return fallback end
    return nil
end

local function autoquestUseOfficialTeleport(runtimePg, route)
    if runtimePg == nil or route == nil or tonumber(route.markId) == nil or route.markId == 0 then
        return false, "TP unavailable"
    end
    local player = safeRead(runtimePg, "me")
    local pawn = safeRead(runtimePg, "pawn")
    local checkTeleport = safeRead(pawn, "checkTeleport")
    local checkOk, allowed = safeCall(checkTeleport, pawn)
    if not checkOk or allowed ~= true then return false, "TP unavailable" end

    local teleportEvents = select(1, requireTable("Data.teleport_mark_event_data"))
    local eventData = teleportEvents and safeRead(teleportEvents, route.markId)
    local defaultEnabled = safeRead(eventData, "defaultEnabled")
    if defaultEnabled == true or tonumber(defaultEnabled) == 1 then
        local doEvent = safeRead(player, "doEventByData")
        local eventParam = {}
        for _, entry in ipairs(autoquestDataEntries(safeRead(eventData, "eventParam")) or {}) do
            eventParam[entry.key] = entry.value
        end
        local event = { safeRead(eventData, "eventType"), eventParam, 0 }
        local eventOk = safeCall(doEvent, player, event)
        if not eventOk then return false, "TP unavailable" end
        return true
    end

    local clientUtils = select(1, requireTable("Utils.ClientUtils"))
    local teleport = clientUtils and safeRead(clientUtils, "playTeleportDissolveEffectAndTeleport")
    if type(teleport) ~= "function" then return false, "TP unavailable" end
    local ok = pcall(teleport, route.teleportSceneId, route.markId)
    if not ok then return false, "TP unavailable" end
    return true
end

local function autoquestResolveTarget(runtimePg, questUtils, questData, questId, acceptedById,
        receivedState, completedState, scopeQuestId)
    local getTargetInfo = safeRead(questUtils, "getQuestTargetInfo")
    if type(getTargetInfo) ~= "function" then return nil, "No target" end

    questId = tonumber(questId) or 0
    local scanRootId = tonumber(scopeQuestId) or autoquestRootQuestId(questUtils, questId)
    if scanRootId == nil or scanRootId <= 0 then scanRootId = questId end
    if scanRootId <= 0 then return nil, "No target" end

    local candidateIds = autoquestCollectDescendantQuestIds(questUtils, scanRootId)
    local player = runtimePg and (safeRead(runtimePg, "pawn") or safeRead(runtimePg, "me"))
    local origin = positionOf(player)
    local map = safeRead(safeRead(runtimePg, "game"), "map")
    local currentSceneId = tonumber(safeRead(safeRead(safeRead(runtimePg, "me"), "space"), "sceneId"))
    local convertScene = safeRead(map, "convertSceneId")
    local currentMainScene = currentSceneId
    if type(convertScene) == "function" and currentSceneId ~= nil then
        local convertOk, converted = safeCall(convertScene, map, currentSceneId)
        if convertOk and tonumber(converted) ~= nil then currentMainScene = tonumber(converted) end
    end

    local hudObjectives, hasHudObjectiveList = autoquestHudObjectiveSet(questUtils, scanRootId)
    local isVisible = safeRead(questUtils, "isQuestVisible")
    local isObjectiveFinished = safeRead(questUtils, "isQuestDataObjFined")
    local getObjectiveDescription = safeRead(questUtils, "formatObjectiveDesc")

    local function questIsVisible(candidateQuestId)
        if type(isVisible) ~= "function" then return true end
        local visibleOk, visible = pcall(isVisible, candidateQuestId)
        return visibleOk and visible == true
    end

    local function targetFromPosition(source, candidateQuestId, candidateState, objectiveId, hudObjective, manualHandIn)
        if type(source) ~= "table" then return nil end
        local position = safeRead(source, "pos") or safeRead(source, "position")
        local x = readAxis(position, 1, "x", "X")
        local y = readAxis(position, 2, "y", "Y")
        local z = readAxis(position, 3, "z", "Z")
        if x == nil or y == nil or z == nil then return nil end

        local posConfig = safeRead(source, "posConfig")
        local targetSceneId = tonumber(safeRead(source, "sceneId") or safeRead(source, "scene") or
            safeRead(posConfig, "scene"))
        local targetMainScene = targetSceneId
        if type(convertScene) == "function" and targetSceneId ~= nil then
            local convertOk, converted = safeCall(convertScene, map, targetSceneId)
            if convertOk and tonumber(converted) ~= nil then targetMainScene = tonumber(converted) end
        end
        if targetMainScene == nil then targetMainScene = currentMainScene end
        local sameArea = currentMainScene ~= nil and targetMainScene ~= nil and
            currentMainScene == targetMainScene
        if not sameArea then return nil end

        local resolvedObjectiveId = tonumber(safeRead(source, "objId")) or tonumber(objectiveId)
        local objective = manualHandIn and "Quest hand-in" or "Quest target"
        if hudObjective ~= nil and type(getObjectiveDescription) == "function" then
            local textOk, textValue = pcall(getObjectiveDescription, hudObjective, candidateQuestId, resolvedObjectiveId)
            if textOk and textValue ~= nil then
                local textValueSafe = scalar(textValue, 120)
                if textValueSafe and textValueSafe ~= "" then objective = textValueSafe end
            end
        end

        local keyPart = resolvedObjectiveId or safeRead(source, "npcStaticId") or "target"
        local targetKey = table.concat({ tostring(candidateQuestId), tostring(candidateState or 0),
            tostring(keyPart), string.format("%.2f:%.2f:%.2f", x, y, z) }, ":")
        local target = {
            position = { x = x, y = y, z = z },
            objective = objective,
            key = targetKey,
            sceneId = targetSceneId or currentSceneId,
            questId = candidateQuestId,
            objectiveId = resolvedObjectiveId,
            sameArea = true
        }
        if origin ~= nil then target.distance = distanceBetween(origin, target.position) end
        return target
    end

    local function preferNearest(best, candidate)
        if candidate == nil then return best end
        if best == nil then return candidate end
        if candidate.distance ~= nil and (best.distance == nil or candidate.distance < best.distance) then
            return candidate
        end
        return best
    end

    local bestTarget = nil
    for _, candidateQuestId in ipairs(candidateIds) do
        local candidateData = (candidateQuestId == questId and questData) or
            autoquestQuestData(questUtils, candidateQuestId, acceptedById)
        local candidateState = tonumber(safeRead(candidateData, "state"))
        local isReceived = receivedState ~= nil and candidateState == receivedState
        local isManualHandIn = completedState ~= nil and candidateState == completedState and
            autoquestIsManualCommit(questUtils, candidateQuestId)

        if candidateData ~= nil and questIsVisible(candidateQuestId) and (isReceived or isManualHandIn) then
            local infoOk, targetInfo = pcall(getTargetInfo, candidateData, false)
            local positions = infoOk and safeRead(targetInfo, "posInfo") or nil
            local positionEntries = positions ~= nil and autoquestDataEntries(positions) or nil
            for _, positionEntry in ipairs(positionEntries or {}) do
                local source = positionEntry.value
                local candidate = nil
                if isManualHandIn then
                    candidate = targetFromPosition(source, candidateQuestId, candidateState,
                        nil, nil, true)
                else
                    local objectiveId = tonumber(safeRead(source, "objId"))
                    local pathId = tonumber(safeRead(source, "pathId"))
                    local showPath = safeRead(source, "showPath")
                    local isDone = false
                    if objectiveId ~= nil then
                        local objectiveData = safeRead(safeRead(candidateData, "objectives"), objectiveId)
                        if objectiveData == nil then
                            isDone = true
                        else
                            local checkedByGame = false
                            if type(isObjectiveFinished) == "function" then
                                local doneOk, doneValue = pcall(isObjectiveFinished, candidateData, objectiveId)
                                if doneOk then
                                    isDone = doneValue == true
                                    checkedByGame = true
                                end
                            end
                            if not checkedByGame then
                                isDone = safeRead(objectiveData, "isComplete") == true
                            end
                        end
                    else
                        isDone = true
                    end
                    local hudKey = objectiveId and (tostring(candidateQuestId) .. ":" .. tostring(objectiveId)) or ""
                    local hudObjective = hudObjectives and hudObjectives[hudKey] or nil
                    local isHudEligible = not hasHudObjectiveList or hudObjective ~= nil
                    if not isDone and isHudEligible and pathId ~= nil and pathId > 0 and
                       (showPath == true or tonumber(showPath) == 1) then
                        candidate = targetFromPosition(source, candidateQuestId, candidateState,
                            objectiveId, hudObjective, false)
                    end
                end
                bestTarget = preferNearest(bestTarget, candidate)
            end
        end
    end

    return bestTarget, bestTarget == nil and "No current marker" or nil
end

local function autoquestWorldAllowed(runtimePg, questUtils, targetSceneId)
    local openWorldMainSceneId = 3000
    local me = runtimePg and safeRead(runtimePg, "me")
    local space = safeRead(me, "space")
    local currentSceneId = tonumber(safeRead(space, "sceneId"))
    local map = safeRead(safeRead(runtimePg, "game"), "map")
    local convert = safeRead(map, "convertSceneId")
    local currentMainSceneId = nil
    if type(convert) == "function" and currentSceneId ~= nil then
        local ok, value = safeCall(convert, map, currentSceneId)
        if ok then currentMainSceneId = tonumber(value) end
    end

    local inOpenWorld = currentMainSceneId == openWorldMainSceneId
    if not inOpenWorld then
        local isWorld = safeRead(questUtils, "isArkScene")
        if type(isWorld) == "function" then
            local ok, result = pcall(isWorld)
            inOpenWorld = ok and result == true
        end
    end
    if not inOpenWorld then return false, "Open world only" end
    if targetSceneId == nil then return true end

    local targetMainSceneId = nil
    if type(convert) == "function" then
        local ok, value = safeCall(convert, map, targetSceneId)
        if ok then targetMainSceneId = tonumber(value) end
    end

    if currentMainSceneId ~= nil and targetMainSceneId ~= nil then
        if currentMainSceneId ~= targetMainSceneId then return false, "Different area" end
        return true
    end
    if tonumber(targetSceneId) == currentSceneId or tonumber(targetSceneId) == openWorldMainSceneId or
       currentMainSceneId == openWorldMainSceneId then
        return true
    end
    if currentMainSceneId == nil and inOpenWorld and
       (targetMainSceneId == nil or targetMainSceneId == openWorldMainSceneId) then
        return true
    end

    if type(convert) ~= "function" then return true end
    return false, "Different area"
end

local function autoquestHasBlockingUi(runtimePg)
    local ui = safeRead(safeRead(runtimePg, "global"), "ui")
    local check = safeRead(ui, "checkUIOpen")
    if type(check) ~= "function" then return false end
    local uiConst = select(1, requireTable("Const.UIConst"))
    if uiConst == nil then return false end
    local ids = {
        safeRead(uiConst, "UI_ID_BOTTOM_DIALOGUE"), safeRead(uiConst, "UI_ID_REPORT"),
        safeRead(uiConst, "UI_ID_DIALOGUE_ID"), safeRead(uiConst, "UI_ID_DIALOG_REVIEW"),
        safeRead(uiConst, "UI_ID_INTERACT_SECOND")
    }
    for _, id in ipairs(ids) do
        if id ~= nil then
            local ok, opened = safeCall(check, ui, id)
            if ok and opened == true then return true end
        end
    end
    return false
end

local function autoquestTryOfficialInteraction(runtimePg, questId)
    local game = safeRead(runtimePg, "game")
    local interaction = safeRead(game, "interaction")
    local roots = safeRead(interaction, "currentInteractList")
    if type(roots) ~= "table" then return false end
    for _, root in ipairs(roots) do
        local units = safeRead(root, "interactUnits")
        if type(units) == "table" then
            for _, unit in ipairs(units) do
                local styleList = safeRead(unit, "questDialogInfo") or safeRead(unit, "questCommitInfo")
                local directId = tonumber(safeRead(unit, "questId"))
                if directId == questId and type(styleList) ~= "table" then styleList = { { questId = directId } } end
                if type(styleList) == "table" then
                    for index, style in ipairs(styleList) do
                        if tonumber(safeRead(style, "questId")) == questId then
                            local canInteract = safeRead(unit, "canInteractive")
                            local canOk, can = safeCall(canInteract, unit)
                            if canOk and can == true then
                                local tryInteractive = safeRead(unit, "tryInteractive")
                                local tryOk, result = safeCall(tryInteractive, unit,
                                    tonumber(safeRead(style, "index")) or index)
                                if tryOk and result ~= false then return true end
                            end
                        end
                    end
                end
            end
        end
    end
    return false
end
return {
    rootQuestId = autoquestRootQuestId,
    readPetForms = autoquestReadPetForms,
    buildHabitatRows = autoquestBuildHabitatRows,
    findHabitatRoute = autoquestFindHabitatRoute,
    useOfficialTeleport = autoquestUseOfficialTeleport,
    trackedQuestId = autoquestTrackedQuestId,
    questTitle = autoquestQuestTitle,
    resolveTarget = autoquestResolveTarget,
    worldAllowed = autoquestWorldAllowed,
    hasBlockingUi = autoquestHasBlockingUi,
    tryOfficialInteraction = autoquestTryOfficialInteraction
}
end)()

function Autoquest:frame(enabled, selectedQuestId, teleportRequest,
        selectedTemplateId, selectedMarkId, selectedMarkRealSceneId)
    self.enabled = enabled == true
    selectedQuestId = tonumber(selectedQuestId) or 0
    teleportRequest = tonumber(teleportRequest) or 0
    selectedTemplateId = tonumber(selectedTemplateId) or 0
    selectedMarkId = tonumber(selectedMarkId) or 0
    selectedMarkRealSceneId = tonumber(selectedMarkRealSceneId) or 0
    if selectedQuestId ~= self.selectedQuestId then
        self.selectedQuestId = selectedQuestId
        self.lastTargetKey = nil
        self.lastTeleportedKey = nil
        self.lastInteractionKey = nil
        self.habitatRouteCacheKey = nil
        self.manualFeedback = ""
    end
    if self.enabled then self.manualFeedback = "" end

    local runtimePg = rawget(_G, "pg")
    local me = runtimePg and safeRead(runtimePg, "me")
    local questUtils = select(1, requireTable("GameApp.Quest.QuestUtils"))
    local questConst = select(1, requireTable("Common.Const.QuestConst"))
    local accepted = me and safeRead(me, "acceptedQuestMap")
    local receivedState = questConst and questConst.QUEST_STATE and tonumber(questConst.QUEST_STATE.RECEIVED)
    local completedState = questConst and questConst.QUEST_STATE and tonumber(questConst.QUEST_STATE.COMPLETED)
    local rows, questById, acceptedById = {}, {}, {}
    local acceptedEntries = accepted ~= nil and autoquestDataEntries(accepted) or nil
    if acceptedEntries ~= nil then
        for _, acceptedEntry in ipairs(acceptedEntries) do
            local mapId, questData = acceptedEntry.key, acceptedEntry.value
            local questId = tonumber(safeRead(questData, "configId")) or tonumber(mapId)
            local questState = tonumber(safeRead(questData, "state"))
            if questId ~= nil then acceptedById[questId] = questData end
            if questId ~= nil and questState ~= nil and
               (questState == receivedState or questState == completedState) then
                local title = "Quest " .. tostring(questId)
                if questUtils and type(questUtils.getQuestName) == "function" then
                    local titleOk, titleKey = pcall(questUtils.getQuestName, questId)
                    if titleOk and titleKey ~= nil then
                        local localized = localizedText(runtimePg, titleKey)
                        title = localized ~= "" and localized or (scalar(titleKey, 100) or title)
                    end
                end
                rows[#rows + 1] = { id = questId, state = questState, title = title }
                questById[questId] = questData
            end
        end
    end
    table.sort(rows, function(left, right) return left.id < right.id end)
    self.activeQuests = rows

    local traceId = questUtils and AutoquestRuntime.trackedQuestId(questUtils, me) or 0
    local resolvedId = selectedQuestId ~= 0 and selectedQuestId or (tonumber(traceId) or 0)
    self.resolvedQuestId = resolvedId
    local questData = questById[resolvedId]
    if questData == nil and questUtils and type(questUtils.getQuestData) == "function" and resolvedId ~= 0 then
        local dataOk, data = pcall(questUtils.getQuestData, resolvedId)
        if dataOk then questData = data end
    end

    local selectedTitle = AutoquestRuntime.questTitle(runtimePg, questUtils, resolvedId)
    for _, row in ipairs(rows) do
        if row.id == resolvedId and row.title ~= "" and row.title ~= ("Quest " .. tostring(resolvedId)) then
            selectedTitle = row.title
            break
        end
    end

    local targetScopeId = resolvedId
    if resolvedId ~= 0 and selectedQuestId == 0 then
        local rootQuestId = AutoquestRuntime.rootQuestId(questUtils, resolvedId)
        if tonumber(rootQuestId) and tonumber(rootQuestId) > 0 then targetScopeId = tonumber(rootQuestId) end
    end

    local target = nil
    if resolvedId ~= 0 and questUtils ~= nil then
        target = AutoquestRuntime.resolveTarget(runtimePg, questUtils, questData, resolvedId,
            acceptedById, receivedState, completedState, targetScopeId)
    end

    local petForms = {}
    if resolvedId ~= 0 and questUtils ~= nil then
        local formsOk, formsValue = pcall(AutoquestRuntime.readPetForms, runtimePg, questUtils, targetScopeId,
            acceptedById, receivedState, completedState)
        if formsOk and type(formsValue) == "table" then petForms = formsValue end
    end
    self.petForms = petForms
    self.habitats = {}
    self.petFormsRequired = #petForms
    self.petFormsOwned = 0
    for _, form in ipairs(petForms) do
        if not form.missing then self.petFormsOwned = self.petFormsOwned + 1 end
    end
    if self.petFormsRequired > 0 then
        local habitatsOk, habitatsValue = pcall(AutoquestRuntime.buildHabitatRows, self, runtimePg, petForms, os.clock())
        if habitatsOk and type(habitatsValue) == "table" then self.habitats = habitatsValue end
        if target == nil then
            self.objective = string.format("Aniimos: %d/%d", self.petFormsOwned, self.petFormsRequired)
        end
    else
        self.habitatCacheKey = nil
        self.habitatIndex = {}
        self.habitatRouteCacheKey = nil
        self.habitatRouteCache = {}
    end
    self.selectedTitle = selectedTitle
    if target and target.objective then self.objective = target.objective
    elseif self.petFormsRequired == 0 then self.objective = "" end
    self.targetDistance = -1
    self.targetPosition = target and target.position or nil
    self.targetKey = target and target.key or nil
    self.targetSceneId = target and target.sceneId or nil
    if target and target.position then
        local origin = positionOf(runtimePg and (safeRead(runtimePg, "pawn") or me))
        if origin then self.targetDistance = distanceBetween(origin, target.position) end
    end

    local habitatTeleportStarted = false
    if teleportRequest ~= self.lastTeleportRequest then
        self.lastTeleportRequest = teleportRequest
        if selectedMarkId ~= 0 then
            local route = AutoquestRuntime.findHabitatRoute(self.habitats, selectedTemplateId,
                selectedMarkId, selectedMarkRealSceneId)
            local isWorld = safeRead(questUtils, "isArkScene")
            local worldOk, inWorld = false, false
            if type(isWorld) == "function" then worldOk, inWorld = pcall(isWorld) end
            if route ~= nil and worldOk and inWorld == true then
                local tpOk, tpError = AutoquestRuntime.useOfficialTeleport(runtimePg, route)
                if tpOk then
                    self.teleports = (self.teleports or 0) + 1
                    local currentSpace = safeRead(me, "space")
                    self.pendingHabitatTeleport = {
                        route = route,
                        startedAt = os.clock(),
                        startSceneId = tonumber(safeRead(currentSpace, "sceneId"))
                    }
                    self.state = "TP requested"
                    self.manualFeedback = self.state
                    habitatTeleportStarted = true
                else
                    self.state = tpError or "TP unavailable"
                    self.manualFeedback = self.state
                end
            elseif route == nil then
                self.state = "Habitat TP unavailable"
                self.manualFeedback = self.state
            else
                self.state = "Open world only"
                self.manualFeedback = self.state
            end
        elseif target and target.position then
            local allowed, reason = AutoquestRuntime.worldAllowed(runtimePg, questUtils, target.sceneId)
            if allowed then
                local ok, teleportError = arkhanTeleportPawn(runtimePg, target.position, 1.5)
                if ok then
                    self.teleports = (self.teleports or 0) + 1
                    self.state = "Teleported"
                    self.lastTeleportedKey = target.key
                    self.lastTargetKey = target.key
                    self.nextActionAt = os.clock()
                    self.manualFeedback = "Teleported"
                else
                    self.state = "TP unavailable"
                    self.lastError = tostring(teleportError or "unavailable")
                    self.manualFeedback = self.state
                end
            else
                self.state = reason or "Open world only"
                self.manualFeedback = self.state
            end
        else
            local preferredTemplateId = selectedTemplateId
            if preferredTemplateId == 0 then
                for _, form in ipairs(petForms) do
                    if form.missing then preferredTemplateId = form.templateId; break end
                end
            end
            local route = AutoquestRuntime.findHabitatRoute(self.habitats, preferredTemplateId, 0, 0)
            local isWorld = safeRead(questUtils, "isArkScene")
            local worldOk, inWorld = false, false
            if type(isWorld) == "function" then worldOk, inWorld = pcall(isWorld) end
            if route and worldOk and inWorld == true then
                local tpOk, tpError = AutoquestRuntime.useOfficialTeleport(runtimePg, route)
                if tpOk then
                    self.teleports = (self.teleports or 0) + 1
                    local currentSpace = safeRead(me, "space")
                    self.pendingHabitatTeleport = {
                        route = route,
                        startedAt = os.clock(),
                        startSceneId = tonumber(safeRead(currentSpace, "sceneId"))
                    }
                    self.state = "TP requested"
                    self.manualFeedback = self.state
                    habitatTeleportStarted = true
                else
                    self.state = tpError or "TP unavailable"
                    self.manualFeedback = self.state
                end
            else
                self.state = self.petFormsRequired > 0 and "No unlocked habitat TP" or "No current marker"
                self.manualFeedback = self.state
            end
        end
    end

    if habitatTeleportStarted then return end
    local pending = self.pendingHabitatTeleport
    if pending ~= nil then
        local route = pending.route or {}
        local map = safeRead(safeRead(runtimePg, "game"), "map")
        local meSpace = safeRead(me, "space")
        local currentScene = tonumber(safeRead(meSpace, "sceneId"))
        local convertScene = safeRead(map, "convertSceneId")
        local currentMain, targetMain = currentScene, tonumber(route.teleportSceneId)
        if type(convertScene) == "function" and currentScene then
            local currentOk, value = safeCall(convertScene, map, currentScene)
            if currentOk and tonumber(value) then currentMain = tonumber(value) end
        end
        local markInfo = nil
        local getMark = safeRead(map, "getSceneMarkInfoByMarkId")
        if type(getMark) == "function" then
            local markOk, value = safeCall(getMark, map, route.markRealSceneId, route.markId)
            if markOk then markInfo = value end
        end
        local playerPosition = positionOf(safeRead(runtimePg, "pawn") or me)
        local markPosition = safeRead(markInfo, "markPosition")
        local mx = readAxis(markPosition, 1, "x", "X")
        local my = readAxis(markPosition, 2, "y", "Y")
        local mz = readAxis(markPosition, 3, "z", "Z")
        local closeToMark = currentScene == tonumber(route.markRealSceneId) and playerPosition and mx and my and mz and
            distanceBetween(playerPosition, { x = mx, y = my, z = mz }) <= 25
        local arrived = currentMain == targetMain and
            (currentScene ~= pending.startSceneId or closeToMark == true)
        if arrived then
            self.pendingHabitatTeleport = nil
            self.state = "At habitat"
            self.manualFeedback = self.state
        elseif os.clock() - (tonumber(pending.startedAt) or 0) > 12 then
            self.pendingHabitatTeleport = nil
            self.state = "TP not confirmed"
            self.manualFeedback = self.state
        else
            self.state = "TP in progress"
            self.manualFeedback = self.state
            return
        end
    end

    if not self.enabled then
        self.state = self.manualFeedback ~= "" and self.manualFeedback or "Disabled"
        return
    end
    if resolvedId == 0 or questData == nil then self.state = "Select a quest"; return end
    if target == nil or target.position == nil then
        self.state = self.petFormsRequired > 0 and ("Aniimos " .. tostring(self.petFormsOwned) .. "/" .. tostring(self.petFormsRequired)) or "No current marker"
        return
    end
    local allowed, reason = AutoquestRuntime.worldAllowed(runtimePg, questUtils, target.sceneId)
    if not allowed then self.state = reason or "Open world only"; return end
    if AutoquestRuntime.hasBlockingUi(runtimePg) then self.state = "Paused"; return end

    if self.lastTargetKey ~= target.key then
        self.lastTargetKey = target.key
        self.lastTeleportedKey = nil
        self.lastInteractionKey = nil
    end
    local now = os.clock()
    if now < (tonumber(self.nextActionAt) or 0) then return end
    if self.lastTeleportedKey ~= target.key then
        local distance = self.targetDistance
        if distance < 0 or distance > 4.0 then
            local ok, teleportError = arkhanTeleportPawn(runtimePg, target.position, 1.5)
            if not ok then
                self.state = "TP unavailable"
                self.lastError = tostring(teleportError or "unavailable")
                self.nextActionAt = now + 2.0
                return
            end
            self.teleports = (self.teleports or 0) + 1
        end
        self.lastTeleportedKey = target.key
        self.nextActionAt = now + 1.0
        self.state = "At target"
        return
    end
    local interacted = false
    if self.lastInteractionKey ~= target.key and target.questId ~= nil then
        interacted = AutoquestRuntime.tryOfficialInteraction(runtimePg, target.questId)
        if not interacted and target.questId ~= resolvedId then
            interacted = AutoquestRuntime.tryOfficialInteraction(runtimePg, resolvedId)
        end
    end
    if self.lastInteractionKey ~= target.key and interacted then
        self.lastInteractionKey = target.key
        self.state = "Waiting for quest"
        self.nextActionAt = now + 1.0
        return
    end
    self.state = self.lastInteractionKey == target.key and "Waiting for quest" or "At target"
end

function Autoquest:appendProtocol(lines)
    local module = Arkhan.modules.autoquest
    local quests = type(module) == "table" and module.activeQuests or {}
    lines[#lines + 1] = string.format(
        "AQ\t%d\t%d\t%d\t%s\t%s\t%s\t%.2f\t%d\t%d",
        type(module) == "table" and module.enabled == true and 1 or 0,
        #quests,
        tonumber(module and module.resolvedQuestId) or 0,
        protocolText(module and module.selectedTitle or ""),
        protocolText(module and module.objective or ""),
        protocolText(module and module.state or "waiting"),
        tonumber(module and module.targetDistance) or -1,
        tonumber(module and module.petFormsOwned) or 0,
        tonumber(module and module.petFormsRequired) or 0)
    for _, quest in ipairs(quests) do
        lines[#lines + 1] = string.format("AQL\t%d\t%d\t%s", tonumber(quest.id) or 0,
            tonumber(quest.state) or 0, protocolText(quest.title or "Quest"))
    end
    for _, pet in ipairs(type(module) == "table" and module.petForms or {}) do
        lines[#lines + 1] = string.format("AQF\t%d\t%d\t%s", tonumber(pet.templateId) or 0,
            pet.missing and 1 or 0, protocolText(pet.name or "Aniimo"))
    end
    for _, habitat in ipairs(type(module) == "table" and module.habitats or {}) do
        lines[#lines + 1] = string.format("AQH\t%d\t%d\t%s\t%s\t%d\t%d\t%d\t%s\t%d",
            tonumber(habitat.templateId) or 0, tonumber(habitat.smallAreaId) or 0,
            protocolText(habitat.areaName or ""), protocolText(habitat.condition or ""),
            tonumber(habitat.markId) or 0, tonumber(habitat.markRealSceneId) or 0,
            tonumber(habitat.teleportSceneId) or 0, protocolText(habitat.markName or ""),
            habitat.areaFallback and 1 or 0)
    end
end
end

Arkhan.registerModule("autoquest", Arkhan.autoquest)

function Arkhan.autoquestFrame(enabled, selectedQuestId, teleportRequest,
        selectedTemplateId, selectedMarkId, selectedMarkRealSceneId)
    local module = Arkhan.modules.autoquest
    if type(module) ~= "table" or type(module.frame) ~= "function" then return false end
    local ok, err = pcall(module.frame, module, enabled, selectedQuestId, teleportRequest,
        selectedTemplateId, selectedMarkId, selectedMarkRealSceneId)
    if not ok then
        module.state = "Unavailable"
        module.lastError = tostring(err)
    end
    return ok == true
end

local function appendEggHasteProtocol(lines)
    lines[#lines + 1] = string.format("H\t%d", EggHaste.liveSpace and 1 or 0)
end

local function buildEspProtocol(controls)
    local width, height = screenSize()
    local lines = { string.format("META\t%.0f\t%.0f\t0", width, height) }
    appendAdaptiveFeatureProtocol(lines)
    appendInterconnectionProtocol(lines)
    appendAutofarmProtocol(lines)
    appendMobVacuumProtocol(lines)
    Arkhan.modules.autoquest:appendProtocol(lines)

    local runtimePg = rawget(_G, "pg")
    local eggTickOk, eggTickErr = pcall(eggHasteTick, controls, runtimePg)
    if not eggTickOk then
        restoreEggControlPatch()
        restoreEggController()
        restoreFastCarryRatio(runtimePg)
        EggHaste.controllingEgg = false
        EggHaste.controllerReady = false
        EggHaste.last = "isolated Egg Haste feature error: " .. protocolText(eggTickErr)
    end
    if eggTickOk then
        local fogOk, fogErr = pcall(eggHasteFogTick, controls, runtimePg)
        if not fogOk then
            restoreEggHasteViewFog(runtimePg)
            restoreEggHasteMapFog()
            EggHaste.last = "isolated Egg Haste fog error: " .. protocolText(fogErr)
        end
    else
        restoreEggHasteViewFog(runtimePg)
        restoreEggHasteMapFog()
    end
    local eggEspActive = eggTickOk and controls.eggHasteEnabled == true and controls.eggHasteEspEnabled == true and EggHaste.liveSpace == true

    if controls.captureAssistant ~= true and controls.espEnabled ~= true and not eggEspActive then
        captureAssistantTick(controls, {}, runtimePg, select(1, loadUtils()))
        appendCaptureAssistantProtocol(lines)
        appendEggHasteProtocol(lines)
        return table.concat(lines, "\n")
    end

    local entityMgr = runtimePg and runtimePg.global and runtimePg.global.entityMgr
    local origin = runtimePg and (runtimePg.pawn or runtimePg.me)
    local originPos = positionOf(origin)
    local Utils = select(1, loadUtils())
    local Const = select(1, requireTable("Common.Const.Const"))
    if entityMgr == nil or originPos == nil or Utils == nil then
        CaptureAssistant.state = "not-ready"
        CaptureAssistant.last = "entity system not ready"
        appendCaptureAssistantProtocol(lines)
        appendEggHasteProtocol(lines)
        lines[1] = string.format("META\t%.0f\t%.0f\t0\tnot-ready", width, height)
        return table.concat(lines, "\n")
    end
    local getAll = safeRead(entityMgr, "getAllEntities")
    local ok, entities = pcall(getAll)
    if not ok then ok, entities = safeCall(getAll, entityMgr) end
    if not ok or type(entities) ~= "table" then
        CaptureAssistant.state = "not-ready"
        CaptureAssistant.last = "entity list unavailable"
        appendCaptureAssistantProtocol(lines)
        appendEggHasteProtocol(lines)
        lines[1] = string.format("META\t%.0f\t%.0f\t0\tentity-list", width, height)
        return table.concat(lines, "\n")
    end

    local espRadius = controls.espEnabled == true and math.max(10, math.min(500, tonumber(controls.espRange) or 150)) or 0
    local captureRadius = controls.captureAssistant == true and math.max(5, math.min(120, tonumber(controls.captureRange) or 50)) or 0
    local eggRadius = eggEspActive and math.max(20, math.min(1000, tonumber(controls.eggHasteEspRange) or 200)) or 0
    local scanRadius = math.max(espRadius, captureRadius, eggRadius)
    local candidates = {}
    local eggCandidates = {}

    for _, entity in pairs(entities) do
        if entity ~= origin and entity ~= runtimePg.me then
            local pos = positionOf(entity)
            if pos then
                local distance = distanceBetween(originPos, pos)
                if distance <= scanRadius then
                    if distance <= math.max(espRadius, captureRadius) then
                        local kind = classify(Utils, origin, runtimePg, entity)
                        local creature = isCreatureKind(kind)
                        if espKindEnabled(kind, controls) or (controls.captureAssistant == true and creature) then
                            local hints = configHints(entity)
                            local name = displayName(runtimePg, entity, hints)
                            if name == nil or name == "" then name = kind end
                            local labelMask = math.max(0, math.floor(tonumber(safeRead(entity, "label")) or 0))
                            candidates[#candidates + 1] = {
                                entity = entity,
                                kind = kind,
                                distance = distance,
                                name = tostring(name),
                                level = tonumber(safeRead(entity, "level")) or 0,
                                labelMask = labelMask,
                                actorId = tostring(captureActorId(entity)),
                                wild = creature and isWildCaptureEntity(Utils, entity, kind) or false
                            }
                        end
                    end

                    if eggEspActive and distance <= eggRadius then
                        local eggKind = eggHasteKind(entity, Utils, Const)
                        if eggKind ~= nil then
                            if eggHasteKindEnabled(eggKind, controls) then
                                local quality = nil
                                local qualityAllowed = true
                                if eggKind == "egg-haste-resource" then
                                    quality = eggHasteQualityForEntity(entity, nil)
                                    qualityAllowed = eggHasteQualityAllowed(controls.eggHasteQualityMask, quality)
                                end
                                if qualityAllowed then
                                    local hints = configHints(entity)
                                    local name = displayName(runtimePg, entity, hints)
                                    if name == nil or name == "" or name == "other" then name = eggHasteFallbackName(eggKind) end
                                    if eggKind == "egg-haste-resource" then
                                        local qualityText = quality and ("[Q" .. tostring(quality) .. " " .. (eggHasteQualityNames[quality] or "") .. "]") or "[Q?]"
                                        name = tostring(name) .. " " .. qualityText
                                    end
                                    eggCandidates[#eggCandidates + 1] = {
                                        entity = entity,
                                        kind = eggKind,
                                        distance = distance,
                                        name = tostring(name),
                                        actorId = tostring(captureActorId(entity)),
                                        quality = quality or 0,
                                        value = ""
                                    }
                                end
                            end
                        end
                    end
                end
            end
        end
    end
    table.sort(candidates, function(a, b) return a.distance < b.distance end)
    table.sort(eggCandidates, function(a, b) return a.distance < b.distance end)

    captureAssistantTick(controls, candidates, runtimePg, Utils)
    appendCaptureAssistantProtocol(lines)
    appendEggHasteProtocol(lines)
    if controls.espEnabled ~= true and not eggEspActive then return table.concat(lines, "\n") end

    local camera = runtimePg and runtimePg.global and runtimePg.global.cameraMgr and safeRead(runtimePg.global.cameraMgr, "worldCameraInst")
    if camera == nil then
        lines[1] = string.format("META\t%.0f\t%.0f\t0\tcamera-not-ready", width, height)
        return table.concat(lines, "\n")
    end

    local itemId = selectedNormalBall(runtimePg, Utils)
    local needHealth = controls.espShowHealthText == true or controls.espShowHealthBar == true
    local needBounds = (tonumber(controls.espBoxMode) or 0) > 0
    local count = 0
    if controls.espEnabled == true then
        for index = 1, math.min(#candidates, 192) do
            local row = candidates[index]
            local allowed = row.distance <= espRadius and espKindEnabled(row.kind, controls)
            if allowed and isCreatureKind(row.kind) then
                allowed = creatureLabelAllowed(row.labelMask, controls) and speciesFilterAllowed(row.name, controls)
                if allowed and (controls.espCatchableOnly == true or controls.espShowCaptureChance == true or (tonumber(controls.espMinCapturePercent) or 0) > 0) then
                    local catchable, chance = ensureCaptureInfo(row, itemId)
                    if controls.espCatchableOnly == true and not catchable then allowed = false end
                    if allowed and (tonumber(controls.espMinCapturePercent) or 0) > 0 and
                       (chance < 0 or chance + 0.0001 < (tonumber(controls.espMinCapturePercent) or 0)) then allowed = false end
                end
            end
            if allowed then
                local worldOk, worldPos = safeCall(safeRead(row.entity, "getPosition"), row.entity)
                if worldOk and worldPos ~= nil then
                    local projectionOk, screenPos = safeCall(safeRead(camera, "WorldToScreenPoint"), camera, worldPos)
                    if projectionOk and screenPos ~= nil then
                        local sx = tonumber(safeRead(screenPos, "x"))
                        local sy = tonumber(safeRead(screenPos, "y"))
                        local sz = tonumber(safeRead(screenPos, "z"))
                        if sx and sy and sz and sz > 0 then
                            local chance = tonumber(row.captureChance) or -1
                            local curHp, maxHp = -1, -1
                            local topX, topY, topZ = -1, -1, -1
                            if isCreatureKind(row.kind) then
                                if needHealth then curHp, maxHp = espHealth(row.entity) end
                                if needBounds then
                                    local topPos = espHeightPosition(row.entity, worldPos)
                                    if topPos ~= nil then
                                        local topOk, topScreen = safeCall(safeRead(camera, "WorldToScreenPoint"), camera, topPos)
                                        if topOk and topScreen ~= nil then
                                            local tx = tonumber(safeRead(topScreen, "x"))
                                            local ty = tonumber(safeRead(topScreen, "y"))
                                            local tz = tonumber(safeRead(topScreen, "z"))
                                            if tx and ty and tz and tz > 0 then
                                                topX, topY, topZ = tx, ty, tz
                                            end
                                        end
                                    end
                                end
                            end
                            if isCreatureKind(row.kind) and (needHealth or needBounds) then
                                lines[#lines + 1] = string.format(
                                    "E\t%s\t%.2f\t%.2f\t%.3f\t%.2f\t%s\t%d\t%d\t%s\t%.2f\t%d\t%.2f\t%.2f\t%.2f\t%.2f\t%.3f",
                                    row.kind, sx, sy, sz, row.distance, protocolText(row.name), row.level, row.labelMask,
                                    protocolText(row.actorId), chance, row.catchable == true and 1 or 0,
                                    curHp, maxHp, topX, topY, topZ)
                            else
                                lines[#lines + 1] = string.format(
                                    "E\t%s\t%.2f\t%.2f\t%.3f\t%.2f\t%s\t%d\t%d\t%s\t%.2f\t%d",
                                    row.kind, sx, sy, sz, row.distance, protocolText(row.name), row.level, row.labelMask,
                                    protocolText(row.actorId), chance, row.catchable == true and 1 or 0)
                            end
                            count = count + 1
                        end
                    end
                end
            end
        end
    end

    if eggEspActive then
        for index = 1, math.min(#eggCandidates, 256) do
            local row = eggCandidates[index]
            local worldOk, worldPos = safeCall(safeRead(row.entity, "getPosition"), row.entity)
            if worldOk and worldPos ~= nil then
                local projectionOk, screenPos = safeCall(safeRead(camera, "WorldToScreenPoint"), camera, worldPos)
                if projectionOk and screenPos ~= nil then
                    local sx = tonumber(safeRead(screenPos, "x"))
                    local sy = tonumber(safeRead(screenPos, "y"))
                    local sz = tonumber(safeRead(screenPos, "z"))
                    if sx and sy and sz and sz > 0 then
                        lines[#lines + 1] = string.format(
                            "E\t%s\t%.2f\t%.2f\t%.3f\t%.2f\t%s\t0\t0\t%s\t-1\t0\t-1\t-1\t-1\t-1\t-1\t%d\t%s",
                            row.kind, sx, sy, sz, row.distance, protocolText(row.name), protocolText(row.actorId),
                            tonumber(row.quality) or 0, protocolText(row.value or ""))
                        count = count + 1
                    end
                end
            end
        end
    end

    lines[1] = string.format("META\t%.0f\t%.0f\t%d", width, height, count)
    return table.concat(lines, "\n")
end

Arkhan._uidVisualOverrideUpdate = (function()
local UidVisualOverride = {
    enabled = false,
    text = "Arkhan",
    lastEnabled = nil,
    lastText = nil,
    lastCopyPlayer = nil,
    originalSetText = nil,
    textSetterHooked = false,
    menuTarget = nil,
    menuControllerHooked = false,
    targets = {}
}
local uidVisualUnpack = table.unpack or unpack

local function uidVisualPack(...)
    return { n = select("#", ...), ... }
end

local function uidVisualCopyArgs(args)
    local copy = { n = args.n }
    for i = 1, args.n do copy[i] = args[i] end
    return copy
end

local function uidVisualSelf()
    local player = safeRead(rawget(_G, "pg"), "me")
    local rawUid = safeRead(player, "uid")
    if rawUid == nil then return nil, player, rawUid end
    local uid = tostring(rawUid)
    if uid == "" then return nil, player, rawUid end
    return uid, player, rawUid
end

local function uidVisualIsCopyPlayer(player)
    local method = safeRead(player, "isFromCopy")
    if type(method) ~= "function" then return false end
    local ok, value = pcall(method, player)
    return ok and value == true
end

local function uidVisualFindMatch(args, uid, isMenuTarget)
    for i = 1, args.n do
        local value = args[i]
        if type(value) == "string" then
            local prefix, digits, suffix = value:match("^(%s*[Uu][Ii][Dd]%s*:%s*)(%d+)(%s*)$")
            if digits == uid then return i, "full", prefix, suffix end
        end
    end
    for i = 1, args.n - 1 do
        local value = args[i]
        if type(value) == "string" and value:match("^%s*[Uu][Ii][Dd]%s*:%s*$") and tostring(args[i + 1]) == uid then
            return i + 1, "split"
        end
    end
    if isMenuTarget then
        for i = 1, args.n do
            if tostring(args[i]) == uid then return i, "plain" end
        end
    end
    return nil
end

local function uidVisualRemember(target, args, index, mode, prefix, suffix)
    if target == nil then return false end
    local entry = nil
    for _, current in ipairs(UidVisualOverride.targets) do
        if current.target == target then entry = current; break end
    end
    if not entry then
        entry = { target = target }
        UidVisualOverride.targets[#UidVisualOverride.targets + 1] = entry
    end
    local nextPrefix = prefix or ""
    local nextSuffix = suffix or ""
    local changed = not entry.args or entry.args.n ~= args.n or entry.index ~= index or entry.mode ~= mode or
                    entry.prefix ~= nextPrefix or entry.suffix ~= nextSuffix
    if not changed then
        for i = 1, args.n do
            if entry.args[i] ~= args[i] then changed = true; break end
        end
    end
    entry.args = uidVisualCopyArgs(args)
    entry.index = index
    entry.mode = mode
    entry.prefix = nextPrefix
    entry.suffix = nextSuffix
    return changed
end

local function uidVisualCaptureKnownTargets()
    local uid, player, rawUid = uidVisualSelf()
    if not uid or uidVisualIsCopyPlayer(player) then return false end
    local changed = false
    local global = safeRead(rawget(_G, "pg"), "global")
    local ui = safeRead(global, "ui")

    local hud = safeRead(ui, "hudV2")
    local hudView = safeRead(hud, "view")
    local hudTarget = safeRead(hudView, "uIDUSDFText")
    if hudTarget ~= nil then
        changed = uidVisualRemember(hudTarget, uidVisualPack("UID: " .. uid), 1, "full", "UID: ", "") or changed
    end

    local menu = safeRead(ui, "funcMenu")
    local menuView = safeRead(menu, "view")
    local menuTarget = safeRead(menuView, "playerUIDUText")
    if menuTarget ~= nil then
        UidVisualOverride.menuTarget = menuTarget
        changed = uidVisualRemember(menuTarget, uidVisualPack(rawUid), 1, "plain") or changed
    end
    return changed
end

local function uidVisualRenderEntry(entry)
    if not UidVisualOverride.originalSetText or not entry or not entry.args then return end
    local args = uidVisualCopyArgs(entry.args)
    if UidVisualOverride.enabled then
        if entry.mode == "full" then
            args[entry.index] = entry.prefix .. UidVisualOverride.text .. entry.suffix
        else
            args[entry.index] = UidVisualOverride.text
        end
    end
    pcall(UidVisualOverride.originalSetText, entry.target, uidVisualUnpack(args, 1, args.n))
end

local function uidVisualApplyCached()
    local _, player = uidVisualSelf()
    if uidVisualIsCopyPlayer(player) then return end
    for _, entry in ipairs(UidVisualOverride.targets) do uidVisualRenderEntry(entry) end
end

local function ensureUidVisualHooks()
    if not UidVisualOverride.textSetterHooked then
        local packageObject = rawget(_G, "package")
        local loadedModules = safeRead(packageObject, "loaded")
        local clientText = rawget(_G, "ClientTextUtils") or safeRead(loadedModules, "Utils.ClientTextUtils")
        local original = safeRead(clientText, "setText")
        if type(clientText) == "table" and type(original) == "function" then
            UidVisualOverride.originalSetText = original
            clientText.setText = function(target, ...)
                local args = uidVisualPack(...)
                local isMenuTarget = target ~= nil and target == UidVisualOverride.menuTarget
                local looksLikeUid = isMenuTarget
                if not looksLikeUid then
                    for i = 1, args.n do
                        local value = args[i]
                        if type(value) == "string" and value:match("^%s*[Uu][Ii][Dd]%s*:") then
                            looksLikeUid = true
                            break
                        end
                    end
                end
                if looksLikeUid then
                    local uid, player = uidVisualSelf()
                    if uid and not uidVisualIsCopyPlayer(player) then
                        local index, mode, prefix, suffix = uidVisualFindMatch(args, uid, isMenuTarget)
                        if index then
                            uidVisualRemember(target, args, index, mode, prefix, suffix)
                            if UidVisualOverride.enabled then
                                if mode == "full" then
                                    args[index] = prefix .. UidVisualOverride.text .. suffix
                                else
                                    args[index] = UidVisualOverride.text
                                end
                            end
                            return original(target, uidVisualUnpack(args, 1, args.n))
                        end
                    end
                end
                return original(target, uidVisualUnpack(args, 1, args.n))
            end
            UidVisualOverride.textSetterHooked = true
        end
    end

    if not UidVisualOverride.menuControllerHooked then
        local packageObject = rawget(_G, "package")
        local loadedModules = safeRead(packageObject, "loaded")
        local controller = rawget(_G, "FuncMenuCtrl") or safeRead(loadedModules, "Guis.Panels.FuncMenu.FuncMenuCtrl")
        local refresh = safeRead(controller, "refreshPlayerInfo")
        if type(controller) == "table" and type(refresh) == "function" then
            controller.refreshPlayerInfo = function(self, ...)
                local view = safeRead(self, "view")
                local target = safeRead(view, "playerUIDUText")
                if target ~= nil then UidVisualOverride.menuTarget = target end
                local results = uidVisualPack(refresh(self, ...))
                return uidVisualUnpack(results, 1, results.n)
            end
            UidVisualOverride.menuControllerHooked = true
        end
    end
end

local function updateUidVisualOverride(enabled, text)
    local setterWasHooked = UidVisualOverride.textSetterHooked
    ensureUidVisualHooks()
    local setterJustHooked = not setterWasHooked and UidVisualOverride.textSetterHooked
    local targetsChanged = uidVisualCaptureKnownTargets()
    local _, player = uidVisualSelf()
    local copyPlayer = uidVisualIsCopyPlayer(player)
    local copyModeChanged = UidVisualOverride.lastCopyPlayer ~= copyPlayer
    text = tostring(text or "Arkhan"):gsub("%c", "")
    local settingsChanged = UidVisualOverride.lastEnabled ~= (enabled == true) or UidVisualOverride.lastText ~= text
    UidVisualOverride.enabled = enabled == true
    UidVisualOverride.text = text
    UidVisualOverride.lastEnabled = UidVisualOverride.enabled
    UidVisualOverride.lastText = text
    UidVisualOverride.lastCopyPlayer = copyPlayer
    if settingsChanged or targetsChanged or setterJustHooked or copyModeChanged then
        uidVisualApplyCached()
    end
end

return updateUidVisualOverride
end)()

function Arkhan.nativeUiFrame(revision, infiniteStamina, noFallDamage, speedBoost, speedMultiplier,
                               autoCollect, collectRange,
                               autoChest, chestRange,
                               captureAssistant, captureAutoThrow, captureFastThrow, captureSkipCameraTransition,
                               captureThrowAnimSpeed, captureBallSpeed, captureRecoveryDelay,
                               captureTargetMode, captureRange, captureMinPercent, captureSpecies,
                               autoDialogueSkip, autoCutsceneSkip,
                               fovOverride, fov,
                               interconnectionGodMode, interconnectionNoCooldown, interconnectionNoCost,
                               interconnectionDamage, interconnectionDamageMultiplier,
                               interconnectionAttackSpeed, interconnectionAttackSpeedMultiplier,
                               interconnectionAttackFurious, interconnectionAttackEnhance, interconnectionAttackHunt,
                               espEnabled, espRange, espCollect, espChest, espEnemy, espCreature, espNpc,
                               espCreatureNormal, espCreatureShiny, espCreatureBoss, espCreatureElite,
                               espCreatureVariant, espCreatureMagic, espCreatureRainbow,
                               espSpeciesFilterMode, espSpeciesFilter, espCatchableOnly, espShowCaptureChance, espMinCapturePercent,
                               eggHasteEnabled, eggHasteEggMovement, eggHasteEggSpeedMultiplier,
                               eggHasteDashBoost, eggHasteDashMultiplier, eggHasteNoDashCooldown,
                               eggHasteFastCarryBoost, eggHasteFastCarryRatio, eggHasteEspEnabled, eggHasteEspRange,
                               eggHasteEspEggs, eggHasteEspNests, eggHasteEspResources, eggHasteEspLoot,
                               eggHasteEspBags, eggHasteEspTransfers, eggHasteEspBreakables,
                               espShowHealthText, espShowHealthBar, espBoxMode, espBoxFill, espSnaplines, espShinyAlert,
                               eggHasteRemoveViewFog, eggHasteRevealFullMap,
                               interconnectionInfiUltimate, interconnectionCaptureBoost,
                               eggHasteQualityMask,
                               hudUidOverrideEnabled, hudUidOverrideText)
    if type(Arkhan._uidVisualOverrideUpdate) == "function" then
        Arkhan._uidVisualOverrideUpdate(hudUidOverrideEnabled == true, hudUidOverrideText)
    end
    local controls = {
        infiniteStamina = infiniteStamina == true, noFallDamage = noFallDamage == true,
        speedBoost = speedBoost == true, speedMultiplier = tonumber(speedMultiplier) or 1.5,
        autoCollect = autoCollect == true, collectRange = tonumber(collectRange) or 30,
        autoChest = autoChest == true, chestRange = tonumber(chestRange) or 18,
        captureAssistant = captureAssistant == true, captureAutoThrow = captureAutoThrow == true,
        captureFastThrow = captureFastThrow == true, captureSkipCameraTransition = captureSkipCameraTransition == true,
        captureThrowAnimSpeed = tonumber(captureThrowAnimSpeed) or 2.0, captureBallSpeed = tonumber(captureBallSpeed) or 2.0,
        captureRecoveryDelay = tonumber(captureRecoveryDelay) or 0.30,
        captureTargetMode = tonumber(captureTargetMode) or 0, captureRange = tonumber(captureRange) or 50,
        captureMinPercent = tonumber(captureMinPercent) or 0, captureSpecies = tostring(captureSpecies or ""),
        autoDialogueSkip = autoDialogueSkip == true, autoCutsceneSkip = autoCutsceneSkip == true,
        fovOverride = fovOverride == true, fov = tonumber(fov) or 80,
        interconnectionGodMode = interconnectionGodMode == true,
        interconnectionNoCooldown = interconnectionNoCooldown == true,
        interconnectionNoCost = interconnectionNoCost == true,
        interconnectionDamage = interconnectionDamage == true,
        interconnectionDamageMultiplier = tonumber(interconnectionDamageMultiplier) or 2,
        interconnectionAttackSpeed = interconnectionAttackSpeed == true,
        interconnectionAttackSpeedMultiplier = tonumber(interconnectionAttackSpeedMultiplier) or 1.5,
        interconnectionAttackFurious = interconnectionAttackFurious == true,
        interconnectionAttackEnhance = interconnectionAttackEnhance == true,
        interconnectionAttackHunt = interconnectionAttackHunt == true,
        interconnectionCaptureBoost = interconnectionCaptureBoost == true,
        interconnectionInfiUltimate = interconnectionInfiUltimate == true,
        espEnabled = espEnabled == true, espRange = tonumber(espRange) or 150,
        espCollect = espCollect == true, espChest = espChest == true, espEnemy = espEnemy == true,
        espCreature = espCreature == true, espNpc = espNpc == true,
        espCreatureNormal = espCreatureNormal == true, espCreatureShiny = espCreatureShiny == true,
        espCreatureBoss = espCreatureBoss == true, espCreatureElite = espCreatureElite == true,
        espCreatureVariant = espCreatureVariant == true, espCreatureMagic = espCreatureMagic == true,
        espCreatureRainbow = espCreatureRainbow == true,
        espSpeciesFilterMode = tonumber(espSpeciesFilterMode) or 0,
        espSpeciesFilter = tostring(espSpeciesFilter or ""),
        espCatchableOnly = espCatchableOnly == true,
        espShowCaptureChance = espShowCaptureChance == true,
        espMinCapturePercent = tonumber(espMinCapturePercent) or 0,
        espShowHealthText = espShowHealthText == true,
        espShowHealthBar = espShowHealthBar == true,
        espBoxMode = math.max(0, math.min(2, math.floor(tonumber(espBoxMode) or 0))),
        espBoxFill = espBoxFill == true,
        espSnaplines = espSnaplines == true,
        espShinyAlert = espShinyAlert == true,
        eggHasteEnabled = eggHasteEnabled == true,
        eggHasteEggMovement = eggHasteEggMovement == true,
        eggHasteEggSpeedMultiplier = tonumber(eggHasteEggSpeedMultiplier) or 1.5,
        eggHasteDashBoost = eggHasteDashBoost == true,
        eggHasteDashMultiplier = tonumber(eggHasteDashMultiplier) or 1.5,
        eggHasteNoDashCooldown = eggHasteNoDashCooldown == true,
        eggHasteFastCarryBoost = eggHasteFastCarryBoost == true,
        eggHasteFastCarryRatio = tonumber(eggHasteFastCarryRatio) or 1.4,
        eggHasteEspEnabled = eggHasteEspEnabled == true,
        eggHasteEspRange = tonumber(eggHasteEspRange) or 200,
        eggHasteEspEggs = eggHasteEspEggs == true,
        eggHasteEspNests = eggHasteEspNests == true,
        eggHasteEspResources = eggHasteEspResources == true,
        eggHasteEspLoot = eggHasteEspLoot == true,
        eggHasteEspBags = eggHasteEspBags == true,
        eggHasteEspTransfers = eggHasteEspTransfers == true,
        eggHasteEspBreakables = eggHasteEspBreakables == true,
        eggHasteRemoveViewFog = eggHasteRemoveViewFog == true,
        eggHasteRevealFullMap = eggHasteRevealFullMap == true,
        eggHasteQualityMask = math.max(0, math.floor(tonumber(eggHasteQualityMask) or 127))
    }
    revision = tonumber(revision) or 0
    if Arkhan.nativeControlRevision ~= revision then
        local module = Arkhan.modules.runtimeFeatures
        if type(module) == "table" and type(module.applyNativeControls) == "function" then
            local ok, result = pcall(module.applyNativeControls, module, controls)
            if not ok or result == false then return "META\t0\t0\t0\tcontrol-apply-failed" end
        end
        Arkhan.nativeControlRevision = revision
    end
    local espProtocol = buildEspProtocol(controls)
    return espProtocol
end


function Arkhan.shutdown()
    local runtimePg = rawget(_G, "pg")
    pcall(restoreEggHasteViewFog, runtimePg)
    pcall(restoreEggHasteMapFog)
    for _, module in pairs(Arkhan.modules) do
        if type(module.shutdown) == "function" then pcall(module.shutdown, module) end
    end
    Arkhan.modules = {}
end

rawset(_G, "__arkhan", Arkhan)
return "ARKHAN_BOOTSTRAP_OK"
