add_executable(test_defect
    catch_main.cpp
    test_defect.cpp
)

target_link_libraries(test_defect PRIVATE gdcut_core)

enable_testing()
add_test(NAME test_defect COMMAND test_defect)