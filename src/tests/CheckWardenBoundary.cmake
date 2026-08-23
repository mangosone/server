# SPDX-License-Identifier: GPL-3.0-or-later
#
# MaNGOS is a full featured server for World of Warcraft, supporting
# the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
#
# Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.

function(read_code PATH OUTPUT)
    # Assertions operate on code, not wording inside comments. This keeps the
    # guard stable while production documentation is expanded.
    file(STRINGS "${PATH}" RAW_LINES)
    set(IN_BLOCK OFF)
    set(CODE_ONLY "")

    foreach(LINE IN LISTS RAW_LINES)
        if(IN_BLOCK)
            string(FIND "${LINE}" "*/" CLOSE_AT)
            if(CLOSE_AT EQUAL -1)
                continue()
            endif()
            math(EXPR CLOSE_AT "${CLOSE_AT} + 2")
            string(SUBSTRING "${LINE}" ${CLOSE_AT} -1 LINE)
            set(IN_BLOCK OFF)
        endif()

        string(REGEX REPLACE "/\\*[^*]*\\*+([^/*][^*]*\\*+)*/" " " LINE "${LINE}")
        string(FIND "${LINE}" "/*" OPEN_AT)
        if(NOT OPEN_AT EQUAL -1)
            string(SUBSTRING "${LINE}" 0 ${OPEN_AT} LINE)
            set(IN_BLOCK ON)
        endif()
        string(REGEX REPLACE "//.*$" "" LINE "${LINE}")
        string(APPEND CODE_ONLY "${LINE}\n")
    endforeach()

    set(${OUTPUT} "${CODE_ONLY}" PARENT_SCOPE)
endfunction()

function(require_count TEXT PATTERN EXPECTED DESCRIPTION)
    string(REGEX MATCHALL "${PATTERN}" MATCHES "${TEXT}")
    list(LENGTH MATCHES COUNT)
    if(NOT COUNT EQUAL EXPECTED)
        message(FATAL_ERROR
            "Warden boundary: ${DESCRIPTION}; expected ${EXPECTED}, found ${COUNT}")
    endif()
endfunction()

set(GAME_ROOT "${SOURCE_ROOT}/src/game")
read_code("${GAME_ROOT}/WorldHandlers/WardenHandler.cpp" WARDEN_HANDLER)
read_code("${GAME_ROOT}/WorldHandlers/World.cpp" WORLD_CPP)
read_code("${GAME_ROOT}/WorldHandlers/WorldSessionMgr.cpp" SESSION_MGR)
read_code("${GAME_ROOT}/WorldHandlers/CharacterHandler.cpp" CHARACTER_HANDLER)
read_code("${GAME_ROOT}/Server/WorldSession.cpp" SESSION_CPP)
read_code("${GAME_ROOT}/Server/WorldSession.h" SESSION_HEADER)
read_code("${GAME_ROOT}/WorldHandlers/WorldConfig.cpp" WORLD_CONFIG)
read_code("${GAME_ROOT}/WorldHandlers/Map.cpp" MAP_CPP)
read_code("${GAME_ROOT}/Server/WardenCheckCatalogLoader.cpp"
    CATALOG_LOADER)
read_code("${GAME_ROOT}/Warden/WardenEnforcementPolicy.cpp" POLICY_CPP)
read_code("${GAME_ROOT}/Warden/WardenEnforcementPolicy.h" POLICY_HEADER)
read_code("${GAME_ROOT}/Warden/WardenServer.cpp" WARDEN_SERVER_CPP)
read_code("${GAME_ROOT}/Warden/WardenServer.h" WARDEN_SERVER_HEADER)
read_code("${SOURCE_ROOT}/src/mangosd/Master.cpp" MASTER_CPP)
file(READ "${SOURCE_ROOT}/cmake/MangosParams.cmake" MANGOS_PARAMS)
file(READ "${SOURCE_ROOT}/src/mangosd/mangosd.conf.dist.in"
    MANGOSD_CONFIG)
file(STRINGS "${SOURCE_ROOT}/src/mangosd/mangosd.conf.dist.in"
    MANGOSD_ACTIVE_EXACT_PROFILE
    REGEX "^[ \\t]*Warden\\.RequireExactProfile[ \\t]*=")

# One grouped outer-opcode handler owns ingress, consumption, and deferred
# teardown; inner commands remain private to WardenServer.
require_count("${WARDEN_HANDLER}" "m_warden->HandleEncrypted[ \\t]*\\(" 1
    "grouped handler must forward ingress exactly once")
require_count("${WARDEN_HANDLER}" "rfinish[ \\t]*\\(" 1
    "grouped handler must consume the packet exactly once")
require_count("${WARDEN_HANDLER}"
    "FinalizeWardenDisengagement[ \\t]*\\(" 1
    "grouped handler must finalize deferred teardown exactly once")
if(WARDEN_HANDLER MATCHES "(^|[^A-Za-z0-9_])switch[ \\t]*\\(")
    message(FATAL_ERROR "Warden boundary: grouped handler contains inner-command dispatch")
endif()

# Charge elapsed time to the state that owned it before a queued reply can
# advance the state machine and replace its deadline.
require_count("${WORLD_CPP}" "UpdateWarden[ \\t]*\\([ \\t]*diff[ \\t]*\\)" 1
    "World::UpdateSessions must own exactly one deadline update")
string(FIND "${WORLD_CPP}" "void World::UpdateSessions(uint32 diff)"
    UPDATE_SESSIONS_BEGIN)
string(FIND "${WORLD_CPP}" "void World::ProcessCliCommands()"
    UPDATE_SESSIONS_END)
if(UPDATE_SESSIONS_BEGIN EQUAL -1 OR UPDATE_SESSIONS_END EQUAL -1 OR
    UPDATE_SESSIONS_END LESS_EQUAL UPDATE_SESSIONS_BEGIN)
    message(FATAL_ERROR
        "Warden boundary: cannot locate World::UpdateSessions body")
endif()
math(EXPR UPDATE_SESSIONS_LENGTH
    "${UPDATE_SESSIONS_END} - ${UPDATE_SESSIONS_BEGIN}")
string(SUBSTRING "${WORLD_CPP}" ${UPDATE_SESSIONS_BEGIN}
    ${UPDATE_SESSIONS_LENGTH} UPDATE_SESSIONS_BODY)
string(FIND "${UPDATE_SESSIONS_BODY}" "pSession->UpdateWarden(diff)"
    WARDEN_CLOCK_AT)
string(FIND "${UPDATE_SESSIONS_BODY}" "pSession->Update(updater)"
    PACKET_UPDATE_AT)
if(WARDEN_CLOCK_AT EQUAL -1 OR PACKET_UPDATE_AT EQUAL -1 OR
    WARDEN_CLOCK_AT GREATER_EQUAL PACKET_UPDATE_AT)
    message(FATAL_ERROR
        "Warden boundary: elapsed time must be charged before packet handlers can reset a deadline")
endif()
require_count("${WORLD_CPP}" "OnAuthenticatedAdmission[ \\t]*\\(" 1
    "immediate AUTH_OK path must admit exactly once")
string(FIND "${WORLD_CPP}" "packet << uint8(AUTH_OK)" AUTH_OK_AT)
string(FIND "${WORLD_CPP}" "s->SendPendingAddonInfo()" ADDON_AT REVERSE)
string(FIND "${WORLD_CPP}" "s->OnAuthenticatedAdmission()" ADMISSION_AT)
if(AUTH_OK_AT EQUAL -1 OR ADDON_AT EQUAL -1 OR ADMISSION_AT EQUAL -1 OR
    ADDON_AT LESS_EQUAL AUTH_OK_AT OR ADMISSION_AT LESS_EQUAL ADDON_AT)
    message(FATAL_ERROR
        "Warden boundary: immediate admission must follow AUTH_OK and addon response")
