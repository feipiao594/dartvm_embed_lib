# runtime/include 统一格式说明文档（逐行范围 + 函数级全量）

本文档统一按同一模板描述 `dart-sdk/sdk/runtime/include` 下所有头文件。

统一模板：
- 头文件作用
- 逐行范围
- 函数级说明（每个函数都包含“函数的作用 / 在整个 DartVM 中的作用 / 使用逻辑”）

## 1. Dart 运行全流程（统一语义基线）
1. 宿主初始化（可选 `dart::embedder::InitOnce`）
2. 设置 VM flags（`Dart_SetVMFlags`）
3. 初始化 VM（`Dart_Initialize`）
4. 创建 isolate/group（`Dart_CreateIsolateGroup*`）
5. 装载脚本与绑定回调（library tag/native/environment）
6. 进入可运行态（`Dart_IsolateMakeRunnable`）
7. 入口执行与事件循环（`Dart_Invoke`/`Dart_RunLoop`）
8. 关闭 isolate（`Dart_ShutdownIsolate`）
9. 清理 VM（`Dart_Cleanup`）

## 2. `analyze_snapshot_api.h`（31 行）

### 头文件作用
- 快照分析接口（离线分析）

### 逐行范围
- L1-L4：版权与许可证注释
- L6-L8：include guard 定义
- L10-L11：标准库与 optional 引入
- L13-L26：snapshot_analyzer 命名空间与结构体/函数声明
- L28-L30：guard 结束

### 函数级说明
- 总函数数：1

#### `Dart_DumpSnapshotInformationAsJson` (`dart-sdk/sdk/runtime/include/analyze_snapshot_api.h:22`)
- 函数的作用
  - 将快照信息导出为 JSON。
- 在整个 DartVM 中的作用
  - 离线分析与诊断阶段。
- 使用逻辑
  - 传入四段快照指针，输出 JSON 缓冲区与长度。

## 3. `dart_version.h`（17 行）

### 头文件作用
- 版本信息接口

### 逐行范围
- L1-L4：版权注释
- L6-L8：include guard
- L10-L12：版本宏声明
- L14-L16：guard 结束

### 函数级说明
- 无导出函数（仅版本或常量定义）。

## 4. `dart_embedder_api.h`（106 行）

### 头文件作用
- embedder 辅助接口（runtime/bin 层）

### 逐行范围
- L1-L4：版权注释
- L6-L8：include guard
- L10-L11：依赖 dart_api / dart_tools_api
- L13-L14：embedder 命名空间
- L16-L27：embedder 生命周期 API（InitOnce/Cleanup）
- L29-L52：IsolateCreationData 结构体
- L54-L100：kernel service/vm-service isolate 创建函数
- L102-L105：guard 结束

### 函数级说明
- 总函数数：5

#### `InitOnce` (`dart-sdk/sdk/runtime/include/dart_embedder_api.h:21`)
- 函数的作用
  - 初始化 embedder 子系统。
- 在整个 DartVM 中的作用
  - 位于 VM 初始化前的宿主准备阶段。
- 使用逻辑
  - 在 Dart_Initialize 前调用一次，失败即停止。

#### `Cleanup` (`dart-sdk/sdk/runtime/include/dart_embedder_api.h:27`)
- 函数的作用
  - 清理 embedder 子系统。
- 在整个 DartVM 中的作用
  - 位于 VM 清理后的宿主收尾阶段。
- 使用逻辑
  - 与 InitOnce 成对调用。

#### `CreateKernelServiceIsolate` (`dart-sdk/sdk/runtime/include/dart_embedder_api.h:55`)
- 函数的作用
  - 创建 kernel service 系统 isolate。
- 在整个 DartVM 中的作用
  - 系统 isolate 启动阶段（编译服务）。
- 使用逻辑
  - 在 isolate 创建回调识别 kernel service 名称时调用。

#### `CreateVmServiceIsolate` (`dart-sdk/sdk/runtime/include/dart_embedder_api.h:85`)
- 函数的作用
  - 从 snapshot 创建 vm-service isolate。
- 在整个 DartVM 中的作用
  - 系统 isolate 启动阶段（调试服务）。
- 使用逻辑
  - 使用 VmServiceConfiguration 配置地址端口和认证策略。

#### `CreateVmServiceIsolateFromKernel` (`dart-sdk/sdk/runtime/include/dart_embedder_api.h:96`)
- 函数的作用
  - 从 kernel 创建 vm-service isolate。
- 在整个 DartVM 中的作用
  - 系统 isolate 启动阶段（调试服务，kernel 路径）。
- 使用逻辑
  - 与 CreateVmServiceIsolate 按工件类型二选一。

## 5. `dart_api_dl.h`（175 行）

### 头文件作用
- 动态链接 API 入口（_DL）

### 逐行范围
- L1-L4：版权注释
- L6-L8：include guard
- L10-L11：依赖 dart_api / dart_native_api
- L13-L27：动态链接 API 说明与初始化函数
- L29-L145：DL 符号表与 ABI 版本约束
- L147-L174：导出宏与声明展开，guard 结束

### 函数级说明
- 总函数数：1

#### `Dart_InitializeApiDL` (`dart-sdk/sdk/runtime/include/dart_api_dl.h:27`)
- 函数的作用
  - 初始化动态链接 API 函数表。
- 在整个 DartVM 中的作用
  - 动态库插件接入阶段。
- 使用逻辑
  - 用 NativeApi.initializeApiDLData 提供数据后初始化一次。

## 6. `dart_native_api.h`（225 行）

### 头文件作用
- native 消息与端口接口

### 逐行范围
- L1-L4：版权注释
- L6-L10：include guard 与依赖
- L12-L95：Dart_CObject 类型体系定义
- L97-L196：消息发送与 native port API
- L198-L222：验证/内部工具 API
- L224：guard 结束

### 函数级说明
- 总函数数：8

#### `Dart_PostCObject` (`dart-sdk/sdk/runtime/include/dart_native_api.h:127`)
- 函数的作用
  - 进行端口与消息通信操作。
- 在整个 DartVM 中的作用
  - 事件循环/消息投递阶段。
- 使用逻辑
  - 目标端口需有效，跨线程调用遵守线程约束。

#### `Dart_PostInteger` (`dart-sdk/sdk/runtime/include/dart_native_api.h:137`)
- 函数的作用
  - 进行端口与消息通信操作。
- 在整个 DartVM 中的作用
  - 事件循环/消息投递阶段。
- 使用逻辑
  - 目标端口需有效，跨线程调用遵守线程约束。

#### `Dart_NewNativePort` (`dart-sdk/sdk/runtime/include/dart_native_api.h:166`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_NewConcurrentNativePort` (`dart-sdk/sdk/runtime/include/dart_native_api.h:182`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_CloseNativePort` (`dart-sdk/sdk/runtime/include/dart_native_api.h:196`)
- 函数的作用
  - 进行端口与消息通信操作。
- 在整个 DartVM 中的作用
  - 事件循环/消息投递阶段。
- 使用逻辑
  - 目标端口需有效，跨线程调用遵守线程约束。

