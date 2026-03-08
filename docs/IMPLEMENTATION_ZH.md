# dartvm_embed_lib 实现详解（逐函数 + patch 设计理由）

本文档面向维护者，目标是回答两类问题：
1. `src/dartvm_embed_lib.cpp` 中每一个函数到底做什么、处于 Dart VM 生命周期哪个阶段、实现思路是什么、具体调用了 Dart 哪些 API、API 在 Dart 源码哪里实现。
2. `dart_sdk.patch` 为什么要这么改，patch 中每个模块/源码项有什么含义，为什么要导入。

## 1. 总体分层

`dartvm_embed_lib` 是一个 C ABI 的宿主层（host runtime facade）。

- VM 级生命周期：初始化和清理。
- Isolate/IsolateGroup 生命周期：JIT(AOT) 两条创建路径。
- 程序工件装载：`.dill` / AOT ELF(`program.bin`)。
- 入口执行：`main` 或自定义 entry。
- 事件循环与回收：`Dart_RunLoop` + isolate 资源回收。

对应 Dart 上游的语义基线基本与 `runtime/bin/main_impl.cc` 一致（`dart`/`dartaotruntime` 主线）。

---

## 2. `src/dartvm_embed_lib.cpp` 逐函数说明

### 2.1 `SetupCoreNativeResolvers`

- 函数作用
  - 给核心库安装 native resolver：builtin/io/cli/vmservice。

- 在 Dart VM 中的作用
  - 处于 isolate 初始化后的“原生函数绑定阶段”。
  - 不做这一步时，依赖 `dart:io` 等 native binding 的路径会在调用时失败。

- 实现思路
  - 进入 scope，调用 bin 层 resolver 注册，再退出 scope。

- 调用 API 与实现位置
  - 调用：`dart::bin::Builtin::SetNativeResolver`、`dart::bin::VmService::SetNativeResolver`
  - 实现：
    - `runtime/bin/builtin.cc`
    - `runtime/bin/vmservice_impl.cc`
  - 底层最终走：`Dart_SetNativeResolver`（实现：`runtime/vm/dart_api_impl.cc:5921`）

### 2.2 `OnIsolateInitialize`

- 函数作用
  - isolate 初始化回调。

- 在 Dart VM 中的作用
  - 这是 `Dart_InitializeParams.initialize_isolate` 钩子，对每个 isolate 生效。

- 实现思路
  - 清空回调输出参数。
  - 调用 `SetupCoreNativeResolvers`，确保核心库 native resolver 已注册。

- 调用 API 与实现位置
  - 回调由 VM 在 isolate 创建流程触发。
  - 触发入口来自 `Dart_Initialize` 参数中的回调配置（`Dart_Initialize` 实现：`runtime/vm/dart_api_impl.cc:1157`）。

### 2.3 `OnIsolateShutdown`

- 函数作用
  - isolate shutdown 回调（当前为空实现）。

- 在 Dart VM 中的作用
  - 预留 isolate 结束时的自定义清理入口。

- 实现思路
  - 当前无动作，主要依赖本库的显式清理函数。

- 调用 API 与实现位置
  - 通过 `Dart_InitializeParams.shutdown_isolate` 注册，回调由 VM 生命周期触发。

### 2.4 `CleanupIsolate`

- 函数作用
  - isolate cleanup 回调（当前为空实现）。

- 在 Dart VM 中的作用
  - VM 结束 isolate 后提供额外清理点。

- 实现思路
  - 当前未使用，真实清理在 `DartVmEmbed_ShutdownIsolate`。

- 调用 API 与实现位置
  - 通过 `Dart_InitializeParams.cleanup_isolate` 注册。

### 2.5 `CleanupGroup`

- 函数作用
  - isolate group cleanup 回调（当前为空实现）。

- 在 Dart VM 中的作用
  - isolate group 生命周期尾部的自定义清理点。

- 实现思路
  - 当前未使用，真实 group 数据释放走 owned-state map。

- 调用 API 与实现位置
  - 通过 `Dart_InitializeParams.cleanup_group` 注册。

### 2.6 `DupMessage`

- 函数作用
  - 复制错误字符串，统一返回可释放的 `char*`。

- 在 Dart VM 中的作用
  - 作为跨 API 边界的错误信息桥接，避免使用临时指针。

- 实现思路
  - `malloc + memcpy`。

- 调用 API 与实现位置
  - 宿主工具函数，不依赖 Dart API。

### 2.7 `IsUnsupportedVerifySdkHashFlag`

- 函数作用
  - 识别并过滤 `verify_sdk_hash` 相关 flag。

- 在 Dart VM 中的作用
  - 解决某些构建/运行组合下该 flag 不被识别导致初始化失败的问题。

- 实现思路
  - 字符串白名单比较。

- 调用 API 与实现位置
  - 结果用于调用 `Dart_SetVMFlags` 前的参数清洗。
  - `Dart_SetVMFlags` 实现：`runtime/vm/dart_api_impl.cc:1178`。

### 2.8 `ReadProgramFile`

- 函数作用
  - 从文件读取程序工件到内存（JIT 的 kernel 文件）。

- 在 Dart VM 中的作用
  - 是 JIT 路径“文件 -> 内存 buffer -> VM 注入”的第一步。

- 实现思路
  - 二进制打开、获取长度、读取到 `std::vector<uint8_t>`。

- 调用 API 与实现位置
  - 本身是文件 I/O；后续 buffer 被传给 `Dart_CreateIsolateGroupFromKernel` / `Dart_LoadScriptFromKernel`。

