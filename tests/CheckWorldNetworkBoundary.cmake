set(REQUIRED_FILES
  "${SOURCE_ROOT}/src/game/Server/WorldGateway.h"
  "${SOURCE_ROOT}/src/game/Server/WorldGateway.cpp"
  "${SOURCE_ROOT}/src/game/Server/SessionMailbox.h"
  "${SOURCE_ROOT}/src/game/Server/SessionMailbox.cpp"
  "${SOURCE_ROOT}/src/game/Server/WorldNetwork.h"
  "${SOURCE_ROOT}/src/game/Server/WorldNetwork.cpp")

string(CONCAT OLD_SOCKET_NAME "World" "Socket")
string(CONCAT OLD_SOCKET_MANAGER_NAME "World" "Socket" "Mgr")
string(CONCAT OLD_LEASE_NAME "Leased" "Ptr")
set(REMOVED_FILES
  "${SOURCE_ROOT}/src/game/Server/${OLD_SOCKET_NAME}.h"
  "${SOURCE_ROOT}/src/game/Server/${OLD_SOCKET_NAME}.cpp"
  "${SOURCE_ROOT}/src/game/Server/${OLD_SOCKET_MANAGER_NAME}.h"
  "${SOURCE_ROOT}/src/game/Server/${OLD_SOCKET_MANAGER_NAME}.cpp"
  "${SOURCE_ROOT}/src/shared/Threading/${OLD_LEASE_NAME}.h")

foreach(FILE_PATH IN LISTS REQUIRED_FILES)
  if(NOT EXISTS "${FILE_PATH}")
    message(FATAL_ERROR "Required decoupling file is missing: ${FILE_PATH}")
  endif()
endforeach()

foreach(FILE_PATH IN LISTS REMOVED_FILES)
  if(EXISTS "${FILE_PATH}")
    message(FATAL_ERROR "Obsolete coupled file still exists: ${FILE_PATH}")
  endif()
endforeach()

file(READ "${SOURCE_ROOT}/src/game/Server/WorldGateway.cpp" WORLD_GATEWAY_SOURCE)
foreach(REQUIRED_TRANSACTION_STEP IN ITEMS
    "WorldSession* const publishedSession = session.release()"
    "session.reset(publishedSession)"
    "Detach(sessionId)")
  string(FIND "${WORLD_GATEWAY_SOURCE}" "${REQUIRED_TRANSACTION_STEP}" STEP_POSITION)
  if(STEP_POSITION EQUAL -1)
    message(FATAL_ERROR
      "WorldGateway session publication is missing rollback step: ${REQUIRED_TRANSACTION_STEP}")
  endif()
endforeach()

string(FIND "${WORLD_GATEWAY_SOURCE}" "link->SendPacket(addonResponse)" EARLY_ADDON_SEND)
if(NOT EARLY_ADDON_SEND EQUAL -1)
  message(FATAL_ERROR "WorldGateway sends addon info before the world-thread auth response")
endif()

file(READ "${SOURCE_ROOT}/src/game/WorldHandlers/World.cpp" WORLD_SOURCE)
foreach(REQUIRED_AUTH_ORDER IN ITEMS
    "AddQueuedSession(s);\n        s->SendPendingAddonInfo();"
    "s->SendPacket(&packet);\n    s->SendPendingAddonInfo();")
  string(FIND "${WORLD_SOURCE}" "${REQUIRED_AUTH_ORDER}" AUTH_ORDER_POSITION)
  if(AUTH_ORDER_POSITION EQUAL -1)
    message(FATAL_ERROR
      "World-thread auth/addon ordering is missing: ${REQUIRED_AUTH_ORDER}")
  endif()
endforeach()

file(READ "${SOURCE_ROOT}/src/proto/ClientConnection.cpp" CLIENT_CONNECTION_SOURCE)
foreach(REQUIRED_SESSION_SNAPSHOT_STEP IN ITEMS
    "SessionId ClientConnection::CurrentSession()"
    "SessionId const session = CurrentSession()"
    "m_gateway.Deliver(session, std::move(packet))")
  string(FIND "${CLIENT_CONNECTION_SOURCE}" "${REQUIRED_SESSION_SNAPSHOT_STEP}" SNAPSHOT_POSITION)
  if(SNAPSHOT_POSITION EQUAL -1)
    message(FATAL_ERROR
      "ClientConnection delivery is missing a locked session snapshot: ${REQUIRED_SESSION_SNAPSHOT_STEP}")
  endif()
endforeach()
