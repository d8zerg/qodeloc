#include <algorithm>
#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <qodeloc/core/api.hpp>
#include <qodeloc/core/config.hpp>
#include <qodeloc/core/hierarchy.hpp>
#include <qodeloc/core/storage.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>

namespace qodeloc::core {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;
using json = nlohmann::json;

struct HttpResponse {
  http::status status{http::status::ok};
  json body = json::object();
};

[[nodiscard]] std::string trim(std::string_view text) {
  const auto begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string_view::npos) {
    return {};
  }

  const auto end = text.find_last_not_of(" \t\r\n");
  return std::string{text.substr(begin, end - begin + 1)};
}

[[nodiscard]] std::string_view target_path(std::string_view target) {
  const auto query = target.find('?');
  return query == std::string_view::npos ? target : target.substr(0, query);
}

[[nodiscard]] std::int64_t to_unix_ms(std::chrono::system_clock::time_point time_point) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(time_point.time_since_epoch())
      .count();
}

[[nodiscard]] std::size_t count_tokens(std::string_view text) {
  std::size_t count = 0;
  bool in_token = false;
  for (const char ch : text) {
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      in_token = false;
      continue;
    }

    if (!in_token) {
      ++count;
      in_token = true;
    }
  }
  return count;
}

[[nodiscard]] json stored_symbol_json(const StoredSymbol& symbol) {
  return json{
      {"file_path", symbol.file_path},           {"module_name", symbol.module_name},
      {"module_path", symbol.module_path},       {"kind", std::string(to_string(symbol.kind))},
      {"qualified_name", symbol.qualified_name}, {"signature", symbol.signature},
      {"start_line", symbol.start_line},         {"end_line", symbol.end_line}};
}

[[nodiscard]] json indexed_symbol_json(const Indexer::IndexedSymbol& symbol) {
  json result = stored_symbol_json(symbol.symbol);
  result["symbol_id"] = symbol.symbol_id;
  result["source_text"] = symbol.source_text;
  return result;
}

[[nodiscard]] json module_record_json(const HierarchicalIndex::ModuleRecord& module) {
  return json{{"module_name", module.module_name},
              {"module_path", module.module_path},
              {"summary", module.summary},
              {"public_symbol_count", module.public_symbol_count},
              {"header_count", module.header_count}};
}

[[nodiscard]] json module_hit_json(const HierarchicalIndex::ModuleHit& module) {
  json result = module_record_json(module.module);
  result["score"] = module.score;
  return result;
}

[[nodiscard]] json symbol_context_json(const Retriever::SymbolContext& symbol) {
  json result;
  result["symbol"] = indexed_symbol_json(symbol.symbol);
  result["score"] = symbol.score;
  result["context"] = symbol.context;
  result["token_count"] = symbol.token_count;
  result["callers"] = json::array();
  for (const auto& caller : symbol.callers) {
    result["callers"].push_back(stored_symbol_json(caller));
  }
  result["callees"] = json::array();
  for (const auto& callee : symbol.callees) {
    result["callees"].push_back(stored_symbol_json(callee));
  }
  return result;
}

[[nodiscard]] json prompt_json(const PromptBuilder::RenderedPrompt& prompt) {
  json result;
  result["template_name"] = prompt.template_name;
  result["context_token_limit"] = prompt.context_token_limit;
  result["token_count"] = prompt.token_count;
  result["system_text"] = prompt.system_text;
  result["user_text"] = prompt.user_text;
  result["messages"] = json::array();
  for (const auto& message : prompt.messages) {
    result["messages"].push_back({{"role", message.role}, {"content", message.content}});
  }
  return result;
}

[[nodiscard]] json chat_response_json(const LlmClient::ChatResponse& response) {
  return json{{"content", response.content}, {"raw", response.raw}};
}

[[nodiscard]] json module_dependency_json(const ModuleDependency& dependency) {
  return json{{"module_name", dependency.module_name},
              {"module_path", dependency.module_path},
              {"depth", dependency.depth}};
}

