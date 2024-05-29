add_executable(sprite_manager_test sprite_manager_test.cc)

target_link_libraries(sprite_manager_test absl::status)
target_link_libraries(sprite_manager_test absl::strings)
target_link_libraries(sprite_manager_test absl::str_format)
target_link_libraries(sprite_manager_test absl::flags)
target_link_libraries(sprite_manager_test absl::flags_parse)
target_link_libraries(sprite_manager_test SDL2main SDL2-static SDL2_image SDL2_ttf)
target_link_libraries(sprite_manager_test ${IMGUI_LIBRARIES}) 
target_link_libraries(sprite_manager_test sprite_manager)

add_executable(bmp_test bmp_test.cc)

add_executable(creator_test creator_test.cc)
target_link_libraries(creator_test shape)
target_link_libraries(creator_test absl::str_format)