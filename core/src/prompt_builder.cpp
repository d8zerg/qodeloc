#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <qodeloc/core/config.hpp>
#include <qodeloc/core/prompt_builder.hpp>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace qodeloc::core {
namespace {

[[nodiscard]] std::string_view ltrim(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.remove_prefix(1);
  }
  return text;
}

[[nodiscard]] std::string_view rtrim(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.remove_suffix(1);
  }
  return text;
}

[[nodiscard]] std::string trim(std::string_view text) {
  return std::string{rtrim(ltrim(text))};
}

[[nodiscard]] std::size_t line_indent(std::string_view line) {
  std::size_t indent = 0;
  while (indent < line.size() && line[indent] == ' ') {
    ++indent;
  }
  return indent;
}

[[nodiscard]] std::size_t count_tokens_text(std::string_view text) {
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

[[nodiscard]] std::vector<std::string> read_lines(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    std::ostringstream oss;
    oss << "Failed to open prompt template: " << path.string();
    throw std::runtime_error(oss.str());
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    lines.push_back(line);
  }
  return lines;
}

[[nodiscard]] bool append_line(std::string& output, std::size_t& tokens_used, std::string_view line,
                               std::size_t token_limit) {
  const auto line_tokens = count_tokens_text(line);
  if (line_tokens == 0) {
    if (!output.empty() && output.back() != '\n') {
      output.push_back('\n');
    }
    return true;
  }

  if (tokens_used + line_tokens > token_limit) {
    return false;
  }

  if (!output.empty() && output.back() != '\n') {
    output.push_back('\n');
  }
  output.append(line.begin(), line.end());
  tokens_used += line_tokens;
  return true;
}

} // namespace

PromptBuilder::PromptBuilder() : PromptBuilder(Config::current().prompt_builder_options()) {}

PromptBuilder::PromptBuilder(Options options) : options_(std::move(options)) {
  if (options_.templates_directory.empty()) {
    throw std::invalid_argument("PromptBuilder templates directory must not be empty");
  }
  if (options_.context_token_limit == 0) {
    throw std::invalid_argument("PromptBuilder context token limit must be greater than zero");
  }
  if (options_.module_limit == 0 || options_.symbol_limit == 0 || options_.local_file_limit == 0) {
    throw std::invalid_argument("PromptBuilder section limits must be greater than zero");
  }
  if (options_.module_token_limit == 0 || options_.symbol_token_limit == 0 ||
      options_.local_file_token_limit == 0) {
    throw std::invalid_argument("PromptBuilder section token limits must be greater than zero");
  }
}

const PromptBuilder::Options& PromptBuilder::options() const noexcept {
  return options_;
}

std::string_view PromptBuilder::request_type_name(RequestType request_type) noexcept {
  switch (request_type) {
  case RequestType::Search:
    return "search";
  case RequestType::Explain:
    return "explain";
  case RequestType::Deps:
    return "deps";
  case RequestType::Callers:
    return "callers";
  case RequestType::Module:
    return "module";
  }

  return "search";
}

PromptBuilder::RenderedPrompt PromptBuilder::build(RequestType request_type, std::string_view query,
                                                   const Retriever::Result& retrieval,
                                                   std::span<const LocalFile> local_files) const {
  const auto template_data = load_template(request_type);
  const auto context_limit =
      std::min(options_.context_token_limit, template_data.context_token_limit == 0
                                                 ? options_.context_token_limit
                                                 : template_data.context_token_limit);

  std::unordered_map<std::string, std::string> variables;
  variables.emplace("request_type", std::string{request_type_name(request_type)});
  variables.emplace("query", std::string{query});
  variables.emplace("context_token_limit", std::to_string(context_limit));
  variables.emplace("module_count", std::to_string(retrieval.modules.size()));
  variables.emplace("symbol_count", std::to_string(retrieval.symbols.size()));
  variables.emplace("local_file_count", std::to_string(local_files.size()));

  variables.emplace(
      "modules",
      format_modules(retrieval, SectionBudget{options_.module_limit, options_.module_token_limit}));
  variables.emplace(
      "symbols",
      format_symbols(retrieval, SectionBudget{options_.symbol_limit, options_.symbol_token_limit}));
  variables.emplace(
      "local_files",
      format_local_files(
          local_files, SectionBudget{options_.local_file_limit, options_.local_file_token_limit}));

  auto system_text = render_template(template_data.system, variables);
  auto user_text = render_template(template_data.user, variables);

  const auto system_tokens = count_tokens(system_text);
  auto token_count = system_tokens + count_tokens(user_text);
  if (token_count > context_limit) {
    const auto remaining = system_tokens >= context_limit ? 0 : context_limit - system_tokens;
    user_text = trim_to_token_limit(std::move(user_text), remaining);
    token_count = system_tokens + count_tokens(user_text);
  }
  if (token_count > context_limit) {
    system_text = trim_to_token_limit(std::move(system_text), context_limit);
    token_count = count_tokens(system_text) + count_tokens(user_text);
  }

  RenderedPrompt rendered;
  rendered.template_name = template_data.name;
  rendered.context_token_limit = context_limit;
  rendered.token_count = token_count;
  rendered.system_text = std::move(system_text);
  rendered.user_text = std::move(user_text);
  rendered.messages = {
      LlmClient::ChatMessage{"system", rendered.system_text},
      LlmClient::ChatMessage{"user", rendered.user_text},
  };
  return rendered;
}

