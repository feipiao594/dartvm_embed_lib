# `main_impl.cc::main` 与 `dartvm_embed_lib.cpp::DartVmEmbed_Initialize` 逐段对比

本文对比：
- 主线：`dart-sdk/sdk/runtime/bin/main_impl.cc` 的 `main(int argc, char** argv)`（重点区间 `1135-1408`）。
- 你的库：`src/dartvm_embed_lib.cpp` 的 `DartVmEmbed_Initialize(const DartVmEmbedInitConfig*, char**)`（区间 `759-882`）。

目标：明确三类差异。
- `主线有，你这里没有`
- `你这里有，主线 main 没有`
- `两边都有，但你这里做了简化/改少`

## 0. 入口职责差异（不是 bug，但会导致“看起来不一样”）

- 主线 `main` 是**完整命令行程序入口**：负责 argv 处理、平台初始化、参数解析、snapshot 选择、运行脚本、退出码。
- 你的 `DartVmEmbed_Initialize` 是**库初始化函数**：只做 VM 初始化相关，不负责 CLI 生命周期。

所以不是所有主线步骤都应该直接出现在 `Initialize` 里。

## 1. 按执行顺序逐段对照

| 步骤 | 主线 `main_impl.cc` | 你的 `Initialize` | 结论 |
|---|---|---|---|
| 1. 崩溃符号预加载 | `1136-1139` `EXEUtils::LoadDartProfilerSymbols(argv[0])` | 无 | 主线有，你没有（库场景通常拿不到 `argv[0]`） |
| 2. CLI/状态变量准备 | `1141-1148` | 无 | 主线有，你没有 |
| 3. 平台初始化 | `1151-1154` `Platform::Initialize()` | 无 | 主线有，你没有 |
| 4. 控制台状态保存 | `1157` `Console::SaveConfig()` | 无 | 主线有，你没有 |
| 5. ICU 初始化 | `1159` `SetupICU()` | 无 | 主线有，你没有 |
| 6. Windows argv UTF-8 转换 | `1161-1164` | 无 | 主线有，你没有 |
| 7. 将 dfe 绑到 Options | `1165-1168` `Options::set_dfe(&dfe)` | 无 | 主线有，你没有（你没有走 `Options::ParseArguments` 链路） |
| 8. 默认 VM 调优 flag 注入 | `1170-1179` | 无 | 主线有，你没有 |
| 9. 解析命令行参数（含 help/version/print_flags） | `1180-1254` | 无 | 主线有，你没有 |
| 10. 读取环境与 crashpad/coredump 策略 | `1256-1262` | 无 | 主线有，你没有 |
| 11. Loader 初始化 | `1264` | `791` | 两边都有 |
| 12. snapshot 探测/类型校验/SetBuffers | `1266-1309` | 无（在你的库里转移到 `CreateIsolateFromProgramFile` 路径） | 主线有，你的 `Initialize` 没有 |
| 13. precompiled 追加 `--precompilation` | `1311-1313` | `794-796` | 两边都有 |
| 14. app-jit/depfile 相关额外流程 | `1314-1329` | 无 | 主线有，你没有 |
| 15. embedder 初始化 | `1331-1336` | `783-790` | 两边都有 |
| 16. `Dart_SetVMFlags` | `1338-1343` | `807-816` | 两边都有 |
| 17. DFE 初始化（在 VM flags 之后） | `1345-1363` | `818-824` | 两边都有；你这里是简化版 |
| 18. 组装 `Dart_InitializeParams` | `1365-1394` | `826-853` | 两边都有；你少了部分字段 |
| 19. `Dart_Initialize` | `1396-1402` | `854-862` | 两边都有 |
| 20. service/file-modified/embedder-info 回调 | `1404-1407` | `864-878` | 你少了 `EmbedderInformationCallback` |
| 21. 后续运行（dartdev / RunMainIsolate / cleanup / exit） | `1408-1479` | 无（由调用方在其他 API 中驱动） | 主线有，你这里没有 |

## 2. `主线有，你这里没有`（聚合清单）

以下均可在主线 `main_impl.cc` 看到，但不在 `DartVmEmbed_Initialize` 内：

1. 平台/进程入口层初始化
- `Platform::Initialize`（`1151-1154`）
- `Console::SaveConfig`（`1157`）
- `SetupICU`（`1159`）
- `ShellUtils::GetUtf8Argv`（`1163`）
- `EXEUtils::LoadDartProfilerSymbols`（`1138`）

