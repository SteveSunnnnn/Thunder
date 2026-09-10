# Thunder 编译警告策略。
#
# 引擎全部目标共用同一套警告等级，避免各目标各行其是。
# 仓库源码一律 UTF-8（含本地化内容键与中文文档注释），因此显式指定
# 源/执行字符集，使 `-Werror` / `/WX` 不依赖宿主机代码页。

function(thunder_apply_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive- /utf-8)
        if(THUNDER_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target_name} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${target_name} PRIVATE -Wno-free-nonheap-object)
        endif()
        if(THUNDER_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# MinGW 构建时把编译器自带的 C++ 运行库 DLL 复制到每个可执行文件旁。
# Windows 的 DLL 搜索顺序是“应用程序目录 → 系统 → PATH”：若 PATH 中先出现
# 另一份不兼容的 libstdc++-6.dll（例如 Git Bash 自带的旧版运行库），测试会以
# 0xc0000139 (STATUS_ENTRYPOINT_NOT_FOUND) 加载失败。就近部署让门禁与 shell
# 环境解耦——ctest 传入的 PATH 永远不影响测试可执行文件装载哪个运行库。
function(thunder_bundle_runtime_dlls target_name)
    if(WIN32 AND NOT MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        get_filename_component(_thunder_gnu_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        foreach(_dll IN ITEMS libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
            if(EXISTS "${_thunder_gnu_bin_dir}/${_dll}")
                add_custom_command(TARGET ${target_name} POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                            "${_thunder_gnu_bin_dir}/${_dll}"
                            "$<TARGET_FILE_DIR:${target_name}>/${_dll}"
                    COMMENT "Bundling ${_dll} next to ${target_name}")
            endif()
        endforeach()
    endif()
endfunction()

# 引擎命令行工具：约定 目标名 == src/apps/<目标名>.cpp，且只能依赖 thunder_runtime。
function(thunder_add_tool target_name)
    add_executable(${target_name} "src/apps/${target_name}.cpp")
    target_link_libraries(${target_name} PRIVATE thunder_runtime)
    thunder_apply_warnings(${target_name})
    thunder_bundle_runtime_dlls(${target_name})
endfunction()
