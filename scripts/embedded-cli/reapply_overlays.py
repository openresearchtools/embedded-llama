from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
README_BANNER = """# embedded-llama overlay

This fork keeps upstream `llama.cpp` intact and adds an embedded, no-HTTP CLI (`llama-embedded-cli`) so chat, completion, embeddings, rerank, tokenize, etc. can run in-process without starting `llama-server`. The original upstream README begins below for reference.

---

"""

CMAKE_NEEDLE = "add_subdirectory(embedded-cli)"
CMAKE_SERVER_LINE = "add_subdirectory(server)"


def ensure_readme_banner() -> bool:
    readme = REPO / "README.md"
    data = readme.read_text(encoding="utf-8")
    if data.startswith(README_BANNER):
        return False
    if data.startswith("# llama.cpp"):
        readme.write_text(README_BANNER + data, encoding="utf-8")
        return True
    raise SystemExit(f"README.md header not recognized; manual merge needed (found: {data[:40]!r})")


def ensure_cmake_hook() -> bool:
    path = REPO / "tools" / "CMakeLists.txt"
    data = path.read_text(encoding="utf-8")
    if CMAKE_NEEDLE in data:
        return False

    if CMAKE_SERVER_LINE not in data:
        raise SystemExit("tools/CMakeLists.txt does not contain add_subdirectory(server); manual merge needed")

    new_data = data.replace(
        CMAKE_SERVER_LINE,
        f"{CMAKE_SERVER_LINE}\n        {CMAKE_NEEDLE}",
        1,
    )
    if new_data == data:
        raise SystemExit("Failed to inject embedded-cli hook; manual merge needed")

    path.write_text(new_data, encoding="utf-8")
    return True


def main() -> None:
    changed = []
    if ensure_readme_banner():
        changed.append("README.md (banner)")
    if ensure_cmake_hook():
        changed.append("tools/CMakeLists.txt (embedded-cli hook)")

    if not changed:
        print("Nothing to reapply; overlays already present.")
    else:
        print("Reapplied overlays: " + ", ".join(changed))


if __name__ == "__main__":
    main()