[[nodiscard]] std::vector<PromptBuilder::LocalFile> parse_local_files(const json& body) {
  std::vector<PromptBuilder::LocalFile> local_files;
  if (!body.contains("local_files") || !body["local_files"].is_array()) {
    return local_files;
  }

  for (const auto& item : body["local_files"]) {
    if (!item.is_object()) {
      continue;
    }

    PromptBuilder::LocalFile local_file;
    local_file.path = std::filesystem::path{item.value("path", std::string{})};
    local_file.content = item.value("content", std::string{});
    if (!local_file.path.empty() || !local_file.content.empty()) {
      local_files.push_back(std::move(local_file));
    }
  }

  return local_files;
}

[[nodiscard]] std::string make_symbol_context_text(const Indexer::IndexedSymbol& symbol,
                                                   const Storage& storage) {
  std::string context;
  context += "Symbol: ";
  context += symbol.symbol.qualified_name;
  context.push_back('\n');

  if (!symbol.symbol.module_name.empty()) {
    context += "Module: ";
    context += symbol.symbol.module_name;
    context.push_back('\n');
  }

  if (!symbol.symbol.signature.empty()) {
    context += "Signature: ";
    context += symbol.symbol.signature;
    context.push_back('\n');
  }

  if (!symbol.source_text.empty()) {
    context += "Source: ";
    context += symbol.source_text;
    context.push_back('\n');
  }

  auto callers = storage.graph().callers_of(symbol.symbol_id);
  auto callees = storage.graph().callees_from(symbol.symbol_id);

  std::sort(callers.begin(), callers.end(), [](const StoredSymbol& lhs, const StoredSymbol& rhs) {
    if (lhs.qualified_name != rhs.qualified_name) {
      return lhs.qualified_name < rhs.qualified_name;
    }
    return lhs.file_path < rhs.file_path;
  });
  std::sort(callees.begin(), callees.end(), [](const StoredSymbol& lhs, const StoredSymbol& rhs) {
    if (lhs.qualified_name != rhs.qualified_name) {
      return lhs.qualified_name < rhs.qualified_name;
    }
    return lhs.file_path < rhs.file_path;
  });

  if (!callers.empty()) {
    context += "Callers:\n";
    for (const auto& caller : callers) {
      context += "- ";
      context += caller.qualified_name;
      if (!caller.signature.empty()) {
        context += " :: ";
        context += caller.signature;
      }
      if (!caller.module_name.empty()) {
        context += " [";
        context += caller.module_name;
        context += "]";
      }
      context.push_back('\n');
    }
  }

  if (!callees.empty()) {
    context += "Callees:\n";
    for (const auto& callee : callees) {
      context += "- ";
      context += callee.qualified_name;
      if (!callee.signature.empty()) {
        context += " :: ";
        context += callee.signature;
      }
      if (!callee.module_name.empty()) {
        context += " [";
        context += callee.module_name;
        context += "]";
      }
      context.push_back('\n');
    }
  }

  if (!context.empty() && context.back() == '\n') {
    context.pop_back();
  }

  return context;
}

[[nodiscard]] Retriever::SymbolContext make_symbol_context(const Indexer::IndexedSymbol& symbol,
                                                           const Storage& storage) {
  Retriever::SymbolContext context;
  context.symbol = symbol;
  context.score = 1.0;
  context.callers = storage.graph().callers_of(symbol.symbol_id);
  context.callees = storage.graph().callees_from(symbol.symbol_id);
  context.context = make_symbol_context_text(symbol, storage);
  context.token_count = count_tokens(context.context);
  return context;
}