#### `Dart_CompileAll` (`dart-sdk/sdk/runtime/include/dart_native_api.h:210`)
- 函数的作用
  - 执行内核编译或预编译操作。
- 在整个 DartVM 中的作用
  - 编译/预编译阶段。
- 使用逻辑
  - 通常用于工具链路径，需准备编译输入与回调。

#### `Dart_FinalizeAllClasses` (`dart-sdk/sdk/runtime/include/dart_native_api.h:215`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_ExecuteInternalCommand` (`dart-sdk/sdk/runtime/include/dart_native_api.h:222`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

## 7. `dart_tools_api.h`（628 行）

### 头文件作用
- 调试/服务/时间线工具接口

### 逐行范围
- L1-L14：版权与 tools API 总览
- L16-L35：isolate/isolate group 非法 ID 常量
- L37-L188：vm-service 回调注册与主动调用
- L190-L260：service stream 与数据事件
- L262-L420：reload/timeline API
- L422-L560：timeline callback 与 metrics API
- L562-L627：user tag 与 heap snapshot API，guard 结束

### 函数级说明
- 总函数数：27

#### `Dart_RegisterIsolateServiceRequestCallback` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:101`)
- 函数的作用
  - 操作 isolate/isolate group 生命周期或元信息。
- 在整个 DartVM 中的作用
  - 执行单元生命周期阶段。
- 使用逻辑
  - 调用前确认当前线程和 isolate 上下文。

#### `Dart_RegisterRootServiceRequestCallback` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:118`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_SetEmbedderInformationCallback` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:163`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_InvokeVMServiceMethod` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:183`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_SetServiceStreamCallbacks` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:225`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_ServiceSendDataEvent` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:252`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_SetFileModifiedCallback` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:274`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_IsReloading` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:280`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_SetEnabledTimelineCategory` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:317`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_TimelineGetMicros` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:326`)
- 函数的作用
  - 收集诊断与性能信息。
- 在整个 DartVM 中的作用
  - 运行期观测与诊断阶段。
- 使用逻辑
  - 建议仅在调试或运维场景启用。

#### `Dart_TimelineGetTicks` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:333`)
- 函数的作用
  - 收集诊断与性能信息。
- 在整个 DartVM 中的作用
  - 运行期观测与诊断阶段。
- 使用逻辑
  - 建议仅在调试或运维场景启用。

#### `Dart_TimelineGetTicksFrequency` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:340`)
- 函数的作用
  - 收集诊断与性能信息。
- 在整个 DartVM 中的作用
  - 运行期观测与诊断阶段。
- 使用逻辑
  - 建议仅在调试或运维场景启用。

#### `Dart_RecordTimelineEvent` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:405`)
- 函数的作用
  - 收集诊断与性能信息。
- 在整个 DartVM 中的作用
  - 运行期观测与诊断阶段。
- 使用逻辑
  - 建议仅在调试或运维场景启用。

#### `Dart_SetThreadName` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:421`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_SetTimelineRecorderCallback` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:505`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_IsolateGroupHeapOldUsedMetric` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:517`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsolateGroupHeapOldCapacityMetric` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:519`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsolateGroupHeapOldExternalMetric` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:521`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsolateGroupHeapNewUsedMetric` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:523`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsolateGroupHeapNewCapacityMetric` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:525`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsolateGroupHeapNewExternalMetric` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:527`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_GetCurrentUserTag` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:541`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_GetDefaultUserTag` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:548`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_NewUserTag` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:557`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_SetCurrentUserTag` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:566`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_GetUserTagLabel` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:576`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_WriteHeapSnapshot` (`dart-sdk/sdk/runtime/include/dart_tools_api.h:623`)
- 函数的作用
  - 执行快照生成或读取相关操作。
- 在整个 DartVM 中的作用
  - 编译产物生成或预编译阶段。
- 使用逻辑
  - 输入工件需与 VM 模式匹配并检查返回错误。

## 8. `dart_api.h`（4163 行）

### 头文件作用
- Dart VM 嵌入主 API

### 逐行范围
- L1-L69：版权、导出宏、基础类型
- L70-L559：错误模型与 handle 生命周期
- L560-L1001：初始化与全局配置
- L1002-L1798：isolate/group 生命周期与消息循环
- L1799-L1848：scope 管理
- L1849-L3265：对象/类型/集合/调用 API
- L3266-L3559：环境回调、native resolver、加载回调、kernel 装载
- L3561-L4162：root library、编译与快照、诊断 API
### 函数级说明
- 总函数数：260

#### `Dart_IsError` (`dart-sdk/sdk/runtime/include/dart_api.h:272`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsApiError` (`dart-sdk/sdk/runtime/include/dart_api.h:283`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsUnhandledExceptionError` (`dart-sdk/sdk/runtime/include/dart_api.h:297`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsCompilationError` (`dart-sdk/sdk/runtime/include/dart_api.h:308`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsFatalError` (`dart-sdk/sdk/runtime/include/dart_api.h:318`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_GetError` (`dart-sdk/sdk/runtime/include/dart_api.h:330`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_ErrorHasException` (`dart-sdk/sdk/runtime/include/dart_api.h:335`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_ErrorGetException` (`dart-sdk/sdk/runtime/include/dart_api.h:340`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_ErrorGetStackTrace` (`dart-sdk/sdk/runtime/include/dart_api.h:345`)
- 函数的作用
  - 收集诊断与性能信息。
- 在整个 DartVM 中的作用
  - 运行期观测与诊断阶段。
- 使用逻辑
  - 建议仅在调试或运维场景启用。

#### `Dart_NewApiError` (`dart-sdk/sdk/runtime/include/dart_api.h:354`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_NewCompilationError` (`dart-sdk/sdk/runtime/include/dart_api.h:355`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_NewUnhandledExceptionError` (`dart-sdk/sdk/runtime/include/dart_api.h:368`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_PropagateError` (`dart-sdk/sdk/runtime/include/dart_api.h:396`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_ToString` (`dart-sdk/sdk/runtime/include/dart_api.h:407`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_IdentityEquals` (`dart-sdk/sdk/runtime/include/dart_api.h:421`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_HandleFromPersistent` (`dart-sdk/sdk/runtime/include/dart_api.h:426`)
- 函数的作用
  - 管理持久/弱/终结句柄生命周期。
- 在整个 DartVM 中的作用
  - GC 交互与宿主资源管理阶段。
- 使用逻辑
  - 严格按所有权与回调约束使用。

#### `Dart_HandleFromWeakPersistent` (`dart-sdk/sdk/runtime/include/dart_api.h:433`)
- 函数的作用
  - 管理持久/弱/终结句柄生命周期。
- 在整个 DartVM 中的作用
  - GC 交互与宿主资源管理阶段。
- 使用逻辑
  - 严格按所有权与回调约束使用。

#### `Dart_NewPersistentHandle` (`dart-sdk/sdk/runtime/include/dart_api.h:444`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_SetPersistentHandle` (`dart-sdk/sdk/runtime/include/dart_api.h:454`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_DeletePersistentHandle` (`dart-sdk/sdk/runtime/include/dart_api.h:462`)
- 函数的作用
  - 管理持久/弱/终结句柄生命周期。