endif()

require_count("${SESSION_CPP}" "m_warden->Start[ \\t]*\\(" 1
    "session bootstrap seam must own the only direct Start call")
require_count("${SESSION_CPP}"
    "m_warden->Update[ \\t]*\\([ \\t]*eligible[ \\t]*,[ \\t]*diffMs[ \\t]*\\)" 1
    "session update must pass only derived eligibility and elapsed time")
require_count("${SESSION_CPP}" "WardenEvidenceBatch const&" 2
    "session adapter must bind and consume one complete Warden evidence batch")
require_count("${SESSION_CPP}"
    "void WorldSession::HandleWardenEvidenceBatch[ \\t]*\\(" 1
    "complete-batch policy application must have one session owner")
require_count("${SESSION_CPP}"
    "for[ \\t]*\\([ \\t]*warden::WardenEvidence const& evidence[ \\t]*:[ \\t]*batch\\.evidence[ \\t]*\\)" 1
    "session adapter must consume normalized Warden evidence exactly once")
require_count("${SESSION_CPP}" "m_warden->QueueConfirmation[ \\t]*\\(" 1
    "session policy must own one isolated confirmation path")
# Admission must preserve the authenticated string locale independently of the
# numeric DBC fallback and document its fail-open/strict-profile policy.
require_count("${SESSION_CPP}" "IsWardenEnforcementProfile[ \\t]*\\(" 1
    "session enforcement must use the exact-profile predicate")
require_count("${SESSION_CPP}" "ClassifyWardenProfile[ \\t]*\\(" 1
    "session admission must classify the exact-profile policy once")
require_count("${SESSION_CPP}"
    "CONFIG_BOOL_WARDEN_REQUIRE_EXACT_PROFILE" 1
    "session admission must snapshot the strict-profile setting once")
if(SESSION_CPP MATCHES "m_clientPlatform\\.c_str[ \\t]*\\(")
    message(FATAL_ERROR
        "Warden boundary: raw authenticated platform bytes must not reach logs")
endif()
if(SESSION_CPP MATCHES "m_clientLocale\\.c_str[ \\t]*\\(")
    message(FATAL_ERROR
        "Warden boundary: raw authenticated locale bytes must not reach logs")
endif()
require_count("${WORLD_CONFIG}"
    "Warden\\.RequireExactProfile\"[ \\t]*,[ \\t]*true" 1
    "exact-profile admission must default to fail-closed in world configuration")
require_count("${MANGOSD_ACTIVE_EXACT_PROFILE}"
    "Warden\\.RequireExactProfile[ \\t]*=[ \\t]*1" 1
    "distributed exact-profile admission must have one active fail-closed setting")
foreach(EXACT_PROFILE IN ITEMS
    "8606/Win/enUS" "8606/Win/enGB" "8606/Win/deDE" "8606/Win/esES"
    "8606/Win/frFR" "8606/Win/koKR" "8606/Win/ruRU" "8606/Win/zhCN")
    if(MANGOSD_CONFIG MATCHES "${EXACT_PROFILE}")
        message(FATAL_ERROR
            "Warden boundary: distributed config must not hardcode profile ${EXACT_PROFILE}")
    endif()
endforeach()
foreach(REQUIRED_TEXT IN ITEMS
    "repeated, confirmed, actionable Warden check mismatches"
    "Protocol/lifecycle failures close enforcing sessions without an incident"
    "\\(build,platform,locale\\) profile in `warden_checks`"
    "explicitly opt in to unsupported unprofiled clients"
    "Operational failures append a non-incident `warden_audit` row with `check_id = 0`")
    if(NOT MANGOSD_CONFIG MATCHES "${REQUIRED_TEXT}")
        message(FATAL_ERROR
            "Warden boundary: distributed config is missing ${REQUIRED_TEXT}")
    endif()
endforeach()
foreach(FORBIDDEN_TEXT IN ITEMS
    "confirmed Warden memory violations"
    "memory-check profile"
    "memory catalogue")
    if(MANGOSD_CONFIG MATCHES "${FORBIDDEN_TEXT}")
        message(FATAL_ERROR
            "Warden boundary: distributed config contains obsolete wording ${FORBIDDEN_TEXT}")
    endif()
endforeach()
if(NOT MANGOSD_CONFIG MATCHES
    "reject unprofiled clients after authentication as incompatible")
    message(FATAL_ERROR
        "Warden boundary: distributed config must explain opt-in strict-profile rejection")
endif()
if(NOT MANGOSD_CONFIG MATCHES
    "Catalogue loading is mandatory at startup in every enforcement mode")
    message(FATAL_ERROR
        "Warden boundary: distributed config must explain mandatory catalogue startup loading")
endif()
require_count("${MANGOS_PARAMS}"
    "set\\(MANGOS_WORLD_VER[ \\t]+2026082300\\)" 1
    "Warden security-default change must advance the distributed config version")
require_count("${SESSION_CPP}"
    "m_clientLocale[ \\t]*=[ \\t]*std::move[ \\t]*\\([ \\t]*admission\\.clientLocale[ \\t]*\\)" 1
    "session must preserve the unfallbacked client locale exactly once")

require_count("${SESSION_CPP}"
    "admission\\.clientLocale" 1
    "Warden profile selection must use the authenticated exact client locale")
if(SESSION_CPP MATCHES
    "localeNames\\[GetClientLocale[ \\t]*\\([ \\t]*\\)[ \\t]*\\]")
    message(FATAL_ERROR
        "Warden boundary: profile selection must not reconstruct the numeric DBC locale")
endif()
require_count("${SESSION_CPP}" "Warden healthy for player %s" 1
    "stable evidence must have one normal operator health message")
if(NOT SESSION_CPP MATCHES "GetPlayer[ \\t]*\\([ \\t]*\\)" OR
    NOT SESSION_CPP MATCHES "IsInWorld[ \\t]*\\([ \\t]*\\)" OR
    NOT SESSION_CPP MATCHES "m_playerLoading")
    message(FATAL_ERROR
        "Warden boundary: eligibility must require a non-loading player in world")
endif()
if(SESSION_CPP MATCHES "clientTick|checksum|decrypted|packet body")
    message(FATAL_ERROR
        "Warden boundary: session observability must not expose timing internals")
endif()

string(FIND "${SESSION_CPP}" "WorldSession::~WorldSession()"
    SESSION_DESTRUCTOR_BEGIN)
string(FIND "${SESSION_CPP}" "void WorldSession::SizeError("
    SESSION_DESTRUCTOR_END)
if(SESSION_DESTRUCTOR_BEGIN EQUAL -1 OR SESSION_DESTRUCTOR_END EQUAL -1 OR
    SESSION_DESTRUCTOR_END LESS_EQUAL SESSION_DESTRUCTOR_BEGIN)
    message(FATAL_ERROR
        "Warden boundary: cannot locate WorldSession destructor")
endif()
math(EXPR SESSION_DESTRUCTOR_LENGTH
    "${SESSION_DESTRUCTOR_END} - ${SESSION_DESTRUCTOR_BEGIN}")
string(SUBSTRING "${SESSION_CPP}" ${SESSION_DESTRUCTOR_BEGIN}
    ${SESSION_DESTRUCTOR_LENGTH} SESSION_DESTRUCTOR_BODY)
require_count("${SESSION_DESTRUCTOR_BODY}"
    "DrainWardenPendingConfirmations[ \t]*\\(" 1
    "session destruction must drain pending Warden confirmations exactly once")
