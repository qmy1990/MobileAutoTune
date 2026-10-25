#include <jni.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "mat/key_analysis.h"

namespace {
std::string Utf8Path(JNIEnv* env, jstring value) {
  if (!value) {
    return {};
  }
  const jchar* text = env->GetStringChars(value, nullptr);
  if (!text) {
    return {};
  }
  std::string output;
  jsize length = env->GetStringLength(value);
  for (jsize i = 0; i < length; ++i) {
    uint32_t code = text[i];
    if (code >= 0xd800 && code <= 0xdbff && i + 1 < length &&
        text[i + 1] >= 0xdc00 && text[i + 1] <= 0xdfff) {
      code = 0x10000 + ((code - 0xd800) << 10) + text[++i] - 0xdc00;
    }
    if (code == 0) {
      output.clear();
      break;
    }
    if (code < 0x80) {
      output.push_back(code);
    } else if (code < 0x800) {
      output.push_back(0xc0 | (code >> 6));
      output.push_back(0x80 | (code & 63));
    } else if (code < 0x10000) {
      output.push_back(0xe0 | (code >> 12));
      output.push_back(0x80 | ((code >> 6) & 63));
      output.push_back(0x80 | (code & 63));
    } else {
      output.push_back(0xf0 | (code >> 18));
      output.push_back(0x80 | ((code >> 12) & 63));
      output.push_back(0x80 | ((code >> 6) & 63));
      output.push_back(0x80 | (code & 63));
    }
  }
  env->ReleaseStringChars(value, text);
  return output;
}
struct Progress {
  JNIEnv* env;
  jobject listener;
  jmethodID method;
};
int ReportProgress(float progress, void* context) {
  auto* callback = static_cast<Progress*>(context);
  jboolean proceed = callback->env->CallBooleanMethod(
      callback->listener, callback->method, progress);
  return callback->env->ExceptionCheck() || !proceed;
}
}  // namespace

extern "C" {
JNIEXPORT jlong JNICALL Java_com_moon_mobileautotune_KeyAnalyzer_nCreate(
    JNIEnv* env, jclass, jstring directory, jint threads, jint execution,
    jboolean modulations) {
  try {
    auto path = Utf8Path(env, directory);
    if (path.empty()) {
      return 0;
    }
    auto options = mat_default_key_options();
    options.threads = threads;
    options.execution = static_cast<mat_key_execution>(execution);
    options.detect_modulations = modulations;
    return reinterpret_cast<jlong>(mat_key_create(path.c_str(), &options));
  } catch (...) {
    return 0;
  }
}
JNIEXPORT void JNICALL Java_com_moon_mobileautotune_KeyAnalyzer_nDestroy(
    JNIEnv*, jclass, jlong handle) {
  mat_key_destroy(reinterpret_cast<mat_key_analyzer*>(handle));
}
JNIEXPORT void JNICALL Java_com_moon_mobileautotune_KeyAnalyzer_nCancel(
    JNIEnv*, jclass, jlong handle) {
  mat_key_cancel(reinterpret_cast<mat_key_analyzer*>(handle));
}
JNIEXPORT jstring JNICALL Java_com_moon_mobileautotune_KeyAnalyzer_nAnalyze(
    JNIEnv* env, jclass, jlong handle, jstring source, jobject listener) {
  auto* analyzer = reinterpret_cast<mat_key_analyzer*>(handle);
  try {
    auto path = Utf8Path(env, source);
    if (env->ExceptionCheck()) {
      return nullptr;
    }
    jclass type = env->GetObjectClass(listener);
    jmethodID method = env->GetMethodID(type, "nativeProgress", "(F)Z");
    env->DeleteLocalRef(type);
    if (!method) {
      return nullptr;
    }
    Progress progress{env, listener, method};
    mat_key_result result{};
    int code = mat_key_analyze_wav(analyzer, path.c_str(), &result,
                                   ReportProgress, &progress);
    if (env->ExceptionCheck()) {
      return nullptr;
    }
    if (code) {
      const char* exception = code == -5
                                  ? "java/util/concurrent/CancellationException"
                                  : "java/io/IOException";
      jclass error = env->FindClass(exception);
      env->ThrowNew(error, mat_key_last_error(analyzer));
      env->DeleteLocalRef(error);
      return nullptr;
    }
    std::vector<char> json(mat_key_result_json(&result, nullptr, 0));
    if (json.empty()) {
      throw std::runtime_error("Cannot serialize key analysis result");
    }
    mat_key_result_json(&result, json.data(), json.size());
    return env->NewStringUTF(json.data());
  } catch (const std::exception& failure) {
    if (!env->ExceptionCheck()) {
      jclass error = env->FindClass("java/io/IOException");
      env->ThrowNew(error, failure.what());
      env->DeleteLocalRef(error);
    }
    return nullptr;
  }
}
}  // extern "C"
