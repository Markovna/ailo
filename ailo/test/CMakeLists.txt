# All engine tests in one executable, run one after the other. They use assert, so they only check in Debug builds.
add_executable(ailo_tests
        main.cpp
        app_tests.cpp
        asset_server_tests.cpp
        embedded_asset_tests.cpp
        di_tests.cpp
        texture_tests.cpp
        model_tests.cpp
        lighting_tests.cpp
        transform_tests.cpp
        physics_tests.cpp
)
# ailo_texture and ailo_model are private to ailo, so their include dirs come from linking them here.
target_link_libraries(ailo_tests PRIVATE ailo ailo_texture ailo_model stb_image)

embed_assets(ailo_tests PACKAGE test_assets ROOT test_data FILES
        hello.txt
        nested/world.txt
        materials/unlit.mat
)