### 2.9 `SetupCurrentIsolate`（仅 `DARTVM_EMBED_ENABLE_FULL_ISOLATE_SETUP`）

- 函数作用
  - 对“当前 isolate”做完整 setup（接近 `main_impl.cc` 的 setup 语义）。

- 在 Dart VM 中的作用
  - 位于 create isolate 之后、加载脚本/可运行化之前。

- 实现思路
  - 安装 library tag handler。
  - 安装 deferred load handler。
  - 调用 `PrepareForScriptLoading`。
  - 调用 `SetupPackageConfig`。
  - 注册 environment callback。
  - 执行 `Loader::InitForSnapshot`。

- 调用 API 与实现位置
  - `Dart_SetLibraryTagHandler`：`runtime/vm/dart_api_impl.cc:5411`
  - `Dart_SetDeferredLoadHandler`：`runtime/vm/dart_api_impl.cc:5419`
  - `Dart_SetEnvironmentCallback`：`runtime/vm/dart_api_impl.cc:5368`
  - `dart::bin::DartUtils::PrepareForScriptLoading`：`runtime/bin/dartutils.cc:570`
  - `dart::bin::DartUtils::SetupPackageConfig`：`runtime/bin/dartutils.cc:555`
  - `dart::bin::Loader::InitForSnapshot`：`runtime/bin/loader.cc:21`

### 2.10 `DartVmEmbed_Initialize`

- 函数作用
  - 初始化 VM（幂等）。

- 在 Dart VM 中的作用
  - 全局入口，属于 VM 生命周期 `init` 阶段。

- 实现思路
  - 可选先执行 `dart::embedder::InitOnce` 和 `Loader::InitOnce`。
  - 在 JIT full setup 下初始化 DFE。
  - 组装并设置 VM flags。
  - 填充 `Dart_InitializeParams`（snapshot 指针、isolate 回调、文件/熵回调）。
  - 调用 `Dart_Initialize`。

- 调用 API 与实现位置
  - `dart::embedder::InitOnce`：`runtime/bin/dart_embedder_api_impl.cc:33`
  - `dart::bin::Loader::InitOnce`：`runtime/bin/loader.cc:175`
  - `dart::bin::dfe.Init` / `set_use_dfe`：`runtime/bin/dfe.cc`
  - `Dart_SetVMFlags`：`runtime/vm/dart_api_impl.cc:1178`
  - `Dart_Initialize`：`runtime/vm/dart_api_impl.cc:1157`

### 2.11 `DartVmEmbed_Cleanup`

- 函数作用
  - 清理 VM。

- 在 Dart VM 中的作用
  - VM 生命周期尾部。

- 实现思路
  - 调用 `Dart_Cleanup`；full setup 时补 `dart::embedder::Cleanup`。

- 调用 API 与实现位置
  - `Dart_Cleanup`：`runtime/vm/dart_api_impl.cc:1173`
  - `dart::embedder::Cleanup`：`runtime/bin/dart_embedder_api_impl.cc`

### 2.12 `DartVmEmbed_CreateIsolateFromKernel`

- 函数作用
  - 通过 kernel buffer 创建 JIT isolate。

- 在 Dart VM 中的作用
  - 属于 JIT 运行路径核心创建点。

- 实现思路
  - 组装 `Dart_IsolateFlags`。
  - full setup 模式下创建并持有 `IsolateGroupData/IsolateData`。
  - 尝试通过 DFE 提供 platform kernel（否则退回到程序 kernel）。
  - 调 `Dart_CreateIsolateGroupFromKernel` 创建。
  - 可选注册 native resolver。
  - full setup 下执行 `SetupCurrentIsolate`。
  - `Dart_LoadScriptFromKernel` 加载脚本。
  - `Dart_IsolateMakeRunnable` 使 isolate 进入 runnable。
  - owned 资源登记到 `g_owned_isolates`。

- 调用 API 与实现位置
  - `Dart_CreateIsolateGroupFromKernel`：`runtime/vm/dart_api_impl.cc:1353`
  - `Dart_LoadScriptFromKernel`：`runtime/vm/dart_api_impl.cc:5426`
  - `Dart_IsolateMakeRunnable`：`runtime/vm/dart_api_impl.cc:1938`
  - 语义参考：`runtime/bin/main_impl.cc` 的 create/setup/load 路径（约 468, 490, 802 附近）

### 2.13 `DartVmEmbed_CreateIsolateFromAppSnapshot`

- 函数作用
  - 通过 app snapshot data/instructions 创建 AOT isolate。

- 在 Dart VM 中的作用
  - 属于 AOT 运行路径核心创建点。

- 实现思路
  - 与 kernel 路径相同的 group/isolate data 管理。
  - 调 `Dart_CreateIsolateGroup`。
  - 可选注册 resolver。
  - full setup 下执行 `SetupCurrentIsolate` 后 make runnable。

- 调用 API 与实现位置
  - `Dart_CreateIsolateGroup`：`runtime/vm/dart_api_impl.cc:1318`
  - `Dart_IsolateMakeRunnable`：`runtime/vm/dart_api_impl.cc:1938`

### 2.14 `DartVmEmbed_CreateIsolateFromProgramFile`

- 函数作用
  - 给外部一个统一入口：从程序文件创建 isolate（JIT/AOT 由编译期 flavor 决定）。

- 在 Dart VM 中的作用
  - 是库层“文件装载路由器”。