string(FIND "${SESSION_DESTRUCTOR_BODY}"
    "DrainWardenPendingConfirmations()" DESTRUCTOR_DRAIN_AT)
string(FIND "${SESSION_DESTRUCTOR_BODY}" "m_wardenPolicy.reset()"
    DESTRUCTOR_POLICY_RESET_AT)
if(DESTRUCTOR_DRAIN_AT EQUAL -1 OR DESTRUCTOR_POLICY_RESET_AT EQUAL -1 OR
    DESTRUCTOR_POLICY_RESET_AT LESS_EQUAL DESTRUCTOR_DRAIN_AT)
    message(FATAL_ERROR
        "Warden boundary: session destruction must audit pending confirmations before clearing policy")
endif()

# Lifecycle and enforcement helpers are kept in a fixed ownership order so the
# source-range checks below cannot accidentally inspect unrelated functions.
string(FIND "${SESSION_CPP}"
    "void WorldSession::HandleWardenLifecycle(" LIFECYCLE_BEGIN)
string(FIND "${SESSION_CPP}"
    "void WorldSession::HandleWardenEvidenceBatch(" EVIDENCE_BEGIN)
string(FIND "${SESSION_CPP}"
    "void WorldSession::ApplyWardenPolicyDecisions(" POLICY_APPLY_BEGIN)
string(FIND "${SESSION_CPP}"
    "void WorldSession::DrainWardenPendingConfirmations()" DRAIN_BEGIN)
string(FIND "${SESSION_CPP}"
    "void WorldSession::RequestWardenDisengagement()" DISENGAGE_BEGIN)
string(FIND "${SESSION_CPP}"
    "void WorldSession::FinalizeWardenDisengagement()" FINALIZE_BEGIN)
string(FIND "${SESSION_CPP}"
    "void WorldSession::PersistWardenAudit(" AUDIT_BEGIN)
string(FIND "${SESSION_CPP}"
    "void WorldSession::PersistWardenOperationalAudit("
    OPERATIONAL_AUDIT_BEGIN)
string(FIND "${SESSION_CPP}"
    "void WorldSession::PersistWardenIncidentAndKick(" PERSIST_BEGIN)
string(FIND "${SESSION_CPP}"
    "void WorldSession::StartWardenBootstrap()" WARDEN_START_BEGIN)
if(LIFECYCLE_BEGIN EQUAL -1 OR EVIDENCE_BEGIN EQUAL -1 OR
    POLICY_APPLY_BEGIN EQUAL -1 OR DRAIN_BEGIN EQUAL -1 OR
    DISENGAGE_BEGIN EQUAL -1 OR FINALIZE_BEGIN EQUAL -1 OR
    AUDIT_BEGIN EQUAL -1 OR OPERATIONAL_AUDIT_BEGIN EQUAL -1 OR
    PERSIST_BEGIN EQUAL -1 OR
    WARDEN_START_BEGIN EQUAL -1 OR
    EVIDENCE_BEGIN LESS_EQUAL LIFECYCLE_BEGIN OR
    POLICY_APPLY_BEGIN LESS_EQUAL EVIDENCE_BEGIN OR
    DRAIN_BEGIN LESS_EQUAL POLICY_APPLY_BEGIN OR
    DISENGAGE_BEGIN LESS_EQUAL DRAIN_BEGIN OR
    FINALIZE_BEGIN LESS_EQUAL DISENGAGE_BEGIN OR
    AUDIT_BEGIN LESS_EQUAL FINALIZE_BEGIN OR
    OPERATIONAL_AUDIT_BEGIN LESS_EQUAL AUDIT_BEGIN OR
    PERSIST_BEGIN LESS_EQUAL OPERATIONAL_AUDIT_BEGIN OR
    WARDEN_START_BEGIN LESS_EQUAL PERSIST_BEGIN)
    message(FATAL_ERROR
        "Warden boundary: cannot locate ordered session enforcement helpers")
endif()

string(FIND "${SESSION_HEADER}" "void HandleWardenLifecycle("
    HEADER_LIFECYCLE_BEGIN)
string(FIND "${SESSION_HEADER}" "void StartWardenBootstrap()"
    HEADER_WARDEN_START_BEGIN)
string(FIND "${SESSION_HEADER}" "void UpdateWarden(uint32 diffMs)"
    HEADER_WARDEN_UPDATE_BEGIN)
string(FIND "${SESSION_HEADER}" "void HandleWardenEvidenceBatch("
    HEADER_EVIDENCE_BEGIN)
string(FIND "${SESSION_HEADER}" "void ApplyWardenPolicyDecisions("
    HEADER_POLICY_APPLY_BEGIN)
string(FIND "${SESSION_HEADER}" "void DrainWardenPendingConfirmations()"
    HEADER_DRAIN_BEGIN)
string(FIND "${SESSION_HEADER}" "void RequestWardenDisengagement()"
    HEADER_DISENGAGE_BEGIN)
string(FIND "${SESSION_HEADER}" "void FinalizeWardenDisengagement()"
    HEADER_FINALIZE_BEGIN)
string(FIND "${SESSION_HEADER}" "void PersistWardenAudit("
    HEADER_AUDIT_BEGIN)
string(FIND "${SESSION_HEADER}" "void PersistWardenOperationalAudit("
    HEADER_OPERATIONAL_AUDIT_BEGIN)
string(FIND "${SESSION_HEADER}" "void PersistWardenIncidentAndKick("
    HEADER_PERSIST_BEGIN)
if(HEADER_WARDEN_START_BEGIN EQUAL -1 OR HEADER_WARDEN_UPDATE_BEGIN EQUAL -1 OR
    HEADER_LIFECYCLE_BEGIN EQUAL -1 OR HEADER_EVIDENCE_BEGIN EQUAL -1 OR
    HEADER_POLICY_APPLY_BEGIN EQUAL -1 OR HEADER_DRAIN_BEGIN EQUAL -1 OR
    HEADER_DISENGAGE_BEGIN EQUAL -1 OR
    HEADER_FINALIZE_BEGIN EQUAL -1 OR HEADER_AUDIT_BEGIN EQUAL -1 OR
    HEADER_OPERATIONAL_AUDIT_BEGIN EQUAL -1 OR
    HEADER_PERSIST_BEGIN EQUAL -1 OR
    HEADER_WARDEN_UPDATE_BEGIN LESS_EQUAL HEADER_WARDEN_START_BEGIN OR
    HEADER_LIFECYCLE_BEGIN LESS_EQUAL HEADER_WARDEN_UPDATE_BEGIN OR
    HEADER_EVIDENCE_BEGIN LESS_EQUAL HEADER_LIFECYCLE_BEGIN OR
    HEADER_POLICY_APPLY_BEGIN LESS_EQUAL HEADER_EVIDENCE_BEGIN OR
    HEADER_DRAIN_BEGIN LESS_EQUAL HEADER_POLICY_APPLY_BEGIN OR
    HEADER_DISENGAGE_BEGIN LESS_EQUAL HEADER_DRAIN_BEGIN OR
    HEADER_FINALIZE_BEGIN LESS_EQUAL HEADER_DISENGAGE_BEGIN OR
    HEADER_AUDIT_BEGIN LESS_EQUAL HEADER_FINALIZE_BEGIN OR
    HEADER_OPERATIONAL_AUDIT_BEGIN LESS_EQUAL HEADER_AUDIT_BEGIN OR
    HEADER_PERSIST_BEGIN LESS_EQUAL HEADER_OPERATIONAL_AUDIT_BEGIN)
    message(FATAL_ERROR
        "Warden boundary: session header must preserve enforcement helper order")
endif()