- 在整个 DartVM 中的作用
  - GC 交互与宿主资源管理阶段。
- 使用逻辑
  - 严格按所有权与回调约束使用。

#### `Dart_NewWeakPersistentHandle` (`dart-sdk/sdk/runtime/include/dart_api.h:494`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_DeleteWeakPersistentHandle` (`dart-sdk/sdk/runtime/include/dart_api.h:505`)
- 函数的作用
  - 管理持久/弱/终结句柄生命周期。
- 在整个 DartVM 中的作用
  - GC 交互与宿主资源管理阶段。
- 使用逻辑
  - 严格按所有权与回调约束使用。

#### `Dart_NewFinalizableHandle` (`dart-sdk/sdk/runtime/include/dart_api.h:542`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_DeleteFinalizableHandle` (`dart-sdk/sdk/runtime/include/dart_api.h:556`)
- 函数的作用
  - 管理持久/弱/终结句柄生命周期。
- 在整个 DartVM 中的作用
  - GC 交互与宿主资源管理阶段。
- 使用逻辑
  - 严格按所有权与回调约束使用。

#### `Dart_VersionString` (`dart-sdk/sdk/runtime/include/dart_api.h:572`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_IsolateFlagsInitialize` (`dart-sdk/sdk/runtime/include/dart_api.h:603`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_Initialize` (`dart-sdk/sdk/runtime/include/dart_api.h:963`)
- 函数的作用
  - 初始化 Dart VM。
- 在整个 DartVM 中的作用
  - VM 全局初始化阶段。
- 使用逻辑
  - 在任何 isolate 创建前调用；失败即停止流程。

#### `Dart_Cleanup` (`dart-sdk/sdk/runtime/include/dart_api.h:975`)
- 函数的作用
  - 清理并关闭 Dart VM。
- 在整个 DartVM 中的作用
  - VM 全局退出阶段。
- 使用逻辑
  - 确保 isolate 已关闭后调用。

#### `Dart_SetVMFlags` (`dart-sdk/sdk/runtime/include/dart_api.h:988`)
- 函数的作用
  - 设置 VM 启动参数。
- 在整个 DartVM 中的作用
  - 初始化前配置注入点。
- 使用逻辑
  - 必须先于 Dart_Initialize 调用。

#### `Dart_IsVMFlagSet` (`dart-sdk/sdk/runtime/include/dart_api.h:999`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_CreateIsolateGroup` (`dart-sdk/sdk/runtime/include/dart_api.h:1044`)
- 函数的作用
  - 从 snapshot 创建 isolate group。
- 在整个 DartVM 中的作用
  - AOT/快照创建阶段。
- 使用逻辑
  - 传 snapshot data/instructions 与 callback data。

#### `Dart_CreateIsolateInGroup` (`dart-sdk/sdk/runtime/include/dart_api.h:1076`)
- 函数的作用
  - 操作 isolate/isolate group 生命周期或元信息。
- 在整个 DartVM 中的作用
  - 执行单元生命周期阶段。
- 使用逻辑
  - 调用前确认当前线程和 isolate 上下文。

#### `Dart_CreateIsolateGroupFromKernel` (`dart-sdk/sdk/runtime/include/dart_api.h:1116`)
- 函数的作用
  - 从 kernel 创建 isolate group。
- 在整个 DartVM 中的作用
  - JIT 创建阶段。
- 使用逻辑
  - 通常随后加载脚本并 make runnable。

#### `Dart_ShutdownIsolate` (`dart-sdk/sdk/runtime/include/dart_api.h:1132`)
- 函数的作用
  - 关闭当前 isolate。
- 在整个 DartVM 中的作用
  - isolate 生命周期结束阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并做宿主资源回收。

#### `Dart_CurrentIsolate` (`dart-sdk/sdk/runtime/include/dart_api.h:1139`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_CurrentIsolateData` (`dart-sdk/sdk/runtime/include/dart_api.h:1145`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_IsolateData` (`dart-sdk/sdk/runtime/include/dart_api.h:1151`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_CurrentIsolateGroup` (`dart-sdk/sdk/runtime/include/dart_api.h:1157`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_CurrentIsolateGroupData` (`dart-sdk/sdk/runtime/include/dart_api.h:1163`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_CurrentIsolateGroupId` (`dart-sdk/sdk/runtime/include/dart_api.h:1171`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_IsolateGroupData` (`dart-sdk/sdk/runtime/include/dart_api.h:1179`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_DebugName` (`dart-sdk/sdk/runtime/include/dart_api.h:1187`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_DebugNameToCString` (`dart-sdk/sdk/runtime/include/dart_api.h:1198`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_IsolateServiceId` (`dart-sdk/sdk/runtime/include/dart_api.h:1205`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_EnterIsolate` (`dart-sdk/sdk/runtime/include/dart_api.h:1214`)
- 函数的作用
  - 线程进入指定 isolate 上下文。
- 在整个 DartVM 中的作用
  - 多 isolate 线程切换阶段。
- 使用逻辑
  - 与 Dart_ExitIsolate 成对使用。

#### `Dart_KillIsolate` (`dart-sdk/sdk/runtime/include/dart_api.h:1228`)
- 函数的作用
  - 操作 isolate/isolate group 生命周期或元信息。
- 在整个 DartVM 中的作用
  - 执行单元生命周期阶段。
- 使用逻辑
  - 调用前确认当前线程和 isolate 上下文。

#### `Dart_NotifyIdle` (`dart-sdk/sdk/runtime/include/dart_api.h:1240`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_EnableHeapSampling` (`dart-sdk/sdk/runtime/include/dart_api.h:1254`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_DisableHeapSampling` (`dart-sdk/sdk/runtime/include/dart_api.h:1259`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_RegisterHeapSamplingCallback` (`dart-sdk/sdk/runtime/include/dart_api.h:1276`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_ReportSurvivingAllocations` (`dart-sdk/sdk/runtime/include/dart_api.h:1300`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_SetHeapSamplingPeriod` (`dart-sdk/sdk/runtime/include/dart_api.h:1312`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_NotifyDestroyed` (`dart-sdk/sdk/runtime/include/dart_api.h:1321`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_NotifyLowMemory` (`dart-sdk/sdk/runtime/include/dart_api.h:1328`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_SetPerformanceMode` (`dart-sdk/sdk/runtime/include/dart_api.h:1361`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_StartProfiling` (`dart-sdk/sdk/runtime/include/dart_api.h:1367`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_StopProfiling` (`dart-sdk/sdk/runtime/include/dart_api.h:1376`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_ThreadDisableProfiling` (`dart-sdk/sdk/runtime/include/dart_api.h:1387`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_ThreadEnableProfiling` (`dart-sdk/sdk/runtime/include/dart_api.h:1397`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_AddSymbols` (`dart-sdk/sdk/runtime/include/dart_api.h:1405`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_ExitIsolate` (`dart-sdk/sdk/runtime/include/dart_api.h:1415`)
- 函数的作用
  - 线程退出当前 isolate 上下文。
- 在整个 DartVM 中的作用
  - 多 isolate 线程切换阶段。
- 使用逻辑
  - 必须和 Enter 成对，避免上下文泄漏。

