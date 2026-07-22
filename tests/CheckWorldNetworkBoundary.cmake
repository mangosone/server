set(REQUIRED_FILES
  "${SOURCE_ROOT}/src/game/Server/WorldGateway.h"
  "${SOURCE_ROOT}/src/game/Server/WorldGateway.cpp"
  "${SOURCE_ROOT}/src/game/Server/SessionMailbox.h"
  "${SOURCE_ROOT}/src/game/Server/SessionMailbox.cpp"
  "${SOURCE_ROOT}/src/game/Server/WorldNetwork.h"
  "${SOURCE_ROOT}/src/game/Server/WorldNetwork.cpp")

set(REMOVED_FILES
  "${SOURCE_ROOT}/src/game/Server/WorldSocket.h"
  "${SOURCE_ROOT}/src/game/Server/WorldSocket.cpp"
  "${SOURCE_ROOT}/src/game/Server/WorldSocketMgr.h"
  "${SOURCE_ROOT}/src/game/Server/WorldSocketMgr.cpp"
  "${SOURCE_ROOT}/src/shared/Threading/LeasedPtr.h")

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