PromptBuilder::Template PromptBuilder::load_template(RequestType request_type) const {
  const auto key = std::string{request_type_name(request_type)};
  {
    std::scoped_lock lock(template_mutex_);
    const auto it = template_cache_.find(key);
    if (it != template_cache_.end()) {
      return it->second;
    }
  }

  const auto path = options_.templates_directory / (key + ".yaml");
  const auto lines = read_lines(path);

  Template template_data;
  std::string current_key;
  std::string current_value;
  bool in_block{false};
  std::size_t block_indent{0};
  bool block_indent_set{false};
  std::size_t key_indent{0};

  const auto flush_block = [&]() {
    if (current_key == "name") {
      template_data.name = clean_scalar(current_value);
    } else if (current_key == "system") {
      template_data.system = rtrim(current_value);
    } else if (current_key == "user") {
      template_data.user = rtrim(current_value);
    } else if (current_key == "context_token_limit") {
      const auto value = clean_scalar(current_value);
      template_data.context_token_limit =
          value.empty() ? 0 : static_cast<std::size_t>(std::stoull(value));
    } else {
      std::ostringstream oss;
      oss << "Unknown prompt template key in block scalar: " << current_key;
      throw std::runtime_error(oss.str());
    }
  };

  for (std::size_t i = 0; i < lines.size(); ++i) {
    const auto& line = lines[i];
    const auto trimmed = trim(line);

    if (in_block) {
      if (trimmed.empty() || line_indent(line) > key_indent) {
        if (!trimmed.empty()) {
          const auto indent = line_indent(line);
          if (!block_indent_set) {
            block_indent = indent;
            block_indent_set = true;
          }
          if (indent < block_indent) {
            std::ostringstream oss;
            oss << "Inconsistent indentation in prompt template " << path.string();
            throw std::runtime_error(oss.str());
          }
          current_value.append(line.substr(block_indent));
        }
        current_value.push_back('\n');
        continue;
      }

      flush_block();
      current_key.clear();
      current_value.clear();
      in_block = false;
      block_indent = 0;
      block_indent_set = false;
      --i;
      continue;
    }

    if (trimmed.empty() || trimmed.starts_with('#')) {
      continue;
    }

    const auto colon = trimmed.find(':');
    if (colon == std::string::npos) {
      std::ostringstream oss;
      oss << "Invalid prompt template line in " << path.string() << ": " << line;
      throw std::runtime_error(oss.str());
    }

    current_key = trim(trimmed.substr(0, colon));
    auto value = trim(trimmed.substr(colon + 1));
    key_indent = line_indent(line);

    if (value == "|" || value == ">") {
      in_block = true;
      current_value.clear();
      continue;
    }

    if (current_key == "name") {
      template_data.name = clean_scalar(value);
    } else if (current_key == "context_token_limit") {
      template_data.context_token_limit =
          value.empty() ? 0 : static_cast<std::size_t>(std::stoull(clean_scalar(value)));
    } else if (current_key == "system") {
      template_data.system = clean_scalar(value);
    } else if (current_key == "user") {
      template_data.user = clean_scalar(value);
    } else {
      std::ostringstream oss;
      oss << "Unknown prompt template key in " << path.string() << ": " << current_key;
      throw std::runtime_error(oss.str());
    }
  }

  if (in_block) {
    flush_block();
  }

  if (template_data.name.empty()) {
    template_data.name = key;
  }
  if (template_data.name != key) {
    std::ostringstream oss;
    oss << "Prompt template name mismatch for " << path.string() << ": expected " << key
        << " but got " << template_data.name;
    throw std::runtime_error(oss.str());
  }
  if (template_data.system.empty() || template_data.user.empty()) {
    std::ostringstream oss;
    oss << "Prompt template is missing system or user text: " << path.string();
    throw std::runtime_error(oss.str());
  }

  {
    std::scoped_lock lock(template_mutex_);
    template_cache_.emplace(key, template_data);
  }

  return template_data;
}