- 实现思路
  - AOT flavor：`LoadAotElf -> Initialize(override vm snapshots) -> CreateIsolateFromAppSnapshot`。
  - JIT flavor：`Initialize(no-precompilation) -> ReadProgramFile -> CreateIsolateFromKernel`。
  - 为资源回收登记对应 map（ELF handle 或 kernel buffer）。

- 调用 API 与实现位置
  - 间接调用上述创建 API。
  - AOT 路径依赖 `Dart_LoadELF`（见下一节）。

### 2.15 `DartVmEmbed_LoadAotElf`

- 函数作用
  - 从 AOT ELF 文件加载 snapshot 四元组（vm/iso data+instructions）。

- 在 Dart VM 中的作用
  - 是 AOT “文件 -> snapshot 指针”桥接层。

- 实现思路
  - 调 `Dart_LoadELF`，返回 `Dart_LoadedElf*` 作为 handle。

- 调用 API 与实现位置
  - `Dart_LoadELF` 声明：`runtime/bin/elf_loader.h:40`
  - `Dart_LoadELF` 实现：`runtime/bin/elf_loader.cc:587`

### 2.16 `DartVmEmbed_UnloadAotElf`

- 函数作用
  - 卸载 AOT ELF 映射。

- 在 Dart VM 中的作用
  - AOT 资源回收。

- 实现思路
  - 调 `Dart_UnloadELF`。

- 调用 API 与实现位置
  - 声明：`runtime/bin/elf_loader.h:63`
  - 实现：`runtime/bin/elf_loader.cc:639`

### 2.17 `DartVmEmbed_RunEntry`

- 函数作用
  - 运行指定库入口。

- 在 Dart VM 中的作用
  - isolate 已 runnable 后的执行入口。

- 实现思路
  - 先 `Dart_GetField(library, entry)` 获取 top-level getter/tear-off。
  - 若失败，fallback `Dart_Invoke(library, entry, 0, nullptr)`。
  - getter 成功并且是 closure 时，调用 `dart:isolate` 的 `_startMainIsolate`，最后 `Dart_RunLoop`。

- 调用 API 与实现位置
  - `Dart_GetField`：`runtime/vm/dart_api_impl.cc:4706`
  - `Dart_Invoke`：`runtime/vm/dart_api_impl.cc:4580`
  - `Dart_RunLoop`：`runtime/vm/dart_api_impl.cc:1997`
  - 同源语义：`runtime/bin/main_impl.cc`（约 1061, 1078, 1083）

### 2.18 `DartVmEmbed_RunRootEntry`

- 函数作用
  - 获取 root library 后调用 `RunEntry`。

- 在 Dart VM 中的作用
  - 根库入口执行包装。

- 实现思路
  - `Dart_RootLibrary` + `DartVmEmbed_RunEntry`。

- 调用 API 与实现位置
  - `Dart_RootLibrary`：`runtime/vm/dart_api_impl.cc:5480`

### 2.19 `DartVmEmbed_RunRootEntryChecked`

- 函数作用
  - 运行 root entry 并把错误转为 `char*`。

- 在 Dart VM 中的作用
  - 提供 C 侧友好的错误模型（bool + error message）。

- 实现思路
  - 调 `RunRootEntry`，若 `Dart_IsError` 则 `Dart_GetError`。

- 调用 API 与实现位置
  - `Dart_IsError` / `Dart_GetError`：`runtime/include/dart_api.h`（实现位于 `runtime/vm/dart_api_impl.cc`）。

### 2.20 `DartVmEmbed_RunRootEntryOnIsolate`

- 函数作用
  - 在指定 isolate 上执行 root entry。

- 在 Dart VM 中的作用
  - 多 isolate 场景下的调度包装。

- 实现思路
  - 若当前线程未进入 isolate，先 `Dart_EnterIsolate`。
  - 进入 scope 执行 `RunRootEntryChecked`。
  - 最后 `Dart_ExitIsolate`。

- 调用 API 与实现位置
  - `Dart_EnterIsolate`：`runtime/vm/dart_api_impl.cc:1526`
  - `Dart_ExitIsolate`：`runtime/vm/dart_api_impl.cc:1865`

### 2.21 `DartVmEmbed_RunLoop`

- 函数作用
  - 运行 isolate message loop。

- 在 Dart VM 中的作用
  - 驱动异步消息/事件，直到可退出。

- 实现思路
  - 直接转发 `Dart_RunLoop`。

- 调用 API 与实现位置
  - `Dart_RunLoop`：`runtime/vm/dart_api_impl.cc:1997`

### 2.22 `DartVmEmbed_ShutdownIsolate`

- 函数作用
  - 关闭当前 isolate，并回收本库维护资源。

- 在 Dart VM 中的作用
  - isolate 生命周期终点。

- 实现思路
  - 若存在当前 isolate，先 `Dart_ShutdownIsolate`。
  - 再清理 `g_isolate_loaded_aot_elfs`、`g_isolate_kernel_buffers`。
  - full setup 模式清理 owned `IsolateData/IsolateGroupData`。

- 调用 API 与实现位置
  - `Dart_ShutdownIsolate`：`runtime/vm/dart_api_impl.cc:1415`

### 2.23 `DartVmEmbed_ShutdownIsolateByHandle`

- 函数作用
  - 按 isolate handle 执行 shutdown。

- 在 Dart VM 中的作用
  - 给外部显式控制特定 isolate 的结束。

- 实现思路
  - 若当前线程未进入 isolate，先 enter，再调用 `DartVmEmbed_ShutdownIsolate`。

