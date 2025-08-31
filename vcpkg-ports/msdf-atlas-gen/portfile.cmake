vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO Chlumsky/msdf-atlas-gen
        REF 77804c2f4108f49eb73491cda9296a2afb714d4c
        SHA512 af797c52d96004339f87e96af48ab8c586ce5f58bd09327d05b5902cd4d66a42a26b65ee9337a83c53820671d42b65df76fa03157d467cf1e84d0bb3f7aa2b91
        HEAD_REF master
)
vcpkg_cmake_configure(
        SOURCE_PATH ${SOURCE_PATH}
        OPTIONS
        -DMSDF_ATLAS_BUILD_STANDALONE=OFF
        -DMSDF_ATLAS_NO_ARTERY_FONT=ON
        -DMSDF_ATLAS_MSDFGEN_EXTERNAL=ON
        -DMSDF_ATLAS_INSTALL=ON
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake)

file(
        INSTALL "${SOURCE_PATH}/LICENSE.txt"
        DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
        RENAME copyright
)

file(
        COPY "${CURRENT_PACKAGES_DIR}/share/${PORT}/${PORT}"
        DESTINATION "${CURRENT_PACKAGES_DIR}/share"
)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/share/${PORT}/${PORT}")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")