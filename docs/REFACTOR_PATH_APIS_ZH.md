# 运行时启动骨架函数（对应 `main_impl.cc::main`）

1. `RuntimeEarlyBoot(...)`
- 语义：最早期启动步骤（符号、平台、ICU、控制台）。
- 目标：把 `main_impl` 里 `main` 前段的进程初始化骨架独立出来。

1. `RuntimeInitLoaderAndEnvironment(...)`
- 语义：环境变量注入、crash/coredump 策略、`Loader::InitOnce`。
- 目标：和 `Dart_Initialize` 前置条件解耦。

```cpp
bool RuntimeEarlyBoot(char** error);
bool RuntimeInitLoaderAndEnvironment(char** error);
```

说明：
- 这是内部骨架函数，不是对外业务 API。
- 对齐 `main_impl` 的作用是“启动顺序一致”，不是强制复制 CLI 行为。

---

# VM 参数与 DFE 骨架函数（`Dart_SetVMFlags` 前后）

1. `BuildVmFlags(...)`
- 语义：统一生成 VM flags（flavor、用户传入、默认值）。
- 目标：避免分散在多个创建路径里拼 flags。

1. `ApplyVmFlags(...)`
- 语义：调用 `Dart_SetVMFlags` 并做标准错误转换。

1. `InitDfeAfterVmFlags(...)`
- 语义：严格在 `Dart_SetVMFlags` 后初始化 DFE。
- 目标：对齐 `main_impl` 的关键顺序约束。

```cpp
bool BuildVmFlags(const DartVmEmbedInitConfig* config,
                  std::vector<const char*>* out_flags,
                  char** error);

bool ApplyVmFlags(const std::vector<const char*>& flags, char** error);

bool InitDfeAfterVmFlags(const char* script_uri_hint, char** error);
```

说明：
- 这里是重构核心：把“顺序约束”固化为函数边界。
- `InitDfeAfterVmFlags` 可先做最小实现，后续再补 `verbosity` / `ReadScript`。

---

# Snapshot 解析骨架函数（AOT/JIT 关键分界）

1. `ResolveVmSnapshotForRuntime(...)`
- 语义：在 VM 初始化前，确定 `vm_snapshot_data/instructions` 来源。
- 目标：杜绝 AOT 场景“先 init 后加载 snapshot”的错误时序。

1. `ResolveIsolateSnapshotForAot(...)`
- 语义：AOT 创建 isolate 所需的 isolate snapshot 解析。

1. `ResolveKernelBufferForJitPath(...)`
- 语义：JIT 路径读取 kernel/dill。

```cpp
struct VmSnapshotPair {
  const uint8_t* data;
  const uint8_t* instructions;
};

struct IsolateSnapshotPair {
  const uint8_t* data;
  const uint8_t* instructions;
};

bool ResolveVmSnapshotForRuntime(const DartVmEmbedInitConfig* config,
                                 VmSnapshotPair* out_vm_snapshot,
                                 char** error);

bool ResolveIsolateSnapshotForAot(const char* aot_path,
                                  DartVmEmbedAotElfHandle* out_handle,
                                  IsolateSnapshotPair* out_isolate_snapshot,
                                  char** error);

bool ResolveKernelBufferForJitPath(const char* kernel_path,
                                   std::vector<uint8_t>* out_kernel,
                                   char** error);
```

说明：
- 本组函数是你和 `main_impl` 当前差异最大的地方，必须优先重构。
- `ResolveVmSnapshotForRuntime` 必须在 `Dart_Initialize` 之前调用。

---

# Dart VM 初始化骨架函数（`Dart_InitializeParams` 组装）

1. `BuildInitializeParams(...)`
- 语义：统一构造 `Dart_InitializeParams`。
- 目标：把 callback、IO hooks、entropy、start_kernel_isolate 策略集中管理。

1. `StartDartVm(...)`
- 语义：执行 `dart::embedder::InitOnce` + `Dart_Initialize`。
- 目标：形成唯一 VM 启动入口。

```cpp
bool BuildInitializeParams(const VmSnapshotPair& vm_snapshot,
                           const DartVmEmbedInitConfig* config,
                           Dart_InitializeParams* out_params,
                           char** error);

bool StartDartVm(const Dart_InitializeParams& params, char** error);
```

