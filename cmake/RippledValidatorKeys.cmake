option (validator_keys "Enables building of validator-keys-tool as a separate target (imported via FetchContent)" OFF)

if (validator_keys)
  # Pin to a specific commit to avoid upstream breaking changes
  # On Dec 11, 2025, ripple/validator-keys-tool renamed ripple:: to xrpl:: namespace
  # (commit db143b6), which breaks compatibility with postfiatd
  # TODO: Fork validator-keys-tool and maintain compatibility, or
  #       update postfiatd to match upstream namespace conventions
  set (validator_keys_commit "05742108d4b7c0b7d9a97c073acd690d2c75a2b4")  # Last working commit before namespace rename (Sep 5, 2025)
  message (STATUS "Using ValidatorKeys commit: ${validator_keys_commit}")

  FetchContent_Declare (
    validator_keys_src
    GIT_REPOSITORY https://github.com/ripple/validator-keys-tool.git
    GIT_TAG        "${validator_keys_commit}"
  )
  FetchContent_GetProperties (validator_keys_src)
  if (NOT validator_keys_src_POPULATED)
    message (STATUS "Pausing to download ValidatorKeys...")
    FetchContent_Populate (validator_keys_src)
  endif ()
  add_subdirectory (${validator_keys_src_SOURCE_DIR} ${CMAKE_BINARY_DIR}/validator-keys)
endif ()