- 调用 API 与实现位置
  - 依赖 `Dart_EnterIsolate`/`Dart_ShutdownIsolate`。

---

## 3. 关键全局状态为什么存在

- `g_vm_initialized`
  - 防止重复 `Dart_Initialize`。

- `g_isolate_loaded_aot_elfs`
  - isolate 与 AOT ELF handle 的绑定关系。
  - 作用：shutdown 时可正确 `Dart_UnloadELF`。

- `g_isolate_kernel_buffers`
  - isolate 与 kernel buffer 的生命周期绑定。
  - 作用：避免 VM 仍在使用 kernel 数据时 buffer 已释放。

- `g_owned_isolates`（full setup）
  - 记录是否由库内创建 `IsolateGroupData/IsolateData`。
  - 作用：只释放“自己拥有”的对象，避免双重释放。

---

## 4. `dart_sdk.patch` 逐模块说明（为什么要这样改）

> 该 patch 只改了两个 GN 文件：
> - `BUILD.gn`
> - `runtime/bin/BUILD.gn`

### 4.1 顶层 `BUILD.gn` 新增 `group("libdart_embedder")`

- 模块含义
  - 在 SDK 顶层新增一个聚合目标，把 embedder runtime 的不同 flavor 打包成同一个入口。

- 为什么要导入
  - 你外部只执行一次 `build.py ... libdart_embedder`，即可拿全套库，不需要分别记很多 runtime/bin 子目标。

- 包含的子目标
  - `runtime/bin:dart_embedder_runtime_jit_shared`
  - `runtime/bin:dart_embedder_runtime_jit_static`
  - `runtime/bin:dart_embedder_runtime_aot_shared`
  - `runtime/bin:dart_embedder_runtime_aot_static`
  - `runtime/bin:dart_embedder_runtime_interp_shared`
  - `runtime/bin:dart_embedder_runtime_interp_static`

### 4.2 `runtime/bin/BUILD.gn` 新增 3 个 `source_set`

#### 4.2.1 `dart_embedder_runtime_jit_full_set`

- 模块含义
  - 可用于 JIT/embedder 的完整 runtime/bin 管线。

- 为什么要导入这些源码
  - `builtin.cc`：注册内建库 native resolver。
  - `dart_embedder_api_impl.cc`：embedder 统一初始化入口（`InitOnce/Cleanup`）。
  - `dfe.cc`：JIT 前端编译服务（platform kernel/增量编译相关）。
  - `loader.cc`：URI/包/库加载策略。
  - `main_options.cc` + `options.cc`：命令行/VM 选项解析逻辑复用。
  - `snapshot_utils.cc`：snapshot 相关工具。
  - `vmservice_impl.cc`：vm-service isolate 与 resolver。
  - `gzip.cc`：压缩数据处理支持（runtime 资源路径需要）。
  - `error_exit.cc`：统一错误退出辅助。

- 为什么要导入这些依赖
  - `:dart_kernel_platform_cc` / `:dart_snapshot_cc`：JIT 需要 platform/kernel/snapshot 资源。
  - `:standalone_dart_io`：`dart:io` 独立运行时实现。
  - `..:libdart_jit` + `../platform:libdart_platform_jit`：JIT VM 核心与平台层。
  - `boringssl`/`zlib`：TLS/压缩基础依赖。

#### 4.2.2 `dart_embedder_runtime_aot_full_set`

- 模块含义
  - AOT 可运行管线（非 precompiled config）。

- 为什么要导入这些源码
  - 与 JIT 版本近似，但不引入 `dfe.*`，改用 `snapshot_empty.cc`。
  - 原因：AOT 运行期不需要 JIT 前端编译服务；snapshot 来源是外部 AOT 工件。

- 为什么要导入这些依赖
  - `:elf_loader`：AOT ELF 装载入口（`Dart_LoadELF/Dart_UnloadELF`）。
  - `..:libdart_aotruntime` + `../platform:libdart_platform_aotruntime`：AOT VM 核心与平台层。
  - 其余 `standalone_dart_io/boringssl/zlib` 同上。

#### 4.2.3 `dart_embedder_runtime_aot_precompiled_full_set`

- 模块含义
  - dartaotruntime 语义等价的 precompiled AOT 运行时。

- 为什么要导入 `configs += ["..:dart_aotruntime_config"]`
  - 强制使用 precompiled 语义一致的编译配置，避免 runtime 与 snapshot 风格不一致。

- 为什么额外导入 `:native_assets_api`
  - precompiled 运行时场景需要 native assets 接口以匹配主线运行时能力。

### 4.3 新增静态/动态库目标

- `dart_embedder_runtime_jit_static/shared`
- `dart_embedder_runtime_aot_static/shared`
- `dart_embedder_runtime_aot_precompiled_static/shared`

模块含义：
- 把前面的 full_set 封装成可直接消费的库产物。

为什么要这样做：
- 让外部 embedding 项目直接链接，不需要理解 runtime/bin 内部大量 source_set。

### 4.4 新增 “interp” 目标（group）

- `dart_embedder_runtime_interp_static/shared` 只是映射到 JIT。

模块含义：
- 对外暴露“解释模式”命名，但实现上 Dart VM 在这个管线实际走 JIT runtime。

为什么这样做：
- API/构建接口兼容层，便于上层用统一术语选择 flavor。

---

## 5. 为什么主线工具也调用 embedder API（语义是否冲突）

不冲突，恰恰是“语义对齐”的实现方式。