#### `Dart_CreateSnapshot` (`dart-sdk/sdk/runtime/include/dart_api.h:1444`)
- 函数的作用
  - 执行快照生成或读取相关操作。
- 在整个 DartVM 中的作用
  - 编译产物生成或预编译阶段。
- 使用逻辑
  - 输入工件需与 VM 模式匹配并检查返回错误。

#### `Dart_IsKernel` (`dart-sdk/sdk/runtime/include/dart_api.h:1459`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsolateMakeRunnable` (`dart-sdk/sdk/runtime/include/dart_api.h:1474`)
- 函数的作用
  - 把 isolate 切换到可运行态。
- 在整个 DartVM 中的作用
  - 创建后转执行前的状态门。
- 使用逻辑
  - setup/load 完成后调用。

#### `Dart_SetMessageNotifyCallback` (`dart-sdk/sdk/runtime/include/dart_api.h:1522`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_GetMessageNotifyCallback` (`dart-sdk/sdk/runtime/include/dart_api.h:1532`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_ShouldPauseOnStart` (`dart-sdk/sdk/runtime/include/dart_api.h:1558`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_SetShouldPauseOnStart` (`dart-sdk/sdk/runtime/include/dart_api.h:1567`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_IsPausedOnStart` (`dart-sdk/sdk/runtime/include/dart_api.h:1574`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_SetPausedOnStart` (`dart-sdk/sdk/runtime/include/dart_api.h:1582`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_ShouldPauseOnExit` (`dart-sdk/sdk/runtime/include/dart_api.h:1589`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_SetShouldPauseOnExit` (`dart-sdk/sdk/runtime/include/dart_api.h:1597`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_IsPausedOnExit` (`dart-sdk/sdk/runtime/include/dart_api.h:1604`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_SetPausedOnExit` (`dart-sdk/sdk/runtime/include/dart_api.h:1612`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_SetStickyError` (`dart-sdk/sdk/runtime/include/dart_api.h:1623`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_HasStickyError` (`dart-sdk/sdk/runtime/include/dart_api.h:1628`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_GetStickyError` (`dart-sdk/sdk/runtime/include/dart_api.h:1635`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_HandleMessage` (`dart-sdk/sdk/runtime/include/dart_api.h:1644`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_HandleServiceMessages` (`dart-sdk/sdk/runtime/include/dart_api.h:1659`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_HasServiceMessages` (`dart-sdk/sdk/runtime/include/dart_api.h:1666`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_RunLoop` (`dart-sdk/sdk/runtime/include/dart_api.h:1684`)
- 函数的作用
  - 运行当前 isolate 的消息循环。
- 在整个 DartVM 中的作用
  - 主执行阶段事件泵。
- 使用逻辑
  - 入口调起后调用直到退出/错误。

#### `Dart_RunLoopAsync` (`dart-sdk/sdk/runtime/include/dart_api.h:1704`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_GetMainPortId` (`dart-sdk/sdk/runtime/include/dart_api.h:1713`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_HasLivePorts` (`dart-sdk/sdk/runtime/include/dart_api.h:1720`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_Post` (`dart-sdk/sdk/runtime/include/dart_api.h:1735`)
- 函数的作用
  - 进行端口与消息通信操作。
- 在整个 DartVM 中的作用
  - 事件循环/消息投递阶段。
- 使用逻辑
  - 目标端口需有效，跨线程调用遵守线程约束。

#### `Dart_NewSendPort` (`dart-sdk/sdk/runtime/include/dart_api.h:1749`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_NewSendPortEx` (`dart-sdk/sdk/runtime/include/dart_api.h:1759`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_SendPortGetId` (`dart-sdk/sdk/runtime/include/dart_api.h:1768`)
- 函数的作用
  - 进行端口与消息通信操作。
- 在整个 DartVM 中的作用
  - 事件循环/消息投递阶段。
- 使用逻辑
  - 目标端口需有效，跨线程调用遵守线程约束。

#### `Dart_SendPortGetIdEx` (`dart-sdk/sdk/runtime/include/dart_api.h:1778`)
- 函数的作用
  - 进行端口与消息通信操作。
- 在整个 DartVM 中的作用
  - 事件循环/消息投递阶段。
- 使用逻辑
  - 目标端口需有效，跨线程调用遵守线程约束。

#### `Dart_SetCurrentThreadOwnsIsolate` (`dart-sdk/sdk/runtime/include/dart_api.h:1786`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_GetCurrentThreadOwnsIsolate` (`dart-sdk/sdk/runtime/include/dart_api.h:1796`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_EnterScope` (`dart-sdk/sdk/runtime/include/dart_api.h:1813`)
- 函数的作用
  - 管理本地 handle 的作用域。
- 在整个 DartVM 中的作用
  - API 调用栈管理阶段。
- 使用逻辑
  - Enter/Exit 必须成对，避免句柄泄漏。

#### `Dart_ExitScope` (`dart-sdk/sdk/runtime/include/dart_api.h:1822`)
- 函数的作用
  - 管理本地 handle 的作用域。
- 在整个 DartVM 中的作用
  - API 调用栈管理阶段。
- 使用逻辑
  - Enter/Exit 必须成对，避免句柄泄漏。

#### `Dart_ScopeAllocate` (`dart-sdk/sdk/runtime/include/dart_api.h:1846`)
- 函数的作用
  - 管理本地 handle 的作用域。
- 在整个 DartVM 中的作用
  - API 调用栈管理阶段。
- 使用逻辑
  - Enter/Exit 必须成对，避免句柄泄漏。

