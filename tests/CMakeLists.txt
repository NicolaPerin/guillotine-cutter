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

add_executable(test_problem
    catch_main.cpp
    test_problem.cpp
)
target_link_libraries(test_problem PRIVATE gdcut_core)

add_executable(test_cut_candidates
    catch_main.cpp
    test_cut_candidates.cpp
)
target_link_libraries(test_cut_candidates PRIVATE gdcut_core)

add_executable(test_solver
    catch_main.cpp
    test_solver.cpp
)
target_link_libraries(test_solver PRIVATE gdcut_core)

add_executable(test_all
    catch_main.cpp
    test_defect.cpp
    test_patterns.cpp
    test_pure_table.cpp
    test_problem.cpp
    test_cut_candidates.cpp
    test_solver.cpp
)
target_link_libraries(test_all PRIVATE gdcut_core)

enable_testing()
add_test(NAME test_defect COMMAND test_defect)
add_test(NAME test_patterns COMMAND test_patterns)
add_test(NAME test_pure_table COMMAND test_pure_table)
add_test(NAME test_problem COMMAND test_problem)
add_test(NAME test_cut_candidates COMMAND test_cut_candidates)
add_test(NAME test_solver COMMAND test_solver)
add_test(NAME test_all COMMAND test_all)