- `dart` / `dartaotruntime` 是官方宿主程序。
- embedder API 是“宿主与 VM 的契约层”。
- 主线工具复用这层 API，意味着外部宿主可以复用同一套生命周期与错误语义，而不是再发明一套私有启动流程。

这也是你现在库里很多实现看起来和 `main_impl.cc` 很像的根本原因。

---

## 6. 与 `main_impl.cc` 的语义对应（你关心的“统一归一”）

你这份 `dartvm_embed_lib.cpp` 的关键路径与 `runtime/bin/main_impl.cc` 一一对应：

- 创建组：`Dart_CreateIsolateGroupFromKernel` / `Dart_CreateIsolateGroup`
- setup：library tag/deferred/environment/package/loader init
- 加载：`Dart_LoadScriptFromKernel`（JIT）或 AOT snapshot 指针
- runnable：`Dart_IsolateMakeRunnable`
- 入口：`_startMainIsolate` + `Dart_RunLoop`

所以 JIT/AOT 在“工件输入”不同，但在“创建 isolate group + runnable + run loop”的后半段会归于同一套 VM 生命周期模型。

---

## 7. 你后续要扩 VM-service 时的落点

推荐最小改动路径：

1. 在 `DartVmEmbedInitConfig.vm_flags` 注入 service flags
   - `--enable-vm-service=<port>`
   - 调试可选：`--disable-service-auth-codes`

2. 保持当前 isolate 创建路径不变
   - 重点是 init flags，不是重写 create 流程。

3. 若需要更细控制
   - 复用 `runtime/bin/vmservice_impl.cc` 的语义，而不是另写一套 service isolate 管理。

---

## 8. 维护者常见误区（针对当前实现）

- `Dart_GetField(lib, entry)` 失败并不代表入口不能运行。
  - 某些模式下 top-level getter 不可见或被 tree-shaking/entrypoint 规则影响，故需要 `Dart_Invoke` fallback。

- AOT 不是 `execve` 程序加载语义。
  - 这里是“宿主进程内映射 ELF 并把 snapshot 指针交给 VM”。
  - 不是替换进程镜像，不走 `execve` 跳入口。

- isolate 不是 OS 线程。
  - isolate 是 VM 隔离执行单元；可由线程池调度，和线程不是 1:1 概念。


---

## 9. `runtime/include` 核心 API 说明（按 Dart 运行过程）

这一节只挑“嵌入运行链路”中真正核心的 API，避免把 `dart_api.h` 全量罗列成索引。

### 9.1 先看完整运行时序（从宿主角度）

1. 进程启动后，先做 embedder 子系统初始化。
2. 设置 VM flags。
3. 调 `Dart_Initialize` 初始化 VM。
4. 创建 isolate/group（JIT 从 kernel，AOT 从 snapshot）。
5. 安装加载器/环境回调（按需）。
6. 使 isolate runnable。
7. 调入口函数并进入 `Dart_RunLoop`。
8. 运行结束后关闭 isolate。
9. 最后调用 `Dart_Cleanup`。

下面 API 说明都按这个顺序组织。

### 9.2 阶段 A：Embedder 子系统初始化（`dart_embedder_api.h`）

#### `dart::embedder::InitOnce` (`runtime/include/dart_embedder_api.h:21`)

- 作用
  - 初始化 runtime/bin 侧的 embedder 子系统（如 timer、平台相关初始化）。

- 在整个 Dart VM 中的作用
  - 位于 `Dart_Initialize` 之前，属于“宿主预初始化阶段”。

- 使用逻辑
  - 只调用一次；失败则不应继续 `Dart_Initialize`。

#### `dart::embedder::Cleanup` (`runtime/include/dart_embedder_api.h:27`)

- 作用
  - 清理 `InitOnce` 建立的 embedder 子系统状态。

- 在整个 Dart VM 中的作用
  - 位于 `Dart_Cleanup` 之后，属于“宿主收尾阶段”。

- 使用逻辑
  - 与 `InitOnce` 成对调用；确保进程多次启动/停止 VM 时状态干净。

### 9.3 阶段 B：VM 初始化（`dart_api.h`）

#### `Dart_SetVMFlags` (`runtime/include/dart_api.h:988`)

- 作用
  - 设置 VM 启动参数。

- 在整个 Dart VM 中的作用
  - 初始化前的配置注入点。

- 使用逻辑
  - 必须在 `Dart_Initialize` 前调用；调用后失败返回 `char* error`。

#### `Dart_Initialize` (`runtime/include/dart_api.h:963`)

- 作用
  - 初始化 Dart VM。

- 在整个 Dart VM 中的作用
  - VM 生命周期起点。

- 使用逻辑
  - 传 `Dart_InitializeParams`：snapshot 指针、isolate 回调、文件/熵回调等。
  - 成功后才能创建 isolate。

#### `Dart_Cleanup` (`runtime/include/dart_api.h:975`)

- 作用
  - 关闭 VM。

- 在整个 Dart VM 中的作用
  - VM 生命周期终点。

- 使用逻辑
  - 应在 isolate 全部退出后调用；失败返回错误字符串。

### 9.4 阶段 C：创建 IsolateGroup / Isolate

#### `Dart_CreateIsolateGroup` (`runtime/include/dart_api.h:1045`)

- 作用
  - 用 snapshot（data/instructions）创建 isolate group。

- 在整个 Dart VM 中的作用
  - AOT 主路径的创建接口；也是通用 snapshot 路径。

- 使用逻辑
  - 传 `script_uri/name/flags/isolate_group_data/isolate_data`。
  - 返回 `Dart_Isolate`，失败用 `error` 输出。

