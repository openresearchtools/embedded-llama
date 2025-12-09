#include "server-context.h"
#include "server-common.h"
#include "server-http.h"

#include "arg.h"
#include "common.h"
#include "log.h"

#include <algorithm>
#include <cctype>
#include <atomic>
#include <csignal>
#include <functional>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// simple signal flag to allow graceful interruption
static std::atomic<bool> g_should_stop = false;

static void handle_signal(int) {
    g_should_stop.store(true);
}

struct cli_options {
    std::string op = "chat";
    std::string body;
    std::string body_file;
    std::string text;
    std::string query;
    std::vector<std::string> documents;
    std::string documents_file;
    std::optional<int> top_n;
    bool use_stdin = false;
    bool has_stream_override = false;
    bool stream_override = false;
    bool help = false;
};

static void print_cli_usage(const char * prog) {
    std::cerr << "Embedded llama.cpp CLI (no HTTP listener)\n";
    std::cerr << "Usage: " << prog << " [--op chat|completion|embeddings|rerank|tokenize] [embedded-cli opts] [llama/server opts]\n";
    std::cerr << "Embedded CLI opts:\n"
              << "  --op <name>            Route to run (chat, completion, embeddings, rerank, tokenize, detokenize, apply-template, props)\n"
              << "  --text <str>           Plain text to use when no JSON body is supplied\n"
              << "  --body|--json <str>    Raw JSON payload (same shape as the HTTP API)\n"
              << "  --body-file <path>     File containing raw JSON payload\n"
              << "  --stdin                Read raw JSON payload from stdin\n"
              << "  --query <str>          Rerank query (fallbacks to --text/-p prompt)\n"
              << "  --document <str>       Rerank document (repeatable)\n"
              << "  --documents-file <p>   Newline-delimited rerank documents\n"
              << "  --top-n <n>            Rerank cutoff (optional)\n"
              << "  --stream/--no-stream   Override stream flag for chat/completion bodies\n"
              << "  --help-cli             Show this help without invoking the model\n";
    std::cerr << "Examples:\n"
              << "  " << prog << " chat --text \"hello\" -m model.gguf --no-stream\n"
              << "  " << prog << " embeddings --text \"embed me\" -m model.gguf\n"
              << "  " << prog << " rerank --query \"title\" --document \"doc a\" --document \"doc b\" -m model.gguf --top-n 1\n"
              << "  " << prog << " chat --body-file request.json -m model.gguf\n";
}

static std::string normalize_op(std::string op) {
    std::transform(op.begin(), op.end(), op.begin(), [](unsigned char c) { return (char) std::tolower(c); });

    if (op == "chat/completions" || op == "chat-completions" || op == "chat_completion") {
        return "chat";
    }
    if (op == "completions" || op == "completion" || op == "cmpl") {
        return "completion";
    }
    if (op == "emb" || op == "embedding" || op == "embeddings") {
        return "embedding";
    }
    if (op == "reranking") {
        return "rerank";
    }
    if (op == "health" || op == "healthz") {
        return "health";
    }
    return op;
}

static std::string slurp_file(const std::string & path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + path);
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static std::string read_stdin_all() {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    return ss.str();
}

static std::vector<std::string> read_lines(const std::string & path) {
    std::vector<std::string> lines;
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + path);
    }
    std::string cur;
    while (std::getline(file, cur)) {
        if (!cur.empty()) {
            lines.push_back(cur);
        }
    }
    return lines;
}