[[nodiscard]] std::optional<Indexer::IndexedSymbol> find_symbol_by_name(const Indexer& indexer,
                                                                        std::string_view name) {
  if (name.empty()) {
    return std::nullopt;
  }

  const auto symbol_id = indexer.storage().graph().symbol_id_for_name(name);
  const auto& symbols = indexer.symbols();
  const auto by_id = std::find_if(symbols.begin(), symbols.end(),
                                  [&symbol_id](const Indexer::IndexedSymbol& symbol) {
                                    return symbol_id.has_value() && symbol.symbol_id == *symbol_id;
                                  });
  if (by_id != symbols.end()) {
    return *by_id;
  }

  const auto by_name =
      std::find_if(symbols.begin(), symbols.end(), [name](const Indexer::IndexedSymbol& symbol) {
        return symbol.symbol.qualified_name == name || symbol.symbol.qualified_name.ends_with(name);
      });
  if (by_name != symbols.end()) {
    return *by_name;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<HierarchicalIndex::ModuleRecord>
find_module_record(const Retriever& retriever, std::string_view module_name) {
  const auto& modules = retriever.hierarchy().modules();
  const auto module = std::find_if(
      modules.begin(), modules.end(), [module_name](const HierarchicalIndex::ModuleRecord& record) {
        return record.module_name == module_name || record.module_path == module_name;
      });
  if (module == modules.end()) {
    return std::nullopt;
  }

  return *module;
}

[[nodiscard]] HierarchicalIndex::ModuleRecord fallback_module_record(const Indexer& indexer,
                                                                     std::string_view module_name) {
  HierarchicalIndex::ModuleRecord module;
  module.module_name = std::string(module_name);
  module.module_path = std::string(module_name);

  std::size_t header_count = 0;
  for (const auto& symbol : indexer.symbols()) {
    if (symbol.symbol.module_name != module_name && symbol.symbol.module_path != module_name) {
      continue;
    }

    if (module.summary.empty()) {
      module.summary = symbol.symbol.qualified_name;
    } else {
      module.summary += "\n";
      module.summary += symbol.symbol.qualified_name;
    }

    ++module.public_symbol_count;
    const auto extension =
        std::filesystem::path(symbol.symbol.file_path).extension().generic_string();
    if (extension == ".h" || extension == ".hh" || extension == ".hpp" || extension == ".hxx") {
      ++header_count;
    }
  }

  module.header_count = header_count;
  return module;
}

[[nodiscard]] HttpResponse json_error(http::status status, std::string message) {
  return HttpResponse{status, json{{"error", std::move(message)}}};
}

} // namespace

struct ApiServer::Impl {
  explicit Impl(Options options_in) : options(std::move(options_in)) {
    if (options.host.empty()) {
      throw std::invalid_argument("ApiServer host must not be empty");
    }
    if (options.max_body_bytes == 0) {
      throw std::invalid_argument("ApiServer max body bytes must be greater than zero");
    }
    if (options.request_timeout.count() <= 0) {
      throw std::invalid_argument("ApiServer request timeout must be positive");
    }
  }

  [[nodiscard]] bool ready() const noexcept {
    return !options.host.empty() && options.max_body_bytes > 0 &&
           options.request_timeout.count() > 0 && indexer != nullptr && retriever != nullptr &&
           prompt_builder != nullptr && llm_client != nullptr;
  }

  [[nodiscard]] bool running_state() const noexcept {
    return running.load(std::memory_order_relaxed);
  }

  void wire_storage() noexcept {
    if (indexer != nullptr && retriever != nullptr) {
      retriever->attach_storage(indexer->storage());
    }
  }

  [[nodiscard]] Status snapshot() const {
    Status status;
    status.running = running_state();
    status.host = options.host;
    status.port = bound_port != 0 ? bound_port : options.port;
    if (indexer != nullptr) {
      status.root_directory = indexer->options().root_directory;
      status.symbol_count = indexer->ready() ? indexer->storage().graph().symbol_count() : 0U;
      status.indexed_files = indexer->last_stats().files_indexed;
      status.last_stats = indexer->last_stats();
      status.last_indexed_at = indexer->last_indexed_at();
      status.last_operation = std::string(indexer->last_operation());
    }
    if (retriever != nullptr) {
      status.module_count = retriever->hierarchy().modules().size();
      status.retriever_ready = retriever->ready();
    }
    status.llm_ready = llm_client != nullptr && llm_client->ready();
    return status;
  }

  [[nodiscard]] HttpResponse handle_search(const json& body) const {
    if (retriever == nullptr || !retriever->ready()) {
      return json_error(http::status::service_unavailable,
                        "Retriever is not ready; index the corpus first");
    }

    const auto query = trim(body.value("query", std::string{}));
    if (query.empty()) {
      return json_error(http::status::bad_request, "Missing required field: query");
    }

    const auto retrieval = retriever->retrieve(query);
    json response;
    response["query"] = query;
    response["modules"] = json::array();
    for (const auto& module : retrieval.modules) {
      response["modules"].push_back(module_hit_json(module));
    }
    response["symbols"] = json::array();
    for (const auto& symbol : retrieval.symbols) {
      response["symbols"].push_back(symbol_context_json(symbol));
    }
    return HttpResponse{http::status::ok, std::move(response)};
  }

  [[nodiscard]] HttpResponse handle_explain(const json& body) const {
    if (indexer == nullptr || !indexer->ready()) {
      return json_error(http::status::service_unavailable,
                        "Indexer is not ready; run an initial index first");
    }
    if (prompt_builder == nullptr) {
      return json_error(http::status::service_unavailable, "Prompt builder is not attached");
    }
    if (llm_client == nullptr || !llm_client->ready()) {
      return json_error(http::status::service_unavailable, "LLM client is not ready");
    }

    const auto name = trim(body.value("name", body.value("symbol_name", std::string{})));
    if (name.empty()) {
      return json_error(http::status::bad_request, "Missing required field: name");
    }

    const auto symbol = find_symbol_by_name(*indexer, name);
    if (!symbol.has_value()) {
      return json_error(http::status::not_found, "Symbol not found: " + name);
    }

    Retriever::Result retrieval;
    retrieval.query = name;
    retrieval.query_embedding = {};
    if (const auto module = find_module_record(*retriever, symbol->symbol.module_name)) {
      retrieval.modules.push_back(HierarchicalIndex::ModuleHit{*module, 1.0});
    } else {
      retrieval.modules.push_back(HierarchicalIndex::ModuleHit{
          fallback_module_record(*indexer, symbol->symbol.module_name), 1.0});
    }
    retrieval.symbols.push_back(make_symbol_context(*symbol, indexer->storage()));

    const auto local_files = parse_local_files(body);
    const auto prompt =
        prompt_builder->build(PromptBuilder::RequestType::Explain, name, retrieval, local_files);

    LlmClient::ChatRequest request;
    request.messages = prompt.messages;
    const auto completion = llm_client->complete(request);

    json response;
    response["name"] = name;
    response["symbol"] = indexed_symbol_json(*symbol);
    response["retrieval"]["modules"] = json::array();
    for (const auto& module_hit : retrieval.modules) {
      response["retrieval"]["modules"].push_back(module_hit_json(module_hit));
    }
    response["retrieval"]["symbols"] = json::array();
    for (const auto& symbol_hit : retrieval.symbols) {
      response["retrieval"]["symbols"].push_back(symbol_context_json(symbol_hit));
    }
    response["prompt"] = prompt_json(prompt);
    response["completion"] = chat_response_json(completion);
    return HttpResponse{http::status::ok, std::move(response)};
  }

  [[nodiscard]] HttpResponse handle_deps(const json& body) const {
    if (indexer == nullptr || !indexer->ready()) {
      return json_error(http::status::service_unavailable,
                        "Indexer is not ready; run an initial index first");
    }

    const auto name = trim(body.value("name", body.value("symbol_name", std::string{})));
    const auto depth = body.value("depth", 2U);
    if (name.empty()) {
      return json_error(http::status::bad_request, "Missing required field: name");
    }

    const auto symbol = find_symbol_by_name(*indexer, name);
    if (!symbol.has_value()) {
      return json_error(http::status::not_found, "Symbol not found: " + name);
    }

    json response;
    response["name"] = name;
    response["symbol"] = indexed_symbol_json(*symbol);
    response["callers"] = json::array();
    for (const auto& caller : indexer->storage().graph().callers_of(symbol->symbol_id)) {
      response["callers"].push_back(stored_symbol_json(caller));
    }
    response["callees"] = json::array();
    for (const auto& callee : indexer->storage().graph().callees_from(symbol->symbol_id)) {
      response["callees"].push_back(stored_symbol_json(callee));
    }
    response["module_dependencies"] = json::array();
    for (const auto& dependency : indexer->storage().graph().transitive_module_dependencies(
             symbol->symbol.module_name, depth)) {
      response["module_dependencies"].push_back(module_dependency_json(dependency));
    }
    return HttpResponse{http::status::ok, std::move(response)};
  }

  [[nodiscard]] HttpResponse handle_callers(const json& body) const {
    if (indexer == nullptr || !indexer->ready()) {
      return json_error(http::status::service_unavailable,
                        "Indexer is not ready; run an initial index first");
    }

    const auto name = trim(body.value("name", body.value("symbol_name", std::string{})));
    if (name.empty()) {
      return json_error(http::status::bad_request, "Missing required field: name");
    }

    const auto symbol = find_symbol_by_name(*indexer, name);
    if (!symbol.has_value()) {
      return json_error(http::status::not_found, "Symbol not found: " + name);
    }

    json response;
    response["name"] = name;
    response["symbol"] = indexed_symbol_json(*symbol);
    response["callers"] = json::array();
    for (const auto& caller : indexer->storage().graph().callers_of(symbol->symbol_id)) {
      response["callers"].push_back(stored_symbol_json(caller));
    }
    return HttpResponse{http::status::ok, std::move(response)};
  }

  [[nodiscard]] HttpResponse handle_module(const json& body) const {
    if (indexer == nullptr || !indexer->ready()) {
      return json_error(http::status::service_unavailable,
                        "Indexer is not ready; run an initial index first");
    }

    const auto module_name = trim(body.value("module_name", body.value("name", std::string{})));
    if (module_name.empty()) {
      return json_error(http::status::bad_request, "Missing required field: module_name");
    }

    const auto depth = body.value("depth", 1U);
    json response;
    response["module_name"] = module_name;
    response["symbols"] = json::array();
    response["dependencies"] = json::array();

    const auto dependency_list =
        indexer->storage().graph().transitive_module_dependencies(module_name, depth);
    for (const auto& dependency : dependency_list) {
      response["dependencies"].push_back(module_dependency_json(dependency));
    }

    std::vector<Indexer::IndexedSymbol> module_symbols;
    module_symbols.reserve(indexer->symbols().size());
    for (const auto& symbol : indexer->symbols()) {
      if (symbol.symbol.module_name == module_name || symbol.symbol.module_path == module_name) {
        module_symbols.push_back(symbol);
      }
    }

    if (!module_symbols.empty()) {
      response["module"] = module_record_json(fallback_module_record(*indexer, module_name));
      if (retriever != nullptr && retriever->ready()) {
        if (const auto module_record = find_module_record(*retriever, module_name)) {
          response["module"] = module_record_json(*module_record);
        }
      }
      for (const auto& symbol : module_symbols) {
        response["symbols"].push_back(indexed_symbol_json(symbol));
      }
      return HttpResponse{http::status::ok, std::move(response)};
    }

    return json_error(http::status::not_found, "Module not found: " + module_name);
  }

  [[nodiscard]] HttpResponse handle_status() const {
    const auto current = snapshot();
    json response;
    response["running"] = current.running;
    response["host"] = current.host;
    response["port"] = current.port;
    response["root_directory"] = current.root_directory.generic_string();
    response["symbol_count"] = current.symbol_count;
    response["module_count"] = current.module_count;
    response["indexed_files"] = current.indexed_files;
    response["last_indexed_at_ms"] = to_unix_ms(current.last_indexed_at);
    response["last_operation"] = current.last_operation;
    response["last_stats"] = json{{"files_scanned", current.last_stats.files_scanned},
                                  {"files_indexed", current.last_stats.files_indexed},
                                  {"symbols_indexed", current.last_stats.symbols_indexed},
                                  {"parse_errors", current.last_stats.parse_errors},
                                  {"embedding_batches", current.last_stats.embedding_batches},
                                  {"elapsed_ms", current.last_stats.elapsed.count()}};
    response["retriever_ready"] = current.retriever_ready;
    response["llm_ready"] = current.llm_ready;
    return HttpResponse{http::status::ok, std::move(response)};
  }

  [[nodiscard]] HttpResponse handle_reindex(const json& body) {
    if (indexer == nullptr || !indexer->ready()) {
      return json_error(http::status::service_unavailable,
                        "Indexer is not ready; run an initial index first");
    }

    Indexer::Result result;
    std::string mode;
    if (body.contains("changed_files") && body["changed_files"].is_array() &&
        !body["changed_files"].empty()) {
      std::vector<std::filesystem::path> changed_files;
      for (const auto& item : body["changed_files"]) {
        if (item.is_string()) {
          changed_files.emplace_back(item.get<std::string>());
        }
      }
      result = indexer->update(changed_files);
      mode = "changed_files";
    } else {
      const auto base_ref = trim(body.value("base_ref", std::string{}));
      result = indexer->update_from_git(base_ref);
      mode = base_ref.empty() ? "git_diff" : "git_diff:" + base_ref;
    }

    json warnings = json::array();
    if (retriever != nullptr) {
      try {
        auto rebuilt = *retriever;
        rebuilt.attach_storage(indexer->storage());
        if (module_embedding_batch) {
          rebuilt.build(indexer->symbols(), module_embedding_batch);
        } else {
          rebuilt.build(indexer->symbols());
        }
        *retriever = std::move(rebuilt);
      } catch (const std::exception& error) {
        warnings.push_back(std::string("Retriever rebuild failed: ") + error.what());
        spdlog::warn("API reindex retriever refresh failed: {}", error.what());
      }
    }

    json response;
    response["mode"] = mode;
    response["changed_files"] = json::array();
    std::unordered_set<std::string> unique_changed_files;
    for (const auto& symbol : result.symbols) {
      const auto& path = symbol.symbol.file_path;
      if (unique_changed_files.insert(path).second) {
        response["changed_files"].push_back(path);
      }
    }
    response["stats"] = json{{"files_scanned", result.stats.files_scanned},
                             {"files_indexed", result.stats.files_indexed},
                             {"symbols_indexed", result.stats.symbols_indexed},
                             {"parse_errors", result.stats.parse_errors},
                             {"embedding_batches", result.stats.embedding_batches},
                             {"elapsed_ms", result.stats.elapsed.count()}};
    response["status"] = snapshot_json();
    response["warnings"] = std::move(warnings);
    return HttpResponse{http::status::ok, std::move(response)};
  }

  [[nodiscard]] json snapshot_json() const {
    const auto snapshot = this->snapshot();
    json response;
    response["running"] = snapshot.running;
    response["host"] = snapshot.host;
    response["port"] = snapshot.port;
    response["root_directory"] = snapshot.root_directory.generic_string();
    response["symbol_count"] = snapshot.symbol_count;
    response["module_count"] = snapshot.module_count;
    response["indexed_files"] = snapshot.indexed_files;
    response["last_indexed_at_ms"] = to_unix_ms(snapshot.last_indexed_at);
    response["last_operation"] = snapshot.last_operation;
    response["last_stats"] = json{{"files_scanned", snapshot.last_stats.files_scanned},
                                  {"files_indexed", snapshot.last_stats.files_indexed},
                                  {"symbols_indexed", snapshot.last_stats.symbols_indexed},
                                  {"parse_errors", snapshot.last_stats.parse_errors},
                                  {"embedding_batches", snapshot.last_stats.embedding_batches},
                                  {"elapsed_ms", snapshot.last_stats.elapsed.count()}};
    response["retriever_ready"] = snapshot.retriever_ready;
    response["llm_ready"] = snapshot.llm_ready;
    return response;
  }

  [[nodiscard]] HttpResponse dispatch(const http::request<http::string_body>& request) {
    const auto path = target_path(request.target());
    try {
      if (request.method() == http::verb::get && path == "/status") {
        return handle_status();
      }

      if (request.method() != http::verb::post) {
        return json_error(http::status::method_not_allowed, "Unsupported HTTP method");
      }

      const auto body =
          request.body().empty() ? json::object() : json::parse(request.body(), nullptr, false);
      if (body.is_discarded()) {
        return json_error(http::status::bad_request, "Invalid JSON payload");
      }

      if (path == "/search") {
        return handle_search(body);
      }
      if (path == "/explain") {
        return handle_explain(body);
      }
      if (path == "/deps") {
        return handle_deps(body);
      }
      if (path == "/callers") {
        return handle_callers(body);
      }
      if (path == "/module") {
        return handle_module(body);
      }
      if (path == "/reindex") {
        return handle_reindex(body);
      }

      return json_error(http::status::not_found, "Unknown endpoint: " + std::string(path));
    } catch (const std::exception& error) {
      spdlog::error("API request failed for {} {}: {}", request.method_string(), path,
                    error.what());
      return json_error(http::status::internal_server_error, error.what());
    }
  }

  void schedule_accept() {
    if (acceptor == nullptr || !running_state()) {
      return;
    }

    auto socket = std::make_shared<tcp::socket>(io_context);
    acceptor->async_accept(*socket, [this, socket](const boost::system::error_code& ec) {
      if (ec) {
        if (running_state() && ec != asio::error::operation_aborted) {
          spdlog::warn("API accept failed: {}", ec.message());
        }
        return;
      }

      handle_session(std::move(*socket));
      if (running_state()) {
        schedule_accept();
      }
    });
  }

  void handle_session(tcp::socket socket) {
    beast::tcp_stream stream(std::move(socket));
    stream.expires_after(options.request_timeout);

    beast::flat_buffer buffer;
    http::request_parser<http::string_body> parser;
    parser.body_limit(options.max_body_bytes);

    boost::system::error_code ec;
    http::read(stream, buffer, parser, ec);
    if (ec) {
      if (ec != http::error::end_of_stream) {
        spdlog::warn("API request read failed: {}", ec.message());
      }
      return;
    }

    const auto request = parser.get();
    const auto response_data = dispatch(request);

    http::response<http::string_body> response{response_data.status, request.version()};
    response.set(http::field::server, "qodeloc-api");
    response.set(http::field::content_type, "application/json");
    response.keep_alive(false);
    response.body() = response_data.body.dump();
    response.prepare_payload();

    stream.expires_after(options.request_timeout);
    http::write(stream, response, ec);
    if (ec) {
      spdlog::warn("API response write failed: {}", ec.message());
      return;
    }

    ec = stream.socket().shutdown(tcp::socket::shutdown_send, ec);
  }

  Options options;
  Indexer* indexer{nullptr};
  Retriever* retriever{nullptr};
  PromptBuilder* prompt_builder{nullptr};
  LlmClient* llm_client{nullptr};
  ModuleEmbeddingBatchFn module_embedding_batch;
  asio::io_context io_context;
  std::unique_ptr<tcp::acceptor> acceptor;
  std::thread worker;
  std::atomic<bool> running{false};
  std::uint16_t bound_port{};
};

ApiServer::ApiServer() : ApiServer(Config::current().api_options()) {}

ApiServer::ApiServer(Options options) : impl_(std::make_unique<Impl>(std::move(options))) {}

ApiServer::~ApiServer() = default;

std::string_view ApiServer::module_name() const noexcept {
  return "api";
}

bool ApiServer::ready() const noexcept {
  return impl_ != nullptr && impl_->ready();
}

const ApiServer::Options& ApiServer::options() const noexcept {
  return impl_->options;
}

bool ApiServer::running() const noexcept {
  return impl_ != nullptr && impl_->running_state();
}

std::uint16_t ApiServer::bound_port() const noexcept {
  if (impl_ == nullptr) {
    return 0;
  }

  return impl_->bound_port != 0 ? impl_->bound_port : impl_->options.port;
}

ApiServer::Status ApiServer::status() const {
  if (impl_ == nullptr) {
    return {};
  }

  return impl_->snapshot();
}

void ApiServer::attach_indexer(Indexer& indexer) noexcept {
  impl_->indexer = &indexer;
  impl_->wire_storage();
}

void ApiServer::attach_retriever(Retriever& retriever) noexcept {
  impl_->retriever = &retriever;
  impl_->wire_storage();
}

void ApiServer::attach_prompt_builder(PromptBuilder& prompt_builder) noexcept {
  impl_->prompt_builder = &prompt_builder;
}

void ApiServer::attach_llm_client(LlmClient& llm_client) noexcept {
  impl_->llm_client = &llm_client;
}

void ApiServer::attach_module_embedding_batch(ModuleEmbeddingBatchFn module_embedding_batch) {
  impl_->module_embedding_batch = std::move(module_embedding_batch);
}

void ApiServer::start() {
  if (impl_ == nullptr) {
    throw std::runtime_error("ApiServer is not initialized");
  }
  if (impl_->running.exchange(true, std::memory_order_relaxed)) {
    throw std::runtime_error("ApiServer is already running");
  }
  if (!ready()) {
    impl_->running.store(false, std::memory_order_relaxed);
    throw std::runtime_error("ApiServer is not ready");
  }

  try {
    tcp::resolver resolver{impl_->io_context};
    const auto results = resolver.resolve(impl_->options.host, std::to_string(impl_->options.port));
    if (results.empty()) {
      throw std::runtime_error("Failed to resolve API endpoint");
    }

    impl_->acceptor = std::make_unique<tcp::acceptor>(impl_->io_context);
    const auto endpoint = results.begin()->endpoint();
    impl_->acceptor->open(endpoint.protocol());
    impl_->acceptor->set_option(tcp::acceptor::reuse_address(true));
    impl_->acceptor->bind(endpoint);
    impl_->acceptor->listen();
    impl_->bound_port = impl_->acceptor->local_endpoint().port();
    impl_->io_context.restart();
    impl_->schedule_accept();

    impl_->worker = std::thread([this] {
      try {
        impl_->io_context.run();
      } catch (const std::exception& error) {
        spdlog::error("API server loop failed: {}", error.what());
      }
      impl_->running.store(false, std::memory_order_relaxed);
    });
  } catch (...) {
    impl_->running.store(false, std::memory_order_relaxed);
    if (impl_->acceptor != nullptr) {
      boost::system::error_code ec;
      ec = impl_->acceptor->close(ec);
      impl_->acceptor.reset();
    }
    throw;
  }
}

void ApiServer::stop() noexcept {
  if (impl_ == nullptr) {
    return;
  }

  impl_->running.store(false, std::memory_order_relaxed);
  if (impl_->acceptor != nullptr) {
    boost::system::error_code ec;
    ec = impl_->acceptor->close(ec);
  }
  impl_->io_context.stop();
  if (impl_->worker.joinable()) {
    impl_->worker.join();
  }
  impl_->acceptor.reset();
}

} // namespace qodeloc::core
