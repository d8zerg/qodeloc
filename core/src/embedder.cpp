#include <algorithm>
#include <httplib/httplib.h>
#include <iterator>
#include <nlohmann/json.hpp>
#include <qodeloc/core/embedder.hpp>
#include <sstream>
#include <stdexcept>

namespace qodeloc::core {

Embedder::Embedder() : Embedder(Options{}) {}

Embedder::Embedder(Options options) : options_(std::move(options)) {
  if (options_.host.empty()) {
    throw std::invalid_argument("Embedder host must not be empty");
  }
  if (options_.port == 0) {
    throw std::invalid_argument("Embedder port must be greater than zero");
  }
  if (options_.api_path.empty()) {
    throw std::invalid_argument("Embedder API path must not be empty");
  }
  if (options_.model.empty()) {
    throw std::invalid_argument("Embedder model must not be empty");
  }
  if (options_.batch_size == 0) {
    throw std::invalid_argument("Embedder batch size must be greater than zero");
  }
}

std::string_view Embedder::module_name() const noexcept {
  return "embedder";
}

bool Embedder::ready() const noexcept {
  return !options_.host.empty() && options_.port != 0 && !options_.api_path.empty() &&
         !options_.model.empty() && options_.batch_size != 0;
}

const Embedder::Options& Embedder::options() const noexcept {
  return options_;
}

Embedder::Embedding Embedder::embed(std::string_view text) const {
  std::vector<std::string> single_text{std::string{text}};
  auto batch = embed_batch(single_text);
  if (batch.empty()) {
    return {};
  }

  return std::move(batch.front());
}

Embedder::Embeddings Embedder::embed_batch(std::span<const std::string> texts) const {
  Embeddings result;
  result.reserve(texts.size());

  for (std::size_t offset = 0; offset < texts.size(); offset += options_.batch_size) {
    const auto count = std::min(options_.batch_size, texts.size() - offset);
    std::vector<std::string> chunk;
    chunk.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      chunk.emplace_back(texts[offset + index]);
    }

    auto batch = request_batch(chunk);
    if (batch.size() != count) {
      std::ostringstream oss;
      oss << "Embedding backend returned " << batch.size() << " vectors for " << count << " inputs";
      throw std::runtime_error(oss.str());
    }

    result.insert(result.end(), std::make_move_iterator(batch.begin()),
                  std::make_move_iterator(batch.end()));
  }

  return result;
}

Embedder::Embeddings Embedder::request_batch(std::span<const std::string> texts) const {
  httplib::Client client(options_.host, options_.port);
  client.set_connection_timeout(options_.timeout);
  client.set_read_timeout(options_.timeout);
  client.set_write_timeout(options_.timeout);

  nlohmann::json request;
  request["model"] = options_.model;
  request["encoding_format"] = "float";
  if (texts.size() == 1) {
    request["input"] = texts.front();
  } else {
    request["input"] = nlohmann::json::array();
    for (const auto& text : texts) {
      request["input"].push_back(text);
    }
  }

  const auto response = client.Post(options_.api_path.c_str(), request.dump(), "application/json");
  if (!response) {
    std::ostringstream oss;
    oss << "Failed to reach embedding backend at http://" << options_.host << ':' << options_.port
        << options_.api_path;
    throw std::runtime_error(oss.str());
  }

  if (response->status < 200 || response->status >= 300) {
    std::ostringstream oss;
    oss << "Embedding backend at http://" << options_.host << ':' << options_.port
        << options_.api_path << " returned HTTP " << response->status << ": " << response->body;
    throw std::runtime_error(oss.str());
  }

  return parse_embeddings_response(response->body, texts.size());
}

Embedder::Embeddings Embedder::parse_embeddings_response(const std::string& body,
                                                         std::size_t expected_count) {
  if (expected_count == 0) {
    return {};
  }

  const auto json = nlohmann::json::parse(body);

  if (json.contains("data") && json["data"].is_array()) {
    struct Item {
      std::size_t index{};
      Embedding embedding;
    };

    std::vector<Item> items;
    items.reserve(json["data"].size());

    for (const auto& row : json["data"]) {
      Item item;
      item.index = row.value("index", items.size());
      item.embedding = row.at("embedding").get<Embedding>();
      items.push_back(std::move(item));
    }

    if (items.size() != expected_count) {
      std::ostringstream oss;
      oss << "Embedding backend returned " << items.size() << " rows for " << expected_count
          << " inputs";
      throw std::runtime_error(oss.str());
    }

    std::sort(items.begin(), items.end(),
              [](const Item& lhs, const Item& rhs) { return lhs.index < rhs.index; });

    Embeddings embeddings;
    embeddings.reserve(items.size());
    for (auto& item : items) {
      embeddings.push_back(std::move(item.embedding));
    }
    return embeddings;
  }

  if (json.contains("embedding") && expected_count == 1) {
    return {json.at("embedding").get<Embedding>()};
  }

  throw std::runtime_error("Embedding backend returned an unexpected JSON shape");
}

} // namespace qodeloc::core