2. 命令行与工具链调度
- `Options::ParseArguments` 全链路（`1180-1254`）
- `DartUtils::SetEnvironment(Options::environment())`（`1256`）
- `InitializeCrashpadClient`/coredump 策略（`1258-1262`）
- app-jit / depfile / exit-hook（`1314-1329`）
- dartdev 调度与参数处理（`1410+`）

3. `main` 内 snapshot 预处理
- `Snapshot::TryReadAppSnapshot*` + `SetBuffers`（`1266-1309`）

4. 初始化参数中的额外字段
- `init_params.get_service_assets`（`1381`）
- fuchsia 特定 `vmex_resource`（`1388-1393`）

5. 回调
- `Dart_SetEmbedderInformationCallback`（`1407`）

## 3. `你这里有，主线 main 里没有`（聚合清单）

这些是库 API 形态带来的扩展，不是主线 `main` 直接写法：

1. 显式幂等初始化
- `g_vm_initialized` 防重入（`764-766`）。

2. 通过环境变量直接读取 VM service 配置
- `DARTVM_EMBED_VM_SERVICE_IP/PORT/AUTH_CODES_DISABLED`（`768-781`）。

3. API 级 VM flag 过滤
- 过滤 `verify_sdk_hash` 类参数（`799-802`）。

4. 可覆盖 VM snapshot 指针
- `config->vm_snapshot_data_override / vm_snapshot_instructions_override`（`829-836`）。

5. 初始化失败后的库内回滚
- `Dart_SetFileModifiedCallback` 失败时主动 `Dart_Cleanup`（`866-877`）。

## 4. `两边都有，但你这里改少了/简化了`

1. DFE 相关（主线更完整）
- 主线：`dfe.Init(); dfe.set_verbosity(...); dfe.ReadScript(...); set_application_kernel_buffer(...)`（`1349-1361`）。
- 你：`dfe.Init(); set_use_dfe(); set_use_incremental_compiler(true)`（`821-823`）。
- 结论：你保留了“能用 DFE”但省略了 verbosity 与 script 预读缓存流程。

2. `Dart_InitializeParams` 字段
- 主线额外设置了 `get_service_assets`、Fuchsia `vmex_resource`。
- 你未设置这两项。

3. start_kernel_isolate 决策
- 主线：`dfe.UseDartFrontend() && dfe.CanUseDartFrontend()`（`1383-1384`）。
- 你：JIT 下用配置值（`840-841`），AOT 强制 false（`837-839`）。
- 结论：你是“可配置简化版”，不是主线完全同判定。

4. VM service 相关回调
- 两边都设置 `Dart_SetServiceStreamCallbacks`、`Dart_SetFileModifiedCallback`。
- 主线还设置 `Dart_SetEmbedderInformationCallback`，你没有。

## 5. 你当前实现和主线对齐程度（仅看 Initialize 阶段）

已经对齐的关键骨架：
1. `Loader::InitOnce` 在 VM 初始化前。
2. `Dart_SetVMFlags` 在 `dfe.Init` 前（你已按主线顺序修正）。
3. 构造 `Dart_InitializeParams` 并调用 `Dart_Initialize`。
4. 初始化后安装 service/file-modified 回调。

仍然不是“完整 main_impl 等价”的点：
1. 缺少平台入口初始化（Platform/Console/ICU）。
2. 缺少 main 级 CLI/Options/snapshot 预处理调度。
3. 缺少 `Dart_SetEmbedderInformationCallback` 与 `get_service_assets` 等扩展参数。
4. DFE 使用路径仍比主线精简。

## 6. 总结（按你的原问题）

1. `哪些是他的，我没有`：见第 2 节，尤其是平台初始化、Options 解析、snapshot 预处理、`EmbedderInformationCallback`。
2. `哪些我这边改少了`：见第 4 节，尤其是 DFE 预读流程和 `start_kernel_isolate` 判定策略。
3. `哪些我这边额外做了`：见第 3 节，主要是库 API 的幂等、可覆盖 snapshot、环境变量参数化。