std::string
PromptBuilder::render_template(std::string_view template_text,
                               const std::unordered_map<std::string, std::string>& variables) {
  std::string rendered;
  rendered.reserve(template_text.size());

  for (std::size_t index = 0; index < template_text.size();) {
    if (index + 1 < template_text.size() && template_text[index] == '{' &&
        template_text[index + 1] == '{') {
      const auto close = template_text.find("}}", index + 2);
      if (close == std::string_view::npos) {
        rendered.append(template_text.substr(index));
        break;
      }

      const auto key = trim(template_text.substr(index + 2, close - (index + 2)));
      const auto it = variables.find(key);
      if (it == variables.end()) {
        std::ostringstream oss;
        oss << "Missing prompt variable: " << key;
        throw std::runtime_error(oss.str());
      }

      rendered.append(it->second);
      index = close + 2;
      continue;
    }

    rendered.push_back(template_text[index]);
    ++index;
  }

  return rendered;
}

std::size_t PromptBuilder::count_tokens(std::string_view text) {
  return count_tokens_text(text);
}

std::string PromptBuilder::trim_to_token_limit(std::string text, std::size_t limit) {
  if (count_tokens(text) <= limit) {
    return text;
  }

  std::istringstream input{text};
  std::string output;
  std::string line;
  std::size_t tokens_used = 0;
  while (std::getline(input, line)) {
    if (!append_line(output, tokens_used, line, limit)) {
      break;
    }
  }

  if (output.empty()) {
    return {};
  }

  if (count_tokens(output) > limit) {
    return trim_to_token_limit(std::move(output), limit);
  }

  if (count_tokens(text) > limit && count_tokens(output) < limit) {
    if (!output.empty() && output.back() != '\n') {
      output.push_back('\n');
    }
    output.append("[truncated]");
  }

  return output;
}

std::string PromptBuilder::format_modules(const Retriever::Result& retrieval,
                                          PromptBuilder::SectionBudget budget) {
  std::string output;
  std::size_t tokens_used = 0;
  std::size_t emitted = 0;

  for (const auto& hit : retrieval.modules) {
    if (emitted >= budget.item_limit) {
      break;
    }

    std::ostringstream line;
    line << "- " << hit.module.module_name;
    if (!hit.module.module_path.empty()) {
      line << " (" << hit.module.module_path << ")";
    }
    line << " score=" << hit.score;
    line << " public=" << hit.module.public_symbol_count;
    line << " headers=" << hit.module.header_count;
    if (!append_line(output, tokens_used, line.str(), budget.token_limit)) {
      break;
    }

    if (!hit.module.summary.empty()) {
      const auto summary = truncate_line(hit.module.summary, 24);
      if (!append_line(output, tokens_used, "  summary: " + summary, budget.token_limit)) {
        break;
      }
    }

    ++emitted;
  }

  if (output.empty()) {
    return "  none";
  }

  return output;
}

std::string PromptBuilder::format_symbols(const Retriever::Result& retrieval,
                                          PromptBuilder::SectionBudget budget) {
  std::string output;
  std::size_t tokens_used = 0;
  std::size_t emitted = 0;

  for (const auto& hit : retrieval.symbols) {
    if (emitted >= budget.item_limit) {
      break;
    }

    std::ostringstream header;
    header << "- " << hit.symbol.symbol.qualified_name;
    if (!hit.symbol.symbol.module_name.empty()) {
      header << " [" << hit.symbol.symbol.module_name << "]";
    }
    header << " score=" << hit.score;
    if (!append_line(output, tokens_used, header.str(), budget.token_limit)) {
      break;
    }

    if (!hit.symbol.symbol.signature.empty()) {
      if (!append_line(output, tokens_used, "  signature: " + hit.symbol.symbol.signature,
                       budget.token_limit)) {
        break;
      }
    }

    if (!hit.context.empty()) {
      if (!append_line(output, tokens_used, "  context:", budget.token_limit)) {
        break;
      }

      std::istringstream context_stream{hit.context};
      std::string context_line;
      while (std::getline(context_stream, context_line)) {
        if (!append_line(output, tokens_used, "    " + context_line, budget.token_limit)) {
          break;
        }
      }
    }

    ++emitted;
  }

  if (output.empty()) {
    return "  none";
  }

  return output;
}

