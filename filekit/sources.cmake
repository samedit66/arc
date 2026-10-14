if(WIN32)
    set(filekit_platform windows)
else()
    set(filekit_platform unix)
endif()
set(filekit_sources
    "${CMAKE_CURRENT_LIST_DIR}/support.c"
    "${CMAKE_CURRENT_LIST_DIR}/file_system_${filekit_platform}.c")
if(WIN32)
    list(APPEND filekit_sources "${CMAKE_CURRENT_LIST_DIR}/windows_text.c")
endif()
