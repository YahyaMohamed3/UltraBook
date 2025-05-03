include(FetchContent)
FetchContent_Declare(
  catch
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.3 # use latest stable
)
FetchContent_MakeAvailable(catch)

add_executable(tests
    test_main.cpp
    test_matching_engine.cpp
)

target_link_libraries(tests PRIVATE engine Catch2::Catch2WithMain)
