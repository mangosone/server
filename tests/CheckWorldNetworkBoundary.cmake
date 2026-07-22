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