#### `Dart_Null` (`dart-sdk/sdk/runtime/include/dart_api.h:1859`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_IsNull` (`dart-sdk/sdk/runtime/include/dart_api.h:1864`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_EmptyString` (`dart-sdk/sdk/runtime/include/dart_api.h:1871`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_TypeDynamic` (`dart-sdk/sdk/runtime/include/dart_api.h:1879`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_TypeVoid` (`dart-sdk/sdk/runtime/include/dart_api.h:1880`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_TypeNever` (`dart-sdk/sdk/runtime/include/dart_api.h:1881`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_ObjectEquals` (`dart-sdk/sdk/runtime/include/dart_api.h:1898`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_ObjectIsType` (`dart-sdk/sdk/runtime/include/dart_api.h:1914`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_IsInstance` (`dart-sdk/sdk/runtime/include/dart_api.h:1925`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsNumber` (`dart-sdk/sdk/runtime/include/dart_api.h:1926`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsInteger` (`dart-sdk/sdk/runtime/include/dart_api.h:1927`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsDouble` (`dart-sdk/sdk/runtime/include/dart_api.h:1928`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsBoolean` (`dart-sdk/sdk/runtime/include/dart_api.h:1929`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsString` (`dart-sdk/sdk/runtime/include/dart_api.h:1930`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsStringLatin1` (`dart-sdk/sdk/runtime/include/dart_api.h:1931`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsList` (`dart-sdk/sdk/runtime/include/dart_api.h:1932`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsMap` (`dart-sdk/sdk/runtime/include/dart_api.h:1933`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsLibrary` (`dart-sdk/sdk/runtime/include/dart_api.h:1934`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsType` (`dart-sdk/sdk/runtime/include/dart_api.h:1935`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsFunction` (`dart-sdk/sdk/runtime/include/dart_api.h:1936`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsVariable` (`dart-sdk/sdk/runtime/include/dart_api.h:1937`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsTypeVariable` (`dart-sdk/sdk/runtime/include/dart_api.h:1938`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsClosure` (`dart-sdk/sdk/runtime/include/dart_api.h:1939`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsTypedData` (`dart-sdk/sdk/runtime/include/dart_api.h:1940`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsByteBuffer` (`dart-sdk/sdk/runtime/include/dart_api.h:1941`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsFuture` (`dart-sdk/sdk/runtime/include/dart_api.h:1942`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_InstanceGetType` (`dart-sdk/sdk/runtime/include/dart_api.h:1966`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_ClassName` (`dart-sdk/sdk/runtime/include/dart_api.h:1974`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_FunctionName` (`dart-sdk/sdk/runtime/include/dart_api.h:1982`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_FunctionOwner` (`dart-sdk/sdk/runtime/include/dart_api.h:1996`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_FunctionIsStatic` (`dart-sdk/sdk/runtime/include/dart_api.h:2010`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_IsTearOff` (`dart-sdk/sdk/runtime/include/dart_api.h:2024`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_ClosureFunction` (`dart-sdk/sdk/runtime/include/dart_api.h:2032`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_ClassLibrary` (`dart-sdk/sdk/runtime/include/dart_api.h:2041`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_IntegerFitsIntoInt64` (`dart-sdk/sdk/runtime/include/dart_api.h:2057`)
- 函数的作用
  - 进行数值/布尔的构造与转换。
- 在整个 DartVM 中的作用
  - 运行期基础类型转换阶段。
- 使用逻辑
  - 先做类型检查，再读取或写入值。

#### `Dart_IntegerFitsIntoUint64` (`dart-sdk/sdk/runtime/include/dart_api.h:2068`)
- 函数的作用
  - 进行数值/布尔的构造与转换。
- 在整个 DartVM 中的作用
  - 运行期基础类型转换阶段。
- 使用逻辑
  - 先做类型检查，再读取或写入值。

#### `Dart_NewInteger` (`dart-sdk/sdk/runtime/include/dart_api.h:2079`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_NewIntegerFromUint64` (`dart-sdk/sdk/runtime/include/dart_api.h:2089`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_NewIntegerFromHexCString` (`dart-sdk/sdk/runtime/include/dart_api.h:2100`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_IntegerToInt64` (`dart-sdk/sdk/runtime/include/dart_api.h:2112`)
- 函数的作用
  - 进行数值/布尔的构造与转换。
- 在整个 DartVM 中的作用
  - 运行期基础类型转换阶段。
- 使用逻辑
  - 先做类型检查，再读取或写入值。

#### `Dart_IntegerToUint64` (`dart-sdk/sdk/runtime/include/dart_api.h:2126`)
- 函数的作用
  - 进行数值/布尔的构造与转换。
- 在整个 DartVM 中的作用
  - 运行期基础类型转换阶段。
- 使用逻辑
  - 先做类型检查，再读取或写入值。

#### `Dart_IntegerToHexCString` (`dart-sdk/sdk/runtime/include/dart_api.h:2139`)
- 函数的作用
  - 进行数值/布尔的构造与转换。
- 在整个 DartVM 中的作用
  - 运行期基础类型转换阶段。
- 使用逻辑
  - 先做类型检查，再读取或写入值。

#### `Dart_NewDouble` (`dart-sdk/sdk/runtime/include/dart_api.h:2150`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_DoubleValue` (`dart-sdk/sdk/runtime/include/dart_api.h:2160`)
- 函数的作用
  - 进行数值/布尔的构造与转换。
- 在整个 DartVM 中的作用
  - 运行期基础类型转换阶段。
- 使用逻辑
  - 先做类型检查，再读取或写入值。

#### `Dart_GetStaticMethodClosure` (`dart-sdk/sdk/runtime/include/dart_api.h:2172`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_True` (`dart-sdk/sdk/runtime/include/dart_api.h:2189`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_False` (`dart-sdk/sdk/runtime/include/dart_api.h:2198`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_NewBoolean` (`dart-sdk/sdk/runtime/include/dart_api.h:2208`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_BooleanValue` (`dart-sdk/sdk/runtime/include/dart_api.h:2218`)
- 函数的作用
  - 进行数值/布尔的构造与转换。
- 在整个 DartVM 中的作用
  - 运行期基础类型转换阶段。
- 使用逻辑
  - 先做类型检查，再读取或写入值。

#### `Dart_StringLength` (`dart-sdk/sdk/runtime/include/dart_api.h:2234`)
- 函数的作用
  - 进行字符串编解码与属性操作。
- 在整个 DartVM 中的作用
  - 运行期数据编解码阶段。
- 使用逻辑
  - 注意 UTF 编码与输出缓冲区长度。

#### `Dart_StringUTF8Length` (`dart-sdk/sdk/runtime/include/dart_api.h:2244`)
- 函数的作用
  - 进行字符串编解码与属性操作。
- 在整个 DartVM 中的作用
  - 运行期数据编解码阶段。
- 使用逻辑
  - 注意 UTF 编码与输出缓冲区长度。

#### `Dart_NewStringFromCString` (`dart-sdk/sdk/runtime/include/dart_api.h:2258`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_NewStringFromUTF8` (`dart-sdk/sdk/runtime/include/dart_api.h:2271`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_NewStringFromUTF16` (`dart-sdk/sdk/runtime/include/dart_api.h:2283`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_NewStringFromUTF32` (`dart-sdk/sdk/runtime/include/dart_api.h:2295`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_StringToCString` (`dart-sdk/sdk/runtime/include/dart_api.h:2309`)
- 函数的作用
  - 进行字符串编解码与属性操作。
- 在整个 DartVM 中的作用
  - 运行期数据编解码阶段。
- 使用逻辑
  - 注意 UTF 编码与输出缓冲区长度。

#### `Dart_StringToUTF8` (`dart-sdk/sdk/runtime/include/dart_api.h:2328`)
- 函数的作用
  - 进行字符串编解码与属性操作。
- 在整个 DartVM 中的作用
  - 运行期数据编解码阶段。
- 使用逻辑
  - 注意 UTF 编码与输出缓冲区长度。

#### `Dart_CopyUTF8EncodingOfString` (`dart-sdk/sdk/runtime/include/dart_api.h:2346`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_StringToLatin1` (`dart-sdk/sdk/runtime/include/dart_api.h:2363`)
- 函数的作用
  - 进行字符串编解码与属性操作。
- 在整个 DartVM 中的作用
  - 运行期数据编解码阶段。
- 使用逻辑
  - 注意 UTF 编码与输出缓冲区长度。

#### `Dart_StringToUTF16` (`dart-sdk/sdk/runtime/include/dart_api.h:2378`)
- 函数的作用
  - 进行字符串编解码与属性操作。
- 在整个 DartVM 中的作用
  - 运行期数据编解码阶段。