static void parse_cli_args(int argc, char ** argv, cli_options & opts, std::vector<std::string> & llama_args) {
    bool op_set = false;

    auto require_value = [&](const char * name, int & idx) -> std::string {
        if (idx + 1 >= argc) {
            throw std::invalid_argument(std::string("missing value for ") + name);
        }
        return argv[++idx];
    };

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--op" || arg == "--mode" || arg == "--route") {
            opts.op = require_value(arg.c_str(), i);
            op_set = true;
            continue;
        }
        if (arg == "--body" || arg == "--json" || arg == "--input-json") {
            opts.body = require_value(arg.c_str(), i);
            continue;
        }
        if (arg == "--body-file" || arg == "--json-file") {
            opts.body_file = require_value(arg.c_str(), i);
            continue;
        }
        if (arg == "--text" || arg == "-t") {
            opts.text = require_value(arg.c_str(), i);
            continue;
        }
        if (arg == "--query") {
            opts.query = require_value(arg.c_str(), i);
            continue;
        }
        if (arg == "--document" || arg == "--doc") {
            opts.documents.push_back(require_value(arg.c_str(), i));
            continue;
        }
        if (arg == "--documents-file") {
            opts.documents_file = require_value(arg.c_str(), i);
            continue;
        }
        if (arg == "--top-n") {
            opts.top_n = std::stoi(require_value(arg.c_str(), i));
            continue;
        }
        if (arg == "--stdin") {
            opts.use_stdin = true;
            continue;
        }
        if (arg == "--stream") {
            opts.has_stream_override = true;
            opts.stream_override = true;
            continue;
        }
        if (arg == "--no-stream") {
            opts.has_stream_override = true;
            opts.stream_override = false;
            continue;
        }
        if (arg == "--help-cli") {
            opts.help = true;
            continue;
        }

        if (!arg.empty() && arg[0] != '-') {
            if (!op_set) {
                opts.op = arg;
                op_set = true;
                continue;
            }
        }

        llama_args.push_back(std::move(arg));
    }
}

static std::string build_body(const cli_options & opts, const std::string & op, const common_params & params) {
    if (!opts.body.empty()) {
        return opts.body;
    }
    if (!opts.body_file.empty()) {
        return slurp_file(opts.body_file);
    }
    if (opts.use_stdin) {
        return read_stdin_all();
    }

    json payload;
    const std::string text = !opts.text.empty() ? opts.text : params.prompt;
    const bool stream_flag = opts.has_stream_override ? opts.stream_override : false;

    if (op == "chat") {
        if (text.empty()) {
            throw std::runtime_error("chat requires --text or -p/--prompt content");
        }
        payload["messages"] = json::array({{{"role", "user"}, {"content", text}}});
        payload["stream"] = stream_flag;
        return safe_json_to_str(payload);
    }

    if (op == "completion") {
        if (text.empty()) {
            throw std::runtime_error("completion requires --text or -p/--prompt content");
        }
        payload["prompt"] = text;
        payload["stream"] = stream_flag;
        return safe_json_to_str(payload);
    }

    if (op == "embedding") {
        if (text.empty()) {
            throw std::runtime_error("embeddings require --text or -p/--prompt content");
        }
        payload["input"] = text;
        return safe_json_to_str(payload);
    }

    if (op == "rerank") {
        std::vector<std::string> docs = opts.documents;
        if (!opts.documents_file.empty()) {
            auto from_file = read_lines(opts.documents_file);
            docs.insert(docs.end(), from_file.begin(), from_file.end());
        }
        if (docs.empty()) {
            throw std::runtime_error("rerank requires at least one --document or --documents-file line");
        }

        const std::string q = !opts.query.empty() ? opts.query : text;
        if (q.empty()) {
            throw std::runtime_error("rerank requires --query or --text/-p content");
        }

        payload["query"] = q;
        payload["documents"] = docs;
        if (opts.top_n.has_value()) {
            payload["top_n"] = *opts.top_n;
        }
        return safe_json_to_str(payload);
    }

    if (op == "tokenize") {
        if (text.empty()) {
            throw std::runtime_error("tokenize requires --text or -p/--prompt content or a raw JSON body");
        }
        payload["content"] = text;
        return safe_json_to_str(payload);
    }

    throw std::runtime_error("operation '" + op + "' requires a JSON body (--body/--body-file/--stdin)");
}