# Terminal operational failures audit first. Observe mode disengages; enforcing
# modes close the link without ever creating a cheating incident or ban.
math(EXPR LIFECYCLE_LENGTH "${EVIDENCE_BEGIN} - ${LIFECYCLE_BEGIN}")
string(SUBSTRING "${SESSION_CPP}" ${LIFECYCLE_BEGIN} ${LIFECYCLE_LENGTH}
    LIFECYCLE_BODY)
if(LIFECYCLE_BODY MATCHES
    "WardenIncidentStore|PersistWardenIncidentAndKick|m_wardenEnforcementClosed")
    message(FATAL_ERROR
        "Warden boundary: lifecycle failure must never persist an incident")
endif()
require_count("${LIFECYCLE_BODY}"
    "PersistWardenOperationalAudit[ \\t]*\\([ \\t]*event\\.failure" 1
    "terminal lifecycle failure must persist one non-punitive operational audit")
require_count("${LIFECYCLE_BODY}"
    "DrainWardenPendingConfirmations[ \\t]*\\(" 1
    "lifecycle failure must drain pending confirmations exactly once")
require_count("${LIFECYCLE_BODY}" "RequestWardenDisengagement[ \\t]*\\(" 1
    "lifecycle failure must request deferred teardown exactly once")
require_count("${LIFECYCLE_BODY}"
    "m_wardenPolicy->EvaluateLifecycle[ \\t]*\\([ \\t]*event[ \\t]*\\)" 1
    "lifecycle failure must classify enforcing-session closure exactly once")
require_count("${LIFECYCLE_BODY}" "KickPlayer[ \\t]*\\(" 1
    "lifecycle failure must contain one enforcing-session close path")
string(FIND "${LIFECYCLE_BODY}"
    "PersistWardenOperationalAudit(event.failure)"
    LIFECYCLE_OPERATIONAL_AUDIT_AT)
string(FIND "${LIFECYCLE_BODY}" "DrainWardenPendingConfirmations()"
    LIFECYCLE_DRAIN_AT)
string(FIND "${LIFECYCLE_BODY}"
    "m_wardenPolicy->EvaluateLifecycle(event)" LIFECYCLE_POLICY_AT)
string(FIND "${LIFECYCLE_BODY}" "RequestWardenDisengagement()"
    LIFECYCLE_DISENGAGE_AT)
string(FIND "${LIFECYCLE_BODY}" "KickPlayer()" LIFECYCLE_KICK_AT)
if(LIFECYCLE_OPERATIONAL_AUDIT_AT EQUAL -1 OR
    LIFECYCLE_DRAIN_AT EQUAL -1 OR LIFECYCLE_POLICY_AT EQUAL -1 OR
    LIFECYCLE_DISENGAGE_AT EQUAL -1 OR LIFECYCLE_KICK_AT EQUAL -1 OR
    LIFECYCLE_DRAIN_AT LESS_EQUAL LIFECYCLE_OPERATIONAL_AUDIT_AT OR
    LIFECYCLE_POLICY_AT LESS_EQUAL LIFECYCLE_DRAIN_AT OR
    LIFECYCLE_DISENGAGE_AT LESS_EQUAL LIFECYCLE_POLICY_AT OR
    LIFECYCLE_KICK_AT LESS_EQUAL LIFECYCLE_DISENGAGE_AT)
    message(FATAL_ERROR
        "Warden boundary: lifecycle failure must audit, classify, tear down, then conditionally close")
endif()

math(EXPR POLICY_APPLY_LENGTH "${DRAIN_BEGIN} - ${POLICY_APPLY_BEGIN}")
string(SUBSTRING "${SESSION_CPP}" ${POLICY_APPLY_BEGIN}
    ${POLICY_APPLY_LENGTH} POLICY_APPLY_BODY)
string(FIND "${POLICY_APPLY_BODY}"
    "case warden::WardenPolicyAction::QueueConfirmation:"
    POLICY_QUEUE_BEGIN)
string(FIND "${POLICY_APPLY_BODY}"
    "case warden::WardenPolicyAction::ConfirmationCleared:"
    POLICY_QUEUE_END)
if(POLICY_QUEUE_BEGIN EQUAL -1 OR POLICY_QUEUE_END EQUAL -1 OR
    POLICY_QUEUE_END LESS_EQUAL POLICY_QUEUE_BEGIN)
    message(FATAL_ERROR
        "Warden boundary: cannot locate queue-confirmation failure path")
endif()
math(EXPR POLICY_QUEUE_LENGTH "${POLICY_QUEUE_END} - ${POLICY_QUEUE_BEGIN}")
string(SUBSTRING "${POLICY_APPLY_BODY}" ${POLICY_QUEUE_BEGIN}
    ${POLICY_QUEUE_LENGTH} POLICY_QUEUE_BODY)
require_count("${POLICY_QUEUE_BODY}"
    "DrainWardenPendingConfirmations[ \\t]*\\(" 1
    "queue-confirmation failure must drain pending audit identities")
require_count("${POLICY_QUEUE_BODY}"
    "m_wardenConfiguration\\.enforcementMode" 1
    "queue-confirmation failure must read the session enforcement mode once")
require_count("${POLICY_QUEUE_BODY}"
    "warden::WardenEnforcementMode::Observe" 1
    "queue-confirmation failure must distinguish Observe mode")
require_count("${POLICY_QUEUE_BODY}" "if[ \\t]*\\([ \\t]*enforcing[ \\t]*\\)" 1
    "queue-confirmation failure must gate the close on enforcing mode")
require_count("${POLICY_QUEUE_BODY}" "RequestWardenDisengagement[ \\t]*\\(" 1
    "queue-confirmation failure must request deferred teardown")
require_count("${POLICY_QUEUE_BODY}" "KickPlayer[ \\t]*\\(" 1
    "queue-confirmation failure must close enforcing sessions")
string(FIND "${POLICY_QUEUE_BODY}" "DrainWardenPendingConfirmations()"
    POLICY_QUEUE_DRAIN_AT)
string(FIND "${POLICY_QUEUE_BODY}" "RequestWardenDisengagement()"
    POLICY_QUEUE_DISENGAGE_AT)
string(FIND "${POLICY_QUEUE_BODY}" "KickPlayer()" POLICY_QUEUE_KICK_AT)
if(POLICY_QUEUE_DRAIN_AT EQUAL -1 OR POLICY_QUEUE_DISENGAGE_AT EQUAL -1 OR
    POLICY_QUEUE_KICK_AT EQUAL -1 OR
    POLICY_QUEUE_DISENGAGE_AT LESS_EQUAL POLICY_QUEUE_DRAIN_AT OR
    POLICY_QUEUE_KICK_AT LESS_EQUAL POLICY_QUEUE_DISENGAGE_AT)
    message(FATAL_ERROR
        "Warden boundary: queue failure must audit, tear down, then conditionally close")
endif()

require_count("${POLICY_HEADER}"
    "(^|[^A-Za-z0-9_])Kick([^A-Za-z0-9_]|$)" 1
    "policy must expose one non-incident session-close action")
require_count("${POLICY_HEADER}" "EvaluateLifecycle" 1
    "policy must expose one lifecycle classification seam")
require_count("${POLICY_CPP}" "WardenPolicyAction::Kick" 2
    "lifecycle and confirmation-contract classification may close enforcing sessions")
string(FIND "${POLICY_CPP}"
    "WardenEnforcementPolicy::AbortPendingConfirmations()" POLICY_ABORT_BEGIN)
string(FIND "${POLICY_CPP}"
    "WardenEnforcementPolicy::EvaluateLifecycle(" POLICY_LIFECYCLE_BEGIN)
string(FIND "${POLICY_CPP}"
    "WardenEnforcementPolicy::ConfirmationContractViolation()"
    POLICY_CONTRACT_BEGIN)
