# Commit of this repository, recorded in the library and exposed by the wheel:
# a result is reproducible to the bit (docs/companion.md, section 11) only if it
# names the code that produced it.
#
# Read at configure time, so an incremental build refreshes it on the next
# reconfigure. A build without a .git directory (a Docker context, a source
# archive) passes it explicitly: -DQM_GIT_SHA=<sha>.
set(QM_GIT_SHA "" CACHE STRING "Commit of quant-microstructure being built (default: read from git)")

if(QM_GIT_SHA)
  set(QM_BUILD_GIT_SHA "${QM_GIT_SHA}")
else()
  set(QM_BUILD_GIT_SHA "unknown")
  find_package(Git QUIET)
  if(Git_FOUND)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      OUTPUT_VARIABLE _qm_head
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
      RESULT_VARIABLE _qm_git_result
    )
    if(_qm_git_result EQUAL 0)
      set(QM_BUILD_GIT_SHA "${_qm_head}")
      # A tree with uncommitted changes is not the commit it sits on.
      execute_process(
        COMMAND ${GIT_EXECUTABLE} status --porcelain
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE _qm_status
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
      )
      if(_qm_status)
        string(APPEND QM_BUILD_GIT_SHA "-dirty")
      endif()
    endif()
  endif()
endif()

message(STATUS "quantMicrostructure build stamp: ${PROJECT_VERSION} ${QM_BUILD_GIT_SHA}")
