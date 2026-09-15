# ESP-IDF 5.5.2 keeps WebSocket bytes received with the HTTP upgrade in a
# transport buffer, but ws_poll_read only polls the now-empty TCP socket.
# Compile a patched copy for this project; do not modify the shared IDF install.
idf_component_get_property(ws_transport_dir tcp_transport COMPONENT_DIR)
idf_component_get_property(ws_transport_lib tcp_transport COMPONENT_LIB)
set(ws_transport_source "${ws_transport_dir}/transport_ws.c")
file(READ "${ws_transport_source}" ws_transport_code)
set(ws_poll_original [=[static int ws_poll_read(esp_transport_handle_t t, int timeout_ms)
{
    transport_ws_t *ws = esp_transport_get_context_data(t);
    return esp_transport_poll_read(ws->parent, timeout_ms);
}]=])
set(ws_poll_fixed [=[static int ws_poll_read(esp_transport_handle_t t, int timeout_ms)
{
    transport_ws_t *ws = esp_transport_get_context_data(t);
    // The upgrade response may already contain the first WebSocket frame.
    if (ws->buffer_len > 0) {
        return 1;
    }
    return esp_transport_poll_read(ws->parent, timeout_ms);
}]=])
string(FIND "${ws_transport_code}" "${ws_poll_original}" ws_poll_position)
if(ws_poll_position EQUAL -1)
    message(FATAL_ERROR "tcp_transport changed: review the buffered WebSocket read fix for this IDF version")
endif()
string(REPLACE "${ws_poll_original}" "${ws_poll_fixed}" ws_transport_code "${ws_transport_code}")
# Header parsing has a second direct socket poll; it must also honor the buffer.
set(ws_header_original [=[    ws->frame_state.header_received = false;
    if ((poll_read = esp_transport_poll_read(ws->parent, timeout_ms)) <= 0) {]=])
set(ws_header_fixed [=[    ws->frame_state.header_received = false;
    if (ws->buffer_len == 0 &&
        (poll_read = esp_transport_poll_read(ws->parent, timeout_ms)) <= 0) {]=])
string(FIND "${ws_transport_code}" "${ws_header_original}" ws_header_position)
if(ws_header_position EQUAL -1)
    message(FATAL_ERROR "tcp_transport changed: review the buffered WebSocket header fix for this IDF version")
endif()
string(REPLACE "${ws_header_original}" "${ws_header_fixed}" ws_transport_code "${ws_transport_code}")
set(ws_transport_patched "${CMAKE_BINARY_DIR}/patched/transport_ws.c")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/patched")
file(WRITE "${ws_transport_patched}" "${ws_transport_code}")
get_target_property(ws_transport_sources ${ws_transport_lib} SOURCES)
list(REMOVE_ITEM ws_transport_sources "${ws_transport_source}" "transport_ws.c")
set_property(TARGET ${ws_transport_lib} PROPERTY SOURCES "${ws_transport_sources};${ws_transport_patched}")
