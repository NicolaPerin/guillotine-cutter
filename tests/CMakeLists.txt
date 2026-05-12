add_executable(test_defect
    catch_main.cpp
    test_defect.cpp
)
target_link_libraries(test_defect PRIVATE gdcut_core)

add_executable(test_patterns
    catch_main.cpp
    test_patterns.cpp
)
target_link_libraries(test_patterns PRIVATE gdcut_core)

add_executable(test_pure_table
    catch_main.cpp
    test_pure_table.cpp
)
target_link_libraries(test_pure_table PRIVATE gdcut_core)

enable_testing()
add_test(NAME test_defect COMMAND test_defect)
add_test(NAME test_patterns COMMAND test_patterns)
add_test(NAME test_pure_table COMMAND test_pure_table)