string(FIND "${POLICY_CPP}"
    "uint64 WardenEnforcementPolicy::AuditKey(" POLICY_CONTRACT_END)
if(POLICY_ABORT_BEGIN EQUAL -1 OR POLICY_LIFECYCLE_BEGIN EQUAL -1 OR
    POLICY_CONTRACT_BEGIN EQUAL -1 OR POLICY_CONTRACT_END EQUAL -1 OR
    POLICY_LIFECYCLE_BEGIN LESS_EQUAL POLICY_ABORT_BEGIN OR
    POLICY_CONTRACT_BEGIN LESS_EQUAL POLICY_LIFECYCLE_BEGIN OR
    POLICY_CONTRACT_END LESS_EQUAL POLICY_CONTRACT_BEGIN)
    message(FATAL_ERROR
        "Warden boundary: cannot locate ordered policy failure helpers")
endif()
math(EXPR POLICY_ABORT_LENGTH
    "${POLICY_LIFECYCLE_BEGIN} - ${POLICY_ABORT_BEGIN}")
string(SUBSTRING "${POLICY_CPP}" ${POLICY_ABORT_BEGIN}
    ${POLICY_ABORT_LENGTH} POLICY_ABORT_BODY)
require_count("${POLICY_ABORT_BODY}"
    "WardenPolicyAction::PersistAudit" 1
    "aborted confirmation identities must become audit decisions")
require_count("${POLICY_ABORT_BODY}"
    "WardenCheckOutcome::Unavailable" 1
    "aborted confirmation identities must be classified unavailable")
require_count("${POLICY_ABORT_BODY}"
    "m_pendingConfirmations\\.clear[ \\t]*\\(" 1
    "aborted confirmation identities must be cleared exactly once")
if(POLICY_ABORT_BODY MATCHES
    "PersistAndKick|Disengage|WardenPolicyAction::Kick")
    message(FATAL_ERROR
        "Warden boundary: aborting pending confirmations must only audit")
endif()
math(EXPR POLICY_CONTRACT_LENGTH
    "${POLICY_CONTRACT_END} - ${POLICY_CONTRACT_BEGIN}")
string(SUBSTRING "${POLICY_CPP}" ${POLICY_CONTRACT_BEGIN}
    ${POLICY_CONTRACT_LENGTH} POLICY_CONTRACT_BODY)
string(FIND "${POLICY_CONTRACT_BODY}" "AbortPendingConfirmations()"
    POLICY_CONTRACT_ABORT_AT)
string(FIND "${POLICY_CONTRACT_BODY}" "WardenPolicyAction::Disengage"
    POLICY_CONTRACT_DISENGAGE_AT)
string(FIND "${POLICY_CONTRACT_BODY}" "WardenPolicyAction::Kick"
    POLICY_CONTRACT_KICK_AT)
if(POLICY_CONTRACT_ABORT_AT EQUAL -1 OR
    POLICY_CONTRACT_DISENGAGE_AT EQUAL -1 OR
    POLICY_CONTRACT_KICK_AT EQUAL -1 OR
    POLICY_CONTRACT_DISENGAGE_AT LESS_EQUAL POLICY_CONTRACT_ABORT_AT OR
    POLICY_CONTRACT_KICK_AT LESS_EQUAL POLICY_CONTRACT_ABORT_AT OR
    NOT POLICY_CONTRACT_BODY MATCHES
        "m_mode[ \\t]*==[ \\t]*WardenEnforcementMode::Observe")
    message(FATAL_ERROR
        "Warden boundary: contract failure must audit before mode-specific close or disengagement")
endif()

if(WARDEN_SERVER_HEADER MATCHES "m_transitionedSinceUpdate" OR
    WARDEN_SERVER_CPP MATCHES "m_transitionedSinceUpdate")
    message(FATAL_ERROR
        "Warden boundary: state transitions must not suppress elapsed deadline charging")
endif()

math(EXPR ENFORCEMENT_LENGTH "${WARDEN_START_BEGIN} - ${EVIDENCE_BEGIN}")
string(SUBSTRING "${SESSION_CPP}" ${EVIDENCE_BEGIN} ${ENFORCEMENT_LENGTH}
    ENFORCEMENT_BODY)
if(ENFORCEMENT_BODY MATCHES
    "GetSecurity[ \\t]*\\(|SEC_[A-Z_]+|gmlevel|GameMaster")
    message(FATAL_ERROR
        "Warden boundary: enforcement must not exempt privileged accounts")
endif()
if(NOT ENFORCEMENT_BODY MATCHES "CheckPlanPurpose::Initial" OR
    NOT ENFORCEMENT_BODY MATCHES "DEBUG_LOG[ \\t]*\\(")
    message(FATAL_ERROR
        "Warden boundary: recurring clean evidence must use debug logging")
endif()

math(EXPR AUDIT_LENGTH "${PERSIST_BEGIN} - ${AUDIT_BEGIN}")
string(SUBSTRING "${SESSION_CPP}" ${AUDIT_BEGIN} ${AUDIT_LENGTH}
    AUDIT_BODY)
if(AUDIT_BODY MATCHES
    "KickPlayer|WardenIncidentStore|account_banned|m_wardenAggressive|m_wardenEnforcementClosed")
    message(FATAL_ERROR
        "Warden boundary: audit persistence must never enforce or alter escalation")
endif()

math(EXPR PERSIST_LENGTH "${WARDEN_START_BEGIN} - ${PERSIST_BEGIN}")
string(SUBSTRING "${SESSION_CPP}" ${PERSIST_BEGIN} ${PERSIST_LENGTH}
    PERSIST_BODY)
string(FIND "${PERSIST_BODY}" "DrainWardenPendingConfirmations()"
    INCIDENT_DRAIN_AT)
string(FIND "${PERSIST_BODY}" "WardenIncidentStore::Instance().Record"
    INCIDENT_RECORD_AT)
string(FIND "${PERSIST_BODY}" "KickPlayer()" INCIDENT_KICK_AT)
if(INCIDENT_DRAIN_AT EQUAL -1 OR INCIDENT_RECORD_AT EQUAL -1 OR
    INCIDENT_KICK_AT EQUAL -1 OR
    INCIDENT_RECORD_AT LESS_EQUAL INCIDENT_DRAIN_AT OR
    INCIDENT_KICK_AT LESS_EQUAL INCIDENT_RECORD_AT)
    message(FATAL_ERROR
        "Warden boundary: confirmed incident must drain other confirmations, persist, then close")
endif()
require_count("${PERSIST_BODY}"
    "DrainWardenPendingConfirmations[ \\t]*\\(" 1
    "terminal incident must audit every other pending confirmation")
require_count("${PERSIST_BODY}" "KickPlayer[ \\t]*\\(" 1
    "confirmed violation must request one idempotent link close")

string(FIND "${SESSION_CPP}" "void WorldSession::UpdateWarden("
    WARDEN_UPDATE_BEGIN)
string(FIND "${SESSION_CPP}" "void WorldSession::QueuePacket("
    WARDEN_UPDATE_END)
if(WARDEN_UPDATE_BEGIN EQUAL -1 OR WARDEN_UPDATE_END EQUAL -1 OR
    WARDEN_UPDATE_BEGIN LESS_EQUAL WARDEN_START_BEGIN OR
    WARDEN_UPDATE_END LESS_EQUAL WARDEN_UPDATE_BEGIN)
    message(FATAL_ERROR
        "Warden boundary: cannot locate ordered Warden session wrappers")
endif()
math(EXPR WARDEN_START_LENGTH
    "${WARDEN_UPDATE_BEGIN} - ${WARDEN_START_BEGIN}")
