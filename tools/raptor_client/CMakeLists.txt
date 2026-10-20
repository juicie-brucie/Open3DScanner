add_executable(raptor_client main.cpp capture.cpp)
target_include_directories(raptor_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Hardware-free smoke test for the replay's pacing and its bounded reply
# read (row #117): the framing selftest, the absolute-timestamp -> delta
# conversion, and the deadline-bound read against a silent loopback peer.
add_test(NAME raptor_client_selftest COMMAND raptor_client --dry-run)
add_test(NAME raptor_client_reply_timeout COMMAND raptor_client --selftest-timeout)

# Hardware-free smoke test for the reply-structure dump (row #127): the
# dump lines for the recorded 11-element array, for the same array with a
# payload in a bin slot, and for a top-level bin result, produced against
# a loopback mock of the daemon's reply side.
add_test(NAME raptor_client_reply_dump COMMAND raptor_client --selftest-dump)

# Hardware-free smoke test for the capture recorder (row #140): the
# round-trip of the recorded replies, the manifest's hashes, and the
# refusal to write a bundle inside a repository.
add_test(NAME raptor_client_capture COMMAND raptor_client --selftest-capture)

# Hardware-free smoke test for the live path's method names (row #157): the
# interface suffix on every call, the call sequence, and the reply
# unwrap, against a loopback mock that refuses a bare method name the
# way the daemon does.
add_test(NAME raptor_client_live_suffix COMMAND raptor_client --selftest-live)

# Hardware-free smoke test for the live path's bounded wait (row #161): a
# loopback peer that accepts the connection and never answers must produce a
# named timeout and the timeout exit code, not a hang — the depth
# interface's observed stall.
add_test(NAME raptor_client_live_timeout COMMAND raptor_client --selftest-live-timeout)
