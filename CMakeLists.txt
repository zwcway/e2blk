cmake_minimum_required(VERSION 3.4.1)
project(e2blk LANGUAGES C)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(NCURSES REQUIRED ncursesw)
pkg_check_modules(COMERR REQUIRED com_err)
pkg_check_modules(EXT2FS REQUIRED ext2fs)
pkg_check_modules(E2P REQUIRED e2p)

add_executable(${PROJECT_NAME}  
    e2blk.c e2blk.h
    preview.c
    move.c
    window.c
)

set(Libraries 
    m 
    Threads::Threads
    ${EXT2FS_LIBRARIES}
    ${E2P_LIBRARIES}
    ${COMERR_LIBRARIES}
    ${NCURSES_LIBRARIES}
)

target_include_directories(${PROJECT_NAME} PRIVATE ${EXT2FS_INCLUDE_DIRS} ${E2P_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME} ${Libraries})