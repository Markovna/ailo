# All engine tests in one executable, run one after the other, checked with ailo_testing (libs/testing).
add_executable(ailo_tests
        main.cpp
        app_tests.cpp
        asset_server_tests.cpp
        embedded_asset_tests.cpp
        texture_tests.cpp
        model_tests.cpp
        lighting_tests.cpp
        material_instance_tests.cpp
        transform_tests.cpp
        physics_tests.cpp
        skeleton_tests.cpp
        expr_vm_tests.cpp
        anim_graph_tests.cpp
        sync_track_tests.cpp
        foot_marker_tests.cpp
        render_import_tests.cpp
        # modelc's foot marker detection works on package types, so it's tested here.
        ${CMAKE_SOURCE_DIR}/tools/modelc/src/FootMarkers.cpp
)
# ailo_texture and ailo_model are private to ailo, so their include dirs come from linking them here.
target_link_libraries(ailo_tests PRIVATE ailo ailo_texture ailo_model stb_image ailo_testing)
target_include_directories(ailo_tests PRIVATE ${CMAKE_SOURCE_DIR}/tools/modelc/src)

embed_assets(ailo_tests PACKAGE test_assets ROOT test_data FILES
        hello.txt
        nested/world.txt
        materials/unlit.mat
        materials/tinted.mat
        materials/red.matinst
        materials/blur.mat
)
