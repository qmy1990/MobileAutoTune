cmake_minimum_required(VERSION 3.22)
project(MobileAutoTune VERSION 0.4.0 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
option(MAT_BUILD_TOOLS "Desktop WAV processor" ON)
option(MAT_BUILD_TESTS "Core validation" ON)
option(MAT_SANITIZE "Address/undefined sanitizers" OFF)
if(MAT_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
set(RN ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rnnoise)
add_library(mat_rnnoise STATIC
 ${RN}/src/denoise.c ${RN}/src/rnn.c ${RN}/src/pitch.c
 ${RN}/src/kiss_fft.c ${RN}/src/celt_lpc.c ${RN}/src/nnet.c
 ${RN}/src/nnet_default.c ${RN}/src/parse_lpcnet_weights.c
 ${RN}/src/rnnoise_data.c ${RN}/src/rnnoise_tables.c)
target_include_directories(mat_rnnoise PRIVATE ${RN}/src PUBLIC ${RN}/include)
target_compile_definitions(mat_rnnoise PRIVATE RNNOISE_BUILD)
target_compile_options(mat_rnnoise PRIVATE -O3)
set(RNL ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rnnoise-legacy)
add_library(mat_rnnoise_legacy STATIC ${RNL}/src/denoise.c ${RNL}/src/rnn.c ${RNL}/src/rnn_data.c ${RNL}/src/pitch.c ${RNL}/src/kiss_fft.c ${RNL}/src/celt_lpc.c)
target_include_directories(mat_rnnoise_legacy PRIVATE ${RNL}/src ${RNL}/include)
target_compile_options(mat_rnnoise_legacy PRIVATE -O3)
target_compile_definitions(mat_rnnoise_legacy PRIVATE RNNOISE_BUILD
 rnnoise_get_size=mat_legacy_get_size rnnoise_get_frame_size=mat_legacy_get_frame_size
 rnnoise_init=mat_legacy_init rnnoise_create=mat_legacy_create
 rnnoise_destroy=mat_legacy_destroy rnnoise_process_frame=mat_legacy_process_frame
 rnnoise_model_from_file=mat_legacy_model_from_file rnnoise_model_free=mat_legacy_model_free
 compute_rnn=mat_legacy_compute_rnn compute_dense=mat_legacy_compute_dense compute_gru=mat_legacy_compute_gru
 rnn_pitch_downsample=mat_legacy_pitch_downsample rnn_pitch_search=mat_legacy_pitch_search
 rnn_remove_doubling=mat_legacy_remove_doubling rnn_autocorr=mat_legacy_autocorr
 rnn_lpc=mat_legacy_lpc rnn_fir=mat_legacy_fir rnn_iir=mat_legacy_iir
 opus_fft_alloc_twiddles=mat_legacy_fft_alloc_twiddles opus_fft_free=mat_legacy_fft_free
 opus_fft_impl=mat_legacy_fft_impl opus_fft_c=mat_legacy_fft_c opus_ifft_c=mat_legacy_ifft_c
 common=mat_legacy_common compute_band_corr=mat_legacy_band_corr
 compute_band_energy=mat_legacy_band_energy interp_band_gain=mat_legacy_interp_band_gain
 pitch_filter=mat_legacy_pitch_filter celt_pitch_xcorr=mat_legacy_pitch_xcorr
 pitch_downsample=mat_legacy_downsample pitch_search=mat_legacy_search remove_doubling=mat_legacy_remove_doubling_impl
 opus_fft_alloc=mat_legacy_fft_alloc opus_fft_alloc_arch_c=mat_legacy_fft_alloc_arch_c
 opus_fft_free_arch_c=mat_legacy_fft_free_arch_c _celt_autocorr=mat_legacy_celt_autocorr
 _celt_lpc=mat_legacy_celt_lpc celt_fir=mat_legacy_celt_fir celt_iir=mat_legacy_celt_iir)
add_library(mobileautotune STATIC src/mobile_audio.cpp src/offline.cpp)
target_include_directories(mobileautotune PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include> PRIVATE
 third_party/signalsmith-stretch third_party/signalsmith-linear/include)
target_link_libraries(mobileautotune PRIVATE mat_rnnoise mat_rnnoise_legacy)
target_compile_options(mobileautotune PRIVATE -O3 -Wall -Wextra)
# NDK r27 needs both flags for LOAD and RELRO alignment on16KB devices.
# Propagate to host shared libraries when the core is linked statically.
if(ANDROID AND (ANDROID_ABI STREQUAL "arm64-v8a" OR ANDROID_ABI STREQUAL "x86_64"))
 target_link_options(mobileautotune INTERFACE "-Wl,-z,max-page-size=16384" "-Wl,-z,common-page-size=16384")
endif()
if(UNIX)
 target_link_libraries(mobileautotune PRIVATE m)
endif()
if(MAT_BUILD_TOOLS AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
 add_executable(mat_process tools/process.cpp)
 target_link_libraries(mat_process PRIVATE mobileautotune)
 add_executable(mat_benchmark tools/benchmark.cpp)
 target_link_libraries(mat_benchmark PRIVATE mobileautotune)
endif()
if(MAT_BUILD_TESTS AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
 enable_testing()
 find_package(Threads REQUIRED)
 add_executable(mat_tests tests/core_tests.cpp)
 target_link_libraries(mat_tests PRIVATE mobileautotune Threads::Threads)
 add_test(NAME core COMMAND mat_tests)
 add_executable(mat_offline_tests tests/offline_tests.cpp)
 target_link_libraries(mat_offline_tests PRIVATE mobileautotune)
 add_test(NAME offline_wav COMMAND mat_offline_tests)
endif()
install(TARGETS mobileautotune mat_rnnoise mat_rnnoise_legacy ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
