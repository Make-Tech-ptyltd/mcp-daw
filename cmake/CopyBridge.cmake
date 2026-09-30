# Copy the freshly built MCP bridge next to MCP-DAW.exe without failing the build when the old
# copy is in use (an MCP client such as Claude Desktop keeps mcp-daw-bridge.exe running).
# Windows cannot overwrite or delete a running .exe, but it CAN rename it: move the busy one aside
# as mcp-daw-bridge.old-<n>.exe, then copy the new one in. Clients pick up the new bridge the next
# time they start it. Usage: cmake -DSRC=<built exe> -DDST_DIR=<app dir> -P CopyBridge.cmake
get_filename_component(name "${SRC}" NAME)
set(dst "${DST_DIR}/${name}")
file(MAKE_DIRECTORY "${DST_DIR}")

# Tidy old renamed copies that are no longer running (removal fails silently if still in use).
file(GLOB stale "${DST_DIR}/mcp-daw-bridge.old-*")
foreach(f IN LISTS stale)
    file(REMOVE "${f}")
endforeach()

execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SRC}" "${dst}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0)
    string(TIMESTAMP stamp "%Y%m%d%H%M%S")
    file(RENAME "${dst}" "${DST_DIR}/mcp-daw-bridge.old-${stamp}.exe" RESULT rename_rc)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy "${SRC}" "${dst}" RESULT_VARIABLE rc2 OUTPUT_QUIET ERROR_QUIET)
    if(rc2 EQUAL 0)
        message(STATUS "mcp-daw-bridge was in use (an MCP client is connected); old copy moved aside, new bridge installed. "
                       "Restart the MCP client to use it.")
    else()
        message(WARNING "Could not update ${dst} (in use?). MCP DAW still builds; restart your MCP client and rebuild to refresh the bridge.")
    endif()
endif()
