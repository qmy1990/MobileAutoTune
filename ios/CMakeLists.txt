cmake_minimum_required(VERSION 3.22)
project(MobileAudioIOS LANGUAGES C CXX OBJCXX Swift)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0")
set(MAT_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(MAT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MAT_BUILD_KEY_ANALYSIS ON CACHE BOOL "" FORCE)
add_subdirectory(.. ${CMAKE_BINARY_DIR}/core)
add_library(MobileAudioSDK STATIC SDK/MATAudioSDK.mm SDK/MATKeyAnalyzer.mm)
target_include_directories(MobileAudioSDK PUBLIC SDK ../include PRIVATE ../src)
target_compile_options(MobileAudioSDK PRIVATE -fobjc-arc -O3)
target_link_libraries(MobileAudioSDK PUBLIC mobileautotune_key "-framework Foundation" "-framework AVFoundation" "-framework AudioToolbox")
add_executable(MobileAudioDemo MACOSX_BUNDLE Demo/App.swift)
file(GLOB MAT_KEY_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/../models/key/*")
target_sources(MobileAudioDemo PRIVATE ${MAT_KEY_ASSETS})
set_source_files_properties(${MAT_KEY_ASSETS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/KeyModels.bundle")
file(GLOB MAT_KEY_NOTICES "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/onnxruntime/LICENSE"
 "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/onnxruntime/ThirdPartyNotices*.txt"
 "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/key_models/*LICENSE")
target_sources(MobileAudioDemo PRIVATE ${MAT_KEY_NOTICES})
set_source_files_properties(${MAT_KEY_NOTICES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/ThirdPartyLicenses.bundle")
target_link_libraries(MobileAudioDemo PRIVATE MobileAudioSDK "-framework UIKit" "-framework UniformTypeIdentifiers")
set_target_properties(MobileAudioDemo PROPERTIES
 MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Demo/Info.plist"
 XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.moon.mobileautotune.demo"
 XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
 XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/Demo/BridgingHeader.h"
 XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
 XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")
