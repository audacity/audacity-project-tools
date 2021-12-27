add_executable(
    sqlite3_shell

    sqlite3.c
    shell.c
)

target_compile_definitions(
    sqlite3_shell
    PRIVATE
    -DSQLITE_ENABLE_BYTECODE_VTAB
    -DSQLITE_ENABLE_DBPAGE_VTAB
    -DSQLITE_ENABLE_DBSTAT_VTAB
    -DSQLITE_ENABLE_STMTVTAB
    -DSQLITE_ENABLE_COLUMN_METADATA
    -DSQLITE_ENABLE_EXPLAIN_COMMENTS
)

set_target_properties(sqlite3_shell PROPERTIES OUTPUT_NAME "sqlite3")

find_package(Threads REQUIRED)

target_link_libraries(sqlite3_shell Threads::Threads ${CMAKE_DL_LIBS})