- 使用逻辑
  - 注意 UTF 编码与输出缓冲区长度。

#### `Dart_StringStorageSize` (`dart-sdk/sdk/runtime/include/dart_api.h:2391`)
- 函数的作用
  - 进行字符串编解码与属性操作。
- 在整个 DartVM 中的作用
  - 运行期数据编解码阶段。
- 使用逻辑
  - 注意 UTF 编码与输出缓冲区长度。

#### `Dart_StringGetProperties` (`dart-sdk/sdk/runtime/include/dart_api.h:2407`)
- 函数的作用
  - 进行字符串编解码与属性操作。
- 在整个 DartVM 中的作用
  - 运行期数据编解码阶段。
- 使用逻辑
  - 注意 UTF 编码与输出缓冲区长度。

#### `Dart_NewList` (`dart-sdk/sdk/runtime/include/dart_api.h:2426`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_NewListOfType` (`dart-sdk/sdk/runtime/include/dart_api.h:2439`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_NewListOfTypeFilled` (`dart-sdk/sdk/runtime/include/dart_api.h:2457`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_ListLength` (`dart-sdk/sdk/runtime/include/dart_api.h:2471`)
- 函数的作用
  - 操作 Dart 集合对象。
- 在整个 DartVM 中的作用
  - 运行期数据交互阶段。
- 使用逻辑
  - 先确保 handle 类型正确，再进行读写。

#### `Dart_ListGetAt` (`dart-sdk/sdk/runtime/include/dart_api.h:2486`)
- 函数的作用
  - 操作 Dart 集合对象。
- 在整个 DartVM 中的作用
  - 运行期数据交互阶段。
- 使用逻辑
  - 先确保 handle 类型正确，再进行读写。

#### `Dart_ListGetRange` (`dart-sdk/sdk/runtime/include/dart_api.h:2502`)
- 函数的作用
  - 操作 Dart 集合对象。
- 在整个 DartVM 中的作用
  - 运行期数据交互阶段。
- 使用逻辑
  - 先确保 handle 类型正确，再进行读写。

#### `Dart_ListSetAt` (`dart-sdk/sdk/runtime/include/dart_api.h:2520`)
- 函数的作用
  - 操作 Dart 集合对象。
- 在整个 DartVM 中的作用
  - 运行期数据交互阶段。
- 使用逻辑
  - 先确保 handle 类型正确，再进行读写。

#### `Dart_ListGetAsBytes` (`dart-sdk/sdk/runtime/include/dart_api.h:2527`)
- 函数的作用
  - 操作 Dart 集合对象。
- 在整个 DartVM 中的作用
  - 运行期数据交互阶段。
- 使用逻辑
  - 先确保 handle 类型正确，再进行读写。

#### `Dart_ListSetAsBytes` (`dart-sdk/sdk/runtime/include/dart_api.h:2535`)
- 函数的作用
  - 操作 Dart 集合对象。
- 在整个 DartVM 中的作用
  - 运行期数据交互阶段。
- 使用逻辑
  - 先确保 handle 类型正确，再进行读写。

#### `Dart_MapGetAt` (`dart-sdk/sdk/runtime/include/dart_api.h:2557`)
- 函数的作用
  - 操作 Dart 集合对象。
- 在整个 DartVM 中的作用
  - 运行期数据交互阶段。
- 使用逻辑
  - 先确保 handle 类型正确，再进行读写。

#### `Dart_MapContainsKey` (`dart-sdk/sdk/runtime/include/dart_api.h:2569`)
- 函数的作用
  - 操作 Dart 集合对象。
- 在整个 DartVM 中的作用
  - 运行期数据交互阶段。
- 使用逻辑
  - 先确保 handle 类型正确，再进行读写。

#### `Dart_MapKeys` (`dart-sdk/sdk/runtime/include/dart_api.h:2581`)
- 函数的作用
  - 操作 Dart 集合对象。
- 在整个 DartVM 中的作用
  - 运行期数据交互阶段。
- 使用逻辑
  - 先确保 handle 类型正确，再进行读写。

#### `Dart_GetTypeOfTypedData` (`dart-sdk/sdk/runtime/include/dart_api.h:2614`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_GetTypeOfExternalTypedData` (`dart-sdk/sdk/runtime/include/dart_api.h:2622`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_NewTypedData` (`dart-sdk/sdk/runtime/include/dart_api.h:2634`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_NewExternalTypedData` (`dart-sdk/sdk/runtime/include/dart_api.h:2647`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_NewExternalTypedDataWithFinalizer` (`dart-sdk/sdk/runtime/include/dart_api.h:2668`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_NewUnmodifiableExternalTypedDataWithFinalizer` (`dart-sdk/sdk/runtime/include/dart_api.h:2675`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_NewByteBuffer` (`dart-sdk/sdk/runtime/include/dart_api.h:2691`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_TypedDataAcquireData` (`dart-sdk/sdk/runtime/include/dart_api.h:2715`)
- 函数的作用
  - 操作 typed data / byte buffer。
- 在整个 DartVM 中的作用
  - 运行期二进制数据桥接阶段。
- 使用逻辑
  - 注意 acquire/release 与外部内存生命周期。

#### `Dart_TypedDataReleaseData` (`dart-sdk/sdk/runtime/include/dart_api.h:2730`)
- 函数的作用
  - 操作 typed data / byte buffer。
- 在整个 DartVM 中的作用
  - 运行期二进制数据桥接阶段。
- 使用逻辑
  - 注意 acquire/release 与外部内存生命周期。

#### `Dart_GetDataFromByteBuffer` (`dart-sdk/sdk/runtime/include/dart_api.h:2740`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_New` (`dart-sdk/sdk/runtime/include/dart_api.h:2765`)
- 函数的作用
  - 创建 Dart 对象或句柄。
- 在整个 DartVM 中的作用
  - 对象构造阶段。
- 使用逻辑
  - 需在有效 scope 中创建并处理错误 handle。

#### `Dart_Allocate` (`dart-sdk/sdk/runtime/include/dart_api.h:2779`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_AllocateWithNativeFields` (`dart-sdk/sdk/runtime/include/dart_api.h:2793`)
- 函数的作用
  - 处理 native 接口桥接。
- 在整个 DartVM 中的作用
  - Dart 与宿主互操作阶段。
- 使用逻辑
  - 参数类型需匹配，错误通过 handle 传播。

#### `Dart_Invoke` (`dart-sdk/sdk/runtime/include/dart_api.h:2821`)
- 函数的作用
  - 调用对象或库上的方法。
- 在整个 DartVM 中的作用
  - 宿主触发 Dart 执行的核心接口。
- 使用逻辑
  - 传方法名和参数数组，检查错误 handle。

#### `Dart_InvokeClosure` (`dart-sdk/sdk/runtime/include/dart_api.h:2837`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_InvokeConstructor` (`dart-sdk/sdk/runtime/include/dart_api.h:2862`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_GetField` (`dart-sdk/sdk/runtime/include/dart_api.h:2888`)
- 函数的作用
  - 获取对象/库字段或 top-level getter。
- 在整个 DartVM 中的作用
  - 入口解析与反射调用阶段。