#### `Dart_CreateIsolateGroupFromKernel` (`runtime/include/dart_api.h:1117`)

- 作用
  - 用 kernel buffer 创建 isolate group。

- 在整个 Dart VM 中的作用
  - JIT 主路径创建接口。

- 使用逻辑
  - 传 kernel bytes 与大小。
  - 后续一般还需要 `Dart_LoadScriptFromKernel`（按宿主流程而定）。

#### `Dart_LoadScriptFromKernel` (`runtime/include/dart_api.h:3550`)

- 作用
  - 把程序 kernel 装入当前 isolate 作为 root script。

- 在整个 Dart VM 中的作用
  - JIT 创建后、入口执行前的装载阶段。

- 使用逻辑
  - 需要当前线程已进入该 isolate 且在 scope 内。

#### `Dart_IsolateMakeRunnable` (`runtime/include/dart_api.h:1474`)

- 作用
  - 将新 isolate 从创建态切换到可运行态。

- 在整个 Dart VM 中的作用
  - 创建阶段与执行阶段之间的状态转换点。

- 使用逻辑
  - 常见顺序：create -> setup/load -> exit isolate -> make runnable。

### 9.5 阶段 D：脚本加载/库解析相关回调

#### `Dart_SetLibraryTagHandler` (`runtime/include/dart_api.h:3483`)

- 作用
  - 注册库加载标签处理器（import/export/part 等）。

- 在整个 Dart VM 中的作用
  - 影响 URI 解析与库加载行为，是宿主接管 loader 的关键点。

- 使用逻辑
  - 一般在 isolate setup 阶段设置一次，配合 `Loader::LibraryTagHandler`。

#### `Dart_SetDeferredLoadHandler` (`runtime/include/dart_api.h:3506`)

- 作用
  - 注册 deferred load 处理器。

- 在整个 Dart VM 中的作用
  - 控制延迟加载库的解析流程。

- 使用逻辑
  - 通常与 `Dart_SetLibraryTagHandler` 成对设置。

#### `Dart_SetEnvironmentCallback` (`runtime/include/dart_api.h:3266`)

- 作用
  - 注册环境变量读取回调。

- 在整个 Dart VM 中的作用
  - 让 Dart 程序读取宿主环境配置（`String.fromEnvironment` 等场景）。

- 使用逻辑
  - isolate setup 阶段设置；由 VM 在环境查询时回调。

#### `Dart_SetNativeResolver` (`runtime/include/dart_api.h:3277`)

- 作用
  - 为某个库注册 native 函数解析器。

- 在整个 Dart VM 中的作用
  - 连接 Dart 层 native 声明与 C/C++ 实现。

- 使用逻辑
  - 常用于 `dart:io`、vmservice、内建库；在 isolate 初始化后配置。

### 9.6 阶段 E：执行入口与事件循环

#### `Dart_RootLibrary` (`runtime/include/dart_api.h:3561`)

- 作用
  - 获取当前 isolate 的 root library。

- 在整个 Dart VM 中的作用
  - 入口调用前定位“目标库”。

- 使用逻辑
  - 常配合 `Dart_GetField` / `Dart_Invoke` 调入口。

#### `Dart_GetField` (`runtime/include/dart_api.h:2889`)

- 作用
  - 从库/对象获取字段或 top-level getter。

- 在整个 Dart VM 中的作用
  - 入口 closure 获取路径（如拿 `main` tear-off）。

- 使用逻辑
  - 若返回错误，可 fallback 到 `Dart_Invoke` 直接调用函数名。

#### `Dart_Invoke` (`runtime/include/dart_api.h:2822`)

- 作用
  - 调用对象/库上的方法。

- 在整个 Dart VM 中的作用
  - 入口执行、宿主主动触发 Dart 方法调用的基础 API。

- 使用逻辑
  - 参数个数与 `Dart_Handle[]` 一致；错误通过 `Dart_IsError` 检查。

#### `Dart_RunLoop` (`runtime/include/dart_api.h:1684`)

- 作用
  - 运行当前 isolate 消息循环。

- 在整个 Dart VM 中的作用
  - 异步调度驱动器，主执行阶段核心 API。

- 使用逻辑
  - 通常在入口调起后调用，直到可退出或返回错误。

### 9.7 阶段 F：线程/隔离上下文与退出

#### `Dart_EnterIsolate` (`runtime/include/dart_api.h:1214`)

- 作用
  - 让当前线程进入指定 isolate 上下文。

- 在整个 Dart VM 中的作用
  - 多 isolate 管理时的线程-上下文切换入口。

- 使用逻辑
  - 对应调用 `Dart_ExitIsolate`；跨线程操作 isolate 前必须 enter。

#### `Dart_ExitIsolate` (`runtime/include/dart_api.h:1415`)

- 作用
  - 当前线程退出 isolate 上下文。

- 在整个 Dart VM 中的作用
  - 与 `EnterIsolate` 成对，保证上下文正确释放。

- 使用逻辑
  - 尤其在手动调度多个 isolate 时必须严格配对。

#### `Dart_ShutdownIsolate` (`runtime/include/dart_api.h:1132`)

- 作用
  - 关闭当前 isolate。

- 在整个 Dart VM 中的作用
  - isolate 生命周期终点。

- 使用逻辑
  - 调用后，宿主还需要清理自己持有的映射资源（ELF handle、kernel buffer、回调数据）。