string(SUBSTRING "${SESSION_CPP}" ${WARDEN_START_BEGIN}
    ${WARDEN_START_LENGTH} WARDEN_START_BODY)
string(FIND "${WARDEN_START_BODY}" "m_warden->Start()"
    WARDEN_START_CALL_AT)
string(FIND "${WARDEN_START_BODY}" "FinalizeWardenDisengagement()"
    WARDEN_START_FINALIZE_AT)
if(WARDEN_START_CALL_AT EQUAL -1 OR WARDEN_START_FINALIZE_AT EQUAL -1 OR
    WARDEN_START_FINALIZE_AT LESS_EQUAL WARDEN_START_CALL_AT)
    message(FATAL_ERROR
        "Warden boundary: bootstrap wrapper must finalize teardown after Start returns")
endif()
require_count("${WARDEN_START_BODY}" "FinalizeWardenDisengagement[ \\t]*\\(" 1
    "bootstrap wrapper must finalize deferred teardown exactly once")
math(EXPR WARDEN_UPDATE_LENGTH
    "${WARDEN_UPDATE_END} - ${WARDEN_UPDATE_BEGIN}")
string(SUBSTRING "${SESSION_CPP}" ${WARDEN_UPDATE_BEGIN}
    ${WARDEN_UPDATE_LENGTH} WARDEN_UPDATE_BODY)
string(FIND "${WARDEN_UPDATE_BODY}"
    "m_warden->Update(eligible, diffMs)" WARDEN_UPDATE_CALL_AT)
string(FIND "${WARDEN_UPDATE_BODY}"
    "!m_link || m_link->IsClosed()" WARDEN_LINK_GUARD_AT)
string(FIND "${WARDEN_UPDATE_BODY}" "FinalizeWardenDisengagement()"
    WARDEN_UPDATE_FINALIZE_AT)
if(WARDEN_LINK_GUARD_AT EQUAL -1 OR WARDEN_UPDATE_CALL_AT EQUAL -1 OR
    WARDEN_UPDATE_FINALIZE_AT EQUAL -1 OR
    WARDEN_UPDATE_CALL_AT LESS_EQUAL WARDEN_LINK_GUARD_AT OR
    WARDEN_UPDATE_FINALIZE_AT LESS_EQUAL WARDEN_UPDATE_CALL_AT)
    message(FATAL_ERROR
        "Warden boundary: update wrapper must reject closed links before updating and finalizing")
endif()
require_count("${WARDEN_UPDATE_BODY}" "FinalizeWardenDisengagement[ \\t]*\\(" 1
    "update wrapper must finalize deferred teardown exactly once")

string(FIND "${SESSION_CPP}" "void WorldSession::OnAuthenticatedAdmission()"
    SESSION_ADMISSION_BEGIN)
string(FIND "${SESSION_CPP}" "void WorldSession::HandleWardenLifecycle("
    SESSION_ADMISSION_END)
if(SESSION_ADMISSION_BEGIN EQUAL -1 OR SESSION_ADMISSION_END EQUAL -1 OR
    SESSION_ADMISSION_END LESS_EQUAL SESSION_ADMISSION_BEGIN)
    message(FATAL_ERROR
        "Warden boundary: cannot locate authenticated admission body")
endif()
math(EXPR SESSION_ADMISSION_LENGTH
    "${SESSION_ADMISSION_END} - ${SESSION_ADMISSION_BEGIN}")
string(SUBSTRING "${SESSION_CPP}" ${SESSION_ADMISSION_BEGIN}
    ${SESSION_ADMISSION_LENGTH} SESSION_ADMISSION_BODY)
if(SESSION_ADMISSION_BODY MATCHES
    "StartWardenBootstrap[ \\t]*\\(|m_warden->Start[ \\t]*\\(")
    message(FATAL_ERROR
        "Warden boundary: authenticated admission must provision without emitting")
endif()
string(FIND "${SESSION_ADMISSION_BODY}"
    "WardenProfileDisposition::Reject" PROFILE_REJECT_AT)
string(FIND "${SESSION_ADMISSION_BODY}"
    "if (!exactProfile)" PROFILE_UNAVAILABLE_AT)
string(FIND "${SESSION_ADMISSION_BODY}"
    "WardenManager::Instance().Create" WARDEN_CREATE_AT)
if(PROFILE_REJECT_AT EQUAL -1 OR PROFILE_UNAVAILABLE_AT EQUAL -1 OR
    WARDEN_CREATE_AT EQUAL -1 OR
    PROFILE_UNAVAILABLE_AT LESS_EQUAL PROFILE_REJECT_AT OR
    WARDEN_CREATE_AT LESS_EQUAL PROFILE_UNAVAILABLE_AT)
    message(FATAL_ERROR
        "Warden boundary: strict unprofiled rejection must precede Warden creation")
endif()
math(EXPR PROFILE_REJECT_LENGTH
    "${PROFILE_UNAVAILABLE_AT} - ${PROFILE_REJECT_AT}")
string(SUBSTRING "${SESSION_ADMISSION_BODY}" ${PROFILE_REJECT_AT}
    ${PROFILE_REJECT_LENGTH} PROFILE_REJECT_BODY)
string(FIND "${PROFILE_REJECT_BODY}" "admission.Clear()" REJECT_CLEAR_AT)
string(FIND "${PROFILE_REJECT_BODY}" "KickPlayer()" REJECT_KICK_AT)
string(FIND "${PROFILE_REJECT_BODY}" "return;" REJECT_RETURN_AT)
if(REJECT_CLEAR_AT EQUAL -1 OR REJECT_KICK_AT EQUAL -1 OR
    REJECT_RETURN_AT EQUAL -1 OR
    REJECT_KICK_AT LESS_EQUAL REJECT_CLEAR_AT OR
    REJECT_RETURN_AT LESS_EQUAL REJECT_KICK_AT)
    message(FATAL_ERROR
        "Warden boundary: strict rejection must cleanse, close, and terminate admission in order")
endif()
require_count("${SESSION_ADMISSION_BODY}" "KickPlayer[ \\t]*\\(" 2
    "authenticated admission must contain strict-profile and creation-failure closes")
require_count("${PROFILE_REJECT_BODY}" "KickPlayer[ \\t]*\\(" 1
    "strict-profile rejection must own the admission kick")
if(SESSION_ADMISSION_BODY MATCHES
    "WardenIncidentStore::Instance[ \\t]*\\(\\)[ \\t]*\\.[ \\t]*Record")
    message(FATAL_ERROR
        "Warden boundary: admission rejection must never record an incident")
endif()

string(FIND "${SESSION_ADMISSION_BODY}" "if (!server)" NULL_SERVER_BEGIN)
string(FIND "${SESSION_ADMISSION_BODY}"
    "m_wardenPolicy = std::make_unique" NULL_SERVER_END)
if(NULL_SERVER_BEGIN EQUAL -1 OR NULL_SERVER_END EQUAL -1 OR
    NULL_SERVER_END LESS_EQUAL NULL_SERVER_BEGIN)
    message(FATAL_ERROR
        "Warden boundary: cannot locate exact-profile creation failure branch")
endif()
math(EXPR NULL_SERVER_LENGTH "${NULL_SERVER_END} - ${NULL_SERVER_BEGIN}")
string(SUBSTRING "${SESSION_ADMISSION_BODY}" ${NULL_SERVER_BEGIN}
    ${NULL_SERVER_LENGTH} NULL_SERVER_BODY)
if(NOT NULL_SERVER_BODY MATCHES "sLog\\.outError[ \\t]*\\(" OR
    NOT NULL_SERVER_BODY MATCHES "return[ \\t]*;" OR
    NULL_SERVER_BODY MATCHES "WardenIncidentStore|BanAccount")
    message(FATAL_ERROR
        "Warden boundary: creation failure must log without recording an incident or ban")