- 使用逻辑
  - 失败可 fallback 到 Dart_Invoke。

#### `Dart_SetField` (`dart-sdk/sdk/runtime/include/dart_api.h:2911`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_ThrowException` (`dart-sdk/sdk/runtime/include/dart_api.h:2943`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_ReThrowException` (`dart-sdk/sdk/runtime/include/dart_api.h:2956`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_GetNativeInstanceFieldCount` (`dart-sdk/sdk/runtime/include/dart_api.h:2968`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_GetNativeInstanceField` (`dart-sdk/sdk/runtime/include/dart_api.h:2976`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_SetNativeInstanceField` (`dart-sdk/sdk/runtime/include/dart_api.h:2985`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_GetNativeIsolateGroupData` (`dart-sdk/sdk/runtime/include/dart_api.h:3002`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_GetNativeArguments` (`dart-sdk/sdk/runtime/include/dart_api.h:3072`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_GetNativeArgument` (`dart-sdk/sdk/runtime/include/dart_api.h:3081`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_GetNativeArgumentCount` (`dart-sdk/sdk/runtime/include/dart_api.h:3088`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_GetNativeFieldsOfArgument` (`dart-sdk/sdk/runtime/include/dart_api.h:3102`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_GetNativeReceiver` (`dart-sdk/sdk/runtime/include/dart_api.h:3111`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_GetNativeStringArgument` (`dart-sdk/sdk/runtime/include/dart_api.h:3123`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_GetNativeIntegerArgument` (`dart-sdk/sdk/runtime/include/dart_api.h:3134`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_GetNativeBooleanArgument` (`dart-sdk/sdk/runtime/include/dart_api.h:3145`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_GetNativeDoubleArgument` (`dart-sdk/sdk/runtime/include/dart_api.h:3156`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_SetReturnValue` (`dart-sdk/sdk/runtime/include/dart_api.h:3167`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_SetWeakHandleReturnValue` (`dart-sdk/sdk/runtime/include/dart_api.h:3170`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_SetBooleanReturnValue` (`dart-sdk/sdk/runtime/include/dart_api.h:3173`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_SetIntegerReturnValue` (`dart-sdk/sdk/runtime/include/dart_api.h:3176`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_SetDoubleReturnValue` (`dart-sdk/sdk/runtime/include/dart_api.h:3179`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_SetEnvironmentCallback` (`dart-sdk/sdk/runtime/include/dart_api.h:3265`)
- 函数的作用
  - 注册环境变量查询回调。
- 在整个 DartVM 中的作用
  - 脚本加载/环境注入阶段。
- 使用逻辑
  - setup 阶段设置，运行时按需回调。

#### `Dart_SetNativeResolver` (`dart-sdk/sdk/runtime/include/dart_api.h:3276`)
- 函数的作用
  - 为库注册 native 方法解析器。
- 在整个 DartVM 中的作用
  - Dart 与 Native 绑定阶段。
- 使用逻辑
  - 在库可用后设置 resolver 和 symbol resolver。

#### `Dart_GetNativeResolver` (`dart-sdk/sdk/runtime/include/dart_api.h:3290`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_GetNativeSymbol` (`dart-sdk/sdk/runtime/include/dart_api.h:3301`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_SetFfiNativeResolver` (`dart-sdk/sdk/runtime/include/dart_api.h:3317`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_InitializeNativeAssetsResolver` (`dart-sdk/sdk/runtime/include/dart_api.h:3418`)
- 函数的作用
  - 处理 native 接口桥接。
- 在整个 DartVM 中的作用
  - Dart 与宿主互操作阶段。
- 使用逻辑
  - 参数类型需匹配，错误通过 handle 传播。

#### `Dart_SetLibraryTagHandler` (`dart-sdk/sdk/runtime/include/dart_api.h:3482`)
- 函数的作用
  - 注册库加载标签处理器。
- 在整个 DartVM 中的作用
  - 脚本加载控制阶段。
- 使用逻辑
  - 通常 isolate setup 时设置一次。

#### `Dart_SetDeferredLoadHandler` (`dart-sdk/sdk/runtime/include/dart_api.h:3505`)
- 函数的作用
  - 注册 deferred load 处理器。
- 在整个 DartVM 中的作用
  - 延迟加载控制阶段。
- 使用逻辑
  - 和 LibraryTagHandler 配套设置。

#### `Dart_DeferredLoadComplete` (`dart-sdk/sdk/runtime/include/dart_api.h:3516`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_DeferredLoadCompleteError` (`dart-sdk/sdk/runtime/include/dart_api.h:3533`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_LoadScriptFromKernel` (`dart-sdk/sdk/runtime/include/dart_api.h:3549`)
- 函数的作用
  - 将 kernel 装为当前 isolate 的根脚本。
- 在整个 DartVM 中的作用
  - JIT 装载阶段。
- 使用逻辑
  - 需在 current isolate + scope 内调用。

#### `Dart_RootLibrary` (`dart-sdk/sdk/runtime/include/dart_api.h:3561`)
- 函数的作用
  - 获取当前 isolate 根库。
- 在整个 DartVM 中的作用
  - 入口定位阶段。
- 使用逻辑
  - 常与 Dart_GetField/Dart_Invoke 配合。

#### `Dart_SetRootLibrary` (`dart-sdk/sdk/runtime/include/dart_api.h:3568`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_GetType` (`dart-sdk/sdk/runtime/include/dart_api.h:3584`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_GetNullableType` (`dart-sdk/sdk/runtime/include/dart_api.h:3603`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_GetNonNullableType` (`dart-sdk/sdk/runtime/include/dart_api.h:3622`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_TypeToNullableType` (`dart-sdk/sdk/runtime/include/dart_api.h:3636`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_TypeToNonNullableType` (`dart-sdk/sdk/runtime/include/dart_api.h:3646`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_IsNullableType` (`dart-sdk/sdk/runtime/include/dart_api.h:3657`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_IsNonNullableType` (`dart-sdk/sdk/runtime/include/dart_api.h:3658`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_GetClass` (`dart-sdk/sdk/runtime/include/dart_api.h:3669`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_LibraryUrl` (`dart-sdk/sdk/runtime/include/dart_api.h:3678`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_LibraryResolvedUrl` (`dart-sdk/sdk/runtime/include/dart_api.h:3683`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_GetLoadedLibraries` (`dart-sdk/sdk/runtime/include/dart_api.h:3688`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_LookupLibrary` (`dart-sdk/sdk/runtime/include/dart_api.h:3690`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_LibraryHandleError` (`dart-sdk/sdk/runtime/include/dart_api.h:3704`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_LoadLibraryFromKernel` (`dart-sdk/sdk/runtime/include/dart_api.h:3717`)
- 函数的作用
  - 执行内核编译或预编译操作。
- 在整个 DartVM 中的作用
  - 编译/预编译阶段。
- 使用逻辑
  - 通常用于工具链路径，需准备编译输入与回调。