### 9.8 AOT 装载相关（声明不在 `runtime/include` 顶层，但属于关键路径）

#### `Dart_LoadELF` / `Dart_UnloadELF` (`runtime/bin/elf_loader.h:40,63`)

- 作用
  - 将 AOT ELF 映射并提取 snapshot 指针；结束后卸载映射。

- 在整个 Dart VM 中的作用
  - AOT 文件装载阶段的入口，不是 `execve` 模型，而是“进程内映射+VM 消费指针”。

- 使用逻辑
  - `LoadELF -> Dart_Initialize(override vm snapshot) -> Dart_CreateIsolateGroup -> Shutdown -> UnloadELF`。

### 9.9 vm-service 与 tools（`dart_tools_api.h`）

#### `Dart_RegisterIsolateServiceRequestCallback` (`runtime/include/dart_tools_api.h:101`)

- 作用
  - 注册 isolate 级 vm-service RPC 处理器。

- 在整个 Dart VM 中的作用
  - 扩展 vm-service 命令面向 isolate 请求。

- 使用逻辑
  - 回调在目标 isolate 上下文内执行。

#### `Dart_RegisterRootServiceRequestCallback` (`runtime/include/dart_tools_api.h:118`)

- 作用
  - 注册 root 级 vm-service RPC 处理器。

- 在整个 Dart VM 中的作用
  - 扩展全局 vm-service 命令。

- 使用逻辑
  - 回调执行时可能没有当前 isolate。

#### `Dart_InvokeVMServiceMethod` (`runtime/include/dart_tools_api.h:183`)

- 作用
  - 宿主主动发起 vm-service JSON-RPC 调用。

- 在整个 Dart VM 中的作用
  - 调试/诊断控制面 API。

- 使用逻辑
  - 只能在 `Dart_Initialize` 之后且 `Dart_Cleanup` 之前使用。

#### `Dart_SetServiceStreamCallbacks` (`runtime/include/dart_tools_api.h:225`)

- 作用
  - 设置 vm-service stream listen/cancel 回调。

- 在整个 Dart VM 中的作用
  - 连接外部可观测事件系统。

- 使用逻辑
  - 注册后由 service 层按订阅状态回调。

#### `Dart_ServiceSendDataEvent` (`runtime/include/dart_tools_api.h:252`)

- 作用
  - 向 vm-service 事件流发送二进制数据事件。

- 在整个 Dart VM 中的作用
  - 用于 stdout/stderr 或自定义流数据转发。

- 使用逻辑
  - 只有目标 stream 有订阅者时才会被消费。

### 9.10 动态链接场景 API（`dart_api_dl.h`）

#### `Dart_InitializeApiDL` (`runtime/include/dart_api_dl.h:27`)

- 作用
  - 初始化 `_DL` 函数指针表，使动态库插件可调用 Dart API 子集。

- 在整个 Dart VM 中的作用
  - 解决“插件不直接链接 libdart，只通过运行时提供函数表调用”的问题。

- 使用逻辑
  - 在动态库中链接 `dart_api_dl.c`，并用 Dart 侧 `NativeApi.initializeApiDLData` 初始化。
  - 成功后调用 `Dart_PostCObject_DL`、`Dart_EnterIsolate_DL` 等 `_DL` 符号。

---

## 10. 给维护者的“最小正确调用模板”

按当前库语义，最小正确顺序是：

1. （可选）`dart::embedder::InitOnce`。
2. `Dart_SetVMFlags`。
3. `Dart_Initialize`。
4. `Dart_CreateIsolateGroupFromKernel` 或 `Dart_CreateIsolateGroup`。
5. （JIT）`Dart_LoadScriptFromKernel`。
6. `Dart_IsolateMakeRunnable`。
7. `Dart_RootLibrary` + `Dart_GetField/Dart_Invoke`。
8. `Dart_RunLoop`。
9. `Dart_ShutdownIsolate`。
10. `Dart_Cleanup`。
11. （可选）`dart::embedder::Cleanup`。

如果要接 vm-service，则在步骤 2 注入 service flags，并在步骤 3~8 期间使用 `dart_tools_api.h` 的注册/调用接口。

---

## 11. 与 `runtime/bin/main_impl.cc` 的实现对比（补充）

本节对比：
- Dart 官方二进制主线实现：`dart-sdk/sdk/runtime/bin/main_impl.cc`
- 你当前库实现：`src/dartvm_embed_lib.cpp`

目标：明确两者“相同语义”和“简化差异”，帮助你后续继续向主线行为靠拢。

### 11.1 结论先行

1. 你的库已经复用了主线最核心的 VM 生命周期骨架：
- `Dart_SetVMFlags -> Dart_Initialize -> CreateIsolateGroup* -> MakeRunnable -> _startMainIsolate -> Dart_RunLoop -> Dart_ShutdownIsolate -> Dart_Cleanup`

2. 你的库是“库形态简化版”，主线是“完整 CLI 宿主”：
- 主线多了 CLI 参数系统、DFE 编译链、vm-service 细节、timeline/metrics/reload 配套、dartdev 入口、native assets 细化。

3. 你库当前设计是合理的：
- 作为可嵌入静态库，保留主线关键语义，去掉 CLI 工具层复杂度。

### 11.2 初始化阶段对比

主线位置：`main_impl.cc:1332-1396` 附近

- 主线做法
  - `dart::embedder::InitOnce`
  - `Dart_SetVMFlags`
  - JIT 下提前 `dfe.Init()`/加载脚本
  - 填 `Dart_InitializeParams`，其中 `create_group = CreateIsolateGroupAndSetup`
  - 配置 file I/O / entropy / service assets / start_kernel_isolate
  - 调 `Dart_Initialize`