endif()
require_count("${NULL_SERVER_BODY}"
    "disposition[ \\t]*==[ \\t]*warden::WardenProfileDisposition::Enforce" 1
    "exact-profile creation failure must distinguish enforcing sessions")
require_count("${NULL_SERVER_BODY}" "KickPlayer[ \\t]*\\(" 1
    "exact-profile creation failure must close enforcing sessions")

require_count("${CHARACTER_HANDLER}" "StartWardenBootstrap[ \\t]*\\(" 2
    "character list and player login must each schedule bootstrap once")

string(FIND "${CHARACTER_HANDLER}"
    "void WorldSession::HandleCharEnum(QueryResult* result)" CHAR_ENUM_BEGIN)
string(FIND "${CHARACTER_HANDLER}"
    "void WorldSession::HandleCharEnumOpcode" CHAR_ENUM_END)
if(CHAR_ENUM_BEGIN EQUAL -1 OR CHAR_ENUM_END EQUAL -1 OR
    CHAR_ENUM_END LESS_EQUAL CHAR_ENUM_BEGIN)
    message(FATAL_ERROR "Warden boundary: cannot locate character-enum body")
endif()
math(EXPR CHAR_ENUM_LENGTH "${CHAR_ENUM_END} - ${CHAR_ENUM_BEGIN}")
string(SUBSTRING "${CHARACTER_HANDLER}" ${CHAR_ENUM_BEGIN}
    ${CHAR_ENUM_LENGTH} CHAR_ENUM_BODY)
require_count("${CHAR_ENUM_BODY}" "StartWardenBootstrap[ \\t]*\\(" 1
    "character-enum completion must schedule bootstrap exactly once")
string(FIND "${CHAR_ENUM_BODY}" "SendPacket(&data)" CHAR_LIST_SEND_AT)
string(FIND "${CHAR_ENUM_BODY}" "StartWardenBootstrap()" CHAR_ENUM_START_AT)
if(CHAR_LIST_SEND_AT EQUAL -1 OR CHAR_ENUM_START_AT EQUAL -1 OR
    CHAR_ENUM_START_AT LESS_EQUAL CHAR_LIST_SEND_AT)
    message(FATAL_ERROR
        "Warden boundary: character-list send must precede bootstrap emission")
endif()

string(FIND "${CHARACTER_HANDLER}"
    "void WorldSession::HandlePlayerLoginOpcode" PLAYER_LOGIN_BEGIN)
string(FIND "${CHARACTER_HANDLER}"
    "void WorldSession::HandlePlayerLogin(LoginQueryHolder* holder)"
    PLAYER_LOGIN_END)
if(PLAYER_LOGIN_BEGIN EQUAL -1 OR PLAYER_LOGIN_END EQUAL -1 OR
    PLAYER_LOGIN_END LESS_EQUAL PLAYER_LOGIN_BEGIN)
    message(FATAL_ERROR "Warden boundary: cannot locate player-login opcode body")
endif()
math(EXPR PLAYER_LOGIN_LENGTH "${PLAYER_LOGIN_END} - ${PLAYER_LOGIN_BEGIN}")
string(SUBSTRING "${CHARACTER_HANDLER}" ${PLAYER_LOGIN_BEGIN}
    ${PLAYER_LOGIN_LENGTH} PLAYER_LOGIN_BODY)
require_count("${PLAYER_LOGIN_BODY}" "StartWardenBootstrap[ \\t]*\\(" 1
    "player-login path must retain one non-gating bootstrap safety net")
string(FIND "${PLAYER_LOGIN_BODY}" "PlayerLoading()" PLAYER_LOGIN_GUARD_AT)
string(FIND "${PLAYER_LOGIN_BODY}" "StartWardenBootstrap()"
    PLAYER_LOGIN_START_AT)
string(FIND "${PLAYER_LOGIN_BODY}" "m_playerLoading = true"
    PLAYER_LOADING_SET_AT)
if(PLAYER_LOGIN_GUARD_AT EQUAL -1 OR PLAYER_LOGIN_START_AT EQUAL -1 OR
    PLAYER_LOADING_SET_AT EQUAL -1 OR
    PLAYER_LOGIN_START_AT LESS_EQUAL PLAYER_LOGIN_GUARD_AT OR
    PLAYER_LOGIN_START_AT GREATER_EQUAL PLAYER_LOADING_SET_AT)
    message(FATAL_ERROR
        "Warden boundary: login safety net must follow the duplicate guard and never gate loading")
endif()

string(FIND "${SESSION_MGR}" "void World::AddQueuedSession" ADD_QUEUE_BEGIN)
string(FIND "${SESSION_MGR}" "bool World::RemoveQueuedSession" ADD_QUEUE_END)
if(ADD_QUEUE_BEGIN EQUAL -1 OR ADD_QUEUE_END EQUAL -1 OR
    ADD_QUEUE_END LESS_EQUAL ADD_QUEUE_BEGIN)
    message(FATAL_ERROR "Warden boundary: cannot locate AddQueuedSession body")
endif()
math(EXPR ADD_QUEUE_LENGTH "${ADD_QUEUE_END} - ${ADD_QUEUE_BEGIN}")
string(SUBSTRING "${SESSION_MGR}" ${ADD_QUEUE_BEGIN} ${ADD_QUEUE_LENGTH}
    ADD_QUEUE_BODY)
if(ADD_QUEUE_BODY MATCHES "OnAuthenticatedAdmission")
    message(FATAL_ERROR "Warden boundary: queued sessions must not start Warden")
endif()
require_count("${SESSION_MGR}" "OnAuthenticatedAdmission[ \\t]*\\(" 1
    "queue release path must admit exactly once")
string(FIND "${SESSION_MGR}" "pop_sess->SendAuthWaitQue(0)" QUEUE_OK_AT)
string(FIND "${SESSION_MGR}" "pop_sess->OnAuthenticatedAdmission()"
    QUEUE_ADMISSION_AT)
if(QUEUE_OK_AT EQUAL -1 OR QUEUE_ADMISSION_AT EQUAL -1 OR
    QUEUE_ADMISSION_AT LESS_EQUAL QUEUE_OK_AT)
    message(FATAL_ERROR
        "Warden boundary: queue release admission must follow AUTH_OK")
endif()

if(MAP_CPP MATCHES "(^|[^A-Za-z0-9_])Warden([^A-Za-z0-9_]|$)")
    message(FATAL_ERROR "Warden boundary: Map.cpp must not own Warden updates")
endif()

require_count("${CATALOG_LOADER}"
    "SELECT COUNT\\(\\*\\) FROM `warden_checks`" 1
    "catalogue loader must perform one explicit emptiness query")
foreach(BINARY_FIELD IN ITEMS platform locale module request expected)
    require_count("${CATALOG_LOADER}"
        "HEX\\(`${BINARY_FIELD}`\\)" 1
        "catalogue loader must project ${BINARY_FIELD} through HEX exactly once")
endforeach()
if(CATALOG_LOADER MATCHES "GetCppString[ \\t]*\\(")
    message(FATAL_ERROR
        "Warden boundary: catalogue loader must not read binary SQL fields as C++ strings")
endif()
require_count("${MASTER_CPP}"
    "WardenCheckCatalogLoader[ \\t]*\\([ \\t]*\\)[ \\t]*\\.LoadAndPublish[ \\t]*\\(" 1
    "mangosd must publish the required catalogue exactly once")
string(FIND "${MASTER_CPP}" "ClearOnlineAccounts();" MASTER_CLEAR_AT)
string(FIND "${MASTER_CPP}"
    "WardenCheckCatalogLoader().LoadAndPublish()" MASTER_WARDEN_AT)