#### `Dart_LoadLibrary` (`dart-sdk/sdk/runtime/include/dart_api.h:3720`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_FinalizeLoading` (`dart-sdk/sdk/runtime/include/dart_api.h:3736`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_GetPeer` (`dart-sdk/sdk/runtime/include/dart_api.h:3762`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_SetPeer` (`dart-sdk/sdk/runtime/include/dart_api.h:3774`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_IsKernelIsolate` (`dart-sdk/sdk/runtime/include/dart_api.h:3813`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_KernelIsolateIsRunning` (`dart-sdk/sdk/runtime/include/dart_api.h:3814`)
- 函数的作用
  - 执行内核编译或预编译操作。
- 在整个 DartVM 中的作用
  - 编译/预编译阶段。
- 使用逻辑
  - 通常用于工具链路径，需准备编译输入与回调。

#### `Dart_KernelPort` (`dart-sdk/sdk/runtime/include/dart_api.h:3815`)
- 函数的作用
  - 进行端口与消息通信操作。
- 在整个 DartVM 中的作用
  - 事件循环/消息投递阶段。
- 使用逻辑
  - 目标端口需有效，跨线程调用遵守线程约束。

#### `Dart_CompileToKernel` (`dart-sdk/sdk/runtime/include/dart_api.h:3847`)
- 函数的作用
  - 执行内核编译或预编译操作。
- 在整个 DartVM 中的作用
  - 编译/预编译阶段。
- 使用逻辑
  - 通常用于工具链路径，需准备编译输入与回调。

#### `Dart_KernelListDependencies` (`dart-sdk/sdk/runtime/include/dart_api.h:3862`)
- 函数的作用
  - 执行内核编译或预编译操作。
- 在整个 DartVM 中的作用
  - 编译/预编译阶段。
- 使用逻辑
  - 通常用于工具链路径，需准备编译输入与回调。

#### `Dart_SetDartLibrarySourcesKernel` (`dart-sdk/sdk/runtime/include/dart_api.h:3873`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。

#### `Dart_DetectNullSafety` (`dart-sdk/sdk/runtime/include/dart_api.h:3880`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_IsServiceIsolate` (`dart-sdk/sdk/runtime/include/dart_api.h:3905`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_WriteProfileToTimeline` (`dart-sdk/sdk/runtime/include/dart_api.h:3918`)
- 函数的作用
  - 收集诊断与性能信息。
- 在整个 DartVM 中的作用
  - 运行期观测与诊断阶段。
- 使用逻辑
  - 建议仅在调试或运维场景启用。

#### `Dart_Precompile` (`dart-sdk/sdk/runtime/include/dart_api.h:3936`)
- 函数的作用
  - 执行内核编译或预编译操作。
- 在整个 DartVM 中的作用
  - 编译/预编译阶段。
- 使用逻辑
  - 通常用于工具链路径，需准备编译输入与回调。

#### `Dart_LoadingUnitLibraryUris` (`dart-sdk/sdk/runtime/include/dart_api.h:3948`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_CreateAppAOTSnapshotAsAssembly` (`dart-sdk/sdk/runtime/include/dart_api.h:4005`)
- 函数的作用
  - 执行快照生成或读取相关操作。
- 在整个 DartVM 中的作用
  - 编译产物生成或预编译阶段。
- 使用逻辑
  - 输入工件需与 VM 模式匹配并检查返回错误。

#### `Dart_CreateAppAOTSnapshotAsAssemblies` (`dart-sdk/sdk/runtime/include/dart_api.h:4010`)
- 函数的作用
  - 执行快照生成或读取相关操作。
- 在整个 DartVM 中的作用
  - 编译产物生成或预编译阶段。
- 使用逻辑
  - 输入工件需与 VM 模式匹配并检查返回错误。

#### `Dart_CreateAppAOTSnapshotAsElf` (`dart-sdk/sdk/runtime/include/dart_api.h:4045`)
- 函数的作用
  - 执行快照生成或读取相关操作。
- 在整个 DartVM 中的作用
  - 编译产物生成或预编译阶段。
- 使用逻辑
  - 输入工件需与 VM 模式匹配并检查返回错误。

#### `Dart_CreateAppAOTSnapshotAsElfs` (`dart-sdk/sdk/runtime/include/dart_api.h:4050`)
- 函数的作用
  - 执行快照生成或读取相关操作。
- 在整个 DartVM 中的作用
  - 编译产物生成或预编译阶段。
- 使用逻辑
  - 输入工件需与 VM 模式匹配并检查返回错误。

#### `Dart_CreateVMAOTSnapshotAsAssembly` (`dart-sdk/sdk/runtime/include/dart_api.h:4063`)
- 函数的作用
  - 执行快照生成或读取相关操作。
- 在整个 DartVM 中的作用
  - 编译产物生成或预编译阶段。
- 使用逻辑
  - 输入工件需与 VM 模式匹配并检查返回错误。

#### `Dart_SortClasses` (`dart-sdk/sdk/runtime/include/dart_api.h:4074`)
- 函数的作用
  - 查询或操作程序元数据。
- 在整个 DartVM 中的作用
  - 加载后反射与元信息访问阶段。
- 使用逻辑
  - 先拿到合法库/类型 handle 再访问。

#### `Dart_CreateAppJITSnapshotAsBlobs` (`dart-sdk/sdk/runtime/include/dart_api.h:4097`)
- 函数的作用
  - 执行快照生成或读取相关操作。
- 在整个 DartVM 中的作用
  - 编译产物生成或预编译阶段。
- 使用逻辑
  - 输入工件需与 VM 模式匹配并检查返回错误。

#### `Dart_GetObfuscationMap` (`dart-sdk/sdk/runtime/include/dart_api.h:4112`)
- 函数的作用
  - 读取 VM/对象/元数据信息。
- 在整个 DartVM 中的作用
  - 运行期查询阶段。
- 使用逻辑
  - 要求当前上下文满足 API 前置条件。

#### `Dart_IsPrecompiledRuntime` (`dart-sdk/sdk/runtime/include/dart_api.h:4120`)
- 函数的作用
  - 查询状态或类型信息。
- 在整个 DartVM 中的作用
  - 运行期状态判断阶段。
- 使用逻辑
  - 在当前 isolate 上下文调用并检查返回值。

#### `Dart_DumpNativeStackTrace` (`dart-sdk/sdk/runtime/include/dart_api.h:4129`)
- 函数的作用
  - 处理 native 接口桥接。
- 在整个 DartVM 中的作用
  - Dart 与宿主互操作阶段。
- 使用逻辑
  - 参数类型需匹配，错误通过 handle 传播。

#### `Dart_PrepareToAbort` (`dart-sdk/sdk/runtime/include/dart_api.h:4135`)
- 函数的作用
  - 执行通用 Dart C API 操作。
- 在整个 DartVM 中的作用
  - Dart VM 运行期通用阶段。
- 使用逻辑
  - 按 API 文档的 isolate/scope 前置条件调用。

#### `Dart_SetDwarfStackTraceFootnoteCallback` (`dart-sdk/sdk/runtime/include/dart_api.h:4159`)
- 函数的作用
  - 设置 VM/对象/回调配置。
- 在整个 DartVM 中的作用
  - 初始化或运行期配置阶段。
- 使用逻辑
  - 在对应生命周期窗口调用，注意线程与 isolate 约束。
