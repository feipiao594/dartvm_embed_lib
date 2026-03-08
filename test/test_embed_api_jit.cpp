#include "dartvm_embed_lib.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

bool AlwaysUnmodified(const char* url, int64_t since) {
  (void)url;
  (void)since;
  return false;
}

bool Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "[FAIL] " << message << "\n";
    return false;
  }
  return true;
}

bool ContainsText(const char* text, const char* needle) {
  if (text == nullptr || needle == nullptr) {
    return false;
  }
  return strstr(text, needle) != nullptr;
}

bool TestCleanupWithoutInit() {
  char* error = nullptr;
  const bool ok = DartVmEmbed_Cleanup(&error);
  const bool pass = Expect(ok, "Cleanup without init should succeed") &&
                    Expect(error == nullptr,
                           "Cleanup without init should not set error");
  free(error);
  return pass;
}

bool TestProgramPathValidation() {
  char* error = nullptr;
  Dart_Isolate isolate = DartVmEmbed_CreateIsolateFromProgramFile(
      nullptr, nullptr, nullptr, nullptr, &error);
  const bool pass = Expect(isolate == nullptr,
                           "CreateIsolateFromProgramFile(nullptr) should fail") &&
                    Expect(error != nullptr,
                           "CreateIsolateFromProgramFile(nullptr) should set error") &&
                    Expect(ContainsText(error, "program_path is null"),
                           "Error should mention null program_path");
  free(error);
  return pass;
}

bool TestRunEntryValidation() {
  char* error = nullptr;
  const bool ok = DartVmEmbed_RunRootEntryOnIsolate(nullptr, "main", &error);
  const bool pass = Expect(!ok, "RunRootEntryOnIsolate(nullptr) should fail") &&
                    Expect(error != nullptr,
                           "RunRootEntryOnIsolate(nullptr) should set error") &&
                    Expect(ContainsText(error, "isolate is null"),
                           "Error should mention null isolate");
  free(error);
  return pass;
}

bool TestCreateFromSourceValidation() {
  char* error = nullptr;
  Dart_Isolate isolate =
      DartVmEmbed_CreateIsolateFromSource(nullptr, nullptr, nullptr, nullptr,
                                          nullptr, &error);
  const bool pass =
      Expect(isolate == nullptr,
             "CreateIsolateFromSource(nullptr) should fail") &&
      Expect(error != nullptr,
             "CreateIsolateFromSource(nullptr) should set error") &&
      Expect(ContainsText(error, "script_path is null"),
             "Error should mention null script_path");
  free(error);
  return pass;
}

bool TestLoadAotInJitFlavor() {
  DartVmEmbedAotElfHandle handle = nullptr;
  const uint8_t* vm_data = nullptr;
  const uint8_t* vm_instr = nullptr;
  const uint8_t* iso_data = nullptr;
  const uint8_t* iso_instr = nullptr;
  char* error = nullptr;

  const bool ok = DartVmEmbed_LoadAotElf("/nonexistent.aot", 0, &handle,
                                         &vm_data, &vm_instr,
                                         &iso_data, &iso_instr, &error);
  const bool pass = Expect(!ok, "LoadAotElf should fail in jit flavor") &&
                    Expect(handle == nullptr,
                           "LoadAotElf failure should keep handle null") &&
                    Expect(error != nullptr,
                           "LoadAotElf failure should set error") &&
                    Expect(ContainsText(error, "only available in AOT runtime flavor"),
                           "LoadAotElf jit error should mention AOT only");
  free(error);
  return pass;
}

bool TestInitializeAndCleanupRoundTrip() {
  const char* vm_flags[] = {"--no-verify_sdk_hash"};
  DartVmEmbedInitConfig config;
  config.start_kernel_isolate = false;
  config.vm_flag_count = 1;
  config.vm_flags = vm_flags;

  char* error = nullptr;
  const bool init_ok = DartVmEmbed_Initialize(&config, &error);
  const bool init_pass = Expect(init_ok, "Initialize should succeed") &&
                         Expect(error == nullptr,
                                "Initialize success should not set error");
  free(error);

  error = nullptr;
  const bool init2_ok = DartVmEmbed_Initialize(&config, &error);
  const bool init2_pass = Expect(init2_ok, "Second initialize should be idempotent") &&
                          Expect(error == nullptr,
                                 "Second initialize should not set error");
  free(error);

  error = nullptr;
  const bool callback_ok =
      DartVmEmbed_SetFileModifiedCallback(AlwaysUnmodified, &error);
  const bool callback_pass = Expect(callback_ok,
                                    "SetFileModifiedCallback should succeed") &&
                             Expect(error == nullptr,
                                    "SetFileModifiedCallback should not set error");
  free(error);

  const bool reloading_pass =
      Expect(!DartVmEmbed_IsReloading(),
             "IsReloading should normally be false in this test");
  const bool service_query_pass =
      Expect(!DartVmEmbed_HasServiceMessages(),
             "HasServiceMessages should be false without service traffic");

  error = nullptr;
  const bool cleanup_ok = DartVmEmbed_Cleanup(&error);
  const bool cleanup_pass = Expect(cleanup_ok, "Cleanup after init should succeed") &&
                            Expect(error == nullptr,
                                   "Cleanup success should not set error");
  free(error);

  return init_pass && init2_pass && callback_pass && reloading_pass &&
         service_query_pass && cleanup_pass;
}

}  // namespace

int main() {
  bool ok = true;
  ok = TestCleanupWithoutInit() && ok;
  ok = TestProgramPathValidation() && ok;
  ok = TestRunEntryValidation() && ok;
  ok = TestCreateFromSourceValidation() && ok;
  ok = TestLoadAotInJitFlavor() && ok;
  ok = TestInitializeAndCleanupRoundTrip() && ok;

  if (!ok) {
    return 1;
  }
  std::cout << "[PASS] dartvm_embed_lib_unit_jit\n";
  return 0;
}