string(FIND "${MASTER_CPP}" "sWorld.SetInitialWorldSettings()" MASTER_WORLD_AT)
if(MASTER_CLEAR_AT EQUAL -1 OR MASTER_WARDEN_AT EQUAL -1 OR
    MASTER_WORLD_AT EQUAL -1 OR MASTER_WARDEN_AT LESS_EQUAL MASTER_CLEAR_AT OR
    MASTER_WORLD_AT LESS_EQUAL MASTER_WARDEN_AT)
    message(FATAL_ERROR
        "Warden boundary: catalogue publication must precede world initialization")
endif()

file(GLOB WARDEN_SOURCES
    "${GAME_ROOT}/Warden/*.h" "${GAME_ROOT}/Warden/*.hpp"
    "${GAME_ROOT}/Warden/*.cpp" "${GAME_ROOT}/Warden/*.cc"
    "${GAME_ROOT}/Server/Warden*.h" "${GAME_ROOT}/Server/Warden*.hpp"
    "${GAME_ROOT}/Server/Warden*.cpp" "${GAME_ROOT}/Server/Warden*.cc")
set(FORBIDDEN
    "LoginDatabase" "CharacterDatabase" "WorldDatabase" "KickPlayer"
    "BanAccount" "ByteArrayToHexStr" "hexlike[ \\t]*\\(")
set(FORBIDDEN_CHECK_CONTENT
    "DBFILESCLIENT" "AREATABLE\\.DBC" "OKAY" "WOW\\.EXE"
    "444246696C6573436C69656E745C417265615461626C652E646263"
    "576F572E657865"
    "D65D59D2E57792A13E8EDF78A574B8F81D0D3CF0"
    "002ECF12A5B297DACE7955E5971E4F68BF8A8DAB"
    "DBE1DBADE50F3A616B48F8909D3C8EF75C0D9220"
    "C7C50539F79BD77E28A6328CB13FCFFA596F1F7B"
    "7AD1756C8A4BD449698A98FDC58775EE2F0FBD6E"
    "FF75C74BDCF685D3ED6A7E00DCAF5DA54E75F436"
    "3D71F5D1E2BB4147FD6E2587C0D7E0167180DAC0"
    "4F4B4159" "4F6B6179" "4F4B" "41636570746172"
    "ED9995EC9DB8" "D09ED09A" "E7A1AEE5AE9A"
    "B9EC18E100E88687F7FFE821FBFFFF68CCDDBB00B9B3120000BA18CBBB00E8BDFCFFFFA3D018E100"
    "2952208" "0X002D0C10"
    "0X44[ \\t]*,[ \\t]*0X42[ \\t]*,[ \\t]*0X46[ \\t]*,[ \\t]*0X69"
    "0X57[ \\t]*,[ \\t]*0X6F[ \\t]*,[ \\t]*0X57[ \\t]*,[ \\t]*0X2E"
    "0X4F[ \\t]*,[ \\t]*0X4B[ \\t]*,[ \\t]*0X41[ \\t]*,[ \\t]*0X59"
    "0X4F[ \\t]*,[ \\t]*0X6B[ \\t]*,[ \\t]*0X61[ \\t]*,[ \\t]*0X79"
    "0XED[ \\t]*,[ \\t]*0X99[ \\t]*,[ \\t]*0X95[ \\t]*,[ \\t]*0XEC"
    "0XD0[ \\t]*,[ \\t]*0X9E[ \\t]*,[ \\t]*0XD0[ \\t]*,[ \\t]*0X9A"
    "0XE7[ \\t]*,[ \\t]*0XA1[ \\t]*,[ \\t]*0XAE[ \\t]*,[ \\t]*0XE5"
    "0XB9[ \\t]*,[ \\t]*0XEC[ \\t]*,[ \\t]*0X18[ \\t]*,[ \\t]*0XE1")
foreach(SOURCE IN LISTS WARDEN_SOURCES)
    read_code("${SOURCE}" WARDEN_CODE)
    string(FIND "${SOURCE}" "${GAME_ROOT}/Warden/" WARDEN_LAYER_AT)
    if(WARDEN_LAYER_AT EQUAL 0)
        foreach(PATTERN IN LISTS FORBIDDEN)
            if(WARDEN_CODE MATCHES "${PATTERN}")
                message(FATAL_ERROR
                    "Warden boundary: forbidden production dependency ${PATTERN} in ${SOURCE}")
            endif()
        endforeach()
    endif()
    if(NOT SOURCE MATCHES "WardenModuleWinData\\.cpp$")
        string(TOUPPER "${WARDEN_CODE}" WARDEN_CODE_UPPER)
        foreach(PATTERN IN LISTS FORBIDDEN_CHECK_CONTENT)
            if(WARDEN_CODE_UPPER MATCHES "${PATTERN}")
                message(FATAL_ERROR
                    "Warden boundary: active check content ${PATTERN} in ${SOURCE}")
            endif()
        endforeach()
    endif()
endforeach()

file(GLOB_RECURSE INCIDENT_STORE_FILES
    "${GAME_ROOT}/*WardenIncidentStore.h"
    "${GAME_ROOT}/*WardenIncidentStore.cpp")
list(LENGTH INCIDENT_STORE_FILES INCIDENT_STORE_COUNT)
if(NOT INCIDENT_STORE_COUNT EQUAL 2)
    message(FATAL_ERROR
        "Warden boundary: expected one incident-store header/source pair")
endif()
foreach(SOURCE IN LISTS INCIDENT_STORE_FILES)
    string(FIND "${SOURCE}" "${GAME_ROOT}/Server/" SERVER_PREFIX_AT)
    if(NOT SERVER_PREFIX_AT EQUAL 0)
        message(FATAL_ERROR
            "Warden boundary: incident store escaped src/game/Server: ${SOURCE}")
    endif()
endforeach()

file(GLOB_RECURSE CATALOG_LOADER_FILES
    "${GAME_ROOT}/*WardenCheckCatalogLoader.h"
    "${GAME_ROOT}/*WardenCheckCatalogLoader.cpp")
list(LENGTH CATALOG_LOADER_FILES CATALOG_LOADER_COUNT)
if(NOT CATALOG_LOADER_COUNT EQUAL 2)
    message(FATAL_ERROR
        "Warden boundary: expected one catalogue-loader header/source pair")
endif()
foreach(SOURCE IN LISTS CATALOG_LOADER_FILES)
    string(FIND "${SOURCE}" "${GAME_ROOT}/Server/" SERVER_PREFIX_AT)
    if(NOT SERVER_PREFIX_AT EQUAL 0)
        message(FATAL_ERROR
            "Warden boundary: catalogue loader escaped src/game/Server: ${SOURCE}")
    endif()
endforeach()

file(GLOB_RECURSE AUDIT_STORE_FILES
    "${GAME_ROOT}/*WardenAuditStore.h"
    "${GAME_ROOT}/*WardenAuditStore.cpp")
list(LENGTH AUDIT_STORE_FILES AUDIT_STORE_COUNT)
if(NOT AUDIT_STORE_COUNT EQUAL 2)
    message(FATAL_ERROR
        "Warden boundary: expected one audit-store header/source pair")
endif()
foreach(SOURCE IN LISTS AUDIT_STORE_FILES)
    string(FIND "${SOURCE}" "${GAME_ROOT}/Server/" SERVER_PREFIX_AT)
    if(NOT SERVER_PREFIX_AT EQUAL 0)
        message(FATAL_ERROR
            "Warden boundary: audit store escaped src/game/Server: ${SOURCE}")
    endif()
endforeach()

message(STATUS "Warden session boundary intact")
