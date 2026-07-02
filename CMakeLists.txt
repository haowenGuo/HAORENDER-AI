cmake_minimum_required(VERSION 3.20)
project(HaoRenderGI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

set(HAORENDER_GI_DEPENDENCY_HINT [=[
Manual configure example:
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
    -DQt5_DIR=D:/anaconda3/Library/lib/cmake/Qt5 ^
    -DQt5Core_DIR=D:/anaconda3/Library/lib/cmake/Qt5Core ^
    -DQt5Gui_DIR=D:/anaconda3/Library/lib/cmake/Qt5Gui ^
    -DQt5Widgets_DIR=D:/anaconda3/Library/lib/cmake/Qt5Widgets ^
    -DOpenCV_DIR=F:/OPENCV/opencv/build ^
    -DEIGEN3_INCLUDE_DIR=E:/Epic Games/UE_5.4/Engine/Source/ThirdParty/Eigen ^
    -DASSIMP_INCLUDE_DIR=E:/Epic Games/UE_5.4/Engine/Plugins/VRM4U/ThirdParty/assimp/include ^
    -DASSIMP_LIBRARY=F:/D-plus/Open GL/Assimp/lib/Debug/assimp-vc143-mtd.lib
]=])

find_package(Qt5 CONFIG QUIET COMPONENTS Widgets OpenGL Network)
if(NOT Qt5_FOUND)
    message(FATAL_ERROR
        "Qt5 Widgets/OpenGL/Network was not found.\n"
        "${HAORENDER_GI_DEPENDENCY_HINT}")
endif()

find_package(Eigen3 CONFIG QUIET)
set(HAORENDER_GI_EIGEN_TARGET "")
set(HAORENDER_GI_EIGEN_INCLUDE "")
if(TARGET Eigen3::Eigen)
    set(HAORENDER_GI_EIGEN_TARGET Eigen3::Eigen)
else()
    find_path(EIGEN3_INCLUDE_DIR NAMES Eigen/Core)
    if(EIGEN3_INCLUDE_DIR)
        set(HAORENDER_GI_EIGEN_INCLUDE ${EIGEN3_INCLUDE_DIR})
    else()
        message(FATAL_ERROR
            "Eigen was not found.\n"
            "${HAORENDER_GI_DEPENDENCY_HINT}")
    endif()
endif()

find_package(assimp CONFIG QUIET)
set(ASSIMP_INCLUDE_DIR "" CACHE PATH "Path to Assimp include directory")
set(ASSIMP_LIBRARY "" CACHE FILEPATH "Path to Assimp import library")
set(HAORENDER_GI_ASSIMP_TARGET "")
set(HAORENDER_GI_ASSIMP_INCLUDE "")
if(TARGET assimp::assimp)
    set(HAORENDER_GI_ASSIMP_TARGET assimp::assimp)
else()
    if(NOT (ASSIMP_INCLUDE_DIR
        AND EXISTS "${ASSIMP_INCLUDE_DIR}/assimp/scene.h"
        AND EXISTS "${ASSIMP_INCLUDE_DIR}/assimp/Importer.hpp"))
        unset(ASSIMP_INCLUDE_DIR CACHE)
        find_path(ASSIMP_INCLUDE_DIR NAMES assimp/scene.h assimp/Importer.hpp)
    endif()
    if(NOT ASSIMP_LIBRARY)
        find_library(ASSIMP_LIBRARY NAMES assimp assimp-vc143-mtd)
    endif()
    if(ASSIMP_INCLUDE_DIR AND ASSIMP_LIBRARY)
        set(HAORENDER_GI_ASSIMP_INCLUDE ${ASSIMP_INCLUDE_DIR})
        set(HAORENDER_GI_ASSIMP_TARGET ${ASSIMP_LIBRARY})
    else()
        message(FATAL_ERROR
            "Assimp was not found.\n"
            "${HAORENDER_GI_DEPENDENCY_HINT}")
    endif()
endif()

set(HAORENDER_GI_SOURCES
    src/main.cpp
    src/app/expression_agent_tools.cpp
    src/app/asset_converter_window.cpp
    src/app/llm_lookdev_client.cpp
    src/app/lookdev_ai.cpp
    src/app/main_window.cpp
    src/app/rig_ai_window.cpp
    src/app/render_viewport.cpp
    src/core/camera.cpp
    src/rigging/ai_bone_mapper.cpp
    src/rigging/animation_retargeter.cpp
    src/rigging/bone_name_normalizer.cpp
    src/rigging/retarget_profile.cpp
    src/rigging/retarget_quality.cpp
    src/rigging/rig_mapping_exporter.cpp
    src/rigging/skeleton_extractor.cpp
    src/rigging/skeleton_types.cpp
    src/scene/animation_sampler.cpp
    src/scene/gltf_animation_loader.cpp
    src/scene/gltf_vrm_expression_loader.cpp
    src/scene/gltf_vrm_humanoid_loader.cpp
    src/scene/gltf_vrm_material_loader.cpp
    src/scene/model_loader.cpp
    src/scene/native_model_converter.cpp
    src/scene/vrm_native_writer.cpp
    src/scene/vrm_expression_controller.cpp
    src/rendering/dxr_ray_tracer.cpp
    src/rendering/opengl_ray_tracer.cpp
    src/rendering/opengl_rasterizer.cpp
)

set(HAORENDER_GI_HEADERS
    src/app/expression_agent_tools.h
    src/app/asset_converter_window.h
    src/app/llm_lookdev_client.h
    src/app/lookdev_ai.h
    src/app/main_window.h
    src/app/rig_ai_window.h
    src/app/render_viewport.h
    src/core/camera.h
    src/core/math_utils.h
    src/rigging/ai_bone_mapper.h
    src/rigging/animation_retargeter.h
    src/rigging/bone_name_normalizer.h
    src/rigging/retarget_profile.h
    src/rigging/retarget_quality.h
    src/rigging/rig_mapping_exporter.h
    src/rigging/skeleton_extractor.h
    src/rigging/skeleton_types.h
    src/scene/animation_sampler.h
    src/scene/gltf_animation_loader.h
    src/scene/gltf_vrm_expression_loader.h
    src/scene/gltf_vrm_humanoid_loader.h
    src/scene/gltf_vrm_material_loader.h
    src/scene/model_loader.h
    src/scene/native_model_converter.h
    src/scene/scene_types.h
    src/scene/vrm_native_writer.h
    src/scene/vrm_expression_controller.h
    src/rendering/dxr_ray_tracer.h
    src/rendering/opengl_ray_tracer.h
    src/rendering/opengl_rasterizer.h
    src/rendering/render_backend.h
)

add_executable(HaoRender-GI ${HAORENDER_GI_SOURCES} ${HAORENDER_GI_HEADERS})
target_include_directories(HaoRender-GI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${HAORENDER_GI_EIGEN_INCLUDE}
    ${HAORENDER_GI_ASSIMP_INCLUDE}
)
target_link_libraries(HaoRender-GI PRIVATE
    Qt5::Widgets
    Qt5::OpenGL
    Qt5::Network
    ${HAORENDER_GI_EIGEN_TARGET}
    ${HAORENDER_GI_ASSIMP_TARGET}
)

set(HAORIG_BATCH_TEST_SOURCES
    src/tools/rig_batch_test.cpp
    src/rigging/ai_bone_mapper.cpp
    src/rigging/animation_retargeter.cpp
    src/rigging/bone_name_normalizer.cpp
    src/rigging/retarget_profile.cpp
    src/rigging/retarget_quality.cpp
    src/rigging/skeleton_extractor.cpp
    src/rigging/skeleton_types.cpp
    src/scene/animation_sampler.cpp
    src/scene/gltf_animation_loader.cpp
    src/scene/gltf_vrm_expression_loader.cpp
    src/scene/gltf_vrm_humanoid_loader.cpp
    src/scene/gltf_vrm_material_loader.cpp
    src/scene/model_loader.cpp
    src/scene/vrm_expression_controller.cpp
)

add_executable(HaoRigBatchTest ${HAORIG_BATCH_TEST_SOURCES})
target_include_directories(HaoRigBatchTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${HAORENDER_GI_EIGEN_INCLUDE}
    ${HAORENDER_GI_ASSIMP_INCLUDE}
)
target_link_libraries(HaoRigBatchTest PRIVATE
    Qt5::Widgets
    ${HAORENDER_GI_EIGEN_TARGET}
    ${HAORENDER_GI_ASSIMP_TARGET}
)

set(HAORENDER_CORE_SOURCES
    src/tools/haorender_core.cpp
    src/rigging/ai_bone_mapper.cpp
    src/rigging/bone_name_normalizer.cpp
    src/rigging/skeleton_extractor.cpp
    src/rigging/skeleton_types.cpp
    src/scene/gltf_animation_loader.cpp
    src/scene/gltf_vrm_expression_loader.cpp
    src/scene/gltf_vrm_humanoid_loader.cpp
    src/scene/gltf_vrm_material_loader.cpp
    src/scene/model_loader.cpp
    src/scene/native_model_converter.cpp
    src/scene/vrm_native_writer.cpp
)

add_executable(haorender-core ${HAORENDER_CORE_SOURCES})
target_include_directories(haorender-core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${HAORENDER_GI_EIGEN_INCLUDE}
    ${HAORENDER_GI_ASSIMP_INCLUDE}
)
target_link_libraries(haorender-core PRIVATE
    Qt5::Core
    Qt5::Gui
    ${HAORENDER_GI_EIGEN_TARGET}
    ${HAORENDER_GI_ASSIMP_TARGET}
)

if(WIN32)
    target_link_libraries(HaoRender-GI PRIVATE d3d11 d3d12 dxgi dxguid dxcompiler opengl32)

    set_target_properties(HaoRender-GI PROPERTIES WIN32_EXECUTABLE TRUE)

    set(QT_BIN_DIR "D:/anaconda3/Library/bin")
    set(QT_PLUGIN_DIR "D:/anaconda3/Library/plugins")
    foreach(QT_RUNTIME IN ITEMS Qt5Core_conda.dll Qt5Gui_conda.dll Qt5Widgets_conda.dll Qt5OpenGL_conda.dll Qt5Network_conda.dll)
        if(EXISTS "${QT_BIN_DIR}/${QT_RUNTIME}")
            add_custom_command(TARGET HaoRender-GI POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${QT_BIN_DIR}/${QT_RUNTIME}"
                    "$<TARGET_FILE_DIR:HaoRender-GI>/${QT_RUNTIME}")
        endif()
    endforeach()

    foreach(QT_RUNTIME IN ITEMS Qt5Core_conda.dll Qt5Gui_conda.dll)
        if(EXISTS "${QT_BIN_DIR}/${QT_RUNTIME}")
            add_custom_command(TARGET haorender-core POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${QT_BIN_DIR}/${QT_RUNTIME}"
                    "$<TARGET_FILE_DIR:haorender-core>/${QT_RUNTIME}")
        endif()
    endforeach()

    foreach(QT_PLATFORM_PLUGIN IN ITEMS qwindows.dll)
        if(EXISTS "${QT_PLUGIN_DIR}/platforms/${QT_PLATFORM_PLUGIN}")
            add_custom_command(TARGET HaoRender-GI POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HaoRender-GI>/platforms"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${QT_PLUGIN_DIR}/platforms/${QT_PLATFORM_PLUGIN}"
                    "$<TARGET_FILE_DIR:HaoRender-GI>/platforms/${QT_PLATFORM_PLUGIN}")
        endif()
    endforeach()

    foreach(QT_IMAGE_PLUGIN IN ITEMS qjpeg.dll qpng.dll qtga.dll)
        if(EXISTS "${QT_PLUGIN_DIR}/imageformats/${QT_IMAGE_PLUGIN}")
            add_custom_command(TARGET HaoRender-GI POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HaoRender-GI>/imageformats"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${QT_PLUGIN_DIR}/imageformats/${QT_IMAGE_PLUGIN}"
                    "$<TARGET_FILE_DIR:HaoRender-GI>/imageformats/${QT_IMAGE_PLUGIN}")
        endif()
    endforeach()

    foreach(QT_IMAGE_PLUGIN IN ITEMS qjpeg.dll qpng.dll qtga.dll)
        if(EXISTS "${QT_PLUGIN_DIR}/imageformats/${QT_IMAGE_PLUGIN}")
            add_custom_command(TARGET haorender-core POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:haorender-core>/imageformats"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${QT_PLUGIN_DIR}/imageformats/${QT_IMAGE_PLUGIN}"
                    "$<TARGET_FILE_DIR:haorender-core>/imageformats/${QT_IMAGE_PLUGIN}")
        endif()
    endforeach()

    foreach(DXR_RUNTIME_DLL IN ITEMS
        "C:/Program Files (x86)/Windows Kits/10/Redist/D3D/x64/dxcompiler.dll"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/dxcompiler.dll"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64/dxcompiler.dll"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.19041.0/x64/dxcompiler.dll")
        if(EXISTS "${DXR_RUNTIME_DLL}")
            add_custom_command(TARGET HaoRender-GI POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DXR_RUNTIME_DLL}"
                    "$<TARGET_FILE_DIR:HaoRender-GI>/dxcompiler.dll")
            break()
        endif()
    endforeach()

    foreach(DXR_RUNTIME_DLL IN ITEMS
        "C:/Program Files (x86)/Windows Kits/10/Redist/D3D/x64/dxil.dll"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/dxil.dll"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64/dxil.dll"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.19041.0/x64/dxil.dll")
        if(EXISTS "${DXR_RUNTIME_DLL}")
            add_custom_command(TARGET HaoRender-GI POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DXR_RUNTIME_DLL}"
                    "$<TARGET_FILE_DIR:HaoRender-GI>/dxil.dll")
            break()
        endif()
    endforeach()

    set(HAORENDER_GI_ASSIMP_RUNTIME_CANDIDATES
        "F:/haorender-main/assimp-vc143-mtd.dll"
        "F:/haorender-main/build-nonhalf/Release/assimp-vc143-mtd.dll"
        "F:/haorender-gi/verify-final/haorender-main/assimp-vc143-mtd.dll"
    )
    foreach(ASSIMP_RUNTIME_DLL IN LISTS HAORENDER_GI_ASSIMP_RUNTIME_CANDIDATES)
        if(EXISTS "${ASSIMP_RUNTIME_DLL}")
            add_custom_command(TARGET HaoRender-GI POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ASSIMP_RUNTIME_DLL}"
                    "$<TARGET_FILE_DIR:HaoRender-GI>/assimp-vc143-mtd.dll")
            break()
        endif()
    endforeach()
endif()