std::string PromptBuilder::format_local_files(std::span<const LocalFile> local_files,
                                              PromptBuilder::SectionBudget budget) {
  std::string output;
  std::size_t tokens_used = 0;
  std::size_t emitted = 0;

  for (const auto& local_file : local_files) {
    if (emitted >= budget.item_limit) {
      break;
    }

    const auto file_text = format_local_file(local_file, budget.token_limit);
    if (!append_line(output, tokens_used, file_text, budget.token_limit)) {
      break;
    }
    ++emitted;
  }

  if (output.empty()) {
    return "  none";
  }

  return output;
}

std::string PromptBuilder::format_symbol_context(const Retriever::SymbolContext& symbol,
                                                 std::size_t token_limit) {
  std::string output;
  std::size_t tokens_used = 0;
  std::ostringstream header;
  header << "- " << symbol.symbol.symbol.qualified_name;
  if (!symbol.symbol.symbol.module_name.empty()) {
    header << " [" << symbol.symbol.symbol.module_name << "]";
  }
  header << " score=" << symbol.score;

  if (!append_line(output, tokens_used, header.str(), token_limit)) {
    return output;
  }
  if (!symbol.symbol.symbol.signature.empty()) {
    if (!append_line(output, tokens_used, "  signature: " + symbol.symbol.symbol.signature,
                     token_limit)) {
      return output;
    }
  }
  if (!symbol.context.empty()) {
    if (!append_line(output, tokens_used, "  context:", token_limit)) {
      return output;
    }
    std::istringstream context_stream{symbol.context};
    std::string context_line;
    while (std::getline(context_stream, context_line)) {
      if (!append_line(output, tokens_used, "    " + context_line, token_limit)) {
        break;
      }
    }
  }
  return output;
}

std::string PromptBuilder::format_module_hit(const HierarchicalIndex::ModuleHit& module,
                                             std::size_t token_limit) {
  std::string output;
  std::size_t tokens_used = 0;
  std::ostringstream header;
  header << "- " << module.module.module_name;
  if (!module.module.module_path.empty()) {
    header << " (" << module.module.module_path << ")";
  }
  header << " score=" << module.score;
  if (!append_line(output, tokens_used, header.str(), token_limit)) {
    return output;
  }
  if (!module.module.summary.empty()) {
    if (!append_line(output, tokens_used, "  summary: " + truncate_line(module.module.summary, 24),
                     token_limit)) {
      return output;
    }
  }
  return output;
}

std::string PromptBuilder::format_local_file(const LocalFile& local_file, std::size_t token_limit) {
  std::string output;
  std::size_t tokens_used = 0;
  if (!append_line(output, tokens_used, "- " + path_to_string(local_file.path), token_limit)) {
    return output;
  }
  if (!append_line(output, tokens_used, "  content:", token_limit)) {
    return output;
  }

  std::istringstream content_stream{local_file.content};
  std::string content_line;
  while (std::getline(content_stream, content_line)) {
    if (!append_line(output, tokens_used, "    " + content_line, token_limit)) {
      break;
    }
  }

  return output;
}

std::string PromptBuilder::truncate_line(std::string_view text, std::size_t token_limit) {
  if (count_tokens(text) <= token_limit) {
    return std::string{text};
  }

  std::istringstream input{std::string{text}};
  std::ostringstream output;
  std::string word;
  std::size_t tokens_used = 0;
  while (input >> word) {
    if (tokens_used == token_limit) {
      break;
    }
    if (tokens_used != 0) {
      output << ' ';
    }
    output << word;
    ++tokens_used;
  }

  output << " ...";
  return output.str();
}

std::string PromptBuilder::path_to_string(const std::filesystem::path& path) {
  return path.generic_string();
}

std::string PromptBuilder::clean_scalar(std::string_view text) {
  auto trimmed = trim(text);
  if (trimmed.size() >= 2) {
    const auto first = trimmed.front();
    const auto last = trimmed.back();
    if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
      trimmed = trimmed.substr(1, trimmed.size() - 2);
    }
  }
  return trimmed;
}

} // namespace qodeloc::core