static server_http_res_ptr dispatch_route(server_routes & routes, const std::string & op_raw, const server_http_req & req) {
    const std::string op = normalize_op(op_raw);

    if (op == "chat") {
        return routes.post_chat_completions(req);
    }
    if (op == "completion") {
        return routes.post_completions(req);
    }
    if (op == "embedding") {
        return routes.post_embeddings(req);
    }
    if (op == "rerank") {
        return routes.post_rerank(req);
    }
    if (op == "tokenize") {
        return routes.post_tokenize(req);
    }
    if (op == "detokenize") {
        return routes.post_detokenize(req);
    }
    if (op == "apply-template") {
        return routes.post_apply_template(req);
    }
    if (op == "props") {
        return routes.get_props(req);
    }
    if (op == "health") {
        return routes.get_health(req);
    }

    throw std::invalid_argument("unsupported op: " + op_raw);
}

static int emit_response(server_http_res & res) {
    if (res.is_stream()) {
        std::string chunk;
        while (!g_should_stop.load() && res.next(chunk)) {
            std::cout << chunk;
            std::cout.flush();
        }
        if (g_should_stop.load()) {
            return 1;
        }
        return res.status >= 400 ? 1 : 0;
    }

    if (res.status >= 400) {
        std::cerr << res.data << std::endl;
        return 1;
    }

    std::cout << res.data << std::endl;
    return 0;
}

int main(int argc, char ** argv) {
    cli_options opts;
    std::vector<std::string> llama_args;

    try {
        parse_cli_args(argc, argv, opts, llama_args);
    } catch (const std::exception & e) {
        LOG_ERR("%s\n", e.what());
        print_cli_usage(argv[0]);
        return 1;
    }

    if (opts.help) {
        print_cli_usage(argv[0]);
        return 0;
    }

    // rebuild argv for llama common parser
    std::vector<std::string> argv_storage;
    argv_storage.reserve(llama_args.size() + 1);
    argv_storage.emplace_back(argv[0]);
    for (auto & arg : llama_args) {
        argv_storage.push_back(arg);
    }

    std::vector<char *> argv_ptrs;
    argv_ptrs.reserve(argv_storage.size());
    for (auto & arg : argv_storage) {
        argv_ptrs.push_back(arg.data());
    }

    common_params params;
    if (!common_params_parse((int) argv_ptrs.size(), argv_ptrs.data(), params, LLAMA_EXAMPLE_SERVER)) {
        return 1;
    }

    const std::string op = normalize_op(opts.op);
    if (op == "embedding" || op == "rerank") {
        params.embedding = true;
    }
    if (params.n_parallel == 1 && params.kv_unified == false && !params.has_speculative()) {
        LOG_WRN("%s: setting n_parallel = 4 and kv_unified = true (add -kvu to disable this)\n", __func__);
        params.n_parallel = 4;
        params.kv_unified = true;
    }

    if (params.model_alias.empty() && !params.model.name.empty()) {
        params.model_alias = params.model.name;
    }

    if (params.model.path.empty()) {
        LOG_ERR("model path is required (use -m or -hf)\n");
        return 1;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    common_init();

    llama_backend_init();
    llama_numa_init(params.numa);

    server_context ctx_server;

    if (!ctx_server.load_model(params)) {
        llama_backend_free();
        return 1;
    }

    ctx_server.init();

    std::thread worker([&ctx_server]() {
        ctx_server.start_loop();
    });

    int rc = 0;

    try {
        const std::string body = build_body(opts, op, params);
        const std::function<bool()> should_stop = []() { return g_should_stop.load(); };

        server_routes routes(params, ctx_server, []() { return true; });

        server_http_req req{{}, {}, op, body, should_stop};

        server_http_res_ptr res;
        try {
            res = dispatch_route(routes, op, req);
        } catch (const std::exception & e) {
            auto err = std::make_unique<server_http_res>();
            err->status = 500;
            err->data = safe_json_to_str({{"error", format_error_response(e.what(), ERROR_TYPE_INVALID_REQUEST)}});
            res = std::move(err);
        }

        rc = emit_response(*res);
    } catch (const std::exception & e) {
        LOG_ERR("%s\n", e.what());
        rc = 1;
    }

    g_should_stop.store(true);
    ctx_server.terminate();
    if (worker.joinable()) {
        worker.join();
    }
    llama_backend_free();

    return rc;
}
