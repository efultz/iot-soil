cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_proj1)


target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "connect_device_package/soil_03_esp32.cert.pem" TEXT)
target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "connect_device_package/soil_03_esp32.private.key" TEXT)
target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "AmazonRootCA1.pem" TEXT)

# add_definitions(-Dsdkconfig)

# aux_source_directory(src SOURCES)

# add_subdirectory( src )

# set(SDKCONFIG sdkconfig)

# add_executable( main "src/main.c" )
# set(elf_file ${CMAKE_PROJECT_NAME}.elf)
# set(project_elf_src main/main.c)
# add_executable(${elf_file} src/main.c)

# idf_build_component(${EXTRA_COMPONENT_DIRS})

# include_directories(BEFORE freertos)
# include_directories(BEFORE freertos-configs)


# target_link_libraries(
#     ${elf_file}
# )
