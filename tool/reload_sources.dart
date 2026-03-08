import 'dart:async';
import 'dart:convert';
import 'dart:io';

Uri normalizeWsUri(String raw) {
  final u = Uri.parse(raw);
  if (u.scheme == 'ws' || u.scheme == 'wss') {
    return u;
  }
  if (u.scheme == 'http' || u.scheme == 'https') {
    var path = u.path;
    if (!path.endsWith('/')) {
      path = '$path/';
    }
    path = '${path}ws';
    return u.replace(scheme: u.scheme == 'https' ? 'wss' : 'ws', path: path);
  }
  throw ArgumentError('Unsupported URI scheme: ${u.scheme}');
}

Future<Map<String, dynamic>> sendRequest(
  WebSocket ws,
  StreamIterator<dynamic> iterator,
  int id,
  String method, {
  Map<String, dynamic>? params,
}) async {
  final req = <String, dynamic>{
    'jsonrpc': '2.0',
    'id': '$id',
    'method': method,
    if (params != null) 'params': params,
  };
  ws.add(jsonEncode(req));

  while (await iterator.moveNext()) {
    final msg = iterator.current;
    if (msg is! String) {
      continue;
    }
    final decoded = jsonDecode(msg);
    if (decoded is! Map<String, dynamic>) {
      continue;
    }
    if (decoded['id'] != '$id') {
      continue;
    }
    return decoded;
  }
  throw StateError('WebSocket closed while waiting for response to $method');
}

String? pickReloadTargetIsolateId(Map<String, dynamic> vm) {
  final isolates = vm['isolates'];
  if (isolates is! List) {
    return null;
  }
  for (final item in isolates) {
    if (item is! Map) {
      continue;
    }
    final id = item['id'];
    final name = item['name'];
    if (id is! String || name is! String) {
      continue;
    }
    if (name == 'vm-service' || name == 'kernel-service') {
      continue;
    }
    return id;
  }
  return null;
}

Future<Map<String, dynamic>?> getIsolate(
  WebSocket ws,
  StreamIterator<dynamic> iterator,
  int id,
  String isolateId,
) async {
  final resp = await sendRequest(
    ws,
    iterator,
    id,
    'getIsolate',
    params: {'isolateId': isolateId},
  );
  if (resp['error'] != null) {
    return null;
  }
  final result = resp['result'];
  return (result is Map<String, dynamic>) ? result : null;
}

Future<int> main(List<String> args) async {
  final rawUri = args.isNotEmpty
      ? args.first
      : (Platform.environment['DARTVM_EMBED_VM_SERVICE_URI'] ??
          'ws://127.0.0.1:8181/ws');

  final wsUri = normalizeWsUri(rawUri);
  stderr.writeln('[reload] connecting: $wsUri');
  final forceReload =
      (Platform.environment['DARTVM_EMBED_RELOAD_FORCE'] ?? '1') != '0';
  final pauseReload =
      (Platform.environment['DARTVM_EMBED_RELOAD_PAUSE'] ?? '0') == '1';
  final rootLibUriEnv = Platform.environment['DARTVM_EMBED_RELOAD_ROOT_LIB_URI'];
  final packagesUriEnv = Platform.environment['DARTVM_EMBED_RELOAD_PACKAGES_URI'];

  final ws = await WebSocket.connect(wsUri.toString());
  final iterator = StreamIterator<dynamic>(ws);
  try {
    var id = 1;
    final vmResp = await sendRequest(ws, iterator, id++, 'getVM');
    if (vmResp['error'] != null) {
      stderr.writeln('[reload] getVM failed: ${vmResp['error']}');
      return 2;
    }

    final result = vmResp['result'];
    if (result is! Map<String, dynamic>) {
      stderr.writeln('[reload] getVM returned unexpected payload');
      return 2;
    }

    final isolateId = pickReloadTargetIsolateId(result);
    if (isolateId == null) {
      stderr.writeln('[reload] no reloadable isolate found');
      return 3;
    }

    stderr.writeln('[reload] target isolate: $isolateId');
    final isolateObj = await getIsolate(ws, iterator, id++, isolateId);
    final rootLibObj =
        isolateObj != null ? isolateObj['rootLib'] : null;
    final inferredRootLibUri = (rootLibObj is Map<String, dynamic>)
        ? rootLibObj['uri'] as String?
        : null;
    final params = <String, dynamic>{
      'isolateId': isolateId,
      if (forceReload) 'force': true,
      if (pauseReload) 'pause': true,
      if (rootLibUriEnv != null && rootLibUriEnv.isNotEmpty)
        'rootLibUri': rootLibUriEnv,
      if ((rootLibUriEnv == null || rootLibUriEnv.isEmpty) &&
          inferredRootLibUri != null &&
          inferredRootLibUri.isNotEmpty)
        'rootLibUri': inferredRootLibUri,
      if (packagesUriEnv != null && packagesUriEnv.isNotEmpty)
        'packagesUri': packagesUriEnv,
    };
    final reloadResp = await sendRequest(
      ws,
      iterator,
      id++,
      'reloadSources',
      params: params,
    );

    if (reloadResp['error'] != null) {
      stderr.writeln('[reload] reloadSources failed: ${reloadResp['error']}');
      return 4;
    }

    stdout.writeln(jsonEncode(reloadResp['result']));
    stderr.writeln('[reload] reloadSources ok');
    return 0;
  } finally {
    await ws.close();
  }
}