### 7. 代码块级对比
```cpp
void main(int argc, char** argv) {
#if !defined(DART_HOST_OS_WINDOWS)
  // Very early so any crashes during startup can also be symbolized.
  EXEUtils::LoadDartProfilerSymbols(argv[0]);
  // [含义] 尽早加载符号，便于启动早期崩溃栈符号化。
  // [对照] 你没有（Initialize 无 argv[0] 入口参数）。
#endif

  char* script_name = nullptr;
  // [含义] 运行目标脚本路径。
  // [对照] 你没有（Initialize 不做脚本参数解析）。
  // Allows the dartdev process to point to the desired package_config.
  char* package_config_override = nullptr;
  // [含义] 允许 dartdev 覆盖 package_config。
  // [对照] 你没有。
  const int EXTRA_VM_ARGUMENTS = 10;
  CommandLineOptions vm_options(argc + EXTRA_VM_ARGUMENTS);
  CommandLineOptions dart_options(argc + EXTRA_VM_ARGUMENTS);
  bool print_flags_seen = false;
  bool verbose_debug_seen = false;
  // [含义] 建立 CLI 参数收集与状态变量。
  // [对照] 你没有（库 API 模式不是 CLI 主程序）。

  // Perform platform specific initialization.
  if (!Platform::Initialize()) {
    Syslog::PrintErr("Initialization failed\n");
    Platform::Exit(kErrorExitCode);
  }
  // [含义] 进程级平台初始化失败直接退出。
  // [对照] 你没有。

  // Save the console state so we can restore it at shutdown.
  Console::SaveConfig();
  // [含义] 保存终端配置，退出时恢复。
  // [对照] 你没有。

  SetupICU();
  // [含义] 初始化 ICU（Unicode/locale 组件）。
  // [对照] 你没有。

  // On Windows, the argv strings are code page encoded and not
  // utf8. We need to convert them to utf8.
  bool argv_converted = ShellUtils::GetUtf8Argv(argc, argv);
  // [含义] Windows 下 argv 转 UTF-8。
  // [对照] 你没有。

#if !defined(DART_PRECOMPILED_RUNTIME)
  // Processing of some command line flags directly manipulates dfe.
  Options::set_dfe(&dfe);
  // [含义] 把 dfe 挂到 Options，允许命令行参数直接影响 dfe。
  // [对照] 你没有（你不走 Options::ParseArguments）。
#endif  // !defined(DART_PRECOMPILED_RUNTIME)

  // When running from the command line we assume that we are optimizing for
  // throughput, and therefore use a larger new gen semi space size and a faster
  // new gen growth factor unless others have been specified.
  if (kWordSize <= 4) {
    vm_options.AddArgument("--new_gen_semi_max_size=16");
  } else {
    vm_options.AddArgument("--new_gen_semi_max_size=32");
  }
  vm_options.AddArgument("--new_gen_growth_factor=4");
  // [含义] 主线给 CLI 默认吞吐优化参数。
  // [对照] 你没有。

  auto parse_arguments = [&](int argc, char** argv,
                             CommandLineOptions* vm_options,
                             CommandLineOptions* dart_options,
                             bool parsing_dart_vm_options) {
    bool success = Options::ParseArguments(
        argc, argv, vm_run_app_snapshot, parsing_dart_vm_options, vm_options,
        &script_name, dart_options, &print_flags_seen, &verbose_debug_seen);
    if (!success) {
      if (Options::help_option()) {
        Options::PrintUsage();
        Platform::Exit(0);
      } else if (Options::version_option()) {
        Options::PrintVersion();
        Platform::Exit(0);
      } else if (print_flags_seen) {
        // Will set the VM flags, print them out and then we exit as no
        // script was specified on the command line.
        char* error =
            Dart_SetVMFlags(vm_options->count(), vm_options->arguments());
        if (error != nullptr) {
          Syslog::PrintErr("Setting VM flags failed: %s\n", error);
          free(error);
          Platform::Exit(kErrorExitCode);
        }
        Platform::Exit(0);
      } else {
        // This usage error case will only be invoked when
        // Options::disable_dart_dev() is false.
        Options::PrintUsage();
        Platform::Exit(kErrorExitCode);
      }
    }
  };
  // [含义] 完整 CLI 参数解析和错误出口。
  // [对照] 你没有（Initialize 不承担 CLI 调度）。

  AppSnapshot* app_snapshot = nullptr;
#if defined(DART_PRECOMPILED_RUNTIME)
  // If the executable binary contains the runtime together with an appended
  // snapshot, load and run that.
  // Any arguments passed to such an executable are meant for the actual
  // application so skip all Dart VM flag parsing.

  const size_t kPathBufSize = PATH_MAX + 1;
  char executable_path[kPathBufSize];
  if (Platform::ResolveExecutablePathInto(executable_path, kPathBufSize) > 0) {
    app_snapshot = Snapshot::TryReadAppendedAppSnapshotElf(executable_path);
    if (app_snapshot != nullptr) {
      script_name = argv[0];

      // Store the executable name.
      Platform::SetExecutableName(argv[0]);

      // Parse out options to be passed to dart main.
      for (int i = 1; i < argc; i++) {
        dart_options.AddArgument(argv[i]);
      }

      // Parse DART_VM_OPTIONS options.
      int env_argc = 0;
      char** env_argv = Options::GetEnvArguments(&env_argc);
      if (env_argv != nullptr) {
        // Any Dart options that are generated based on parsing DART_VM_OPTIONS
        // are useless, so we'll throw them away rather than passing them along.
        CommandLineOptions tmp_options(env_argc + EXTRA_VM_ARGUMENTS);
        parse_arguments(env_argc, env_argv, &vm_options, &tmp_options,
                        /*parsing_dart_vm_options=*/true);
      }
    }
  }
#endif
  // [含义] AOT 主线可从可执行文件尾部附加 snapshot 自举，并解析环境参数。
  // [对照] 你没有（这部分在你库里是 CreateIsolateFromProgramFile 路径承担）。

  // Parse command line arguments.
  if (app_snapshot == nullptr) {
    parse_arguments(argc, argv, &vm_options, &dart_options,
                    /*parsing_dart_vm_options=*/false);
  }
  // [含义] 若没附加 snapshot，按普通 CLI 解析。
  // [对照] 你没有。

  DartUtils::SetEnvironment(Options::environment());
  // [含义] 应用环境变量映射到 Dart 运行环境。
  // [对照] 你没有。

  if (Options::suppress_core_dump()) {
    Platform::SetCoreDumpResourceLimit(0);
  } else {
    InitializeCrashpadClient();
  }
  // [含义] 崩溃/转储策略设置。
  // [对照] 你没有。

  Loader::InitOnce();
  // [含义] 初始化内建库加载器。
  // [对照] 你有（src/dartvm_embed_lib.cpp:791）。

  auto try_load_snapshots_lambda = [&](void) -> void {
    if (app_snapshot == nullptr) {
      // For testing purposes we add a flag to debug-mode to use the
      // in-memory ELF loader.
      const bool force_load_elf_from_memory =
          false DEBUG_ONLY(|| Options::force_load_elf_from_memory());
      app_snapshot =
          Snapshot::TryReadAppSnapshot(script_name, force_load_elf_from_memory);
    }
    if (app_snapshot != nullptr && app_snapshot->IsJITorAOT()) {
      if (app_snapshot->IsAOT() && !Dart_IsPrecompiledRuntime()) {
        Syslog::PrintErr(
            "%s is an AOT snapshot and should be run with 'dartaotruntime'\n",
            script_name);
        Platform::Exit(kErrorExitCode);
      }
      if (app_snapshot->IsJIT() && Dart_IsPrecompiledRuntime()) {
        Syslog::PrintErr(
            "%s is a JIT snapshot, it cannot be run with 'dartaotruntime'\n",
            script_name);
        Platform::Exit(kErrorExitCode);
      }
      vm_run_app_snapshot = true;
      app_snapshot->SetBuffers(&vm_snapshot_data, &vm_snapshot_instructions,
                               &app_isolate_snapshot_data,
                               &app_isolate_snapshot_instructions);
    } else if (app_snapshot == nullptr && Dart_IsPrecompiledRuntime()) {
      Syslog::PrintErr(
          "%s is not an AOT snapshot,"
          " it cannot be run with 'dartaotruntime'\n",
          script_name);
      Platform::Exit(kErrorExitCode);
    }
  };
  // [含义] 识别并校验 JIT/AOT snapshot，并把 VM/Isolate snapshot 指针接入初始化流程。
  // [对照] 你在 Initialize 里没有；你把这块拆到了 CreateIsolateFromProgramFile。

  // At this point, script_name now points to a script if DartDev is disabled
  // or a valid file path was provided as the first non-flag argument.
  // Otherwise, script_name can be nullptr if DartDev should be run.
  if (script_name != nullptr) {
    if (!CheckForInvalidPath(script_name)) {
      Platform::Exit(0);
    }
    try_load_snapshots_lambda();
  }
  // [含义] 运行前脚本路径有效性检查 + snapshot 尝试加载。
  // [对照] 你没有。

#if defined(DART_PRECOMPILED_RUNTIME)
  vm_options.AddArgument("--precompilation");
#endif
  // [含义] precompiled runtime 标志。
  // [对照] 你有（794-796）。

  if (Options::gen_snapshot_kind() == kAppJIT) {
    // App-jit snapshot can be deployed to another machine,
    // so generated code should not depend on the CPU features
    // of the system where snapshot was generated.
    vm_options.AddArgument("--target-unknown-cpu");
#if !defined(TARGET_ARCH_IA32)
    vm_options.AddArgument("--link_natives_lazily");
#endif
  }
  // [含义] app-jit 生成场景的额外 VM flag。
  // [对照] 你没有。

  // If we need to write an app-jit snapshot or a depfile, then add an exit
  // hook that writes the snapshot and/or depfile as appropriate.
  if ((Options::gen_snapshot_kind() == kAppJIT) ||
      (Options::depfile() != nullptr)) {
    Process::SetExitHook(OnExitHook);
  }
  // [含义] 退出钩子处理 snapshot/depfile 输出。
  // [对照] 你没有。

  char* error = nullptr;
  if (!dart::embedder::InitOnce(&error)) {
    Syslog::PrintErr("Standalone embedder initialization failed: %s\n", error);
    free(error);
    Platform::Exit(kErrorExitCode);
  }
  // [含义] embedder 一次性初始化。
  // [对照] 你有（783-790）。

  error = Dart_SetVMFlags(vm_options.count(), vm_options.arguments());
  if (error != nullptr) {
    Syslog::PrintErr("Setting VM flags failed: %s\n", error);
    free(error);
    Platform::Exit(kErrorExitCode);
  }
  // [含义] 把累计的 VM flags 交给 VM。
  // [对照] 你有（807-816）；你还多了 verify_sdk_hash 过滤（主线 main 无）。

// Note: must read platform only *after* VM flags are parsed because
// they might affect how the platform is loaded.
#if !defined(DART_PRECOMPILED_RUNTIME)
  // Load vm_platform_strong.dill for dart:* source support.
  dfe.Init();
  dfe.set_verbosity(Options::verbosity_level());
  if (script_name != nullptr) {
    uint8_t* application_kernel_buffer = nullptr;
    intptr_t application_kernel_buffer_size = 0;
    dfe.ReadScript(script_name, app_snapshot, &application_kernel_buffer,
                   &application_kernel_buffer_size);
    if (application_kernel_buffer != nullptr) {
      // Since we loaded the script anyway, save it.
      dfe.set_application_kernel_buffer(application_kernel_buffer,
                                        application_kernel_buffer_size);
      Options::dfe()->set_use_dfe();
    }
  }
#endif
  // [含义] DFE 初始化、verbosity 配置、预读脚本并缓存 application kernel。
  // [对照] 你有但简化：你只有 dfe.Init + set_use_dfe + set_use_incremental_compiler(true)。

  // Initialize the Dart VM.
  Dart_InitializeParams init_params;
  memset(&init_params, 0, sizeof(init_params));
  init_params.version = DART_INITIALIZE_PARAMS_CURRENT_VERSION;
  init_params.vm_snapshot_data = vm_snapshot_data;
  init_params.vm_snapshot_instructions = vm_snapshot_instructions;
  init_params.create_group = CreateIsolateGroupAndSetup;
  init_params.initialize_isolate = OnIsolateInitialize;
  init_params.shutdown_isolate = OnIsolateShutdown;
  init_params.cleanup_isolate = DeleteIsolateData;
  init_params.cleanup_group = DeleteIsolateGroupData;
  init_params.file_open = DartUtils::OpenFile;
  init_params.file_read = DartUtils::ReadFile;
  init_params.file_write = DartUtils::WriteFile;
  init_params.file_close = DartUtils::CloseFile;
  init_params.entropy_source = DartUtils::EntropySource;
  init_params.get_service_assets = GetVMServiceAssetsArchiveCallback;
#if !defined(DART_PRECOMPILED_RUNTIME)
  init_params.start_kernel_isolate =
      dfe.UseDartFrontend() && dfe.CanUseDartFrontend();
#else
  init_params.start_kernel_isolate = false;
#endif
#if defined(DART_HOST_OS_FUCHSIA)
#if defined(DART_PRECOMPILED_RUNTIME)
  init_params.vmex_resource = ZX_HANDLE_INVALID;
#else
  init_params.vmex_resource = Platform::GetVMEXResource();
#endif
#endif
  // [含义] 组装 VM 初始化参数。
  // [对照] 你有（826-853）但有差异：
  //        1) 你没有 get_service_assets；
  //        2) 你没有 Fuchsia vmex_resource 分支；
  //        3) start_kernel_isolate 判定策略不同（你用 config/固定值）。

  error = Dart_Initialize(&init_params);
  if (error != nullptr) {
    dart::embedder::Cleanup();
    Syslog::PrintErr("VM initialization failed: %s\n", error);
    free(error);
    Platform::Exit(kErrorExitCode);
  }
  // [含义] 真正初始化 VM，失败则清理并退出。
  // [对照] 你有（854-862）。

  Dart_SetServiceStreamCallbacks(&ServiceStreamListenCallback,
                                 &ServiceStreamCancelCallback);
  Dart_SetFileModifiedCallback(&FileModifiedCallback);
  Dart_SetEmbedderInformationCallback(&EmbedderInformationCallback);
  // [含义] 注册 service stream / 文件变更 / embedder 信息回调。
  // [对照] 你有前两项（864-878）；你没有 EmbedderInformationCallback。

  bool ran_dart_dev = false;
  bool should_run_user_program = true;
#if !defined(DART_PRECOMPILED_RUNTIME)
  if (DartDevIsolate::should_run_dart_dev() && !Options::disable_dart_dev() &&
      Options::gen_snapshot_kind() == SnapshotKind::kNone) {
    DartDevIsolate::DartDev_Result dartdev_result = DartDevIsolate::RunDartDev(
        CreateIsolateGroupAndSetup, &package_config_override, &script_name,
        &vm_options, &dart_options);
    ASSERT(dartdev_result != DartDevIsolate::DartDev_Result_Unknown);
    ran_dart_dev = true;
    should_run_user_program =
        (dartdev_result == DartDevIsolate::DartDev_Result_Run);
    if (should_run_user_program) {
      try_load_snapshots_lambda();
    }
  } else if (script_name == nullptr &&
             Options::gen_snapshot_kind() != SnapshotKind::kNone) {
    Syslog::PrintErr(
        "Snapshot generation should be done using the 'dart compile' "
        "command.\n");
    Platform::Exit(kErrorExitCode);
  }

  if (!ran_dart_dev &&
      (Options::resident() ||
       Options::resident_compiler_info_file_path() != nullptr ||
       Options::resident_server_info_file_path() != nullptr)) {
    Syslog::PrintErr(
        "Passing the `--resident` flag to `dart` is invalid. It must be passed "
        "to `dart run`.\n");
    Platform::Exit(kErrorExitCode);
  }
#endif  // !defined(DART_PRECOMPILED_RUNTIME)
  // [含义] dartdev 与 resident 编译器入口控制。
  // [对照] 你没有（库中由调用方自己编排运行流程）。

  if (should_run_user_program) {
    if (Options::gen_snapshot_kind() == kKernel) {
      CompileAndSaveKernel(script_name, package_config_override, &dart_options);
    } else {
      // Run the main isolate until we aren't told to restart.
      RunMainIsolate(script_name, package_config_override, &dart_options);
    }
  }
  // [含义] 主线直接驱动“编译/运行主程序”。
  // [对照] 你没有（你把运行放在 CreateIsolate/RunEntry API）。

  // Terminate process exit-code handler.
  Process::TerminateExitCodeHandler();

  error = Dart_Cleanup();
  if (error != nullptr) {
    Syslog::PrintErr("VM cleanup failed: %s\n", error);
    free(error);
  }
  const intptr_t global_exit_code = Process::GlobalExitCode();
  dart::embedder::Cleanup();
  // [含义] 主线进程尾部统一清理。
  // [对照] 你有对应语义但在 DartVmEmbed_Cleanup（884+）中单独暴露。

  delete app_snapshot;
  free(app_script_uri);
  if (ran_dart_dev && script_name != nullptr) {
    free(script_name);
  }

  // Free copied argument strings if converted.
  if (argv_converted) {
    for (int i = 0; i < argc; i++) {
      free(argv[i]);
    }
  }

  // Free environment if any.
  Options::Cleanup();

  Platform::Exit(global_exit_code);
}
```