说明：
- 这个层次只做 VM 启动，不做 isolate 创建。
- 以后新增回调（比如 embedder information）只改这里。

---

# Isolate 创建骨架函数（你当前要暴露的三路径）

1. `DartVmEmbed_CreateIsolateFromAotPath(...)`
- 输入：app-aot-elf 路径。
- 语义：AOT 专用创建路径。

1. `DartVmEmbed_CreateIsolateFromJitPath(...)`
- 输入：kernel/dill 路径。
- 语义：JIT kernel 专用创建路径。

1. `DartVmEmbed_CreateIsolateFromSourcePath(...)`
- 输入：dart 源文件路径。
- 语义：JIT source 专用创建路径。

```cpp
Dart_Isolate DartVmEmbed_CreateIsolateFromAotPath(
    const char* aot_path,
    const char* script_uri,
    const char* isolate_name,
    void* isolate_group_data,
    void* isolate_data,
    char** error);

Dart_Isolate DartVmEmbed_CreateIsolateFromJitPath(
    const char* kernel_path,
    const char* script_uri,
    const char* isolate_name,
    void* isolate_group_data,
    void* isolate_data,
    char** error);

Dart_Isolate DartVmEmbed_CreateIsolateFromSourcePath(
    const char* source_path,
    const char* script_uri,
    const char* isolate_name,
    void* isolate_group_data,
    void* isolate_data,
    char** error);
```

说明：
- 这是对外 API。
- 其内部必须复用前述骨架函数，不能再把启动顺序散落到分支里。

---

# Isolate 启动与循环骨架函数（运行态）

1. `RunIsolateEntry(...)`
- 语义：执行 root entry（默认 `main`）。

1. `RunIsolateEventLoop(...)`
- 语义：泵消息循环。

1. `ShutdownIsolateHandle(...)`
- 语义：按句柄关闭 isolate，统一回收附属资源。

```cpp
bool RunIsolateEntry(Dart_Isolate isolate, const char* entry_name, char** error);
bool RunIsolateEventLoop(Dart_Isolate isolate, char** error);
void ShutdownIsolateHandle(Dart_Isolate isolate);
```

说明：
- 这一组是运行态骨架，不涉及 CLI。
- 与 `main_impl` 的 `RunMainIsolate` 目标一致，但以库 API 方式暴露。

---

# 服务回调骨架函数（C++层应做的最小职责）

1. `InstallServiceCallbacks(...)`
- 语义：安装 `ServiceStream` / `FileModified` 等 VM 回调。
- 目标：只做“桥接”和“注册”，不在这里实现热重载协议。

```cpp
bool InstallServiceCallbacks(char** error);
```

说明：
- 你说得对：热重载与 VM service 协议主逻辑不应在这层“发明新 API”。
- C++ 层最小职责是把 VM 所需 callback 装好并提供基础消息泵能力。

---

# 错误处理与状态骨架函数

1. `SetLastError(...)`
- 语义：统一记录错误码和错误文本。

1. `GetRuntimeState(...)`
- 语义：查询 VM 是否已初始化、当前 flavor、是否支持 source 路径。

```cpp
enum DartVmEmbedErrorCode {
  kDartVmEmbedOk = 0,
  kDartVmEmbedInvalidArgument = 1,
  kDartVmEmbedUnsupportedInFlavor = 2,
  kDartVmEmbedVmNotInitialized = 3,
  kDartVmEmbedSnapshotOrderError = 4,
  kDartVmEmbedInternalError = 100,
};

void SetLastError(DartVmEmbedErrorCode code, const char* message);
DartVmEmbedErrorCode GetLastErrorCode(void);
const char* GetLastErrorMessage(void);
```

说明：
- 这组函数不改变现有 `char** error` 风格，但能让宿主更稳定处理错误分支。

---

# 实施顺序（按骨架依赖）

1. 第一批（必须）
- VM 参数与 DFE骨架
- Snapshot 解析骨架
- Dart VM 初始化骨架

1. 第二批（对外能力）
- 三路径 Isolate 创建 API
- Isolate 启动与循环骨架

1. 第三批（稳定性）
- 服务回调骨架
- 错误处理与状态骨架

说明：
- 这个顺序是严格依赖顺序：先把启动时序做对，再谈上层 API 易用性。
