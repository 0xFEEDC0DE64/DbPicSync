project(DbPicSync)

find_package(Qt5Core CONFIG REQUIRED)

set(HEADERS
)

set(SOURCES
    main.cpp
)

add_executable(picsync ${HEADERS} ${SOURCES})

target_link_libraries(picsync Qt5::Core dbcorelib)