- 你库做法
  - `DartVmEmbed_Initialize`（`src/dartvm_embed_lib.cpp:226`）
  - 同样做 `InitOnce`（full setup 时）、`Dart_SetVMFlags`、`Dart_Initialize`
  - 参数上保留核心回调：`initialize_isolate/shutdown_isolate/cleanup_*`
  - 可选设置 file I/O 与 entropy（full setup 时）

- 差异含义
  - 主线把 CLI/runtime 的完整系统能力都接入了。
  - 你库聚焦“能稳定创建与运行业务 isolate”的最小必需集合。

### 11.3 Isolate 创建与 Setup 对比

主线位置：
- `main_impl.cc:260-430`（`IsolateSetupHelper`）
- `main_impl.cc:760-860`（`CreateIsolateGroupAndSetupHelper`）

- 主线做法
  - 先 `Dart_SetLibraryTagHandler` + `Dart_SetDeferredLoadHandler`
  - `SetupCoreLibraries`：`PrepareForScriptLoading`、`SetupPackageConfig`、`SetEnvironmentCallback`、builtin/vmservice resolver、I/O setup
  - JIT 路径可走 DFE 编译读取脚本并 `Dart_LoadScriptFromKernel`
  - `Loader::InitForSnapshot`
  - `Dart_InitializeNativeAssetsResolver`
  - `Dart_IsolateMakeRunnable`

- 你库做法
  - `SetupCurrentIsolate`（`src/dartvm_embed_lib.cpp:156`）
  - 同样做 loader tag/deferred/env/package/prepare
  - `DartVmEmbed_CreateIsolateFromKernel` 里显式 `Dart_LoadScriptFromKernel`（`src/dartvm_embed_lib.cpp:342`）
  - AOT 路径 `DartVmEmbed_CreateIsolateFromAppSnapshot`（`src/dartvm_embed_lib.cpp:495`）
  - 都在末尾 `Dart_IsolateMakeRunnable`

- 差异含义
  - 你库保留了主线 setup 核心语义，但去掉了主线中较重的 DFE/CLI/native-assets 深层逻辑。

### 11.4 入口执行与事件循环对比

主线位置：`main_impl.cc:1061-1083`

- 主线做法
  - `Dart_GetField(root_lib, "main")`
  - `Dart_Invoke(dart:isolate, "_startMainIsolate", ... )`
  - `Dart_RunLoop`

- 你库做法
  - `DartVmEmbed_RunEntry`（`src/dartvm_embed_lib.cpp:736`）
  - 先 `GetField(entry_name)`，失败时 fallback `Dart_Invoke(library, entry_name, 0, nullptr)`
  - 成功拿 closure 后同样调用 `_startMainIsolate`
  - `Dart_RunLoop`

- 差异含义
  - 你库比主线更“可配置入口名”（不是只支持 `main`）。
  - fallback 让某些模式下 top-level getter 不可见时仍可运行。

### 11.5 退出与清理对比

主线位置：`main_impl.cc:1023`、`main_impl.cc:1454`

- 主线做法
  - isolate 执行完 `Dart_ShutdownIsolate`
  - 进程级 `Dart_Cleanup`
  - `dart::embedder::Cleanup`

- 你库做法
  - `DartVmEmbed_ShutdownIsolate`（`src/dartvm_embed_lib.cpp:825`）
  - 在 `Dart_ShutdownIsolate` 后，额外清理本库维护的资源 map：
    - `g_isolate_loaded_aot_elfs`
    - `g_isolate_kernel_buffers`
    - `g_owned_isolates`（full setup）
  - `DartVmEmbed_Cleanup` 做 VM 全局 cleanup

- 差异含义
  - 你库作为库形态必须补“宿主拥有资源”清理；主线多数资源绑定在进程/工具生命周期中。

### 11.6 一一映射关系（快速查阅）

- 主线 `SetupCoreLibraries`（`main_impl.cc:145` 附近）
  - 对应你库 `SetupCurrentIsolate`（`src/dartvm_embed_lib.cpp:156`）

- 主线 `IsolateSetupHelper`（`main_impl.cc:260` 附近）
  - 对应你库 `DartVmEmbed_CreateIsolateFromKernel` + `DartVmEmbed_CreateIsolateFromAppSnapshot`

- 主线 `CreateIsolateGroupAndSetupHelper`（`main_impl.cc:760` 附近）
  - 对应你库 `DartVmEmbed_CreateIsolateFromProgramFile` 路由 + 上述 create 函数组合

- 主线入口执行段（`main_impl.cc:1061` 附近）
  - 对应你库 `DartVmEmbed_RunEntry` / `DartVmEmbed_RunRootEntry*`

- 主线 VM 初始化段（`main_impl.cc:1332-1396`）
  - 对应你库 `DartVmEmbed_Initialize`

### 11.7 你下一步若要进一步“贴近主线”

优先级建议：

1. vm-service 全量接入
- 对齐主线对 `dart_tools_api.h` 的 stream/callback 注册路径。

2. native assets resolver 对齐
- 在非 precompiler 模式补齐 `Dart_InitializeNativeAssetsResolver` 接入策略。

3. DFE 路径策略化
- 保留当前简化实现默认路径，同时可选启用主线 DFE 编译/增量链。

这样可以保持库形态简洁，同时在需要时逼近 `dart` 主线语义。
